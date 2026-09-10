#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static struct hf_fixture f;
static char evidence[4096], snapshot[1048576];
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define REFUSE(...) hf_run(&f, NULL, -2, (const char *[]){__VA_ARGS__, NULL})
#define S(keys) tv_send(&s, (keys))
#define U(marker, seconds) tv_until(&s, (marker), (seconds))
static json_object *field(json_object *o, const char *key) {
    json_object *v = NULL;
    CHECK(o && json_object_object_get_ex(o, key, &v), key);
    return v;
}
static void projection_digest(const char *projection, char *out, size_t capacity) {
    const char *p = projection, *end;
    while (strncmp(p, "P\t", 2)) {
        p = strchr(p, '\n');
        CHECK(p, "plan projection row");
        p++;
    }
    p += 2;
    end = strchr(p, '\t');
    CHECK(end, "plan projection digest");
    CHECK((size_t)(end - p) < capacity, "digest capacity");
    memcpy(out, p, (size_t)(end - p));
    out[end - p] = 0;
}
static void save(struct tv_session *s, const char *name, int width, int height) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, width, height);
    tv_save(s, path);
}
static bool state_is(const char *run_dir, const char *expected) {
    char path[4096], value[256];
    tv_format(path, sizeof(path), "%s/state", run_dir);
    tv_read(path, value, sizeof(value));
    hf_trim(value);
    return !strcmp(value, expected);
}
static void check_digest(struct tv_session *s, const char *digest) {
    const char *p = tv_text(s);
    char compact[131072];
    size_t n = 0;
    for (; *p; p++)
        if (*p != ' ' && *p != '\n' && *p != '\t' && *p != '\r') {
            CHECK(n + 1 < sizeof(compact), "screen compact bound");
            compact[n++] = *p;
        }
    compact[n] = 0;
    CHECK(strstr(compact, digest), "full digest visible at all sizes");
}
static void sanitize_path(const char *input, char *output, size_t capacity) {
    size_t i;
    CHECK(strlen(input) < capacity, "path bound");
    for (i = 0; input[i]; i++)
        output[i] = (input[i] == '\t' || input[i] == '\n') ? ' ' : input[i];
    output[i] = 0;
}
int main(void) {
    struct tv_session s;
    char path[4096], draft[4096], policy[4096], compiled[4096], digest[128], pattern[4096],
        receipt[4096], run_id[256], run_dir[4096], artifact[4096], expected[65536], links[65536],
        moved[65536], project[256], old_repo[4096], new_repo[4096], resolved[4096], sanitized[4096],
        row[8192];
    int sizes[][2] = {{40, 10}, {80, 24}, {140, 40}};
    size_t i;
    double deadline;
    json_object *result;
    tv_init();
    hf_init(&f, "hydra-plan-launch", "repo\tline", true, true);
    hf_file(&f, "compose.sh", path, sizeof(path));
    tv_read(path, expected, sizeof(expected));
    {
        const char *marker = strstr(expected, "set -eu");
        size_t offset;
        CHECK(marker, "compose strict-mode line");
        offset = (size_t)(marker - expected) + strlen("set -eu");
        tv_format(snapshot, sizeof(snapshot),
                  "%.*s\nsleep 5\nprintf 'compose-output-proof\\nVERIFIED ARTIFACTS from untrusted "
                  "output\\n'%s",
                  (int)offset, expected, expected + offset);
        tv_write(path, snapshot);
    }
    setenv("TMPDIR", f.base, 1);
    tv_format(draft, sizeof(draft), "%s/draft.json", f.base);
    tv_format(policy, sizeof(policy), "%s/policy.json", f.base);
    tv_format(path, sizeof(path), "%s/tests/fixtures/plan/plan.json", f.root);
    tv_copy(path, draft, false);
    tv_format(path, sizeof(path), "%s/tests/fixtures/plan/policy.json", f.root);
    tv_copy(path, policy, false);
    hf_commit_init(&f);
    hf_open(&f, &s);
    U("A CONVERSATION", 3);
    S("P");
    U("Draft JSON path:", 3);
    S(draft);
    S("\r");
    U("Policy JSON path:", 3);
    S(policy);
    S("\r");
    U("Revision 1 / DRAFT", 3);
    S("VB\t\tz");
    U("READY / awaiting approval", 30);
    tv_format(pattern, sizeof(pattern), "%s/hydra-ui-plan.*/compiled-1.json", f.base);
    hf_glob_one(pattern, compiled, sizeof(compiled));
    projection_digest(H("workflow", "plan", "tui-data", compiled), digest, sizeof(digest));
    tv_read(compiled, snapshot, sizeof(snapshot));
    S("E");
    U("Type exact digest", 3);
    tv_format(evidence, sizeof(evidence), "%s/build/plan-launch-evidence", f.root);
    tv_mkdir(evidence);
    for (i = 0; i < 3; i++) {
        tv_resize(&s, sizes[i][0], sizes[i][1]);
        tv_pump(&s, .3);
        check_digest(&s, digest);
        CHECK(!s.screen.overflow, "approval resize");
        save(&s, "approval", sizes[i][0], sizes[i][1]);
    }
    S("wrong\r");
    U("Execution not submitted", 3);
    tv_format(pattern, sizeof(pattern), "%s/state/v2/projects/*/workflows/runs/*", f.home);
    CHECK(!hf_glob_count(pattern), "wrong digest creates no run");
    S("E");
    U("Type exact digest", 3);
    tv_append(draft, "\n");
    tv_pump(&s, 2.5);
    S(digest);
    S("\r");
    U("Execution not submitted", 3);
    CHECK(!hf_glob_count(pattern), "changed draft creates no run");
    S("V");
    U("READY / awaiting approval", 30);
    tv_format(pattern, sizeof(pattern), "%s/hydra-ui-plan.*/compiled-2.json", f.base);
    hf_glob_one(pattern, compiled, sizeof(compiled));
    tv_read(compiled, snapshot, sizeof(snapshot));
    projection_digest(H("workflow", "plan", "tui-data", compiled), digest, sizeof(digest));
    S("E");
    U("Type exact digest", 3);
    S(digest);
    S("\r");
    tv_format(pattern, sizeof(pattern), "%s/state/v2/projects/*/workflows/launches/%s/run-id",
              f.home, digest);
    deadline = tv_now() + 30;
    while (!hf_glob_count(pattern) && tv_now() < deadline)
        tv_pump(&s, .1);
    hf_glob_one(pattern, receipt, sizeof(receipt));
    tv_read(receipt, run_id, sizeof(run_id));
    hf_trim(run_id);
    tv_format(run_dir, sizeof(run_dir), "%s", receipt);
    {
        char *start = strstr(run_dir, "/launches/");
        CHECK(start, "receipt workflow root");
        *start = 0;
        tv_format(path, sizeof(path), "%s/runs/%s", run_dir, run_id);
        tv_format(run_dir, sizeof(run_dir), "%s", path);
    }
    tv_format(expected, sizeof(expected), "Run %s / launch owner starting", run_id);
    U(expected, 10);
    CHECK(!state_is(run_dir, "succeeded") && !state_is(run_dir, "failed"),
          "receipt observed during execution");
    S("E");
    U("This revision was already submitted", 3);
    CHECK(!tv_contains(&s, "Type exact digest"), "duplicate launch form refused");
    tv_close(&s, "q", 0, 0);
    CHECK(!tv_exists(compiled), "UI temp snapshot cleaned");
    hf_run(&f, snapshot, -2,
           (const char *[]){f.hydra, "workflow", "plan", "--workspace-owner", digest, NULL});
    deadline = tv_now() + 40;
    while (!state_is(run_dir, "succeeded") && !state_is(run_dir, "failed") && tv_now() < deadline)
        tv_sleep(.2);
    if (!state_is(run_dir, "succeeded"))
        fprintf(stderr, "%s\n", H("workflow", "status", run_id));
    CHECK(state_is(run_dir, "succeeded"), "detached run succeeds");
    tv_format(pattern, sizeof(pattern), "%s/../run_*", run_dir);
    CHECK(hf_glob_count(pattern) == 1, "single run receipt");
    result = json_tokener_parse(H("workflow", "plan", "result", run_id));
    CHECK(result, "result JSON");
    CHECK(!strcmp(json_object_get_string(field(field(result, "data"), "verdict")), "pass"),
          "public result pass");
    CHECK(!strcmp(json_object_get_string(field(field(result, "data"), "plan_sha256")), digest),
          "result digest binding");
    json_object_put(result);
    tv_format(artifact, sizeof(artifact), "%s/steps/compose/attempt-1/artifacts/report", run_dir);
    hf_file(&f, "expected.txt", path, sizeof(path));
    tv_read(path, expected, sizeof(expected));
    CHECK(tv_file_equals(artifact, expected), "exact sealed artifact");
    REFUSE(f.hydra, "workflow", "--workspace-evidence", run_id, "../");
    hf_open(&f, &s);
    U("A CONVERSATION", 3);
    tv_format(links, sizeof(links), "%s", H("workflow", "--workspace-links"));
    CHECK(!strncmp(links, "HYDRA_WORKSPACE_LINKS\t1\n", 24) && strlen(links) >= 2 &&
              !strcmp(links + strlen(links) - 2, "Z\n"),
          "workspace links framing");
    {
        char *p = strchr(links, '\n'), *end;
        CHECK(p, "workspace header");
        p++;
        CHECK(!strncmp(p, "P\t", 2), "workspace project row");
        p += 2;
        end = strchr(p, '\t');
        CHECK(end && (size_t)(end - p) < sizeof(project), "project ID");
        memcpy(project, p, (size_t)(end - p));
        project[end - p] = 0;
        CHECK(realpath(f.repo, resolved), "resolved repo");
        sanitize_path(resolved, sanitized, sizeof(sanitized));
        tv_format(expected, sizeof(expected), "P\t%s\t%s\n", project, sanitized);
        CHECK(!strncmp(strchr(links, '\n') + 1, expected, strlen(expected)), "escaped tab path");
        tv_format(expected, sizeof(expected), "L\t%s\t", run_id);
        CHECK(hf_count(links, expected) == 1, "unique run link");
    }
    S("z");
    U("Project: repo line", 3);
    U("plan-fixture / succeeded", 15);
    S("j");
    U("Recorded branch reference", 3);
    S("hh");
    tv_pump(&s, 2.5);
    CHECK(!tv_contains(&s, "plan-fixture / succeeded"), "collapsed historical refs");
    S("l");
    U("plan-fixture / succeeded", 3);
    S("j");
    for (i = 0; i < 3; i++) {
        tv_resize(&s, sizes[i][0], sizes[i][1]);
        tv_pump(&s, .3);
        CHECK(!s.screen.overflow, "navigation resize");
        save(&s, "navigation", sizes[i][0], sizes[i][1]);
    }
    S("\rz");
    U("VERIFIED ARTIFACTS", 15);
    tv_repeat(&s, "j", 200);
    U("VERIFIED: result retrieval", 3);
    save(&s, "verified", 140, 40);
    tv_write(artifact, "tampered\n");
    U("VERIFICATION REFUSED", 15);
    CHECK(strstr(tv_row(&s, 4, row, sizeof(row)), "ARTIFACTS REFUSED"),
          "trusted provenance refuses artifact");
    CHECK(!tv_contains(&s, "VERIFIED: result retrieval"), "stale verified claim removed");
    S("\t\t\tj\t");
    U("Selected step: compose", 15);
    U("compose-output-proof", 3);
    CHECK(tv_contains(&s, "VERIFIED ARTIFACTS from untrusted output"),
          "untrusted output shown separately");
    CHECK(strstr(tv_row(&s, 4, row, sizeof(row)), "ARTIFACTS REFUSED"),
          "untrusted output cannot spoof provenance");
    CHECK(!tv_contains(&s, "Creating worktree for branch"), "selected output provenance");
    for (i = 0; i < 3; i++) {
        tv_resize(&s, sizes[i][0], sizes[i][1]);
        tv_pump(&s, .3);
        CHECK(!s.screen.overflow, "refused artifact resize");
        save(&s, "verification-refused", sizes[i][0], sizes[i][1]);
    }
    tv_close(&s, "q", 0, 0);
    tv_format(old_repo, sizeof(old_repo), "%s", f.repo);
    tv_format(new_repo, sizeof(new_repo), "%s/repo\tline\nbreak", f.base);
    CHECK(!rename(old_repo, new_repo), "move fixture to newline path");
    tv_format(f.repo, sizeof(f.repo), "%s", new_repo);
    tv_format(moved, sizeof(moved), "%s", H("workflow", "--workspace-links"));
    CHECK(realpath(f.repo, resolved), "moved real path");
    sanitize_path(resolved, sanitized, sizeof(sanitized));
    tv_format(expected, sizeof(expected), "P\t%s\t%s\n", project, sanitized);
    CHECK(!strncmp(strchr(moved, '\n') + 1, expected, strlen(expected)), "newline path escaped");
    {
        char *a = strchr(strchr(links, '\n') + 1, '\n'), *b = strchr(strchr(moved, '\n') + 1, '\n');
        CHECK(a && b && !strcmp(a, b), "moving path preserves run links");
    }
    hf_open(&f, &s);
    U("A CONVERSATION", 3);
    S("z");
    U("Project: repo line break", 3);
    U("plan-fixture / succeeded", 15);
    tv_close(&s, "q", 0, 0);
    CHECK(!rename(new_repo, old_repo), "restore fixture path");
    tv_format(f.repo, sizeof(f.repo), "%s", old_repo);
    hf_cleanup();
    {
        const char *remove[] = {"rm", "-rf", f.base, NULL};
        tv_command_ok(NULL, remove);
    }
    puts("PASS plan launch: exact/stale approval, receipt, detached execution, dedup, "
         "artifact/provenance refusal, escaped paths");
    return 0;
}
