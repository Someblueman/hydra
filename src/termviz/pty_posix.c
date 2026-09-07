#define _XOPEN_SOURCE 600
#define _DARWIN_C_SOURCE
#include "pty_posix.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

bool tv_pty_resize(struct tv_pty *p, int columns, int rows) {
    struct winsize size;
    if (p->fd < 0 || columns < 1 || columns > 512 || rows < 1 || rows > 256) return false;
    memset(&size, 0, sizeof(size)); size.ws_col = (unsigned short)columns; size.ws_row = (unsigned short)rows;
    return ioctl(p->fd, TIOCSWINSZ, &size) == 0;
}

bool tv_pty_spawn(struct tv_pty *p, const char *program, char *const argv[],
                   const struct termios *settings, int columns, int rows) {
    char name[256], *path;
    pid_t pid;
    int slave = -1, saved_error;
    memset(p, 0, sizeof(*p)); p->fd = -1; p->pid = -1; p->status = -1;
    if (!program || !argv || !argv[0] || !settings) return false;
    p->fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (p->fd < 0 || grantpt(p->fd) != 0 || unlockpt(p->fd) != 0) goto fail;
    path = ptsname(p->fd);
    if (!path || strlen(path) >= sizeof(name)) goto fail;
    memcpy(name, path, strlen(path) + 1);
    if (fcntl(p->fd, F_SETFD, FD_CLOEXEC) != 0) goto fail;
    slave = open(name, O_RDWR | O_NOCTTY);
    if (slave < 0 || tcsetattr(slave, TCSANOW, settings) != 0 || !tv_pty_resize(p, columns, rows)) goto fail;
    pid = fork();
    if (pid < 0) goto fail;
    if (pid == 0) {
        struct sigaction action;
        sigset_t mask;
        int signals[] = {SIGINT,SIGTERM,SIGHUP,SIGPIPE,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU,SIGCHLD,SIGWINCH};
        size_t i;
        close(p->fd);
        if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) != 0) _exit(126);
        if (dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0 || dup2(slave, STDERR_FILENO) < 0) _exit(126);
        if (slave > STDERR_FILENO) close(slave);
        memset(&action, 0, sizeof(action)); action.sa_handler = SIG_DFL; sigemptyset(&action.sa_mask);
        for (i = 0; i < sizeof(signals)/sizeof(*signals); i++) sigaction(signals[i], &action, NULL);
        sigemptyset(&mask); sigprocmask(SIG_SETMASK, &mask, NULL);
        setenv("TERM", "xterm-256color", 1); setenv("COLORTERM", "truecolor", 1);
        /* UI probes and inherited nesting variables must not bind this new shell
         * to an unrelated multiplexer. Process and files remain ordinary OS state. */
        unsetenv("TMUX"); unsetenv("STY");
        execvp(program, argv);
        _exit(127);
    }
    p->pid = pid;
    close(slave); slave = -1;
    if (fcntl(p->fd, F_SETFL, O_NONBLOCK) != 0) { tv_pty_close(p); return false; }
    return true;
fail:
    saved_error = errno;
    if (slave >= 0) close(slave);
    if (p->fd >= 0) close(p->fd);
    p->fd = -1; errno = saved_error;
    return false;
}

bool tv_pty_enqueue(struct tv_pty *p, const void *data, size_t length) {
    if (p->fd < 0 || p->finished || (!data && length) || length > sizeof(p->pending) - p->pending_length) return false;
    if (length > sizeof(p->pending) - p->pending_start - p->pending_length) {
        memmove(p->pending, p->pending + p->pending_start, p->pending_length); p->pending_start = 0;
    }
    if (length) memcpy(p->pending + p->pending_start + p->pending_length, data, length);
    p->pending_length += length;
    return true;
}

bool tv_pty_flush(struct tv_pty *p) {
    ssize_t count;
    if (!p->pending_length) return true;
    count = write(p->fd, p->pending + p->pending_start, p->pending_length);
    if (count < 0) return errno == EAGAIN || errno == EINTR;
    p->pending_start += (size_t)count; p->pending_length -= (size_t)count;
    if (!p->pending_length) p->pending_start = 0;
    return true;
}

ssize_t tv_pty_read(struct tv_pty *p, void *buffer, size_t capacity) {
    ssize_t count;
    if (p->fd < 0 || !buffer || !capacity) { errno = EINVAL; return -1; }
    count = read(p->fd, buffer, capacity);
    if (!count || (count < 0 && errno == EIO)) { p->eof = true; return 0; }
    return count;
}

bool tv_pty_reap(struct tv_pty *p) {
    int status;
    pid_t result;
    if (p->pid < 0 || p->finished) return true;
    result = waitpid(p->pid, &status, WNOHANG);
    if (!result || (result < 0 && errno == EINTR)) return true;
    if (result < 0) return false;
    p->finished = true; p->status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    p->pending_length = p->pending_start = 0;
    return true;
}

void tv_pty_close(struct tv_pty *p) {
    pid_t foreground = p->fd >= 0 ? tcgetpgrp(p->fd) : -1;
    int i, status;
    if (p->pid > 0 && !p->finished) {
        if (foreground > 0 && foreground != p->pid && getsid(foreground) == p->pid) kill(-foreground, SIGHUP);
        kill(-p->pid, SIGHUP);
    }
    if (p->fd >= 0) { close(p->fd); p->fd = -1; }
    for (i = 0; p->pid > 0 && !p->finished && i < 20; i++) {
        struct timespec pause = {0,10000000L};
        if (!tv_pty_reap(p)) break;
        if (!p->finished) nanosleep(&pause, NULL);
    }
    if (p->pid > 0 && !p->finished) {
        if (foreground > 0 && foreground != p->pid && getsid(foreground) == p->pid) kill(-foreground, SIGKILL);
        kill(-p->pid, SIGKILL); kill(p->pid, SIGKILL);
        while (waitpid(p->pid, &status, 0) < 0 && errno == EINTR) { }
        p->finished = true; p->status = 137;
    }
    p->pending_length = 0;
}
