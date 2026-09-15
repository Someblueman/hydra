#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"

/* Synchronous CLI actions have a private process group and bounded output.
 * Interrupted mutations remain unknown; this transport never retries them. */
static int action_spawn(char *const argv[], pid_t *pid, int *fd) {
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int pipes[2], result;
    size_t count = 0, used = 0;
    char **envp;
    while (environ && environ[count]) count++;
    envp = calloc(count + 2, sizeof(*envp));
    if (!envp) return ENOMEM;
    for (size_t i = 0; i < count; i++)
        if (strncmp(environ[i], "HYDRA_NONINTERACTIVE=", 21) != 0) envp[used++] = environ[i];
    envp[used] = (char *)"HYDRA_NONINTERACTIVE=1";
    if (pipe(pipes) != 0) { result = errno; free(envp); return result; }
    if (fcntl(pipes[0], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(pipes[1], F_SETFD, FD_CLOEXEC) < 0 || fcntl(pipes[0], F_SETFL, O_NONBLOCK) < 0) {
        result = errno; close(pipes[0]); close(pipes[1]); free(envp); return result;
    }
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipes[1], STDERR_FILENO);
    result = posix_spawnp(pid, argv[0], &actions, &attributes, argv, envp);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    free(envp); close(pipes[1]);
    if (result) close(pipes[0]);
    else *fd = pipes[0];
    return result;
}

static long action_elapsed(const struct timespec *started) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(now.tv_sec - started->tv_sec) * 1000L +
           (long)(now.tv_nsec - started->tv_nsec) / 1000000L;
}

/* Retain an unusually slow-to-die child for nonblocking reap on later UI ticks.
 * Do not permit another captured action while cleanup is still unconfirmed. */
void action_capture_reap(struct app *app) {
    int status;
    if (app->action_pid > 0) {
        pid_t result = waitpid(app->action_pid, &status, WNOHANG);
        if (result == app->action_pid || (result < 0 && errno == ECHILD)) app->action_pid = 0;
    }
}

struct action_capture {
    pid_t pid;
    int fd, status, failure;
    char *out;
    size_t size, used;
    bool eof, killed;
    long stopped_at;
    struct timespec started;
};

static void action_drain(struct action_capture *p) {
    /* Bound each drain so continuous output cannot starve the deadline/UI. */
    for (size_t chunks = 0; chunks < 16; chunks++) {
        char bytes[4096];
        ssize_t n = read(p->fd, bytes, sizeof(bytes));
        if (n <= 0) {
            if (!n) p->eof = true;
            else if (errno != EAGAIN && errno != EINTR) p->failure = 125;
            return;
        }
        size_t take = (size_t)n < p->size - p->used - 1 ? (size_t)n : p->size - p->used - 1;
        memcpy(p->out + p->used, bytes, take); p->used += take; p->out[p->used] = '\0';
    }
}

static void action_stop(struct action_capture *p, long elapsed) {
    if (!p->stopped_at) {
        p->stopped_at = elapsed + 1;
        (void)kill(-p->pid, SIGTERM);
    }
    if (!p->killed && elapsed + 1 - p->stopped_at >= 750) {
        (void)kill(-p->pid, SIGKILL);
        p->killed = true;
    }
}

static bool action_reaped(struct action_capture *p) {
    pid_t result = waitpid(p->pid, &p->status, WNOHANG);
    if (result == p->pid) return true;
    if (result < 0 && errno != EINTR) {
        if (!p->failure) p->failure = 125;
        return true;
    }
    return false;
}

static bool action_step(struct app *app, struct action_capture *p, long budget_ms) {
    long elapsed = action_elapsed(&p->started);
    if (!p->failure && (elapsed >= budget_ms || terminal_stopped())) p->failure = 124;
    if (!p->failure && !p->eof) action_drain(p);
    if (p->failure) action_stop(p, elapsed);
    /* Reserve the group ID until EOF, or until cancellation has escalated. */
    if ((!p->failure && p->eof) || p->killed) {
        if (action_reaped(p)) return true;
    }
    if (p->failure && elapsed + 1 - p->stopped_at >= 1000) {
        app->action_pid = p->pid;
        return true;
    }
    return false;
}

static int action_result(struct action_capture *p) {
    close(p->fd);
    if (p->failure) {
        const char *message = p->failure == 124
            ? "\nAction timed out or interrupted; outcome unknown. Inspect recorded state before retrying.\n"
            : "\nAction capture failed; outcome unknown. Inspect recorded state before retrying.\n";
        size_t length = strlen(message);
        size_t offset = length >= p->size ? 0 : p->size - length - 1;
        if (offset > p->used) offset = p->used;
        snprintf(p->out + offset, p->size - offset, "%s", message);
        return p->failure;
    }
    return WIFEXITED(p->status) ? WEXITSTATUS(p->status) : 1;
}

int run_captured(struct app *app, char *const argv[], char *out, size_t size, long budget_ms) {
    struct action_capture capture = {0};
    int error;
    if (!size) return 1;
    out[0] = '\0';
    action_capture_reap(app);
    if (app->action_pid > 0) {
        snprintf(out, size, "Previous action cleanup is unconfirmed; inspect recorded state before retrying.");
        return 125;
    }
    error = action_spawn(argv, &capture.pid, &capture.fd);
    if (error) { snprintf(out, size, "Could not start %s: %s", argv[0], strerror(error)); return 1; }
    capture.out = out; capture.size = size;
    (void)clock_gettime(CLOCK_MONOTONIC, &capture.started);
    while (!action_step(app, &capture, budget_ms)) {
        char key;
        update_size(app); render(app, 0U, false);
        (void)read_key(40, &key);
    }
    return action_result(&capture);
}
