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
/* Pipes belong to run; capture buffers belong to its caller. A buffer starts
 * small and doubles as output arrives, up to F_LIMIT bytes plus a NUL. */
#define CAPTURE_INITIAL 16384U
struct capture_stream {
    int pipe[2];
    char *buffer;
    size_t used, capacity;
    char **owner;
    bool full;
};
static bool stream_init(struct capture_stream *stream, char **owner) {
    stream->owner = owner;
    stream->capacity = CAPTURE_INITIAL;
    stream->buffer = *owner = calloc(CAPTURE_INITIAL + 1, 1);
    return stream->buffer != NULL;
}
/* Room for more output; false once the limit is reached or memory runs out,
 * which run reports like a full capture (status 125). */
static bool stream_room(struct capture_stream *stream) {
    size_t capacity = stream->capacity > F_LIMIT / 2 ? F_LIMIT : stream->capacity * 2;
    char *grown;
    if (stream->used < stream->capacity) return true;
    if (stream->capacity == F_LIMIT || !(grown = realloc(stream->buffer, capacity + 1))) return false;
    stream->buffer = *stream->owner = grown;
    stream->capacity = capacity;
    return true;
}
static void close_stream(struct capture_stream *stream) {
    if (stream->pipe[0] < 0) return;
    close(stream->pipe[0]);
    stream->pipe[0] = -1;
}
/* Drain one stream and report whether its capture limit was reached. */
static bool drain_stream(struct capture_stream *stream, int index, struct f_control *control) {
    if (stream->pipe[0] < 0) return false;
    stream->full = !stream_room(stream);
    ssize_t n = stream->full ? 0 : read(stream->pipe[0], stream->buffer + stream->used, stream->capacity - stream->used);
    if (n > 0) {
        capture_log(control, index, stream->buffer + stream->used, (size_t)n);
        stream->used += (size_t)n;
        stream->buffer[stream->used] = '\0';
        if (control && control->observe && index == 0)
            control->observe(control->context, stream->buffer, stream->used);
    }
    bool full = stream->full || stream->used == F_LIMIT;
    stream->full = full;
    /* A full stream stays open until run has killed the writers: closed
     * first, a writer that ignores SIGPIPE would see EPIPE and could report
     * it on the other stream before the kill lands. */
    if ((n == 0 && !full) || (n < 0 && errno != EAGAIN && errno != EINTR)) close_stream(stream);
    return full;
}
static bool truncated(const struct capture_stream streams[2]) { return streams[0].full || streams[1].full; }
static void measure_input(FILE *in, size_t size, bool child_started, struct f_capture *cap, int result, bool cut) {
    cap->measurement_complete = result == 0 && !cap->stop_unknown && !cut;
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
/* How long output is still read after the direct child exited. A descendant
 * that left the process group (an OpenSSH ControlPersist master calls
 * setsid) can hold the pipes for its whole lifetime; the direct child's exit
 * decides, so reading stops after this grace period. */
#define EXITED_GRACE_MS 1000L
/* True once the group leader exited; it stays unreaped so its PID and
 * process group cannot be reused while run may still signal them. */
static bool leader_exited(pid_t pid) {
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    return !waitid(P_PID, (id_t)pid, &info, WEXITED | WNOHANG | WNOWAIT) && info.si_pid == pid;
}
static void close_streams(struct capture_stream streams[2]) {
    int i;
    for (i = 0; i < 2; i++) if (streams[i].pipe[0] >= 0) { close(streams[i].pipe[0]); streams[i].pipe[0] = -1; }
}
/* Stops reading EXITED_GRACE_MS after the leader exited with pipes still open. */
static void exited_grace(pid_t pid, struct capture_stream streams[2], long *exited_at) {
    bool open = streams[0].pipe[0] >= 0 || streams[1].pipe[0] >= 0;
    if (!open) return;
    if (*exited_at < 0 && leader_exited(pid)) *exited_at = milliseconds();
    if (*exited_at >= 0 && milliseconds() - *exited_at >= EXITED_GRACE_MS) close_streams(streams);
}
/* One turn of run's loop waits for output for at most TICK_MS, so that the
 * deadline, a stop request and a signal are noticed in time. With both
 * streams closed there is no output left to wait for, only the child's
 * exit, and that usually follows the close within microseconds: the wait
 * then starts at REAP_FIRST_US and doubles up to the same tick. A child is
 * reaped within about twice the time it took to exit after closing its
 * streams, instead of a whole tick later, and nothing is checked less often
 * than before. A cancellation keeps the plain tick: its child is not reaped
 * before the grace period has run. */
#define TICK_MS 50
#define REAP_FIRST_US 100L
static void reap_pause(long *wait_us) {
    struct timespec pause;
    pause.tv_sec = 0; pause.tv_nsec = *wait_us * 1000L;
    (void)nanosleep(&pause, NULL);
    *wait_us = *wait_us * 2 < TICK_MS * 1000L ? *wait_us * 2 : TICK_MS * 1000L;
}
/* The child between fork and exec: its own process group, default signals,
 * the input file and the two pipes as its standard streams. */
static void child_exec(char *const argv[], FILE *in, struct capture_stream streams[2]) {
    (void)setpgid(0, 0);
    signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL); signal(SIGHUP, SIG_DFL); signal(SIGPIPE, SIG_DFL);
    dup2(fileno(in), STDIN_FILENO); dup2(streams[0].pipe[1], STDOUT_FILENO); dup2(streams[1].pipe[1], STDERR_FILENO);
    close(streams[0].pipe[0]); close(streams[0].pipe[1]); close(streams[1].pipe[0]); close(streams[1].pipe[1]); fclose(in);
    execvp(argv[0], argv); _exit(127);
}
/* The run's progress through stopping its group: a stop request, the
 * deadline or a cancellation sends SIGTERM, and the grace period ending
 * sends SIGKILL. */
struct stopping { long deadline, cancel_grace; bool stopped; };
static void stop_group(pid_t pid, struct f_capture *cap, struct f_control *control, struct stopping *stop) {
    if (!stop->stopped && control && control->stop && control->stop(control->context)) cap->cancelled = true;
    if (!stop->stopped && (f_stopped || cap->cancelled || milliseconds() >= stop->deadline)) {
        cap->timeout = !f_stopped && !cap->cancelled; stop->stopped = true;
        (void)kill(-pid, SIGTERM); stop->deadline = milliseconds() + (cap->cancelled ? stop->cancel_grace : 500);
    }
    if (stop->stopped && milliseconds() >= stop->deadline - (cap->cancelled ? 200 : 0)) (void)kill(-pid, SIGKILL);
}
static void close_pipe_ends(struct capture_stream streams[2]) {
    int i, end;
    for (i = 0; i < 2; i++) for (end = 0; end < 2; end++) if (streams[i].pipe[end] >= 0) close(streams[i].pipe[end]);
}
static int run(char *const argv[], const char *input, size_t size, unsigned seconds, struct f_capture *cap, struct f_control *control) {
    struct capture_stream streams[2] = {{{-1, -1}, NULL, 0, 0, NULL, false}, {{-1, -1}, NULL, 0, 0, NULL, false}};
    int status = 0, result = -1;
    pid_t pid = -1; FILE *in = NULL;
    long exited_at = -1; bool child_started = false;
    struct stopping stopping = {0, 0L, false};
    long reap_wait = REAP_FIRST_US;
    memset(cap, 0, sizeof(*cap)); cap->status = 1; cap->input_complete = size == 0;
    if (!stream_init(&streams[0], &cap->out) || !stream_init(&streams[1], &cap->err) || !(in = tmpfile())) goto done;
    if (control && control->stop && control->stop(control->context)) { cap->cancelled = true; cap->status = 130; result = 0; goto done; }
    if (size && fwrite(input, 1, size, in) != size) goto done;
    rewind(in);
    if (pipe(streams[0].pipe) || pipe(streams[1].pipe)) goto done;
    pid = fork();
    if (pid < 0) goto done;
    if (!pid) child_exec(argv, in, streams);
    child_started = true;
    (void)setpgid(pid, pid);
    close(streams[0].pipe[1]); streams[0].pipe[1] = -1; close(streams[1].pipe[1]); streams[1].pipe[1] = -1;
    fcntl(streams[0].pipe[0], F_SETFL, O_NONBLOCK); fcntl(streams[1].pipe[0], F_SETFL, O_NONBLOCK);
    stopping.cancel_grace = control ? (long)control->grace_seconds * 1000L : 0L;
    stopping.deadline = milliseconds() + (long)seconds * 1000L;
    for (;;) {
        struct pollfd polls[2] = {{streams[0].pipe[0], POLLIN, 0}, {streams[1].pipe[0], POLLIN, 0}};
        int i;
        stop_group(pid, cap, control, &stopping);
        if (polls[0].fd < 0 && polls[1].fd < 0 && !cap->cancelled) reap_pause(&reap_wait);
        else (void)poll(polls, 2, TICK_MS);
        for (i = 0; i < 2; i++) {
            if (drain_stream(&streams[i], i, control)) {
                (void)kill(-pid, SIGKILL);
                close_stream(&streams[i]);
                stopping.stopped = true;
                stopping.deadline = 0;
            }
        }
        exited_grace(pid, streams, &exited_at);
        /* Do not reap the group leader until its pipes close: its PID cannot be reused. */
        if (streams[0].pipe[0] < 0 && streams[1].pipe[0] < 0 && (!cap->cancelled || milliseconds() >= stopping.deadline - 200)) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) { pid = -1; break; }
        }
        if (cap->cancelled && milliseconds() >= stopping.deadline) { cap->stop_unknown = true; if (control) control->stop_unknown = true; break; }
    }
    cap->status = cap->timeout ? 124 : (f_stopped || cap->cancelled ? 130 : (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)));
    if (truncated(streams)) cap->status = 125;
    result = 0;
done:
    cap->out_bytes = streams[0].used; cap->err_bytes = streams[1].used;
    if (pid > 0) { kill(-pid, SIGKILL); while (waitpid(pid, &status, cap->stop_unknown ? WNOHANG : 0) < 0 && errno == EINTR) { } }
    measure_input(in, size, child_started, cap, result, truncated(streams));
    if (in) fclose(in);
    close_pipe_ends(streams);
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
/* Git reads configuration from GIT_CONFIG_COUNT with GIT_CONFIG_KEY_<n> and
 * GIT_CONFIG_VALUE_<n>, and from GIT_CONFIG_PARAMETERS (what git -c passes to
 * its children). Removing them from this process makes later Git children
 * read only the repository's, the user's and the system's configuration. */
#define GIT_CONFIG_PAIRS_MAX 4096UL
void f_git_isolate_config(void) {
    const char *count = getenv("GIT_CONFIG_COUNT");
    unsigned long pairs = 0, i;
    char name[48];
    if (count) {
        char *end = NULL;
        pairs = strtoul(count, &end, 10);
        if (!end || *end || pairs > GIT_CONFIG_PAIRS_MAX) pairs = GIT_CONFIG_PAIRS_MAX;
    }
    for (i = 0; i < pairs; i++) {
        (void)snprintf(name, sizeof(name), "GIT_CONFIG_KEY_%lu", i);
        (void)unsetenv(name);
        (void)snprintf(name, sizeof(name), "GIT_CONFIG_VALUE_%lu", i);
        (void)unsetenv(name);
    }
    (void)unsetenv("GIT_CONFIG_COUNT");
    (void)unsetenv("GIT_CONFIG_PARAMETERS");
}
