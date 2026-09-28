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
#include <sys/stat.h>

static char root[4096], build[4096], base[4096], tui[4096], state[4096], evidence[4096], label[64];
static const char *fingerprint = "SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8";
static const char *provision_hash = "3f6c2a9e1b7d4c8a0e5f2b9d6c3a1e8f7b4d0c2a9e6f3b1d8c5a2e0f7b4d9c1a";
static const char *install_hash = "5e8a1c4f7b2d9e6a3c0f5b8d1e4a7c2f9b6d3e0a5c8f1b4d7e2a9c6f3b0d5e8a";

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

static void capture(struct tv_session *s, const char *name) {
    char path[4096];
    /* Let an incremental repaint finish so the evidence is a whole frame. */
    tv_pump(s, .3);
    tv_format(path, sizeof(path), "%s/%s-%s.html", evidence, label, name);
    tv_save(s, path);
    tv_format(path, sizeof(path), "%s/%s-%s.txt", evidence, label, name);
    tv_write(path, tv_text(s));
}

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
    CHECK(tv_contains(s, fingerprint) && tv_contains(s, "known_hosts"), "fingerprint and file shown");
    capture(s, "04-trust-key");
    tv_send(s, "no\r");
    tv_until(s, "Type yes (the whole word)", 3);
    CHECK(!called("trust-key"), "a wrong confirmation runs nothing");
    tv_send(s, "\x7f\x7fyes\r");
    tv_until(s, "MISSING REQUIREMENTS ON ovh", 6);
    tv_format(expected, sizeof(expected), "trust-key ovh --fingerprint %s --json", fingerprint);
    CHECK(called(expected), "typed yes trusts exactly the shown fingerprint");
}

static void requirements(struct tv_session *s) {
    CHECK(tv_contains(s, "BLOCKS") && tv_contains(s, "fix: sudo apt-get install git"), "blocking prerequisite and its fix");
    capture(s, "05-preflight-blocked");
    flag("git_fixed");
    tv_send(s, "\r");
    tv_until(s, "REVIEW: INSTALL HYDRA ON ovh", 6);
    CHECK(tv_contains(s, "pinned: its digest is recorded"), "pinned runtime source");
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
    tv_until(s, "REVIEW: INSTALL claude ON ovh", 8);
    tv_format(expected, sizeof(expected), "provision ovh --approve %s --json", provision_hash);
    CHECK(called(expected), "approval runs the reviewed plan hash");
    CHECK(tv_contains(s, "curl -fsSL https://claude.ai/install.sh | bash"), "exact installer command shown");
    capture(s, "08-install-plan");
}

static void install_agent(struct tv_session *s) {
    char expected[512];
    tv_send(s, "y");
    tv_until(s, "SIGN IN TO CLAUDE ON OVH", 10);
    tv_format(expected, sizeof(expected), "install-agent ovh --agent claude --approve %s\n", install_hash);
    CHECK(called(expected) && flagged("tty-install"), "the approved installer ran in the terminal without --json");
    CHECK(flagged("installed"), "installer completed");
    capture(s, "09-sign-in-handoff");
}

static void sign_in(struct tv_session *s, bool fail_once) {
    tv_send(s, "\r");
    tv_until(s, "FAKE SIGN-IN: open", 6);
    CHECK(tv_contains(s, "Hydra: Sign in to claude"), "the terminal is handed over with a label");
    capture(s, "10-sign-in-terminal");
    tv_send(s, "\r");
    if (fail_once) {
        tv_until(s, "Press Enter to return to Hydra", 6);
        tv_send(s, "\r");
        tv_until(s, "Sign-in did not finish", 6);
        CHECK(tv_contains(s, "Enter signs in again"), "a failed sign-in offers another attempt");
        capture(s, "11-sign-in-failed");
        tv_send(s, "\r");
        tv_until(s, "FAKE SIGN-IN: open", 6);
        tv_send(s, "\r");
    }
    tv_until(s, "ovh IS READY", 10);
    CHECK(flagged("tty-sign-in") && flagged("signed"), "sign-in ran in the terminal");
    capture(s, "12-done");
}

static void journey(int cols, int rows, bool fail_once) {
    struct tv_session s;
    const char *flags[] = {"needs_git", fail_once ? "fail_sign_in" : NULL, NULL};
    reset(flags);
    launch(&s, cols, rows);
    add_host(&s);
    trust_key(&s);
    requirements(&s);
    provision(&s);
    install_agent(&s);
    sign_in(&s, fail_once);
    tv_send(&s, "\r");
    tv_until(&s, "+ Add a host", 4);
    tv_until(&s, "set up", 6);
    capture(&s, "13-hosts-after");
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
    CHECK(tv_contains(&s, "ssh-keygen -R ovh.example.net") && !tv_contains(&s, "Type yes"), "manual recovery only");
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
    setenv("HYDRA_BIN_CMD", fake, 1);
    setenv("HYDRA_SETUP_FAKE", state, 1);
    setenv("HYDRA_TUI_FIXTURE", fixture, 1);
    setenv("TERM", "xterm-256color", 1);
    unsetenv("HYDRA_NONINTERACTIVE");
    journey(140, 40, true);
    journey(80, 24, false);
    changed_key(140, 40);
    changed_key(80, 24);
    {
        const char *remove[] = {"rm", "-rf", base, NULL};
        tv_command_ok(NULL, remove);
    }
    puts("PASS remote setup: add host, typed-yes key trust, blocking requirements, declined and approved provision, "
         "installer and sign-in terminal hand-off (with a failed attempt), done; changed key refused; 140x40 and 80x24");
    return 0;
}
