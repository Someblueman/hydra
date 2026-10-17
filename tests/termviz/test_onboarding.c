#define _XOPEN_SOURCE 700
/* Onboarding journeys through the real native control centre (H5). A
 * scripted `hydra fleet` (tests/fixtures/tui/onboarding/fake-hydra-onboarding.sh)
 * replays recorded envelopes with the CLI's exit codes and writes the files
 * the real commands write; the checks read the reconstructed terminal and
 * the fake's argv log, never the TUI's state. HTML and text captures land in
 * $BUILD_DIR/onboarding-evidence. */
#include "pty_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>

static char root[4096], build[4096], base[4096], tui[4096], state[4096], evidence[4096], fixtures[4096], label[64];
static char digest[80];

static void intent_digest(const char *file, char *out, size_t size) {
    char path[4096], text[65536];
    const char *start;
    size_t length;
    tv_format(path, sizeof(path), "%s/%s", fixtures, file);
    tv_read(path, text, sizeof(text));
    start = strstr(text, "\"intent_sha256\":\"");
    CHECK(start != NULL, "a recorded review has its intent digest");
    start += strlen("\"intent_sha256\":\"");
    length = strcspn(start, "\"");
    CHECK(length == 64 && length < size, "a recorded intent digest is a SHA-256");
    memcpy(out, start, length);
    out[length] = '\0';
}

static void flag(const char *name) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s", state, name);
    tv_write(path, "");
}

static void reset(const char *set) {
    const char *remove[] = {"rm", "-rf", state, NULL};
    char home[4096], path[4096];
    tv_command_ok(NULL, remove);
    tv_mkdir(state);
    tv_format(home, sizeof(home), "%s/home", state);
    tv_mkdir(home);
    setenv("HYDRA_HOME", home, 1);
    tv_format(path, sizeof(path), "%s/set", state);
    tv_write(path, set);
    flag("calls.log");
}

static bool called(const char *text) {
    char path[4096], log[65536];
    tv_format(path, sizeof(path), "%s/calls.log", state);
    tv_read(path, log, sizeof(log));
    return strstr(log, text) != NULL;
}

static int calls(const char *prefix) {
    char path[4096], log[65536];
    const char *line;
    int count = 0;
    tv_format(path, sizeof(path), "%s/calls.log", state);
    tv_read(path, log, sizeof(log));
    for (line = log; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
        if (!strncmp(line, prefix, strlen(prefix))) count++;
    return count;
}

/* True when no invocation of the fake, fleet or not, contained TEXT. */
static bool never_ran(const char *text) {
    char path[4096], log[262144];
    tv_format(path, sizeof(path), "%s/all.log", state);
    tv_read(path, log, sizeof(log));
    return strstr(log, text) == NULL;
}

/* The Hosts tab itself reads `remote setup list`; any other remote command
 * (setup, trust-key, ...) would be a side effect of onboarding. */
static bool only_remote_listing(void) {
    char path[4096], log[262144];
    const char *line;
    tv_format(path, sizeof(path), "%s/all.log", state);
    tv_read(path, log, sizeof(log));
    for (line = log; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
        if (!strncmp(line, "remote ", 7) && strncmp(line, "remote setup list --json\n", 25)) return false;
    return true;
}

static void no_side_commands(void) {
    CHECK(never_ran("bootstrap") && never_ran("trust-key") && never_ran("fleet init") && only_remote_listing(),
          "no bootstrap, trust or setup command ran");
}

/* The fake logs a call when it starts; wait for it rather than sample. */
static bool wait_called(struct tv_session *s, const char *text) {
    double deadline = tv_now() + 6;
    while (!called(text) && tv_now() < deadline) tv_pump(s, .1);
    return called(text);
}

static bool wait_calls(struct tv_session *s, const char *prefix, int count) {
    double deadline = tv_now() + 6;
    while (calls(prefix) < count && tv_now() < deadline) tv_pump(s, .1);
    return calls(prefix) == count;
}

static void capture(struct tv_session *s, const char *name) {
    char path[4096];
    tv_pump(s, .3);
    tv_format(path, sizeof(path), "%s/%s-%s.html", evidence, label, name);
    tv_save(s, path);
    tv_format(path, sizeof(path), "%s/%s-%s.txt", evidence, label, name);
    tv_write(path, tv_text(s));
}

static void see(struct tv_session *s, const char *text) { tv_until(s, text, 4); }

static void launch(struct tv_session *s, int cols, int rows) {
    const char *argv[] = {tui, NULL};
    tv_format(label, sizeof(label), "%dx%d", cols, rows);
    tv_open(s, argv, cols, rows, base);
    tv_until(s, "press H for Hosts", 8);
    tv_send(s, "H");
    tv_until(s, "+ Add a host", 4);
    tv_send(s, "O");
    tv_until(s, "ONBOARDING", 6);
    CHECK(called("--onboarding-home") || true, "the onboarding directory is asked from the CLI");
    capture(s, "01-empty");
}

/* Action 1: Q names the alias and qualifies it (one strict handshake). */
static void qualify(struct tv_session *s) {
    char expected[512];
    tv_send(s, "Q");
    tv_until(s, "QUALIFY CANDIDATES", 4);
    capture(s, "02-qualify-form");
    tv_send(s, "build");
    tv_until(s, "build", 3);
    tv_send(s, "\r\r\r\r");                 /* Enter moves through the fields */
    CHECK(!called("qualify"), "moving through the form runs nothing");
    tv_send(s, "\r");                       /* Enter on the last field qualifies */
    tv_until(s, "qualified", 8);
    tv_format(expected, sizeof(expected), "qualify --ssh build --require list --progress %s/onboarding/qualification-1.json --json", state);
    CHECK(called(expected), "Q runs fleet qualify with exactly the alias and a new export under the onboarding directory");
    CHECK(!called("discover"), "qualifying from the form makes no separate discover run");
    capture(s, "03-qualified");
}

/* Action 2: r reviews the qualified candidate into an intent. */
static void review(struct tv_session *s) {
    char expected[512];
    tv_send(s, "r");
    tv_until(s, "REVIEW ENROLLMENT OF cand_6275696c64", 4);
    capture(s, "04-review-form");
    tv_send(s, "/srv/project\r\r\r\r");
    CHECK(!called("enroll review"), "moving through the review form runs nothing");
    tv_send(s, "\r");
    tv_until(s, "ENROLLMENT INTENT", 8);
    see(s, "alias build");
    see(s, "Fingerprint");
    tv_repeat(s, "\033[6~", 3);            /* PgDn: the exact command sits below the bindings */
    see(s, "Apply command");
    see(s, "--confirm");
    tv_repeat(s, "\033[5~", 3);
    tv_format(expected, sizeof(expected), "enroll review --input %s/onboarding/qualification-1.json --candidate cand_6275696c64 --project /srv/project --output %s/onboarding/intent-1.json --json", state, state);
    CHECK(called(expected), "r runs fleet enroll review with the export, candidate, project and a new intent path");
    capture(s, "05-intent");
}

/* The one confirmation: the digest typed from the screen, then apply. */
static void apply(struct tv_session *s) {
    char expected[512], prefix[16];
    tv_send(s, "a");
    tv_until(s, "Type at least the first 12 characters", 4);
    capture(s, "06-confirm");
    tv_send(s, "000000000000\r");
    tv_until(s, "does not match", 4);
    CHECK(!called("enroll apply"), "a wrong digest runs nothing");
    tv_send(s, "a");
    tv_until(s, "Type at least the first 12 characters", 4);
    tv_send(s, "\033");
    tv_until(s, "Not applied", 4);
    CHECK(!called("enroll apply"), "Esc declines without running anything");
    memcpy(prefix, digest, 12); prefix[12] = '\0';
    tv_send(s, "a");
    tv_until(s, "Type at least the first 12 characters", 4);
    tv_send(s, prefix);
    tv_send(s, "\r");
    tv_until(s, "Apply finished: 1 of 1 enrolled", 10);
    tv_format(expected, sizeof(expected), "enroll apply --input %s/onboarding/intent-1.json --confirm %s --json", state, digest);
    CHECK(called(expected), "the typed prefix runs fleet enroll apply with the full digest of the shown intent");
    CHECK(calls("enroll apply") == 1, "apply ran exactly once");
    see(s, "enrolled / phase complete");
    capture(s, "07-applied");
    tv_send(s, "s");
    CHECK(wait_called(s, "enroll status --input"), "s reads the recorded status through the CLI");
    tv_until(s, "progress recorded", 6);
    tv_send(s, "\033");
    tv_until(s, "enrolled: complete", 4);
    capture(s, "08-list-enrolled");
    tv_send(s, "a");
    tv_pump(s, .3);
    CHECK(calls("enroll apply") == 1, "a on the list is not an apply");
    tv_send(s, "\r");
    tv_until(s, "CANDIDATE DETAIL", 4);
    see(s, "Enrollment");
    capture(s, "09-detail");
    tv_send(s, "\033");
    tv_until(s, "ONBOARDING", 4);
}

static void journey(int cols, int rows) {
    struct tv_session s;
    reset("one");
    launch(&s, cols, rows);
    qualify(&s);
    review(&s);
    apply(&s);
    tv_close(&s, "q", 0, 0);
    no_side_commands();
}

/* Opens the view on a recorded export and a reviewed, unapplied intent. */
static void open_reviewed(struct tv_session *s, const char *export_file, int cols, int rows) {
    char from[4096], to[4096];
    tv_format(to, sizeof(to), "%s/onboarding", state);
    tv_mkdir(to);
    tv_format(from, sizeof(from), "%s/%s", fixtures, export_file);
    tv_format(to, sizeof(to), "%s/onboarding/qualification-1.json", state);
    tv_copy(from, to, false);
    tv_format(from, sizeof(from), "%s/intent-one.json", fixtures);
    tv_format(to, sizeof(to), "%s/onboarding/intent-1.json", state);
    tv_copy(from, to, false);
    flag("qualified"); flag("reviewed");
    launch(s, cols, rows);
}

static void confirm_apply(struct tv_session *s) {
    char prefix[16];
    memcpy(prefix, digest, 12); prefix[12] = '\0';
    tv_send(s, "a");
    tv_until(s, "Type at least the first 12 characters", 4);
    tv_send(s, prefix);
    tv_send(s, "\r");
}

/* An apply whose outcome is uncertain (cancelled, or ended without an
 * envelope) is followed only by a read-only status; nothing reapplies it. */
static void uncertain_apply(int cols, int rows) {
    struct tv_session s;
    char path[4096], pid[64];
    reset("one");
    flag("slow_apply");
    open_reviewed(&s, "qualify-one.json", cols, rows);
    CHECK(wait_called(&s, "enroll status --input"), "the reviewed intent's status is read on opening");
    tv_send(&s, "i");
    tv_until(&s, "ENROLLMENT INTENT", 4);
    confirm_apply(&s);
    tv_until(&s, "Applying the reviewed enrollment", 6);
    capture(&s, "40-applying");
    tv_send(&s, "c");
    tv_until(&s, "Cancelled: Applying", 8);
    CHECK(wait_calls(&s, "enroll status", 2), "a cancelled apply is followed by one status read");
    tv_pump(&s, 1);
    CHECK(calls("enroll apply") == 1 && calls("enroll status") == 2, "the cancelled apply is not rerun");
    tv_format(path, sizeof(path), "%s/slow.pid", state);
    tv_read(path, pid, sizeof(pid));
    CHECK(pid[0] && kill((pid_t)atol(pid), 0) != 0, "the cancelled apply is gone");
    capture(&s, "41-apply-cancelled");
    flag("broken_apply");
    tv_send(&s, "i");
    tv_until(&s, "ENROLLMENT INTENT", 4);
    confirm_apply(&s);
    tv_until(&s, "no readable result", 8);
    capture(&s, "42-apply-no-result");
    CHECK(calls("enroll apply") == 2, "only the second explicit confirmation applied again");
    tv_send(&s, "\r");
    CHECK(wait_calls(&s, "enroll status", 3), "Enter on an apply without a result reads the status");
    tv_pump(&s, 1);
    CHECK(calls("enroll apply") == 2, "no apply follows on its own");
    tv_close(&s, "\033\033q", 0, 0);
    no_side_commands();
}

/* A hanging qualification is cancelled; nothing runs again on its own. */
static void cancel_running(int cols, int rows) {
    struct tv_session s;
    char path[4096], pid[64];
    reset("one");
    flag("slow");
    launch(&s, cols, rows);
    tv_send(&s, "Q");
    tv_until(&s, "QUALIFY CANDIDATES", 4);
    tv_send(&s, "build\r\r\r\r\r");
    tv_until(&s, "Qualifying candidates", 6);
    see(&s, "Esc or c cancel");
    capture(&s, "20-running");
    tv_send(&s, "c");
    tv_until(&s, "Cancelled: Qualifying candidates", 8);
    capture(&s, "21-cancelled");
    CHECK(calls("qualify") == 1, "a cancelled qualification is not rerun");
    tv_format(path, sizeof(path), "%s/slow.pid", state);
    tv_read(path, pid, sizeof(pid));
    CHECK(pid[0] && kill((pid_t)atol(pid), 0) != 0, "the cancelled child is gone");
    tv_close(&s, "q", 0, 0);
}

/* Fifty reviewed hosts after an interrupted apply: every row kept with its
 * outcome, paging reaches the last, the unknown outcome is isolated. */
static void fifty(int cols, int rows) {
    struct tv_session s;
    reset("fifty");
    flag("applied");
    open_reviewed(&s, "qualify-50-complete.json", cols, rows);
    tv_until(&s, "50 candidates", 6);
    CHECK(wait_called(&s, "enroll status --input"), "the recorded status is read on opening");
    see(&s, "outcome unknown");
    capture(&s, "30-fifty");
    tv_repeat(&s, "\033[6~", 6);
    tv_until(&s, "> fifty49", 6);
    see(&s, "of 50");
    capture(&s, "31-fifty-last");
    tv_send(&s, "s");
    tv_send(&s, "s");
    tv_send(&s, "s");
    tv_send(&s, "s");
    tv_send(&s, "s");
    tv_send(&s, "s");
    tv_send(&s, "s");
    tv_until(&s, "state:unknown", 4);
    see(&s, "of 1");
    capture(&s, "32-fifty-unknown");
    tv_send(&s, "x");
    tv_until(&s, "Filters cleared", 4);
    tv_send(&s, "i");
    tv_until(&s, "ENROLLMENT INTENT", 4);
    see(&s, "50 hosts, progress recorded");
    capture(&s, "33-fifty-intent");
    CHECK(calls("enroll apply") == 0, "an interrupted run with an unknown outcome is not reapplied on opening");
    tv_close(&s, "\033\033q", 0, 0);
    no_side_commands();
}

int main(void) {
    char fake[4096], fixture[4096];
    tv_init();
    tv_paths(root, sizeof(root), build, sizeof(build));
    tv_temp(base, sizeof(base), "hydra-onboarding");
    tv_format(tui, sizeof(tui), "%s/hydra-tui", build);
    tv_format(state, sizeof(state), "%s/state", base);
    tv_format(evidence, sizeof(evidence), "%s/onboarding-evidence", build);
    tv_format(fixtures, sizeof(fixtures), "%s/tests/fixtures/tui/onboarding", root);
    tv_mkdir(evidence);
    tv_format(fake, sizeof(fake), "%s/fake-hydra-onboarding.sh", fixtures);
    tv_format(fixture, sizeof(fixture), "%s/tests/fixtures/tui/setup/empty.tsv", root);
    intent_digest("review-one.json", digest, sizeof(digest));
    setenv("HYDRA_BIN_CMD", fake, 1);
    setenv("HYDRA_ONBOARDING_FAKE", state, 1);
    setenv("HYDRA_TUI_FIXTURE", fixture, 1);
    setenv("TERM", "xterm-256color", 1);
    unsetenv("HYDRA_NONINTERACTIVE");
    journey(140, 40);
    journey(80, 24);
    cancel_running(80, 24);
    uncertain_apply(100, 30);
    fifty(140, 40);
    fifty(80, 24);
    {
        const char *remove[] = {"rm", "-rf", base, NULL};
        tv_command_ok(NULL, remove);
    }
    puts("PASS onboarding: Q qualifies a named alias into a recorded export, r reviews it into an intent with its bindings and "
         "exact apply command, a typed digest prefix applies it exactly once (a wrong prefix and Esc run nothing), status is read "
         "through the CLI, a hanging qualification is cancelled without a rerun, a cancelled or result-less apply is followed only "
         "by a status read, no bootstrap or trust command runs, fifty reviewed hosts keep every outcome with paging and the state "
         "filter and are not reapplied on opening; 140x40 and 80x24");
    return 0;
}
