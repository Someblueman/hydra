#define _XOPEN_SOURCE 700
/* Planning with an interactive agent: request changes returns the exact
 * revision through the CLI and types the feedback into the agent's pane for
 * the user to send; approval launches the revision and Hydra submits the run
 * receipt and outcome into that same conversation. The "agent" is cat, so
 * everything it receives is written to a file. */
#include "hydra_fixture.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct hf_fixture f;
static char input_path[4096], draft[4096], run_id[256];
#define H(...) hf_run(&f, NULL, 0, (const char *[]){f.hydra, __VA_ARGS__, NULL})
#define REFUSE(...) hf_run(&f, NULL, -2, (const char *[]){f.hydra, __VA_ARGS__, NULL})
#define S(keys) tv_send(s, (keys))
#define U(marker, seconds) tv_until(s, (marker), (seconds))

/* The fixture plan with the guided policy's disk floor and a given objective. */
static void write_draft(const char *objective) {
    char path[4096];
    json_object *plan, *envelope = NULL;
    tv_format(path, sizeof(path), "%s/tests/fixtures/plan/plan.json", f.root);
    plan = json_object_from_file(path);
    CHECK(plan && json_object_object_get_ex(plan, "envelope", &envelope), "fixture plan");
    json_object_object_add(envelope, "disk_mb", json_object_new_int(1024));
    json_object_object_add(plan, "objective", json_object_new_string(objective));
    CHECK(!json_object_to_file(draft, plan), "write draft");
    json_object_put(plan);
    H("workflow", "plan", "propose", draft, "--branch", "planner");
}

static void agent_input(char *out, size_t capacity) {
    out[0] = 0;
    if (tv_exists(input_path))
        tv_read(input_path, out, capacity);
}

/* Pumps the UI until the agent has received a line containing needle. */
static void agent_until(struct tv_session *s, const char *needle, double seconds) {
    char text[65536];
    double deadline = tv_now() + seconds;
    agent_input(text, sizeof(text));
    while (!strstr(text, needle) && tv_now() < deadline) {
        tv_pump(s, .1);
        agent_input(text, sizeof(text));
    }
    if (!strstr(text, needle))
        fprintf(stderr, "Agent input lacks '%s':\n%s\n%s\n", needle, text, tv_text(s));
    CHECK(strstr(text, needle), "agent conversation received the message");
}

static void open_conversation(struct tv_session *s) {
    char command[4200];
    hf_open(&f, s);
    U("PLAN TOGETHER", 3);
    U("Session   running", 5);
    S("a");
    U("INPUT TO AGENT", 5);
    U("shell · session running", 5);
    tv_pump(s, .5);
    tv_format(command, sizeof(command), "/bin/cat > '%s'\r", input_path);
    S(command);
    tv_pump(s, .5);
    S("\002\t");
    S("P");
    U("Review with local policy", 3);
    S("y\r");
    U("Revision 1 / DRAFT", 5);
}

static void request_changes(struct tv_session *s) {
    char text[65536];
    S("V");
    U("READY / awaiting", 30);
    S("F");
    U("Changes to request from planner", 3);
    S("Split verify into two checks\r");
    U("Returned. Feedback typed into planner", 5);
    U("RETURNED / changes requested", 3);
    U("INPUT TO AGENT", 3);
    tv_pump(s, 1);
    agent_input(text, sizeof(text));
    CHECK(!strstr(text, "Split verify"), "feedback waits for the user to press Enter");
    S("\r");
    agent_until(s, "Hydra: the user requested changes to plan revision 1: Split verify into two checks", 5);
}

/* Evidence of the conversation layout: at 80x24 the agent keeps the space and
 * the footer names the key that shows the plan review; at 140x40 the review
 * sits beside the agent. */
static void capture_layout(struct tv_session *s, int cols, int rows, const char *marker) {
    char dir[4096], path[4200];
    tv_format(dir, sizeof(dir), "%s/plan-conversation-evidence", f.build);
    tv_mkdir(dir);
    tv_resize(s, cols, rows);
    tv_pump(s, .5);
    CHECK(!s->screen.overflow, "conversation resize");
    CHECK(tv_contains(s, marker), "conversation layout marker");
    tv_format(path, sizeof(path), "%s/returned-%dx%d.html", dir, cols, rows);
    tv_save(s, path);
}

static void capture_layouts(struct tv_session *s) {
    capture_layout(s, 80, 24, "Ctrl-B B plan review");
    capture_layout(s, 140, 40, "RETURNED / changes requested");
}

static void returned_is_blocked(struct tv_session *s) {
    S("\002\t");
    S("V");
    U("This revision was returned for changes", 3);
    S("E");
    U("returned for changes; nothing runs", 3);
    CHECK(!tv_contains(s, "INPUT TO HYDRA / execution approval"), "a returned revision offers no approval");
    REFUSE("workflow", "plan", "proposal", "planner");
}

static void approve_revision(struct tv_session *s) {
    char pattern[4096];
    write_draft("Revised after requested changes");
    U("Revision 2 / DRAFT", 10);
    S("V");
    U("READY / awaiting", 30);
    S("E");
    U("INPUT TO HYDRA / execution approval", 3);
    U("Execute planner revision 2", 3);
    S("y");
    tv_format(pattern, sizeof(pattern), "%s/state/v2/projects/*/workflows/launches/*/run-id", f.home);
    {
        double deadline = tv_now() + 30;
        while (!hf_glob_count(pattern) && tv_now() < deadline)
            tv_pump(s, .1);
    }
    hf_glob_one(pattern, pattern, sizeof(pattern));
    tv_read(pattern, run_id, sizeof(run_id));
    hf_trim(run_id);
}

static void run_reaches_planner(struct tv_session *s) {
    char needle[512], text[65536];
    tv_format(needle, sizeof(needle), "Hydra: the user approved revision 2 (digest ");
    agent_until(s, needle, 15);
    tv_format(needle, sizeof(needle), "and run %s started. Follow it with hydra workflow status %s --json", run_id, run_id);
    agent_until(s, needle, 5);
    tv_format(needle, sizeof(needle), "Hydra: run %s finished: succeeded.", run_id);
    agent_until(s, needle, 90);
    agent_input(text, sizeof(text));
    CHECK(hf_count(text, "started. Follow it") == 1, "the launch message is typed once");
}

static void owner_recorded_notices(void) {
    char needle[512];
    tv_format(needle, sizeof(needle), "run %s started from your approved plan", run_id);
    CHECK(hf_count(hf_run(&f, NULL, 0, (const char *[]){"grep", "-rl", needle, f.home, NULL}), "\n") == 1,
          "the run owner records one start notice for the planning head");
    tv_format(needle, sizeof(needle), "run %s finished: succeeded", run_id);
    CHECK(hf_count(hf_run(&f, NULL, 0, (const char *[]){"grep", "-rl", needle, f.home, NULL}), "\n") == 1,
          "the run owner records one completion notice for the planning head");
}

int main(void) {
    struct tv_session session, *s = &session;
    tv_init();
    hf_init(&f, "hydra-plan-conversation", "repo", true, true);
    tv_format(input_path, sizeof(input_path), "%s/agent-input", f.base);
    tv_format(draft, sizeof(draft), "%s/draft.json", f.base);
    hf_commit_init(&f);
    H("spawn", "planner", "--no-agent");
    write_draft("Deliver a report and verify its contents");
    open_conversation(s);
    request_changes(s);
    capture_layouts(s);
    returned_is_blocked(s);
    approve_revision(s);
    run_reaches_planner(s);
    tv_close(s, "q", 0, 0);
    owner_recorded_notices();
    hf_cleanup();
    {
        const char *remove[] = {"rm", "-rf", f.base, NULL};
        tv_command_ok(NULL, remove);
    }
    puts("PASS plan conversation: request changes returns the exact revision and types feedback for the user to send; "
         "approval types the run receipt and outcome into the planning agent; the owner records inbox notices");
    return 0;
}
