#include "fleet/support/process.h"
#include "fleet/fleet.h"
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
static void capture_log(struct f_control *control, int stream, const char *bytes, size_t size) {
    if (!control || control->log_fd[stream] < 0) return;
    if (size > control->remaining[stream]) { size = control->remaining[stream]; control->truncated = true; }
    while (size) {
        ssize_t n = write(control->log_fd[stream], bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { control->log_error = true; return; }
        size -= (size_t)n; bytes += n; control->remaining[stream] -= (size_t)n;
    }
}
/* Pipes belong to run; capture buffers belong to its caller. */
struct capture_stream {
    int pipe[2];
    char *buffer;
    size_t used;
};
/* Drain one stream and report whether its fixed capture limit was reached. */
static bool drain_stream(struct capture_stream *stream, int index, struct f_control *control) {
    if (stream->pipe[0] < 0) return false;
    ssize_t n = read(stream->pipe[0], stream->buffer + stream->used, F_LIMIT - stream->used);
    if (n > 0) {
        capture_log(control, index, stream->buffer + stream->used, (size_t)n);
        stream->used += (size_t)n;
        if (control && control->observe && index == 0)
            control->observe(control->context, stream->buffer, stream->used);
    }
    bool full = stream->used == F_LIMIT;
    if (n == 0 || full || (n < 0 && errno != EAGAIN && errno != EINTR)) {
        close(stream->pipe[0]);
        stream->pipe[0] = -1;
    }
    return full;
}
static void measure_input(FILE *in, size_t size, bool child_started, struct f_capture *cap, int result) {
    cap->measurement_complete = result == 0 && !cap->stop_unknown && cap->out_bytes < F_LIMIT && cap->err_bytes < F_LIMIT;
    if (in && size && child_started) {
        off_t consumed = lseek(fileno(in), 0, SEEK_CUR);
        if (consumed < 0 || (unsigned long long)consumed > size) {
            cap->input_complete = false; cap->measurement_complete = false;
        } else {
            cap->in_bytes = (size_t)consumed;
            cap->input_complete = (size_t)consumed == size;
        }
    }
}
static int run(char *const argv[], const char *input, size_t size, unsigned seconds, struct f_capture *cap, struct f_control *control) {
    struct capture_stream streams[2] = {{{-1, -1}, NULL, 0}, {{-1, -1}, NULL, 0}};
    int status = 0, result = -1;
    pid_t pid = -1; FILE *in = NULL;
    long deadline, cancel_grace = control ? (long)control->grace_seconds * 1000L : 0L; bool stopped = false, child_started = false;
    memset(cap, 0, sizeof(*cap)); cap->status = 1; cap->input_complete = size == 0;
    cap->out = calloc(F_LIMIT + 1, 1); cap->err = calloc(F_LIMIT + 1, 1);
    streams[0].buffer = cap->out; streams[1].buffer = cap->err;
    if (!cap->out || !cap->err || !(in = tmpfile())) goto done;
    if (control && control->stop && control->stop(control->context)) { cap->cancelled = true; cap->status = 130; result = 0; goto done; }
    if (size && fwrite(input, 1, size, in) != size) goto done;
    rewind(in);
    if (pipe(streams[0].pipe) || pipe(streams[1].pipe)) goto done;
    pid = fork();
    if (pid < 0) goto done;
    if (!pid) {
        (void)setpgid(0, 0);
        signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL); signal(SIGHUP, SIG_DFL);
        dup2(fileno(in), STDIN_FILENO); dup2(streams[0].pipe[1], STDOUT_FILENO); dup2(streams[1].pipe[1], STDERR_FILENO);
        close(streams[0].pipe[0]); close(streams[0].pipe[1]); close(streams[1].pipe[0]); close(streams[1].pipe[1]); fclose(in);
        execvp(argv[0], argv); _exit(127);
    }
    child_started = true;
    (void)setpgid(pid, pid);
    close(streams[0].pipe[1]); streams[0].pipe[1] = -1; close(streams[1].pipe[1]); streams[1].pipe[1] = -1;
    fcntl(streams[0].pipe[0], F_SETFL, O_NONBLOCK); fcntl(streams[1].pipe[0], F_SETFL, O_NONBLOCK);
    deadline = milliseconds() + (long)seconds * 1000L;
    for (;;) {
        struct pollfd polls[2] = {{streams[0].pipe[0], POLLIN, 0}, {streams[1].pipe[0], POLLIN, 0}};
        int i;
        if (!stopped && control && control->stop && control->stop(control->context)) cap->cancelled = true;
        if (!stopped && (f_stopped || cap->cancelled || milliseconds() >= deadline)) {
            cap->timeout = !f_stopped && !cap->cancelled; stopped = true;
            (void)kill(-pid, SIGTERM); deadline = milliseconds() + (cap->cancelled ? cancel_grace : 500);
        }
        if (stopped && milliseconds() >= deadline - (cap->cancelled ? 200 : 0)) (void)kill(-pid, SIGKILL);
        (void)poll(polls, 2, 50);
        for (i = 0; i < 2; i++) {
            if (drain_stream(&streams[i], i, control)) {
                (void)kill(-pid, SIGKILL);
                stopped = true;
                deadline = 0;
            }
        }
        /* Do not reap the group leader until its pipes close: its PID cannot be reused. */
        if (streams[0].pipe[0] < 0 && streams[1].pipe[0] < 0 && (!cap->cancelled || milliseconds() >= deadline - 200)) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) { pid = -1; break; }
        }
        if (cap->cancelled && milliseconds() >= deadline) { cap->stop_unknown = true; if (control) control->stop_unknown = true; break; }
    }
    cap->status = cap->timeout ? 124 : (f_stopped || cap->cancelled ? 130 : (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)));
    if (streams[0].used == F_LIMIT || streams[1].used == F_LIMIT) cap->status = 125;
    result = 0;
done:
    cap->out_bytes = streams[0].used; cap->err_bytes = streams[1].used;
    if (pid > 0) { kill(-pid, SIGKILL); while (waitpid(pid, &status, cap->stop_unknown ? WNOHANG : 0) < 0 && errno == EINTR) { } }
    measure_input(in, size, child_started, cap, result);
    if (in) fclose(in);
    if (streams[0].pipe[0] >= 0) close(streams[0].pipe[0]);
    if (streams[0].pipe[1] >= 0) close(streams[0].pipe[1]);
    if (streams[1].pipe[0] >= 0) close(streams[1].pipe[0]);
    if (streams[1].pipe[1] >= 0) close(streams[1].pipe[1]);
    return result;
}
int f_run(char *const argv[], const char *input, size_t size, unsigned seconds, struct f_capture *cap) {
    return run(argv, input, size, seconds, cap, NULL);
}
int f_run_controlled(char *const argv[], const char *input, size_t size, unsigned seconds, struct f_capture *cap, struct f_control *control) {
    return run(argv, input, size, seconds, cap, control);
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
