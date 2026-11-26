#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* Signing in and starting agents from the control centre, through the real CLI
 * and a fixture provider installed as `codex`. With no agent installed the
 * new-task form offers only "none", Hosts says which agents the machine lacks,
 * and visiting them creates no head. An interactive `hydra spawn --profile
 * --prompt` opens the control centre with the agent beside Hydra's top bar;
 * Ctrl-] leaves agent input and q returns to the shell, the task kept. A
 * provider that exits needing a login shows its own instruction inside the
 * control centre, Hydra says the pane is now a shell, and a command typed
 * there runs in the head's worktree. Private tmux socket. */
static struct hf_fixture f;
static const char *const TOP_BAR[] = {"1 Work", "2 Inbox", "3 Hosts", "4 Stats", NULL};
static char normal_path[32768];
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define S(keys) kt_send(&s, (keys))
#define U(marker, seconds) kt_until(&s, (marker), (seconds))

static void provider(const char *body) {
    char path[4096], text[4096];
    kt_format(path, sizeof(path), "%s/bin/codex", f.base);
    kt_format(text, sizeof(text),
              "#!/bin/sh\n"
              "case \"${1:-}\" in\n"
              "    --version) echo 'codex-cli 0.0.0-sign-in-fixture'; exit 0 ;;\n"
              "    --help) echo 'Usage: codex [--version] [--help]'; exit 0 ;;\n"
              "esac\n%s", body);
    kt_write(path, text);
    CHECK(!chmod(path, 0755), "fixture provider executable");
}
static void top_bar(struct kt_session *s, const char *message) {
    char row[512];
    int i;
    kt_row(s, 0, row, sizeof(row));
    for (i = 0; TOP_BAR[i]; i++)
        CHECK(strstr(row, TOP_BAR[i]), message);
}
/* Hydra's context line: one short line naming head, agent and repository,
 * without raw identities. */
static bool concise_banner(const char *line) {
    return strstr(line, "repo") && strlen(line) <= 80 && !strstr(line, "head_") && !strstr(line, "project_") &&
           !strstr(line, "instance_");
}
/* What the terminal itself shows: no bootstrap export or environment command
 * in the pane's scrollback, and Hydra's context is a concise banner. */
static void banners(char *scrollback) {
    char *line, *end;
    bool banner = false;
    for (line = strstr(scrollback, "Hydra head "); line; line = end ? strstr(end, "Hydra head ") : NULL) {
        end = line + strcspn(line, "\n");
        end = *end ? end + 1 : NULL;
        if (end) end[-1] = 0;
        CHECK(concise_banner(line), "Hydra's context is a concise banner without raw identities");
        banner = true;
    }
    CHECK(banner, "Hydra printed its one-line context banner");
}
static void clean_terminal(const char *session, const char *message) {
    static char scrollback[262144];
    kt_format(scrollback, sizeof(scrollback), "%s", RUN("tmux", "capture-pane", "-p", "-J", "-S", "-", "-t", session));
    CHECK(!strstr(scrollback, "export HYDRA_") && !strstr(scrollback, "HYDRA_PROJECT_ID=") &&
          !strstr(scrollback, "HYDRA_HEAD_ID=") && !strstr(scrollback, "set-environment"), message);
    banners(scrollback);
}
static void worktree_file(const char *head, const char *name, char *out, size_t size) {
    char worktree[4096];
    kt_format(worktree, sizeof(worktree), "%s", H("path", head));
    hf_trim(worktree);
    kt_format(out, size, "%s/%s", worktree, name);
}
/* Pumps the UI until the file holds exactly the text (or the time is up). */
static bool file_becomes(struct kt_session *s, const char *path, const char *want) {
    char text[256];
    double deadline = kt_now() + 15;
    text[0] = 0;
    while (kt_now() < deadline && strcmp(text, want)) {
        kt_pump(s, .1);
        if (kt_exists(path)) kt_read(path, text, sizeof(text));
    }
    return !strcmp(text, want);
}

/* No agent installed: the form offers none, Hosts names what is missing. */
static void no_agent(void) {
    struct kt_session s;
    char restricted[4096];
    kt_format(restricted, sizeof(restricted), "%s/bin:/usr/bin:/bin:/usr/sbin:/sbin", f.base);
    setenv("PATH", restricted, 1);
    hf_open(&f, &s);
    U("No agent work in this project yet", 10);
    S("n");
    U("New task", 5);
    kt_pump(&s, .5);
    CHECK(kt_contains(&s, "project default") && kt_contains(&s, "none") && !kt_contains(&s, "codex"),
          "the new-task form offers only agents that are available");
    S("\033");
    U("No agent work in this project yet", 5);
    S("3");
    U("This machine", 10);
    U("not found on PATH", 5);
    CHECK(kt_contains(&s, "codex"), "Hosts lists codex among the agents this machine lacks");
    top_bar(&s, "Hosts keeps the control centre's top bar");
    kt_close(&s, "q", 0, 0);
    CHECK(!strstr(H("list", "--json"), "\"branch\""), "visiting the form and Hosts created no head");
    setenv("PATH", normal_path, 1);
}
/* An interactive launch shows the agent and Hydra together. */
static void launch(struct kt_session *s, const char *branch) {
    const char *spawn[] = {NULL, "spawn", branch, "--profile", "codex", "--prompt", "Add a greeting", NULL};
    spawn[0] = f.hydra;
    kt_open_terminal(s, spawn, 80, 24, f.repo);
}
static void interactive(void) {
    struct kt_session s;
    char proof[4096];
    provider("echo 'DETERMINISTIC PROVIDER FIXTURE - ready to discuss'\n"
             "while IFS= read -r line; do printf '%s\\n' \"$line\" >> key-proof; done\n");
    launch(&s, "cli-task");
    U("ready to discuss", 30);
    U("typing to codex", 5);
    top_bar(&s, "the launch shows the top bar beside the agent");
    clean_terminal("cli-task", "a fresh spawn leaves no bootstrap command in the scrollback");
    /* Keys the control centre uses itself reach the attached agent as typed. */
    S("x\ty\033z\r");
    worktree_file("cli-task", "key-proof", proof, sizeof(proof));
    CHECK(file_becomes(&s, proof, "x\ty\033z\n"), "Tab and Esc typed into the attached agent arrive as those bytes");
    S("\x1d");
    U("Keys go to Hydra", 5);
    top_bar(&s, "leaving agent input keeps the user in the control centre");
    CHECK(!kt_contains(&s, "typing to"), "leaving agent input stops typing to the agent");
    kt_close(&s, "q", 0, 0);
    CHECK(strstr(H("list"), "cli-task"), "quitting the control centre keeps the task");
    RUN("tmux", "has-session", "-t", "=cli-task");
}
/* A provider that exits needing a login. */
static void needs_login(void) {
    struct kt_session s;
    char proof[4096];
    provider("echo 'Not logged in. Run: codex login' >&2\nexit 1\n");
    launch(&s, "needs-login");
    U("Not logged in. Run: codex login", 30);
    top_bar(&s, "the provider's sign-in instruction appears inside the control centre");
    U("exited with status 1", 15);
    CHECK(kt_contains(&s, "is now a shell"), "Hydra says the agent exited and the pane is a shell");
    clean_terminal("needs-login", "the shell after the agent exits has no bootstrap command in its scrollback");
    S("printf ok > login-proof\r");
    worktree_file("needs-login", "login-proof", proof, sizeof(proof));
    CHECK(file_becomes(&s, proof, "ok"), "a command typed into the pane runs in the head's worktree");
    S("\x1d");
    U("Keys go to Hydra", 5);
    kt_close(&s, "q", 0, 0);
}

int main(void) {
    char path[4096];
    kt_init();
    hf_init(&f, "hydra-sign-in", "repo", false, true);
    hf_file(&f, "README", path, sizeof(path));
    kt_write(path, "sign-in fixture\n");
    hf_git_init(&f);
    kt_format(normal_path, sizeof(normal_path), "%s", getenv("PATH"));
    no_agent();
    unsetenv("HYDRA_NO_SWITCH");
    unsetenv("HYDRA_SKIP_AI");
    setenv("HYDRA_TUI_BIN", f.tui, 1);
    interactive();
    needs_login();
    hf_finish(&f);
    printf("PASS sign-in recovery: no agent is offered when none is installed, an interactive spawn keeps the task, and a provider that needs a login leaves a usable shell in the worktree\n");
    return 0;
}
