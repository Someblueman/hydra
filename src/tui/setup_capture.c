#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "setup_flow.h"
/* Setup children change remote machines. Unlike observation captures they are
 * not stopped when the control centre quits or is interrupted: the CLI records
 * progress before every side effect, and a later run reconciles it. Only the
 * deadline stops a child, and that result is reported as an unknown outcome. */
#define SETUP_OUTPUT_LIMIT (1024U * 1024U)

static long capture_elapsed(const struct timespec *started) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(now.tv_sec - started->tv_sec) * 1000L + (long)(now.tv_nsec - started->tv_nsec) / 1000000L;
}

static void capture_child(int output, char *const argv[]) {
    int null_fd;
    if (setsid() < 0 || (null_fd = open("/dev/null", O_RDWR)) < 0) _exit(126);
    if (dup2(output, STDOUT_FILENO) < 0 || dup2(null_fd, STDIN_FILENO) < 0 || dup2(null_fd, STDERR_FILENO) < 0) _exit(126);
    close(null_fd); close(output);
    execvp(argv[0], argv);
    _exit(127);
}

bool setup_capture_start(struct setup_capture *c, char *const argv[], long budget_ms) {
    int pipes[2];
    memset(c, 0, sizeof(*c)); c->fd = -1; c->budget_ms = budget_ms;
    if (pipe(pipes) < 0) return false;
    if (fcntl(pipes[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(pipes[0], F_SETFL, O_NONBLOCK) < 0 ||
        !(c->output = tmpfile()) || fcntl(fileno(c->output), F_SETFD, FD_CLOEXEC) < 0) {
        close(pipes[0]); close(pipes[1]);
        if (c->output) fclose(c->output);
        c->output = NULL;
        return false;
    }
    c->pid = fork();
    if (c->pid == 0) { close(pipes[0]); capture_child(pipes[1], argv); }
    close(pipes[1]);
    if (c->pid < 0) { close(pipes[0]); fclose(c->output); c->output = NULL; c->pid = 0; return false; }
    c->fd = pipes[0]; c->active = true;
    (void)clock_gettime(CLOCK_MONOTONIC, &c->started);
    return true;
}

static void capture_drain(struct setup_capture *c) {
    for (size_t chunks = 0; chunks < 32; chunks++) {
        char bytes[8192];
        ssize_t n = read(c->fd, bytes, sizeof(bytes));
        if (n <= 0) {
            if (!n) c->eof = true;
            else if (errno != EAGAIN && errno != EINTR) { c->failed = true; c->eof = true; }
            return;
        }
        if (c->failed) continue;
        if ((size_t)n > SETUP_OUTPUT_LIMIT - c->bytes || fwrite(bytes, 1, (size_t)n, c->output) != (size_t)n) c->failed = true;
        else c->bytes += (size_t)n;
    }
}

bool setup_capture_step(struct setup_capture *c) {
    if (!c->active) return true;
    if (!c->timed_out && capture_elapsed(&c->started) >= c->budget_ms) {
        c->timed_out = true; c->failed = true;
        (void)kill(-c->pid, SIGKILL); (void)kill(c->pid, SIGKILL);
    }
    if (!c->eof) capture_drain(c);
    if (!c->reaped) {
        pid_t result = waitpid(c->pid, &c->status, WNOHANG);
        if (result == c->pid) c->reaped = true;
        else if (result < 0 && errno != EINTR) { c->reaped = true; c->failed = true; }
    }
    /* A background grandchild may hold the pipe; the exit status decides. */
    if (c->reaped && !c->eof) capture_drain(c);
    return c->reaped;
}

void setup_capture_release(struct setup_capture *c) {
    if (c->fd >= 0) close(c->fd);
    if (c->output) fclose(c->output);
    memset(c, 0, sizeof(*c)); c->fd = -1;
}

static int capture_status(const struct setup_capture *c) {
    if (c->timed_out) return 124;
    if (WIFSIGNALED(c->status)) return 128 + WTERMSIG(c->status);
    return WIFEXITED(c->status) ? WEXITSTATUS(c->status) : 125;
}

char *setup_capture_finish(struct setup_capture *c, size_t *length, int *exit_status) {
    char *text = NULL;
    *length = 0;
    *exit_status = capture_status(c);
    if (!c->failed && c->reaped && c->bytes && fflush(c->output) == 0 && fseek(c->output, 0, SEEK_SET) == 0 &&
        (text = malloc(c->bytes + 1)) != NULL) {
        *length = fread(text, 1, c->bytes, c->output);
        text[*length] = '\0';
    }
    setup_capture_release(c);
    return text;
}

/* Hands the terminal to an interactive setup step (agent installer or
 * provider sign-in) the same way other terminal actions do: the control
 * centre restores the terminal, waits, and takes it back. Interrupt keys go
 * to the step, never to Hydra. Returns the step's exit status. */
static int handoff_wait(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) return 125;
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 125;
}

static int handoff_spawn(char *const argv[]) {
    posix_spawnattr_t attributes;
    sigset_t defaults;
    pid_t pid;
    int result;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGINT); sigaddset(&defaults, SIGQUIT); sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGHUP); sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF);
    result = posix_spawnp(&pid, argv[0], NULL, &attributes, argv, environ);
    posix_spawnattr_destroy(&attributes);
    if (result) { fprintf(stderr, "Hydra could not start %s: %s\n", argv[0], strerror(result)); return 127; }
    return handoff_wait(pid);
}

int setup_terminal_handoff(struct app *app, char *const argv[], const char *step) {
    void (*previous_interrupt)(int), (*previous_quit)(int);
    char label[96];
    int status;
    setup_step_label(step, label, sizeof(label));
    restore_terminal(app);
    printf("\033[H\033[2JHydra: %s. This terminal belongs to the step until it finishes.\n"
           "On success you return to Hydra at once; after a failure Hydra waits for Enter.\n\n", label);
    fflush(stdout);
    previous_interrupt = signal(SIGINT, SIG_IGN);
    previous_quit = signal(SIGQUIT, SIG_IGN);
    status = handoff_spawn(argv);
    signal(SIGINT, previous_interrupt);
    signal(SIGQUIT, previous_quit);
    if (status) {
        printf("\nHydra: the step ended with exit status %d. Press Enter to return to Hydra.", status);
        fflush(stdout);
        while (getchar() != '\n' && !feof(stdin)) { }
        clearerr(stdin);
    }
    if (enter_raw(app) != 0) app->running = false;
    frame_invalidate(app);
    return status;
}
