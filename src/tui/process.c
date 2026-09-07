#define _POSIX_C_SOURCE 200809L
#include "process.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

static long output_remaining(const struct output_child *child) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return child->budget_ms - (long)(now.tv_sec - child->started.tv_sec) * 1000L -
           (long)(now.tv_nsec - child->started.tv_nsec) / 1000000L;
}
int output_start(struct output_child *child, char *const argv[], long budget_ms) {
    int pipefd[2], error;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    memset(child, 0, sizeof(*child)); child->fd = -1; child->budget_ms = budget_ms;
    if (pipe(pipefd) != 0) return -1;
    if (pipefd[0] >= FD_SETSIZE) { close(pipefd[0]); close(pipefd[1]); return -1; }
    error = posix_spawn_file_actions_init(&actions);
    if (error) goto pipes;
    error = posix_spawnattr_init(&attributes);
    if (error) goto actions;
    error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (!error) error = posix_spawnattr_setpgroup(&attributes, 0);
    if (!error) error = posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    if (!error) error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (!error) error = posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    if (!error) error = posix_spawnp(&child->pid, argv[0], &actions, &attributes, argv, environ);
    posix_spawnattr_destroy(&attributes);
actions:
    posix_spawn_file_actions_destroy(&actions);
pipes:
    close(pipefd[1]);
    if (error) { close(pipefd[0]); return -1; }
    child->fd = pipefd[0];
    (void)clock_gettime(CLOCK_MONOTONIC, &child->started);
    return 0;
}
ssize_t output_read(struct output_child *child, char *buffer, size_t size) {
    for (;;) {
        fd_set readfds;
        struct timeval timeout;
        long remaining = output_remaining(child);
        int ready;
        ssize_t length;
        if (remaining <= 0L) { child->timed_out = true; return -1; }
        timeout.tv_sec = remaining / 1000L; timeout.tv_usec = (remaining % 1000L) * 1000L;
        FD_ZERO(&readfds); FD_SET(child->fd, &readfds);
        ready = select(child->fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready == 0) { child->timed_out = true; return -1; }
        if (ready < 0) { if (errno == EINTR) continue; return -1; }
        length = read(child->fd, buffer, size);
        if (length < 0 && errno == EINTR) continue;
        return length;
    }
}
int output_finish(struct output_child *child, bool failed) {
    int status = 0;
    pid_t ready = 0;
    struct timespec pause = {0, 10000000L};
    close(child->fd); child->fd = -1;
    while (!failed && !child->timed_out) {
        ready = waitpid(child->pid, &status, WNOHANG);
        if (ready == child->pid) return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
        if (ready < 0 && errno != EINTR) return -1;
        if (output_remaining(child) <= 0L) { child->timed_out = true; break; }
        (void)nanosleep(&pause, NULL);
    }
    (void)kill(-child->pid, SIGKILL); (void)kill(child->pid, SIGKILL);
    do { ready = waitpid(child->pid, &status, 0); } while (ready < 0 && errno == EINTR);
    return -1;
}

