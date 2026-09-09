#ifndef TV_PTY_POSIX_H
#define TV_PTY_POSIX_H
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>

#define TV_PTY_QUEUE 65536
/* Optional POSIX process adapter; caller owns it, calls close on every path.
 * Owns one child session/PTY and a bounded nonblocking input queue. It is not a
 * sandbox or daemon supervisor. Child argv is explicit; no command-string eval. */
struct tv_pty {
    int fd;
    pid_t pid;
    bool finished, eof;
    int status;
    unsigned char pending[TV_PTY_QUEUE];
    size_t pending_start, pending_length;
};
bool tv_pty_spawn(struct tv_pty *pty, const char *program, char *const argv[],
                   const struct termios *settings, int columns, int rows);
bool tv_pty_resize(struct tv_pty *pty, int columns, int rows);
bool tv_pty_enqueue(struct tv_pty *pty, const void *data, size_t length);
bool tv_pty_flush(struct tv_pty *pty);
ssize_t tv_pty_read(struct tv_pty *pty, void *buffer, size_t capacity);
/* Nonblocking reap. finished/status are explicit; normal status is exit code,
 * signal termination is 128+signal. */
bool tv_pty_reap(struct tv_pty *pty);
void tv_pty_close(struct tv_pty *pty);
#endif
