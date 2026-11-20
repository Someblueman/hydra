#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_drive.h"
#include "fleet/workflow/workflow_schedule.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Workers are this driver's children: it learns of their exit from SIGCHLD
 * through a self-pipe and waitpid, instead of polling their state. Only
 * registered workers are reaped here; hooks and captures wait for their own
 * child, so no waitpid(-1) takes another caller's status. */

static int wake_pipe[2] = {-1, -1};
static volatile sig_atomic_t cancel_signal;
static const int trapped[3] = {SIGHUP, SIGINT, SIGTERM};
static struct sigaction entry_actions[3];

static void wake(void) {
    int saved = errno;
    if (wake_pipe[1] >= 0) (void)!write(wake_pipe[1], "w", 1);
    errno = saved;
}
static void on_child(int signal_number) { (void)signal_number; wake(); }
static void on_cancel(int signal_number) { (void)signal_number; cancel_signal = 1; wake(); }

static int handle(int signal_number, void (*handler)(int)) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    return sigaction(signal_number, &action, NULL);
}

int wdr_signals_init(void) {
    size_t i;
    if (pipe(wake_pipe)) return -1;
    for (i = 0; i < 2; i++) {
        (void)fcntl(wake_pipe[i], F_SETFL, fcntl(wake_pipe[i], F_GETFL) | O_NONBLOCK);
        (void)fcntl(wake_pipe[i], F_SETFD, FD_CLOEXEC);
    }
    for (i = 0; i < 3; i++) (void)sigaction(trapped[i], NULL, &entry_actions[i]);
    return handle(SIGCHLD, on_child);
}

/* Handles HUP, INT and TERM as the removed shell driver's `trap` did: a signal
 * that was ignored when the driver started stays ignored, as a non-interactive
 * shell cannot trap it. */
void wdr_cancel_traps(void) {
    size_t i;
    for (i = 0; i < 3; i++)
        if (entry_actions[i].sa_handler != SIG_IGN) (void)handle(trapped[i], on_cancel);
}

bool wdr_take_cancel_signal(void) {
    if (!cancel_signal) return false;
    cancel_signal = 0;
    return true;
}

/* A child starts with the signal dispositions this driver started with. */
static void child_reset(void) {
    sigset_t none; size_t i;
    for (i = 0; i < 3; i++) (void)sigaction(trapped[i], &entry_actions[i], NULL);
    (void)signal(SIGCHLD, SIG_DFL);
    sigemptyset(&none);
    (void)sigprocmask(SIG_SETMASK, &none, NULL);
}

static void remember(struct wdr_run *run, pid_t pid) {
    if (run->child_count == run->child_capacity) {
        size_t capacity = run->child_capacity ? run->child_capacity * 2 : 64;
        struct wdr_child *grown = realloc(run->children, capacity * sizeof(*grown));
        if (!grown) return;
        run->children = grown; run->child_capacity = capacity;
    }
    run->children[run->child_count].pid = pid;
    run->children[run->child_count++].exited = false;
}

pid_t wdr_spawn(struct wdr_run *run, char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        child_reset();
        execvp(argv[0], argv);
        _exit(127);
    }
    if (pid > 0) remember(run, pid);
    return pid;
}

void wdr_reap(struct wdr_run *run) {
    size_t i; int status;
    for (i = 0; i < run->child_count; i++) {
        pid_t done;
        if (run->children[i].exited) continue;
        done = waitpid(run->children[i].pid, &status, WNOHANG);
        if (done == run->children[i].pid || (done < 0 && errno == ECHILD)) { run->children[i].exited = true; run->woken = true; }
    }
}

static int wait_child(pid_t pid) {
    int status;
    while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
}

/* Reads a child's output as `$(...)` does: trailing newlines removed. */
static void read_output(int fd, char *out, size_t size) {
    size_t used = 0; char discard[4096];
    for (;;) {
        ssize_t n = used + 1 < size ? read(fd, out + used, size - 1 - used) : read(fd, discard, sizeof(discard));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (used + 1 < size) used += (size_t)n;
    }
    out[used] = '\0';
    while (used && out[used - 1] == '\n') out[--used] = '\0';
}

static void redirect(int fd, int target) {
    if (fd >= 0 && fd != target) { (void)dup2(fd, target); close(fd); }
}

/* Runs argv with stdout captured into out (when given) or sent to
 * /dev/null (when quiet), stderr to /dev/null when muted. */
static int run_child(char *const argv[], char *out, size_t size, bool quiet, bool muted) {
    int pipe_fds[2] = {-1, -1}; pid_t pid;
    if (out && pipe(pipe_fds)) return 1;
    pid = fork();
    if (pid == 0) {
        child_reset();
        if (out) { close(pipe_fds[0]); redirect(pipe_fds[1], STDOUT_FILENO); }
        else if (quiet) redirect(open("/dev/null", O_WRONLY), STDOUT_FILENO);
        if (muted) redirect(open("/dev/null", O_WRONLY), STDERR_FILENO);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (out) close(pipe_fds[1]);
    if (pid < 0) { if (out) close(pipe_fds[0]); return 1; }
    if (out) { read_output(pipe_fds[0], out, size); close(pipe_fds[0]); }
    return wait_child(pid);
}

int wdr_capture(char *const argv[], char *out, size_t size) { return run_child(argv, out, size, false, true); }

/* `hydra workflow --driver-hook <operation> <run-dir> [a [b]]`. */
int wdr_hook(struct wdr_run *run, char *out, size_t size, bool quiet, const char *operation, const char *a, const char *b) {
    char *argv[8]; size_t n = 0;
    argv[n++] = (char *)f_hydra; argv[n++] = (char *)"workflow"; argv[n++] = (char *)"--driver-hook";
    argv[n++] = (char *)operation; argv[n++] = (char *)run->dir;
    if (a) argv[n++] = (char *)a;
    if (b) argv[n++] = (char *)b;
    argv[n] = NULL;
    if (out) out[0] = '\0';
    return run_child(argv, out, size, quiet, false);
}

/* `hydra-fleet workflow-task next <run-dir>` in a forked child of this
 * process: the same code in a process of its own. */
int wdr_fork_next(struct wdr_run *run, char *out, size_t size) {
    int pipe_fds[2]; pid_t pid;
    out[0] = '\0';
    if (pipe(pipe_fds)) return 1;
    pid = fork();
    if (pid == 0) {
        int status;
        child_reset();
        close(pipe_fds[0]);
        redirect(pipe_fds[1], STDOUT_FILENO);
        setenv("LC_ALL", "C", 1);
        status = ws_next(run->dir);
        fflush(stdout);
        _exit(status ? 1 : 0);
    }
    close(pipe_fds[1]);
    if (pid < 0) { close(pipe_fds[0]); return 1; }
    read_output(pipe_fds[0], out, size);
    close(pipe_fds[0]);
    return wait_child(pid);
}

bool wdr_zombie(pid_t pid) {
    char text[32], out[256]; char *argv[] = {(char *)"ps", (char *)"-o", (char *)"stat=", (char *)"-p", text, NULL};
    snprintf(text, sizeof(text), "%ld", (long)pid);
    (void)wdr_capture(argv, out, sizeof(out));
    return strchr(out, 'Z') != NULL;
}

/* workflow_pid_alive: a live, non-zombie process. A worker of this driver is
 * alive until it is reaped. */
bool wdr_pid_alive(struct wdr_run *run, const char *pid_text) {
    long value; size_t i;
    if (!wdr_number(pid_text, &value)) return false;
    wdr_reap(run);
    for (i = run->child_count; i-- > 0;)
        if ((long)run->children[i].pid == value) return !run->children[i].exited;
    if (kill((pid_t)value, 0)) return false;
    return !wdr_zombie((pid_t)value);
}

struct process_table { long *pid, *parent; size_t count, capacity; };

static int table_add(struct process_table *table, long pid, long parent) {
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2 : 256;
        long *pids = realloc(table->pid, capacity * sizeof(long)), *parents;
        if (!pids) return -1;
        table->pid = pids;
        if (!(parents = realloc(table->parent, capacity * sizeof(long)))) return -1;
        table->parent = parents;
        table->capacity = capacity;
    }
    table->pid[table->count] = pid;
    table->parent[table->count++] = parent;
    return 0;
}

/* `ps -eo pid=,ppid=` as pairs of numbers. */
static void table_read(struct process_table *table) {
    static char out[1 << 20];
    char *argv[] = {(char *)"ps", (char *)"-eo", (char *)"pid=,ppid=", NULL}, *cursor = out, *end;
    memset(table, 0, sizeof(*table));
    (void)wdr_capture(argv, out, sizeof(out));
    for (;;) {
        long pid = strtol(cursor, &end, 10), parent;
        if (end == cursor) break;
        cursor = end;
        parent = strtol(cursor, &end, 10);
        if (end == cursor || table_add(table, pid, parent)) break;
        cursor = end;
    }
}

/* The children of pid, from a process listing taken now. */
static size_t children_of(long pid, struct process_table *children) {
    struct process_table table; size_t i;
    memset(children, 0, sizeof(*children));
    table_read(&table);
    for (i = 0; i < table.count; i++)
        if (table.parent[i] == pid && table.pid[i] != pid && table_add(children, table.pid[i], pid)) break;
    free(table.pid); free(table.parent);
    return children->count;
}

/* operations_signal_tree: each process's children, listed afresh as the
 * shell lists them at every level, are signalled before the process. */
static void signal_from(long pid, int signal_number, int depth) {
    struct process_table children; size_t i;
    if (depth < 64) {
        (void)children_of(pid, &children);
        for (i = 0; i < children.count; i++) signal_from(children.pid[i], signal_number, depth + 1);
        free(children.pid); free(children.parent);
    }
    (void)kill((pid_t)pid, signal_number);
}

void wdr_signal_tree(pid_t pid, int signal_number) { signal_from((long)pid, signal_number, 0); }

static long milliseconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static void drain(void) {
    char buffer[64];
    while (read(wake_pipe[0], buffer, sizeof(buffer)) > 0) {}
}

/* At most a second; a worker's exit or a cancelling signal ends the wait. */
void wdr_sleep(struct wdr_run *run) {
    long deadline = milliseconds() + 1000;
    wdr_reap(run);
    while (!run->woken && !cancel_signal) {
        struct pollfd wait_for = {0, POLLIN, 0};
        long left = deadline - milliseconds();
        if (left <= 0) break;
        wait_for.fd = wake_pipe[0];
        if (poll(&wait_for, 1, (int)left) > 0) drain();
        wdr_reap(run);
    }
    drain();
}
