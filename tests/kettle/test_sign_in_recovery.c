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
/* What the terminal itself shows: no bootstrap export or environment command
 * in the pane's scrollback, and Hydra's context is one short line naming the
 * head, agent and repository without raw identities. */
static void clean_terminal(const char *session, const char *message) {
    char scrollback[262144], *line, *end;
    bool banner = false;
    kt_format(scrollback, sizeof(scrollback), "%s", RUN("tmux", "capture-pane", "-p", "-J", "-S", "-", "-t", session));
    CHECK(!strstr(scrollback, "export HYDRA_") && !strstr(scrollback, "HYDRA_PROJECT_ID=") &&
          !strstr(scrollback, "HYDRA_HEAD_ID=") && !strstr(scrollback, "set-environment"), message);
    for (line = strstr(scrollback, "Hydra head "); line; line = strstr(end, "Hydra head ")) {
        end = line + strcspn(line, "\n");
        if (*end) *end++ = 0;
        CHECK(strstr(line, "repo") && strlen(line) <= 80 && !strstr(line, "head_") && !strstr(line, "project_") &&
              !strstr(line, "instance_"), "Hydra's context is a concise banner without raw identities");
        banner = true;
        if (!*end) break;
    }
    CHECK(banner, "Hydra printed its one-line context banner");
}
static bool no_head(void) {
    return !strstr(H("list", "--json"), "\"branch\"");
}

int main(void) {
    struct kt_session s;
    char path[4096], restricted[4096], normal[32768], worktree[4096];
    const char *spawn[] = {NULL, "spawn", "cli-task", "--profile", "codex", "--prompt", "Add a greeting", NULL};
    kt_init();
    hf_init(&f, "hydra-sign-in", "repo", false, true);
    hf_file(&f, "README", path, sizeof(path));
    kt_write(path, "sign-in fixture\n");
    hf_git_init(&f);
    kt_format(normal, sizeof(normal), "%s", getenv("PATH"));
    kt_format(restricted, sizeof(restricted), "%s/bin:/usr/bin:/bin:/usr/sbin:/sbin", f.base);

    /* No agent installed: the form offers none, Hosts names what is missing. */
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
    CHECK(no_head(), "visiting the form and Hosts created no head");
    setenv("PATH", normal, 1);

    /* An interactive launch shows the agent and Hydra together. */
    provider("echo 'DETERMINISTIC PROVIDER FIXTURE - ready to discuss'\n"
             "while IFS= read -r line; do printf '%s\\n' \"$line\" >> key-proof; done\n");
    unsetenv("HYDRA_NO_SWITCH");
    unsetenv("HYDRA_SKIP_AI");
    setenv("HYDRA_TUI_BIN", f.tui, 1);
    spawn[0] = f.hydra;
    kt_open_terminal(&s, spawn, 80, 24, f.repo);
    U("ready to discuss", 30);
    U("typing to codex", 5);
    top_bar(&s, "the launch shows the top bar beside the agent");
    clean_terminal("cli-task", "a fresh spawn leaves no bootstrap command in the scrollback");
    /* Keys the control centre uses itself reach the attached agent as typed. */
    S("x\ty\033z\r");
    kt_format(worktree, sizeof(worktree), "%s", H("path", "cli-task"));
    hf_trim(worktree);
    kt_format(path, sizeof(path), "%s/key-proof", worktree);
    {
        char text[256];
        double deadline = kt_now() + 15;
        text[0] = 0;
        while (kt_now() < deadline && strcmp(text, "x\ty\033z\n")) {
            kt_pump(&s, .1);
            if (kt_exists(path)) kt_read(path, text, sizeof(text));
        }
        CHECK(!strcmp(text, "x\ty\033z\n"), "Tab and Esc typed into the attached agent arrive as those bytes");
    }
    S("\x1d");
    U("Keys go to Hydra", 5);
    top_bar(&s, "leaving agent input keeps the user in the control centre");
    CHECK(!kt_contains(&s, "typing to"), "leaving agent input stops typing to the agent");
    kt_close(&s, "q", 0, 0);
    CHECK(strstr(H("list"), "cli-task"), "quitting the control centre keeps the task");
    RUN("tmux", "has-session", "-t", "=cli-task");

    /* A provider that exits needing a login. */
    provider("echo 'Not logged in. Run: codex login' >&2\nexit 1\n");
    spawn[2] = "needs-login";
    kt_open_terminal(&s, spawn, 80, 24, f.repo);
    U("Not logged in. Run: codex login", 30);
    top_bar(&s, "the provider's sign-in instruction appears inside the control centre");
    U("exited with status 1", 15);
    CHECK(kt_contains(&s, "is now a shell"), "Hydra says the agent exited and the pane is a shell");
    clean_terminal("needs-login", "the shell after the agent exits has no bootstrap command in its scrollback");
    kt_format(worktree, sizeof(worktree), "%s", H("path", "needs-login"));
    hf_trim(worktree);
    S("printf ok > login-proof\r");
    kt_format(path, sizeof(path), "%s/login-proof", worktree);
    {
        double deadline = kt_now() + 15;
        while (access(path, F_OK) && kt_now() < deadline) kt_pump(&s, .1);
    }
    CHECK(!access(path, F_OK), "a command typed into the pane runs in the head's worktree");
    S("\x1d");
    U("Keys go to Hydra", 5);
    kt_close(&s, "q", 0, 0);
    hf_finish(&f);
    return 0;
}
