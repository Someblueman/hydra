#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* An interactive head whose tmux session is gone: the views offer to restart
 * its terminal, and opening it from Work does exactly that through the public
 * CLI, in the same worktree, with committed and uncommitted work untouched.
 * When the worktree itself is gone the restart is refused and nothing is
 * recreated. Private tmux socket; the fixture guardian removes everything. */
static struct hf_fixture f;
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define S(keys) tv_send(&s, (keys))
#define U(marker, seconds) tv_until(&s, (marker), (seconds))

/* The one terminal session on the private server, or "" when there is none. */
static void only_session(char *out, size_t size) {
    const char *argv[] = {"tmux", "list-sessions", "-F", "#{session_name}", NULL};
    char output[4096];
    out[0] = '\0';
    if (tv_command(f.repo, NULL, output, sizeof(output), 10, argv) == 0 && strchr(output, '\n') == strrchr(output, '\n'))
        tv_format(out, size, "%s", output);
    hf_trim(out);
}
static void kill_terminal(void) {
    char session[256];
    only_session(session, sizeof(session));
    CHECK(session[0], "the head has exactly one terminal session");
    RUN("tmux", "kill-session", "-t", session);
    only_session(session, sizeof(session));
    CHECK(!session[0], "the terminal session is gone");
}
static void wait_for_file(struct tv_session *s, const char *path, double seconds) {
    double deadline = tv_now() + seconds;
    while (access(path, F_OK) && tv_now() < deadline) tv_pump(s, .1);
    CHECK(!access(path, F_OK), "input reaches the restarted terminal in the head's worktree");
}

/* A head with one committed and one uncommitted file, and no terminal. */
static void prepare(char *worktree, size_t size, char head[128], char status[4096]) {
    char path[4096];
    hf_file(&f, "README", path, sizeof(path));
    tv_write(path, "restore fixture\n");
    hf_commit_init(&f);
    H("spawn", "keeper", "--no-agent");
    tv_format(worktree, size, "%s", H("path", "keeper"));
    hf_trim(worktree);
    tv_format(path, sizeof(path), "%s/committed.txt", worktree);
    tv_write(path, "committed work\n");
    RUN("git", "-C", worktree, "add", "committed.txt");
    RUN("git", "-C", worktree, "-c", "commit.gpgSign=false", "commit", "-qm", "work");
    tv_format(path, sizeof(path), "%s/draft.txt", worktree);
    tv_write(path, "uncommitted work\n");
    tv_format(head, 128, "%s", RUN("git", "-C", worktree, "rev-parse", "HEAD"));
    tv_format(status, 4096, "%s", RUN("git", "-C", worktree, "status", "--porcelain"));
    kill_terminal();
}
/* The views offer the restart, and opening the head from Work performs it. */
static void work_kept(const char *worktree, const char *head, const char *status) {
    char path[4096], text[4096];
    tv_format(path, sizeof(path), "%s/draft.txt", worktree);
    tv_read(path, text, sizeof(text));
    CHECK(!strcmp(text, "uncommitted work\n"), "the dirty file keeps its contents");
    CHECK(!strcmp(RUN("git", "-C", worktree, "rev-parse", "HEAD"), head), "committed work is unchanged");
    CHECK(!strcmp(RUN("git", "-C", worktree, "status", "--porcelain"), status), "uncommitted work is unchanged");
}
static void restore(const char *worktree, const char *head, const char *status) {
    struct tv_session s;
    char path[4096], session[256];
    hf_open(&f, &s);
    U("keeper", 10);
    S("1");
    U("a restart its terminal", 10);
    S("\t");
    U("Details: keeper", 5);
    U("a  restart its terminal (files are kept)", 5);
    CHECK(!tv_contains(&s, "a  talk to the agent"), "a gone terminal is not offered as a live agent");
    S("1");
    U("[Work]", 5);
    S("a");
    U("Typing goes to the agent", 30);
    CHECK(!tv_contains(&s, "CLIENT DISCONNECTED"), "the restarted terminal is attached, not a disconnected client");
    only_session(session, sizeof(session));
    CHECK(session[0], "opening the head restarted its terminal session");
    S("printf restored > restored-proof\r");
    tv_format(path, sizeof(path), "%s/restored-proof", worktree);
    wait_for_file(&s, path, 10);
    tv_close(&s, "\002q", 0, 0);
    CHECK(!unlink(path), "remove the input proof");
    work_kept(worktree, head, status);
}
/* Without its worktree the terminal is not restarted and nothing is recreated. */
static void refuse(const char *worktree) {
    struct tv_session s;
    char path[4096], session[256];
    kill_terminal();
    tv_format(path, sizeof(path), "%s/moved-away", f.base);
    CHECK(!rename(worktree, path), "move the worktree away");
    hf_open(&f, &s);
    U("keeper", 10);
    S("1");
    U("[Work]", 5);
    S("a");
    U("not restarted", 30);
    U("is missing", 5);
    CHECK(access(worktree, F_OK), "the refused restart recreated no worktree");
    only_session(session, sizeof(session));
    CHECK(!session[0], "the refused restart started no terminal");
    S("\r");
    tv_close(&s, "q", 0, 0);
}

int main(void) {
    char worktree[4096], head[128], status[4096];
    tv_init();
    hf_init(&f, "hydra-restore", "repo", false, true);
    prepare(worktree, sizeof(worktree), head, status);
    restore(worktree, head, status);
    refuse(worktree);
    hf_finish(&f);
    puts("PASS terminal restore: opening a head whose terminal is gone restarts it in the same worktree with dirty "
         "and committed work kept; a missing worktree is refused without recreating anything");
    return 0;
}
