#ifndef KT_PTY_POSIX_H
#define KT_PTY_POSIX_H
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>

#define KT_PTY_QUEUE 65536
/* Optional POSIX process adapter; caller owns it, calls close on every path.
 * Owns one child session/PTY and a bounded nonblocking input queue. It is not a
 * sandbox or daemon supervisor. Child argv is explicit; no command-string eval. */
struct kt_pty {
    int fd;
    pid_t pid;
    bool finished, eof;
    int status;
    unsigned char pending[KT_PTY_QUEUE];
    size_t pending_start, pending_length;
};
bool kt_pty_spawn(struct kt_pty *pty, const char *program, char *const argv[],
                   const struct termios *settings, int columns, int rows);
bool kt_pty_resize(struct kt_pty *pty, int columns, int rows);
bool kt_pty_enqueue(struct kt_pty *pty, const void *data, size_t length);
bool kt_pty_flush(struct kt_pty *pty);
ssize_t kt_pty_read(struct kt_pty *pty, void *buffer, size_t capacity);
/* Nonblocking reap. finished/status are explicit; normal status is exit code,
 * signal termination is 128+signal. */
bool kt_pty_reap(struct kt_pty *pty);
void kt_pty_close(struct kt_pty *pty);
#endif
