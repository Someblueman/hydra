#include "fleet.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

volatile sig_atomic_t f_stopped;
static long milliseconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
void f_capture_free(struct f_capture *cap) { free(cap->out); free(cap->err); memset(cap, 0, sizeof(*cap)); }
int f_run(char *const argv[], const char *input, size_t size, unsigned seconds, struct f_capture *cap) {
    int out[2] = {-1,-1}, err[2] = {-1,-1}, status = 0, result = -1;
    pid_t pid = -1; FILE *in = NULL; size_t used[2] = {0,0};
    long deadline; bool stopped = false;
    memset(cap, 0, sizeof(*cap)); cap->status = 1;
    cap->out = calloc(F_LIMIT + 1, 1); cap->err = calloc(F_LIMIT + 1, 1);
    if (!cap->out || !cap->err || !(in = tmpfile())) goto done;
    if (size && fwrite(input, 1, size, in) != size) goto done;
    rewind(in);
    if (pipe(out) || pipe(err)) goto done;
    pid = fork();
    if (pid < 0) goto done;
    if (!pid) {
        (void)setpgid(0, 0);
        signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL); signal(SIGHUP, SIG_DFL);
        dup2(fileno(in), STDIN_FILENO); dup2(out[1], STDOUT_FILENO); dup2(err[1], STDERR_FILENO);
        close(out[0]); close(out[1]); close(err[0]); close(err[1]); fclose(in);
        execvp(argv[0], argv); _exit(127);
    }
    (void)setpgid(pid, pid);
    close(out[1]); out[1] = -1; close(err[1]); err[1] = -1;
    fcntl(out[0], F_SETFL, O_NONBLOCK); fcntl(err[0], F_SETFL, O_NONBLOCK);
    deadline = milliseconds() + (long)seconds * 1000L;
    for (;;) {
        struct pollfd polls[2] = {{out[0], POLLIN, 0}, {err[0], POLLIN, 0}};
        int i;
        if (!stopped && (f_stopped || milliseconds() >= deadline)) {
            cap->timeout = !f_stopped; stopped = true;
            (void)kill(-pid, SIGTERM); deadline = milliseconds() + 500;
        }
        if (stopped && milliseconds() >= deadline) (void)kill(-pid, SIGKILL);
        (void)poll(polls, 2, 50);
        for (i = 0; i < 2; i++) {
            int *fd = i ? &err[0] : &out[0];
            char *buffer = i ? cap->err : cap->out;
            if (*fd >= 0) {
                ssize_t n = read(*fd, buffer + used[i], F_LIMIT - used[i]);
                if (n > 0) used[i] += (size_t)n;
                if (n == 0 || used[i] == F_LIMIT || (n < 0 && errno != EAGAIN && errno != EINTR)) {
                    close(*fd); *fd = -1;
                    if (used[i] == F_LIMIT) { (void)kill(-pid, SIGKILL); stopped = true; deadline = 0; }
                }
            }
        }
        /* Do not reap the group leader until its pipes close: its PID cannot be reused. */
        if (out[0] < 0 && err[0] < 0) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) { pid = -1; break; }
        }
    }
    cap->status = cap->timeout ? 124 : (f_stopped ? 130 : (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)));
    if (used[0] == F_LIMIT || used[1] == F_LIMIT) cap->status = 125;
    result = 0;
done:
    if (pid > 0) { kill(-pid, SIGKILL); while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { } }
    if (in) fclose(in);
    if (out[0] >= 0) close(out[0]);
    if (out[1] >= 0) close(out[1]);
    if (err[0] >= 0) close(err[0]);
    if (err[1] >= 0) close(err[1]);
    return result;
}
char *f_quote(const char *value) {
    size_t n = strlen(value), at = 0; char *result;
    if (n > F_LIMIT / 4 || !(result = malloc(n * 4 + 3))) return NULL;
    result[at++] = '\'';
    while (*value) {
        if (*value == '\'') { memcpy(result + at, "'\\''", 4); at += 4; }
        else result[at++] = *value;
        value++;
    }
    result[at++] = '\''; result[at] = '\0'; return result;
}
int f_ssh(const struct f_remote *remote, const char *command, const char *input, size_t size, unsigned seconds, bool tty, struct f_capture *cap) {
    char timeout[64], socket[F_PATH];
    char *argv[24]; size_t n = 0;
    snprintf(timeout, sizeof(timeout), "ConnectTimeout=%u", seconds);
    argv[n++] = (char *)"ssh"; argv[n++] = (char *)(tty ? "-t" : "-T");
    argv[n++] = (char *)"-o"; argv[n++] = (char *)"BatchMode=yes";
    argv[n++] = (char *)"-o"; argv[n++] = (char *)"StrictHostKeyChecking=yes";
    argv[n++] = (char *)"-o"; argv[n++] = timeout;
    if (remote->multiplex) {
        char dir[F_PATH];
        if (f_path(dir, sizeof(dir), f_home, "fleet/sockets") || f_mkdirs(dir)) return -1;
        if (snprintf(socket, sizeof(socket), "ControlPath=%s/%%C", dir) >= (int)sizeof(socket)) return -1;
        argv[n++] = (char *)"-o"; argv[n++] = (char *)"ControlMaster=auto";
        argv[n++] = (char *)"-o"; argv[n++] = (char *)"ControlPersist=60";
        argv[n++] = (char *)"-o"; argv[n++] = socket;
    }
    argv[n++] = (char *)remote->target; argv[n++] = (char *)command; argv[n] = NULL;
    if (tty) { execvp("ssh", argv); return -1; }
    return f_run(argv, input, size, seconds, cap);
}
