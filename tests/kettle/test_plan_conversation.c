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
/* The Plan page's state line: "READY · revision 1 · awaiting you". */
#define STATE(word, n) word " \xc2\xb7 revision " #n
/* Shift-Tab from Work's list: focus goes to the side pane and its Plan page. */
#define PLAN_PAGE "\033[Z"

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
/* The policy question's dialog: the row of its title and the columns of its
 * side borders, from the corners of that row. */
static int dialog_left, dialog_right;
static int form_top(struct kt_session *s) {
    char row[1024];
    int y = 0, x;
    while (y < s->screen.rows && !strstr(kt_row(s, y, row, sizeof(row)), "Review proposal")) y++;
    CHECK(y < s->screen.rows, "policy question visible");
    for (x = 0; x < s->screen.cols && strcmp(s->screen.cells[y * s->screen.cols + x].text, "\xe2\x95\xad"); x++) {}
    dialog_left = x;
    for (x = dialog_left + 1; x < s->screen.cols && strcmp(s->screen.cells[y * s->screen.cols + x].text, "\xe2\x95\xae"); x++) {}
    dialog_right = x;
    CHECK(dialog_left < dialog_right && dialog_right < s->screen.cols, "policy question framed");
    return y;
}

/* A row of the dialog's inside, without its leading and trailing blanks. */
static const char *dialog_row(struct kt_session *s, int y, char *row, size_t capacity) {
    size_t n = 0;
    int x;
    char *start;
    for (x = dialog_left + 1; x < dialog_right; x++) {
        const char *t = s->screen.cells[y * s->screen.cols + x].text;
        size_t len = strlen(t);
        if (!len) { t = " "; len = 1; }
        if (n + len + 1 >= capacity) break;
        memcpy(row + n, t, len);
        n += len;
    }
    row[n] = 0;
    while (n && row[n - 1] == ' ') row[--n] = 0;
    for (start = row; *start == ' '; start++) {}
    memmove(row, start, strlen(start) + 1);
    return row;
}

/* The question's rows, trimmed and joined with single spaces. */
static void form_prompt(struct kt_session *s, char *joined, size_t capacity) {
    char row[1024];
    size_t used = 0;
    int y;
    joined[0] = 0;
    for (y = form_top(s) + 1; y < s->screen.rows && !strstr(dialog_row(s, y, row, sizeof(row)), "Cancel"); y++)
        if (row[0]) used += (size_t)snprintf(joined + used, capacity - used, "%s%s", used ? " " : "", row);
    CHECK(used < capacity, "prompt rows fit");
}

/* The text field wraps its prompt at word boundaries: its rows joined with
 * single spaces reproduce the prompt's phrases exactly, which a row broken
 * inside a word ("No e" / "xecution") would not. */
static bool form_words_whole(struct kt_session *s) {
    static const char *const phrases[] = {"Review with local policy:", "writes only in heads the plan spawns,",
        "1 worker, 4 heads, 60 minutes, 1 MiB artifacts, 1 GiB free disk, no retries or repairs.",
        "The plan carries its prompts and assets; nothing is committed to your checkout.", "No execution yet."};
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
    U("Heads \xe2\x94\x80", 3);
    /* The published proposal makes the planner's side pane its Plan page. */
    U("Plan ready", 8);
    S("a");
    U("typing to ", 5);
    U("shell · session running", 5);
    kt_pump(s, .5);
    kt_format(command, sizeof(command), "/bin/cat > '%s'\r", input_path);
    S(command);
    kt_pump(s, .5);
    S("\035");
    S(PLAN_PAGE);
    kt_pump(s, .3);
    S("P");
    U("Review with local policy", 3);
    CHECK(form_words_whole(s), "policy prompt wraps at words (140 columns)");
    kt_resize(s, 80, 24);
    U("No execution yet.", 5);
    CHECK(form_words_whole(s), "policy prompt wraps at words (80 columns)");
    kt_resize(s, 140, 40);
    U("Review with local policy", 5);
    S("y");
    U(STATE("DRAFT", 1), 5);
}

static void request_changes(struct kt_session *s) {
    char text[65536];
    S("V");
    U(STATE("READY", 1) " \xc2\xb7 awaiting", 30);
    S("F");
    U("Changes to request from planner", 3);
    S("Split verify into two checks\r");
    U("Returned. Feedback typed into planner", 5);
    U(STATE("RETURNED", 1), 3);
    /* The feedback waits in the planner's input, which has focus. */
    U("typing to ", 3);
    kt_pump(s, 1);
    agent_input(text, sizeof(text));
    CHECK(!strstr(text, "Split verify"), "feedback waits for the user to press Enter");
    S("\r");
    agent_until(s, "Hydra: the user requested changes to plan revision 1: Split verify into two checks", 5);
}

/* Evidence of the conversation layout: at 80x24 the agent keeps the shared
 * pane and the footer names the chord that shows the Plan page there; at
 * 140x40 the Plan page sits beside the agent. */
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
    capture_layout(s, 80, 24, "ctrl+\\ B Plan");
    capture_layout(s, 140, 40, STATE("RETURNED", 1));
}

static void returned_is_blocked(struct kt_session *s) {
    S("\035");
    S(PLAN_PAGE);
    kt_pump(s, .3);
    S("V");
    U("This revision was returned for changes", 3);
    S("E");
    U("returned for changes; nothing runs", 3);
    CHECK(!kt_contains(s, "Execute plan"), "a returned revision offers no approval");
    REFUSE("workflow", "plan", "proposal", "planner");
}

/* The agent publishes again while the approval dialog for revision 2 is
 * open: the page behind it moves to revision 3, and confirming the dialog
 * approves nothing, because it named revision 2. */
static void approval_outlived(struct kt_session *s) {
    char pattern[4096];
    write_draft("Revised after requested changes");
    U(STATE("DRAFT", 2), 10);
    S("E");
    U("Approve and run planner revision 2?", 3);
    write_draft("Revised again while the dialog was open");
    U(STATE("DRAFT", 3), 10);
    CHECK(kt_contains(s, "Approve and run planner revision 2?"), "the dialog still names revision 2");
    S("y");
    U("Plan not approved: the revision changed", 5);
    kt_pump(s, 2);
    kt_format(pattern, sizeof(pattern), "%s/state/v2/projects/*/workflows/launches/*", f.home);
    CHECK(!hf_glob_count(pattern), "confirming a dialog for an older revision launches nothing");
}

/* Approval is one action: E on a revision that is not validated yet asks
 * once, validates exactly that revision and runs it when it is ready. */
static void approve_revision(struct kt_session *s) {
    char pattern[4096];
    S("E");
    U("Approve plan", 3);
    U("Approve and run planner revision 3?", 3);
    S("y");
    U("Approved revision 3; validating it, then it runs", 5);
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
    kt_format(needle, sizeof(needle), "Hydra: the user approved revision 3 (digest ");
    agent_until(s, needle, 15);
    kt_format(needle, sizeof(needle), "and run %s started. Follow it with hydra workflow status %s --json", run_id, run_id);
    agent_until(s, needle, 5);
    kt_format(needle, sizeof(needle), "Hydra: run %s finished: succeeded.", run_id);
    agent_until(s, needle, 90);
    agent_input(text, sizeof(text));
    CHECK(hf_count(text, "started. Follow it") == 1, "the launch message is typed once");
    /* The Plan page's graph follows the revision's recorded run, not the preview. */
    U("plan-fixture / succeeded", 15);
    CHECK(!kt_contains(s, "awaiting-approval"), "the finished run's graph no longer awaits approval");
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
    approval_outlived(s);
    approve_revision(s);
    run_reaches_planner(s);
    kt_close(s, "q", 0, 0);
    owner_recorded_notices();
    hf_finish(&f);
    puts("PASS plan conversation: request changes returns the exact revision and types feedback for the user to send; "
         "approval types the run receipt and outcome into the planning agent; the owner records inbox notices");
    return 0;
}
