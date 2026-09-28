#define _XOPEN_SOURCE 700
/* Remote setup journeys through the real native control centre (U10). A
 * scripted `hydra remote` (tests/fixtures/tui/setup/fake-hydra-setup.sh)
 * replays recorded envelopes with the CLI's exit codes; the checks read the
 * reconstructed terminal and the fake's argv log, never the TUI's state.
 * HTML and text captures land in $BUILD_DIR/remote-setup-evidence. */
#include "pty_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>

static char root[4096], build[4096], base[4096], tui[4096], state[4096], evidence[4096], label[64];
static const char *fingerprint = "SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8";
/* Plan hashes of the recorded provision and installer plans. */
static char provision_hash[80], install_hash[80];

static void plan_hash(const char *file, char *out, size_t size) {
    char path[4096], text[65536];
    const char *start;
    size_t length;
    tv_format(path, sizeof(path), "%s/tests/fixtures/tui/setup/%s", root, file);
    tv_read(path, text, sizeof(text));
    start = strstr(text, "\"plan_sha256\":\"");
    CHECK(start != NULL, "a recorded plan has its hash");
    start += strlen("\"plan_sha256\":\"");
    length = strcspn(start, "\"");
    CHECK(length == 64 && length < size, "a recorded plan hash is a SHA-256");
    memcpy(out, start, length);
    out[length] = '\0';
}

static void flag(const char *name) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s", state, name);
    tv_write(path, "");
}

static bool flagged(const char *name) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s", state, name);
    return tv_exists(path);
}

static void reset(const char *const *flags) {
    const char *remove[] = {"rm", "-rf", state, NULL};
    char home[4096];
    tv_command_ok(NULL, remove);
    tv_mkdir(state);
    tv_format(home, sizeof(home), "%s/home", state);
    tv_mkdir(home);
    setenv("HYDRA_HOME", home, 1);
    for (; *flags; flags++) flag(*flags);
    flag("calls.log");
}

static bool called(const char *text) {
    char path[4096], log[65536];
    tv_format(path, sizeof(path), "%s/calls.log", state);
    tv_read(path, log, sizeof(log));
    return strstr(log, text) != NULL;
}

/* Calls whose line starts with prefix. */
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

static void capture(struct tv_session *s, const char *name) {
    char path[4096];
    /* Let an incremental repaint finish so the evidence is a whole frame. */
    tv_pump(s, .3);
    tv_format(path, sizeof(path), "%s/%s-%s.html", evidence, label, name);
    tv_save(s, path);
    tv_format(path, sizeof(path), "%s/%s-%s.txt", evidence, label, name);
    tv_write(path, tv_text(s));
}

/* Screens repaint incrementally; wait for each marker rather than sample. */
static void see(struct tv_session *s, const char *text) { tv_until(s, text, 3); }

static void launch(struct tv_session *s, int cols, int rows) {
    const char *argv[] = {tui, NULL};
    tv_format(label, sizeof(label), "%dx%d", cols, rows);
    tv_open(s, argv, cols, rows, base);
    tv_until(s, "press H for Hosts", 8);
    capture(s, "01-entry");
}

/* The ordinary control centre: H opens Hosts, A adds a host. */
static void add_host(struct tv_session *s) {
    tv_send(s, "H");
    tv_until(s, "+ Add a host", 4);
    capture(s, "02-hosts");
    tv_send(s, "A");
    tv_until(s, "ADD A REMOTE HOST", 4);
    tv_send(s, "deploy@ovh.example.net\tovh");
    tv_until(s, "deploy@ovh.example.net", 3);
    capture(s, "03-form");
    tv_send(s, "\r");
}

static void trust_key(struct tv_session *s) {
    char expected[512];
    tv_until(s, "TRUST THE HOST KEY OF ovh", 6);
    see(s, fingerprint); see(s, "known_hosts");
    capture(s, "04-trust-key");
    tv_send(s, "no\r");
    tv_until(s, "Type yes (the whole word)", 3);
    CHECK(!called("trust-key"), "a wrong confirmation runs nothing");
    tv_send(s, "\x7f\x7fyes\r");
    tv_until(s, "MISSING REQUIREMENTS ON ovh", 6);
    tv_format(expected, sizeof(expected), "trust-key ovh --fingerprint %s --json", fingerprint);
    CHECK(called(expected), "typed yes trusts exactly the shown fingerprint");
    CHECK(called("preflight ovh --json"), "the guided flow checks requirements as their own step");
}

/* Missing tmux stops the guided run (heads cannot run there) until Enter;
 * other warnings, such as a group-writable umask, do not. */
static void requirements(struct tv_session *s, bool no_tmux) {
    see(s, "BLOCKS"); see(s, "install git (for example");
    capture(s, "05-preflight-blocked");
    flag("git_fixed");
    tv_send(s, "\r");
    if (no_tmux) {
        tv_until(s, "HEADS CANNOT RUN ON ovh YET", 6);
        see(s, "fix: sudo apt-get install tmux"); see(s, "Enter continues setup anyway");
        CHECK(!called("setup ovh --json"), "the pause runs nothing further");
        capture(s, "05b-heads-paused");
        tv_send(s, "\r");
    }
    tv_until(s, "REVIEW: INSTALL HYDRA ON ovh", 6);
    CHECK(no_tmux || !tv_contains(s, "HEADS CANNOT RUN"), "other warnings do not pause setup");
    see(s, "pinned: its digest is recorded");
    capture(s, "06-provision-plan");
}

static void provision(struct tv_session *s) {
    char expected[512];
    tv_send(s, "n");
    tv_until(s, "Not approved: nothing was changed", 3);
    CHECK(!called("provision ovh"), "declining runs nothing");
    capture(s, "07-declined");
    tv_send(s, "\r");
    tv_until(s, "REVIEW: INSTALL HYDRA ON ovh", 6);
    tv_send(s, "y");
    tv_until(s, "AGENTS ON ovh", 8);
    tv_format(expected, sizeof(expected), "provision ovh --approve %s --json", provision_hash);
    CHECK(called(expected), "approval runs the reviewed plan hash");
    CHECK(called("agents ovh --json"), "the agent inventory is shown after provisioning");
    see(s, "Install claude");
    capture(s, "08-agents");
    tv_send(s, "j");
    tv_until(s, "> Install claude", 3);
    tv_send(s, "\r");
    tv_until(s, "REVIEW: INSTALL claude ON ovh", 8);
    CHECK(called("install-agent ovh --agent claude --json"), "choosing an agent asks the CLI for its installer plan");
    see(s, "curl -fsSL https://claude.ai/install.sh");
    capture(s, "09-install-plan");
}

static void install_agent(struct tv_session *s) {
    char expected[512];
    tv_send(s, "y");
    tv_until(s, "SIGN IN TO CLAUDE ON OVH", 10);
    tv_format(expected, sizeof(expected), "install-agent ovh --agent claude --approve %s\n", install_hash);
    CHECK(called(expected) && flagged("tty-install"), "the approved installer ran in the terminal without --json");
    CHECK(flagged("installed"), "installer completed");
    capture(s, "10-sign-in-handoff");
}

static void sign_in(struct tv_session *s, bool fail_once) {
    tv_send(s, "\r");
    tv_until(s, "FAKE SIGN-IN: open", 6);
    see(s, "Hydra: Sign in to claude"); see(s, "On success you return to Hydra at once");
    capture(s, "11-sign-in-terminal");
    tv_send(s, "\r");
    if (fail_once) {
        tv_until(s, "Press Enter to return to Hydra", 6);
        tv_send(s, "\r");
        tv_until(s, "Sign-in did not finish", 6);
        see(s, "Enter signs in again");
        see(s, "exited with a nonzero status");
        capture(s, "12-sign-in-failed");
        tv_send(s, "\r");
        tv_until(s, "FAKE SIGN-IN: open", 6);
        tv_send(s, "\r");
    }
    tv_until(s, "ovh IS READY", 10);
    CHECK(flagged("tty-sign-in") && flagged("signed"), "sign-in ran in the terminal");
    capture(s, "13-done");
}

static void journey(int cols, int rows, bool fail_once) {
    struct tv_session s;
    const char *flags[] = {"needs_git", fail_once ? "fail_sign_in" : "no_tmux", NULL};
    reset(flags);
    launch(&s, cols, rows);
    add_host(&s);
    trust_key(&s);
    requirements(&s, !fail_once);
    provision(&s);
    install_agent(&s);
    sign_in(&s, fail_once);
    tv_send(&s, "\r");
    tv_until(&s, "+ Add a host", 4);
    tv_until(&s, "set up", 6);
    capture(&s, "14-hosts-after");
    tv_close(&s, "q", 0, 0);
}

/* A host key that is already known lets the guided run pass preflight on its
 * own; the requirements are still checked and missing tmux still pauses,
 * then Enter shows the plan the guided run returned without rerunning it. */
static void known_key(int cols, int rows) {
    struct tv_session s;
    const char *flags[] = {"known_key", "no_tmux", NULL};
    reset(flags);
    launch(&s, cols, rows);
    add_host(&s);
    tv_until(&s, "HEADS CANNOT RUN ON ovh YET", 6);
    CHECK(called("preflight ovh --json"), "a preflight passed inside the guided run is checked");
    capture(&s, "21-known-key-paused");
    tv_send(&s, "\r");
    tv_until(&s, "REVIEW: INSTALL HYDRA ON ovh", 6);
    CHECK(calls("setup ovh ") == 1, "continuing shows the kept plan without rerunning setup");
    tv_close(&s, "q", 0, 0);
}

/* A changed key is refused: no accept action exists, typing yes does nothing
 * and only the read-only check reruns. */
static void changed_key(int cols, int rows) {
    struct tv_session s;
    const char *flags[] = {"changed", NULL};
    reset(flags);
    launch(&s, cols, rows);
    add_host(&s);
    tv_until(&s, "THE HOST KEY CHANGED", 6);
    see(&s, "ssh-keygen -R ovh.example.net -f /Users/you/.ssh/known_hosts");
    tv_pump(&s, .3);
    CHECK(!tv_contains(&s, "Type yes") && !tv_contains(&s, "y approve"), "manual recovery only");
    capture(&s, "20-key-changed");
    tv_send(&s, "yes y");
    tv_send(&s, "\r");
    tv_pump(&s, 1);
    tv_until(&s, "THE HOST KEY CHANGED", 6);
    CHECK(!called("trust-key"), "a changed key is never trusted");
    tv_send(&s, "\033");
    tv_until(&s, "Continue setup", 3);
    tv_close(&s, "q", 0, 0);
}

/* A hanging step shows what runs and for how long, and Esc or c stops it:
 * the child is gone and the recorded status is shown instead. */
/* Starts setup from the form with a ~/ SSH config, which reaches the CLI
 * as an absolute path. */
static void add_host_tilde(struct tv_session *s) {
    char config[4096];
    tv_send(s, "H");
    tv_until(s, "+ Add a host", 4);
    tv_send(s, "A");
    tv_until(s, "ADD A REMOTE HOST", 4);
    tv_send(s, "deploy@ovh.example.net\tovh\t~/.ssh/hydra-test");
    tv_until(s, "~/.ssh/hydra-test", 3);
    tv_send(s, "\r");
    tv_until(s, "Checking the host key", 6);
    tv_format(config, sizeof(config), "setup ovh deploy@ovh.example.net --ssh-config %s/.ssh/hydra-test --json", getenv("HOME"));
    CHECK(called(config), "a ~/ SSH config path reaches the CLI as an absolute path");
}

static void cancel_running(int cols, int rows, const char *key) {
    struct tv_session s;
    const char *flags[] = {"slow_host_key", NULL};
    char path[4096], text[64];
    long pid;
    reset(flags);
    launch(&s, cols, rows);
    add_host_tilde(&s);
    tv_until(&s, "host key\xe2\x80\xa6 2s", 6);
    see(&s, "c cancel");
    capture(&s, "30-running");
    tv_format(path, sizeof(path), "%s/slow.pid", state);
    tv_read(path, text, sizeof(text));
    pid = strtol(text, NULL, 10);
    CHECK(pid > 0 && kill((pid_t)pid, 0) == 0, "the slow step is running");
    tv_send(&s, key);
    tv_until(&s, "Cancelled: Checking the host key", 6);
    see(&s, "> Continue setup");
    capture(&s, "31-cancelled");
    CHECK(kill((pid_t)pid, 0) != 0, "cancelling stops the step's process");
    CHECK(called("setup status ovh --json"), "after a cancel the recorded status is reread");
    CHECK(!called("trust-key"), "cancelling runs no other step");
    tv_close(&s, "q", 0, 0);
}

/* Hosts: e edits an unfinished setup that has not changed the remote. The
 * same name replaces its record, after its removal plan is approved. */
static void edit_setup(int cols, int rows) {
    struct tv_session s;
    const char *flags[] = {"early", NULL};
    char expected[4096];
    reset(flags);
    launch(&s, cols, rows);
    tv_send(&s, "H");
    tv_until(&s, "Check requirements: blocked", 6);
    tv_send(&s, "j");
    tv_until(&s, "e edit", 3);
    capture(&s, "40-hosts-unfinished");
    tv_send(&s, "e");
    tv_until(&s, "EDIT THE SETUP OF ovh", 4);
    see(&s, "deploy@ovh.example.net");
    tv_send(&s, "\t\t~/.ssh/hydra-private");
    tv_until(&s, "~/.ssh/hydra-private", 3);
    capture(&s, "41-edit-form");
    tv_send(&s, "\r");
    tv_until(&s, "REMOVE THE SETUP RECORD OF ovh?", 6);
    see(&s, "Hydra does not connect");
    CHECK(called("setup remove ovh --json"), "an edit asks the CLI for the removal plan");
    CHECK(!flagged("removed"), "nothing is removed before approval");
    capture(&s, "42-edit-removal-plan");
    tv_send(&s, "y");
    tv_until(&s, "TRUST THE HOST KEY OF ovh", 8);
    tv_format(expected, sizeof(expected), "setup ovh deploy@ovh.example.net --ssh-config %s/.ssh/hydra-private --json", getenv("HOME"));
    CHECK(flagged("removed") && called(expected), "the approved removal restarts setup with the edited SSH config");
    tv_send(&s, "\033");
    tv_until(&s, "Continue setup", 4);
    tv_close(&s, "q", 0, 0);
}

/* Hosts: x removes an unfinished setup's record after its plan; n keeps it. */
static void remove_setup(int cols, int rows) {
    struct tv_session s;
    const char *flags[] = {"early", NULL};
    reset(flags);
    launch(&s, cols, rows);
    tv_send(&s, "H");
    tv_until(&s, "Check requirements: blocked", 6);
    tv_send(&s, "jx");
    tv_until(&s, "REMOVE THE SETUP RECORD OF ovh?", 6);
    see(&s, "Left on remote");
    capture(&s, "43-remove-plan");
    tv_send(&s, "n");
    tv_until(&s, "Kept the setup record for ovh", 4);
    CHECK(!flagged("removed") && !called("--approve"), "declining keeps the record");
    tv_send(&s, "x");
    tv_until(&s, "REMOVE THE SETUP RECORD OF ovh?", 6);
    tv_send(&s, "y");
    tv_until(&s, "Removed the setup record for ovh", 6);
    CHECK(flagged("removed"), "y removes the record");
    tv_pump(&s, 1);
    CHECK(!tv_contains(&s, "Check requirements: blocked"), "the removed setup leaves the Hosts list");
    capture(&s, "44-removed");
    tv_close(&s, "q", 0, 0);
}

/* A new host whose name already has an unfinished setup is caught before
 * anything runs; Enter continues that setup instead. */
static void duplicate_name(int cols, int rows) {
    struct tv_session s;
    const char *flags[] = {"early", NULL};
    reset(flags);
    launch(&s, cols, rows);
    tv_send(&s, "H");
    tv_until(&s, "Check requirements: blocked", 6);
    tv_send(&s, "A");
    tv_until(&s, "ADD A REMOTE HOST", 4);
    tv_send(&s, "deploy@ovh.example.net\tovh\r");
    tv_until(&s, "ovh ALREADY HAS AN UNFINISHED SETUP", 4);
    see(&s, "Enter continues that setup");
    CHECK(!called("setup ovh deploy@"), "a duplicate name runs nothing");
    capture(&s, "45-duplicate");
    tv_send(&s, "\r");
    tv_until(&s, "Continue setup", 6);
    CHECK(called("setup status ovh --json"), "Enter continues the existing setup");
    tv_close(&s, "q", 0, 0);
}

int main(void) {
    char fake[4096], fixture[4096];
    tv_init();
    tv_paths(root, sizeof(root), build, sizeof(build));
    tv_temp(base, sizeof(base), "hydra-remote-setup");
    tv_format(tui, sizeof(tui), "%s/hydra-tui", build);
    tv_format(state, sizeof(state), "%s/state", base);
    tv_format(evidence, sizeof(evidence), "%s/remote-setup-evidence", build);
    tv_mkdir(evidence);
    tv_format(fake, sizeof(fake), "%s/tests/fixtures/tui/setup/fake-hydra-setup.sh", root);
    tv_format(fixture, sizeof(fixture), "%s/tests/fixtures/tui/setup/empty.tsv", root);
    plan_hash("provision-approval.json", provision_hash, sizeof(provision_hash));
    plan_hash("install-approval.json", install_hash, sizeof(install_hash));
    setenv("HYDRA_BIN_CMD", fake, 1);
    setenv("HYDRA_SETUP_FAKE", state, 1);
    setenv("HYDRA_TUI_FIXTURE", fixture, 1);
    setenv("TERM", "xterm-256color", 1);
    unsetenv("HYDRA_NONINTERACTIVE");
    journey(140, 40, true);
    journey(80, 24, false);
    changed_key(140, 40);
    changed_key(80, 24);
    known_key(80, 24);
    cancel_running(80, 24, "c");
    cancel_running(140, 40, "\033");
    edit_setup(80, 24);
    remove_setup(140, 40);
    duplicate_name(80, 24);
    {
        const char *remove[] = {"rm", "-rf", base, NULL};
        tv_command_ok(NULL, remove);
    }
    puts("PASS remote setup: add host, typed-yes key trust, blocking requirements, a missing-tmux pause, declined and "
         "approved provision, agent choice, installer and sign-in terminal hand-off (with a failed attempt), done; changed "
         "key refused; known key checked; a hanging step shown with its time and cancelled; an unfinished setup edited, removed (and kept), and a duplicate name caught; 140x40 and 80x24");
    return 0;
}
