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
#define S(keys) kt_send(s, (keys))
#define U(marker, seconds) kt_until(s, (marker), (seconds))

/* The fixture plan with the guided policy's disk floor and a given objective. */
static void write_draft(const char *objective) {
    char path[4096];
    json_object *plan, *envelope = NULL;
    kt_format(path, sizeof(path), "%s/tests/fixtures/plan/plan.json", f.root);
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
    if (kt_exists(input_path))
        kt_read(input_path, out, capacity);
}

/* Pumps the UI until the agent has received a line containing needle. */
static void agent_until(struct kt_session *s, const char *needle, double seconds) {
    char text[65536];
    double deadline = kt_now() + seconds;
    agent_input(text, sizeof(text));
    while (!strstr(text, needle) && kt_now() < deadline) {
        kt_pump(s, .1);
        agent_input(text, sizeof(text));
    }
    if (!strstr(text, needle))
        fprintf(stderr, "Agent input lacks '%s':\n%s\n%s\n", needle, text, kt_text(s));
    CHECK(strstr(text, needle), "agent conversation received the message");
}

/* Screen row y without trailing spaces. */
static const char *trimmed_row(struct kt_session *s, int y, char *row, size_t capacity) {
    size_t n = strlen(kt_row(s, y, row, capacity));
    while (n && row[n - 1] == ' ') n--;
    row[n] = 0;
    return row;
}

/* The row of the text field's title. */
static int form_top(struct kt_session *s) {
    char row[1024];
    int y = 0;
    while (y < s->screen.rows && !strstr(kt_row(s, y, row, sizeof(row)), "INPUT TO HYDRA / text field")) y++;
    CHECK(y < s->screen.rows, "text field visible");
    return y;
}

/* The text field's prompt rows, trimmed and joined with single spaces. */
static void form_prompt(struct kt_session *s, char *joined, size_t capacity) {
    char row[1024];
    size_t used = 0;
    int y;
    joined[0] = 0;
    for (y = form_top(s) + 1; y < s->screen.rows && trimmed_row(s, y, row, sizeof(row))[0] != '_'; y++)
        used += (size_t)snprintf(joined + used, capacity - used, "%s%s", used ? " " : "", row);
    CHECK(used < capacity, "prompt rows fit");
}

/* The text field wraps its prompt at word boundaries: its rows joined with
 * single spaces reproduce the prompt's phrases exactly, which a row broken
 * inside a word ("No e" / "xecution") would not. */
static bool form_words_whole(struct kt_session *s) {
    static const char *const phrases[] = {"Review with local policy:", "writes only in heads the plan spawns,",
        "1 worker, 4 heads, 60 minutes, 1 MiB artifacts, 1 GiB free disk, no retries or repairs.",
        "The plan carries its prompts and assets; nothing is committed to your checkout.", "No execution yet. y/N:"};
    char joined[4096];
    size_t i;
    form_prompt(s, joined, sizeof(joined));
    for (i = 0; i < sizeof(phrases) / sizeof(phrases[0]); i++)
        if (!strstr(joined, phrases[i])) {
            fprintf(stderr, "Prompt rows lack '%s':\n%s\n", phrases[i], joined);
            return false;
        }
    return true;
}

static void open_conversation(struct kt_session *s) {
    char command[4200];
    hf_open(&f, s);
    U("PLAN TOGETHER", 3);
    U("Session   running", 5);
    S("a");
    U("INPUT TO AGENT", 5);
    U("shell · session running", 5);
    kt_pump(s, .5);
    kt_format(command, sizeof(command), "/bin/cat > '%s'\r", input_path);
    S(command);
    kt_pump(s, .5);
    S("\035");
    S("P");
    U("Review with local policy", 3);
    CHECK(form_words_whole(s), "policy prompt wraps at words (140 columns)");
    kt_resize(s, 80, 24);
    U("No execution yet.", 5);
    CHECK(form_words_whole(s), "policy prompt wraps at words (80 columns)");
    kt_resize(s, 140, 40);
    U("Review with local policy", 5);
    S("y\r");
    U("Revision 1 / DRAFT", 5);
}

static void request_changes(struct kt_session *s) {
    char text[65536];
    S("V");
    U("READY / awaiting", 30);
    S("F");
    U("Changes to request from planner", 3);
    S("Split verify into two checks\r");
    U("Returned. Feedback typed into planner", 5);
    U("RETURNED / changes requested", 3);
    U("INPUT TO AGENT", 3);
    kt_pump(s, 1);
    agent_input(text, sizeof(text));
    CHECK(!strstr(text, "Split verify"), "feedback waits for the user to press Enter");
    S("\r");
    agent_until(s, "Hydra: the user requested changes to plan revision 1: Split verify into two checks", 5);
}

/* Evidence of the conversation layout: at 80x24 the agent keeps the space and
 * the footer names the key that shows the plan review; at 140x40 the review
 * sits beside the agent. */
static void capture_layout(struct kt_session *s, int cols, int rows, const char *marker) {
    char dir[4096], path[4200];
    kt_format(dir, sizeof(dir), "%s/plan-conversation-evidence", f.build);
    kt_mkdir(dir);
    kt_resize(s, cols, rows);
    kt_pump(s, .5);
    CHECK(!s->screen.overflow, "conversation resize");
    CHECK(kt_contains(s, marker), "conversation layout marker");
    kt_format(path, sizeof(path), "%s/returned-%dx%d.html", dir, cols, rows);
    kt_save(s, path);
}

static void capture_layouts(struct kt_session *s) {
    capture_layout(s, 80, 24, "Ctrl-\\ B plan");
    capture_layout(s, 140, 40, "RETURNED / changes requested");
}

static void returned_is_blocked(struct kt_session *s) {
    S("\035");
    S("V");
    U("This revision was returned for changes", 3);
    S("E");
    U("returned for changes; nothing runs", 3);
    CHECK(!kt_contains(s, "INPUT TO HYDRA / execution approval"), "a returned revision offers no approval");
    REFUSE("workflow", "plan", "proposal", "planner");
}

static void approve_revision(struct kt_session *s) {
    char pattern[4096];
    write_draft("Revised after requested changes");
    U("Revision 2 / DRAFT", 10);
    S("V");
    U("READY / awaiting", 30);
    S("E");
    U("INPUT TO HYDRA / execution approval", 3);
    U("Execute planner revision 2", 3);
    S("y");
    kt_format(pattern, sizeof(pattern), "%s/state/v2/projects/*/workflows/launches/*/run-id", f.home);
    {
        double deadline = kt_now() + 30;
        while (!hf_glob_count(pattern) && kt_now() < deadline)
            kt_pump(s, .1);
    }
    hf_glob_one(pattern, pattern, sizeof(pattern));
    kt_read(pattern, run_id, sizeof(run_id));
    hf_trim(run_id);
}

static void run_reaches_planner(struct kt_session *s) {
    char needle[512], text[65536];
    kt_format(needle, sizeof(needle), "Hydra: the user approved revision 2 (digest ");
    agent_until(s, needle, 15);
    kt_format(needle, sizeof(needle), "and run %s started. Follow it with hydra workflow status %s --json", run_id, run_id);
    agent_until(s, needle, 5);
    kt_format(needle, sizeof(needle), "Hydra: run %s finished: succeeded.", run_id);
    agent_until(s, needle, 90);
    agent_input(text, sizeof(text));
    CHECK(hf_count(text, "started. Follow it") == 1, "the launch message is typed once");
}

static void owner_recorded_notices(void) {
    char needle[512];
    kt_format(needle, sizeof(needle), "run %s started from your approved plan", run_id);
    CHECK(hf_count(hf_run(&f, NULL, 0, (const char *[]){"grep", "-rl", needle, f.home, NULL}), "\n") == 1,
          "the run owner records one start notice for the planning head");
    kt_format(needle, sizeof(needle), "run %s finished: succeeded", run_id);
    CHECK(hf_count(hf_run(&f, NULL, 0, (const char *[]){"grep", "-rl", needle, f.home, NULL}), "\n") == 1,
          "the run owner records one completion notice for the planning head");
}

int main(void) {
    struct kt_session session, *s = &session;
    kt_init();
    hf_init(&f, "hydra-plan-conversation", "repo", true, true);
    kt_format(input_path, sizeof(input_path), "%s/agent-input", f.base);
    kt_format(draft, sizeof(draft), "%s/draft.json", f.base);
    hf_commit_init(&f);
    H("spawn", "planner", "--no-agent");
    write_draft("Deliver a report and verify its contents");
    open_conversation(s);
    request_changes(s);
    capture_layouts(s);
    returned_is_blocked(s);
    approve_revision(s);
    run_reaches_planner(s);
    kt_close(s, "q", 0, 0);
    owner_recorded_notices();
    hf_finish(&f);
    puts("PASS plan conversation: request changes returns the exact revision and types feedback for the user to send; "
         "approval types the run receipt and outcome into the planning agent; the owner records inbox notices");
    return 0;
}
