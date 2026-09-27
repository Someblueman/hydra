#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <ctype.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static struct hf_fixture *owned;
static char original_path[32768];
void hf_trim(char *text) {
    size_t n = strlen(text);
    while (n && isspace((unsigned char)text[n - 1]))
        text[--n] = 0;
}
static void quote(const char *value, char *out, size_t cap) {
    size_t at = 0;
    CHECK(cap > 2, "quote capacity");
    out[at++] = '\'';
    while (*value) {
        if (*value == '\'') {
            CHECK(at + 4 < cap, "quote capacity");
            memcpy(out + at, "'\\''", 4);
            at += 4;
        } else {
            CHECK(at + 1 < cap, "quote capacity");
            out[at++] = *value;
        }
        value++;
    }
    CHECK(at + 2 <= cap, "quote capacity");
    out[at++] = '\'';
    out[at] = 0;
}
/* Every process whose command line names the fixture's private socket is one
 * of its tmux server or clients; nothing else can name that path. */
static bool server_running(const struct hf_fixture *f) {
    char output[4096];
    const char *args[] = {"pgrep", "-f", "--", f->socket, NULL};
    return f->socket[0] && tv_command(NULL, NULL, output, sizeof(output), 10, args) == 0;
}
bool hf_server_gone(const struct hf_fixture *f) {
    char output[4096];
    const char *kill_server[] = {f->tmux, "-S", f->socket, "kill-server", NULL};
    const char *kill_rest[] = {"pkill", "-f", "--", f->socket, NULL};
    double deadline = tv_now() + 10;
    if (!f->socket[0])
        return true;
    (void)tv_command(NULL, NULL, output, sizeof(output), 10, kill_server);
    while (server_running(f) && tv_now() < deadline) {
        (void)tv_command(NULL, NULL, output, sizeof(output), 10, kill_rest);
        tv_sleep(.1);
    }
    return !server_running(f);
}
void hf_cleanup(void) {
    if (owned)
        (void)hf_server_gone(owned);
}
void hf_finish(struct hf_fixture *f) {
    const char *remove[] = {"rm", "-rf", f->base, NULL};
    CHECK(hf_server_gone(f), "the fixture's private tmux server is gone");
    if (!getenv("HYDRA_TEST_KEEP_FIXTURE"))
        tv_command_ok(NULL, remove);
}
/* A guardian outlives every exit path of the driver: success, a failed CHECK,
 * a signal or even SIGKILL close the driver's end of the pipe, and the guardian
 * then kills the private tmux server and any process started from the fixture
 * and removes the fixture directory (kept with HYDRA_TEST_KEEP_FIXTURE=1). It
 * runs in its own session so terminal signals aimed at the driver miss it. */
static const char guardian_script[] =
    "cat >/dev/null\n"
    "i=0\n"
    "while [ \"$i\" -lt 50 ]; do\n"
    "    if [ -n \"$HF_SOCKET\" ]; then \"$HF_TMUX\" -S \"$HF_SOCKET\" kill-server 2>/dev/null; fi\n"
    "    busy=0\n"
    "    for name in \"$HF_SOCKET\" \"$HF_BASE\"; do\n"
    "        [ -n \"$name\" ] || continue\n"
    "        if pgrep -f -- \"$name\" >/dev/null 2>&1; then busy=1; pkill -f -- \"$name\" 2>/dev/null; fi\n"
    "    done\n"
    "    [ \"$busy\" -eq 1 ] || break\n"
    "    i=$((i + 1))\n"
    "    sleep 0.2\n"
    "done\n"
    "[ -n \"${HYDRA_TEST_KEEP_FIXTURE:-}\" ] || rm -rf -- \"$HF_BASE\"\n";
static void guardian_child(const struct hf_fixture *f, int input) {
    int null = open("/dev/null", O_RDWR);
    if (setsid() < 0 || null < 0 || dup2(input, 0) < 0 || dup2(null, 1) < 0 || dup2(null, 2) < 0)
        _exit(125);
    if (setenv("HF_SOCKET", f->socket, 1) || setenv("HF_BASE", f->base, 1) || setenv("HF_TMUX", f->tmux, 1))
        _exit(125);
    (void)signal(SIGINT, SIG_IGN);
    (void)signal(SIGHUP, SIG_IGN);
    (void)signal(SIGTERM, SIG_IGN);
    execl("/bin/sh", "sh", "-c", guardian_script, (char *)NULL);
    _exit(127);
}
static void start_guardian(const struct hf_fixture *f) {
    int fds[2];
    pid_t pid;
    CHECK(!pipe(fds), "fixture guardian pipe");
    pid = fork();
    CHECK(pid >= 0, "fixture guardian");
    if (pid == 0) {
        close(fds[1]);
        guardian_child(f, fds[0]);
    }
    close(fds[0]);
    /* Only this driver holds the write end: children never inherit it. */
    CHECK(fcntl(fds[1], F_SETFD, FD_CLOEXEC) == 0, "fixture guardian pipe is private");
}
void hf_init(struct hf_fixture *f, const char *name, const char *repo_name, bool copied_plan,
             bool use_tmux) {
    char source[4096], path[4096], script[32768], quoted_tmux[8192], quoted_socket[8192],
        new_path[32768];
    memset(f, 0, sizeof(*f));
    tv_paths(f->root, sizeof(f->root), f->build, sizeof(f->build));
    tv_temp(f->base, sizeof(f->base), name);
    tv_format(f->repo, sizeof(f->repo), "%s/%s", f->base, repo_name);
    tv_format(f->home, sizeof(f->home), "%s/home", f->base);
    tv_format(f->hydra, sizeof(f->hydra), "%s/bin/hydra", f->root);
    tv_format(f->tui, sizeof(f->tui), "%s/hydra-tui", f->build);
    if (copied_plan) {
        tv_format(source, sizeof(source), "%s/tests/fixtures/plan/repo", f->root);
        tv_copy(source, f->repo, true);
    } else
        tv_mkdir(f->repo);
    tv_format(original_path, sizeof(original_path), "%s", getenv("PATH") ? getenv("PATH") : "");
    if (use_tmux) {
        const char *find[] = {"sh", "-c", "command -v tmux", NULL};
        CHECK(tv_command(NULL, NULL, f->tmux, sizeof(f->tmux), 5, find) == 0, "tmux available");
        hf_trim(f->tmux);
        tv_format(f->socket, sizeof(f->socket), "%s/tmux.sock", f->base);
        tv_format(path, sizeof(path), "%s/bin", f->base);
        tv_mkdir(path);
        tv_format(path, sizeof(path), "%s/bin/tmux", f->base);
        quote(f->tmux, quoted_tmux, sizeof(quoted_tmux));
        quote(f->socket, quoted_socket, sizeof(quoted_socket));
        tv_format(script, sizeof(script), "#!/bin/sh\nexec %s -S %s -f /dev/null \"$@\"\n",
                  quoted_tmux, quoted_socket);
        tv_write(path, script);
        CHECK(!chmod(path, 0755), "tmux wrapper executable");
        tv_format(new_path, sizeof(new_path), "%s/bin:%s", f->base, original_path);
        CHECK(!setenv("PATH", new_path, 1), "fixture PATH");
    }
    CHECK(!setenv("HYDRA_HOME", f->home, 1), "fixture home");
    tv_format(path, sizeof(path), "%s/hydra-fleet", f->build);
    setenv("HYDRA_FLEET_BIN", path, 1);
    setenv("HYDRA_NONINTERACTIVE", "1", 1);
    setenv("HYDRA_SKIP_AI", "1", 1);
    setenv("HYDRA_NO_SWITCH", "1", 1);
    setenv("TERM", "xterm-256color", 1);
    owned = f;
    start_guardian(f);
    CHECK(!atexit(hf_cleanup), "fixture cleanup registration");
}
const char *hf_run(struct hf_fixture *f, const char *input, int expected,
                   const char *const argv[]) {
    int status = tv_command(f->repo, input, f->output, sizeof(f->output), 60, argv);
    if (expected >= 0 && status != expected) {
        fprintf(stderr, "Fixture %s command %s: status=%d expected=%d\n%s\n", f->base, argv[0],
                status, expected, f->output);
        CHECK(false, "fixture command status");
    }
    if (expected == -2)
        CHECK(status != 0, "command must refuse");
    return f->output;
}
void hf_commit_init(struct hf_fixture *f) {
    const char *init[] = {"git", "init", "-q", NULL};
    const char *name[] = {"git", "config", "user.name", "Test", NULL};
    const char *email[] = {"git", "config", "user.email", "test@example.com", NULL};
    const char *add[] = {"git", "add", ".", NULL};
    const char *commit[] = {"git", "commit", "-qm", "fixture", NULL};
    const char *hydra[] = {f->hydra, "init", "--no-agent", "--trust", NULL};
    hf_run(f, NULL, 0, init);
    hf_run(f, NULL, 0, name);
    hf_run(f, NULL, 0, email);
    hf_run(f, NULL, 0, add);
    hf_run(f, NULL, 0, commit);
    hf_run(f, NULL, 0, hydra);
}
void hf_open(struct hf_fixture *f, struct tv_session *s) {
    const char *argv[] = {f->tui, "--hydra", f->hydra, NULL};
    tv_open(s, argv, 140, 40, f->repo);
}
void hf_glob_one(const char *pattern, char *out, size_t capacity) {
    glob_t g;
    memset(&g, 0, sizeof(g));
    CHECK(!glob(pattern, 0, NULL, &g), "matching fixture file");
    CHECK(g.gl_pathc == 1, "unique fixture file");
    tv_format(out, capacity, "%s", g.gl_pathv[0]);
    globfree(&g);
}
size_t hf_glob_count(const char *pattern) {
    glob_t g;
    int status;
    size_t count;
    memset(&g, 0, sizeof(g));
    status = glob(pattern, 0, NULL, &g);
    CHECK(status == 0 || status == GLOB_NOMATCH, "fixture glob");
    count = g.gl_pathc;
    globfree(&g);
    return count;
}
size_t hf_count(const char *text, const char *needle) {
    size_t n = 0, len = strlen(needle);
    CHECK(len, "nonempty needle");
    while ((text = strstr(text, needle)) != NULL) {
        n++;
        text += len;
    }
    return n;
}
void hf_file(struct hf_fixture *f, const char *relative, char *out, size_t capacity) {
    tv_format(out, capacity, "%s/%s", f->repo, relative);
}
