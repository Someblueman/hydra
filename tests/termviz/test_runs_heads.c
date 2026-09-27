#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* A planning head launches a plan run that creates a headless codex worker
 * and a verifier head. The Work list groups them under the run, the worker's
 * details describe its agent step (not a missing terminal), the live step
 * output is readable, Overview centres on the run, and the verifier head is
 * retired when the run finishes. Synthetic codex; private tmux socket. */
static struct hf_fixture f;
static char evidence[4096];
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define S(keys) tv_send(&s, (keys))
#define U(marker, seconds) tv_until(&s, (marker), (seconds))
static void save(struct tv_session *s, const char *name, int width, int height) {
    char path[4096];
    tv_resize(s, width, height);
    tv_pump(s, .6);
    CHECK(!s->screen.overflow, "capture fits the terminal");
    tv_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, width, height);
    tv_save(s, path);
    tv_format(path, sizeof(path), "%s/%s-%dx%d.txt", evidence, name, width, height);
    tv_write(path, tv_text(s));
}
static void both(struct tv_session *s, const char *name) {
    save(s, name, 80, 24);
    save(s, name, 140, 40);
}
static void write_fixture_codex(void) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/bin/codex", f.base);
    tv_write(path,
             "#!/bin/sh\n"
             "set -eu\n"
             "case \" $* \" in\n"
             "    *' --version '*) printf 'codex-cli 9.9.9-fixture\\n'; exit 0 ;;\n"
             "    *' --help '*) printf 'Usage: codex exec --json resume\\n'; exit 0 ;;\n"
             "esac\n"
             "prompt=\"$(cat)\"\n"
             "printf '%s\\n' '{\"type\":\"thread.started\",\"thread_id\":\"fixture-thread\"}' '{\"type\":\"turn.started\"}'\n"
             "printf '%s\\n' '{\"type\":\"item.completed\",\"item\":{\"id\":\"i1\",\"type\":\"command_execution\",\"command\":\"sh -c true\",\"exit_code\":0,\"status\":\"completed\"}}'\n"
             "printf '%s\\n' '{\"type\":\"item.completed\",\"item\":{\"id\":\"i2\",\"type\":\"agent_message\",\"text\":\"Reading the kill command before adding a dry run\"}}'\n"
             "while [ -e \"$CODEX_FIXTURE_HOLD\" ]; do sleep 0.2; done\n"
             "printf 'worker change\\n' >> compose.sh\n"
             "printf '{\"type\":\"item.completed\",\"item\":{\"id\":\"i3\",\"type\":\"agent_message\",\"text\":\"%s\"}}\\n' \"$(printf '%s\\n' \"$prompt\" | sed -n 's/^ARTIFACT=//p')\"\n"
             "printf '%s\\n' '{\"type\":\"turn.completed\",\"usage\":{\"input_tokens\":1200,\"cached_input_tokens\":800,\"output_tokens\":34}}'\n");
    CHECK(!chmod(path, 0755), "fixture codex executable");
}
static void write_plan(void) {
    char path[4096], text[8192];
    tv_format(path, sizeof(path), "%s/policy.json", f.base);
    tv_write(path, "{\"schema_version\":1,\"envelope\":{\"hosts\":[\"local\"],\"tools\":[\"sh\",\"git\",\"make\",\"profile:codex\"],"
                   "\"effects\":[\"execute\",\"worktree\"],\"writes\":[\"@spawned:*\"],\"parallelism\":1,\"timeout_seconds\":600,"
                   "\"artifact_bytes\":65536,\"max_heads\":4,\"disk_mb\":1,\"retry_budget\":0,\"repair_budget\":0}}");
    tv_format(text, sizeof(text), "%s",
              "{\"schema_version\":1,\"id\":\"runs-demo\",\"objective\":\"Add a dry run and verify it\",\"context\":[],\"assumptions\":[],\"questions\":[],"
              "\"envelope\":{\"hosts\":[\"local\"],\"tools\":[\"sh\",\"git\",\"make\",\"profile:codex\"],\"effects\":[\"worktree\",\"execute\"],\"writes\":[],"
              "\"parallelism\":1,\"timeout_seconds\":300,\"artifact_bytes\":4096,\"max_heads\":2,\"disk_mb\":1,\"retry_budget\":0,\"repair_budget\":0},"
              "\"steps\":["
              "{\"id\":\"spawn-worker\",\"role\":\"work\",\"kind\":\"spawn\",\"needs\":[],\"writes\":[],\"args\":{\"branch\":\"runs-demo-worker\",\"terminal_mode\":\"headless\"}},"
              "{\"id\":\"implement\",\"role\":\"compose\",\"kind\":\"exec\",\"needs\":[\"spawn-worker\"],\"writes\":[],\"args\":{\"head\":\"runs-demo-worker\",\"profile\":\"codex\","
              "\"prompt\":\"Write the report.\\nARTIFACT=Delivered report\\n\",\"result_file\":\"report\",\"timeout\":120}},"
              "{\"id\":\"spawn-check\",\"role\":\"work\",\"kind\":\"spawn\",\"needs\":[],\"writes\":[],\"args\":{\"branch\":\"runs-demo-check\",\"terminal_mode\":\"headless\"}},"
              "{\"id\":\"verify\",\"role\":\"verify\",\"kind\":\"exec\",\"needs\":[\"implement\",\"spawn-check\"],\"writes\":[],\"args\":{\"head\":\"runs-demo-check\",\"argv\":[\"sh\",\"check.sh\"],\"timeout\":60}}],"
              "\"deliverables\":[{\"id\":\"report\",\"description\":\"Report\",\"step\":\"implement\",\"output\":\"report\",\"destination\":\"run-artifact\"}],"
              "\"checks\":[{\"id\":\"check\",\"method\":\"executable\",\"definition\":\"Compare expected text\",\"step\":\"verify\",\"input\":\"subject\",\"report\":\"check\",\"deliverable\":\"report\"}],"
              "\"requirements\":[{\"id\":\"content\",\"criterion\":\"Report has the expected contents\",\"deliverable\":\"report\",\"check\":\"check\"}],"
              "\"data\":{\"schema_version\":1,\"inputs\":{\"expected\":{\"path\":\"expected.txt\",\"type\":\"file\",\"max_bytes\":128}},"
              "\"steps\":{\"implement\":{\"outputs\":{\"report\":{\"type\":\"file\",\"path\":\"report\",\"max_bytes\":1024}}},"
              "\"verify\":{\"inputs\":{\"subject\":{\"step\":\"implement\",\"output\":\"report\"},\"expected\":{\"input\":\"expected\"}},"
              "\"outputs\":{\"check\":{\"type\":\"object\",\"path\":\"check.json\",\"max_bytes\":2048}}}}}}");
    tv_format(path, sizeof(path), "%s/plan.json", f.base);
    tv_write(path, text);
}
/* Starts the workspace owner for the compiled plan in the background. */
static void launch(void) {
    char plan[4096], policy[4096], compiled[4096], digest[256], head[256], instance[256], path[4096], project[256];
    tv_format(plan, sizeof(plan), "%s/plan.json", f.base);
    tv_format(policy, sizeof(policy), "%s/policy.json", f.base);
    tv_format(compiled, sizeof(compiled), "%s/compiled.json", f.base);
    H("workflow", "plan", "compile", plan, policy, compiled);
    RUN("sh", "-c", "\"$0\" workflow plan show \"$1\" | sed -n 's/^Acceptance digest: //p'", f.hydra, compiled);
    tv_format(digest, sizeof(digest), "%s", f.output);
    hf_trim(digest);
    tv_format(project, sizeof(project), "%s", RUN("cat", ".git/hydra/project-id"));
    hf_trim(project);
    tv_format(path, sizeof(path), "%s/state/v2/projects/%s/heads/head_*", f.home, project);
    RUN("sh", "-c", "for d in $0; do [ \"$(sed -n 1p \"$d/branch\")\" = planner ] && basename \"$d\"; done", path);
    tv_format(head, sizeof(head), "%s", f.output);
    hf_trim(head);
    tv_format(path, sizeof(path), "%s/state/v2/projects/%s/heads/%s/current-instance", f.home, project, head);
    tv_read(path, instance, sizeof(instance));
    hf_trim(instance);
    tv_format(path, sizeof(path), "%s/owner.log", f.base);
    RUN("sh", "-c", "\"$0\" workflow plan --workspace-owner \"$1\" \"$2\" \"$3\" < \"$4\" > \"$5\" 2>&1 &",
        f.hydra, digest, head, instance, compiled, path);
}
int main(void) {
    struct tv_session s;
    char path[4096], hold[4096], codex_home[4096], text[8192];
    tv_init();
    hf_init(&f, "hydra-runs-heads", "repo", true, true);
    tv_format(evidence, sizeof(evidence), "%s/runs-heads-evidence", f.build);
    tv_mkdir(evidence);
    write_fixture_codex();
    tv_format(codex_home, sizeof(codex_home), "%s/codex-home", f.base);
    tv_mkdir(codex_home);
    tv_format(path, sizeof(path), "%s/config.toml", codex_home);
    tv_write(path, "model = \"fixture-model\"\nmodel_reasoning_effort = \"high\"\n");
    CHECK(!setenv("CODEX_HOME", codex_home, 1), "fixture codex configuration");
    tv_format(hold, sizeof(hold), "%s/hold", f.base);
    CHECK(!setenv("CODEX_FIXTURE_HOLD", hold, 1), "fixture codex hold");
    tv_format(path, sizeof(path), "%s/expected.txt", f.repo);
    tv_write(path, "Delivered report");
    hf_commit_init(&f);
    H("spawn", "planner", "--no-agent");
    write_plan();

    hf_open(&f, &s);
    U("PLAN TOGETHER", 5);
    S("3");
    U("No workflow run yet", 5);
    CHECK(tv_contains(&s, "RUN / no run yet"), "the run panel explains the empty state");
    both(&s, "overview-no-run");

    tv_write(hold, "");
    launch();
    tv_format(path, sizeof(path), "%s/state/v2/projects/*/exec/*/*/.provider-stdout", f.home);
    {
        double deadline = tv_now() + 60;
        while (tv_now() < deadline && !hf_glob_count(path)) tv_pump(&s, .2);
        CHECK(hf_glob_count(path) == 1, "the agent step streams live output");
    }
    S("1");
    U("Heads in this project", 5);
    U("1 head (+1 in runs)", 10);
    U("plan run runs-demo", 10);
    CHECK(!tv_contains(&s, "runs-demo-check"), "run heads start collapsed under their run");
    S("j\r");
    U("worker   runs-demo-worker", 5);
    U("Running step implement on runs-demo-worker", 5);
    both(&s, "work-grouped-running");
    S("1j\r");
    U("Details: runs-demo-worker", 5);
    U("headless (no terminal)", 5);
    U("step implement", 5);
    U("configured default: fixture-model", 5);
    CHECK(!tv_contains(&s, "Session"), "a headless head is not described as a missing terminal session");
    S("p");
    U("STEP OUTPUT", 5);
    U("sh -c true -> exit 0", 10);
    U("Reading the kill command before adding a dry run", 10);
    both(&s, "worker-live-output");
    S("p3");
    U("RUN / plan run runs-demo", 5);
    U("implement", 5);
    both(&s, "overview-run-running");

    CHECK(!unlink(hold), "release the fixture agent");
    U("Worker branch runs-demo-worker holds the result", 90);
    U("(retired)", 30);
    both(&s, "overview-run-succeeded");
    S("1");
    U("Heads in this project", 5);
    U("retired", 5);
    both(&s, "work-grouped-finished");
    S("\r");
    U("Tokens      in 1,200  cached 800  out 34", 10);
    both(&s, "worker-details-finished");
    S("1kk\r");
    U("Details: planner", 5);
    U("approved", 5);
    U("Approval requests  none", 5);
    both(&s, "planner-details");
    S("d");
    U("SOURCES", 5);
    both(&s, "technical-details");
    tv_close(&s, "q", 0, 0);
    RUN("sh", "-c", "\"$0\" list | grep -q ' runs-demo-worker ' && ! \"$0\" list | grep -q ' runs-demo-check '", f.hydra);
    RUN("git", "show-ref", "--verify", "--quiet", "refs/heads/runs-demo-check");
    tv_format(text, sizeof(text), "PASS runs and heads: grouped run heads, headless worker details and live output, "
              "run panel states and verifier retirement (%s; captures in %s)", f.base, evidence);
    puts(text);
    {
        const char *remove[] = {"rm", "-rf", f.base, NULL};
        tv_command_ok(NULL, remove);
    }
    return 0;
}
