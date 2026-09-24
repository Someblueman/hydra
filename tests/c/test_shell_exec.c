#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

/* A case is a leaf job, not a recursive make. Close all inherited descriptors:
 * older Make versions also pass aliases outside the advertised jobserver pair. */
static int close_inherited(void) {
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 3U, ~0U, 0U) == 0) return 0;
#endif
    long limit = sysconf(_SC_OPEN_MAX);
    if (limit < 0) return 1;
    for (int fd = STDERR_FILENO + 1; fd < limit; fd++) close(fd);
    return 0;
}

/* POSIX asynchronous shells ignore INT/QUIT before exec. Restore the foreground
 * test contract before entering sh; an inherited ignored signal cannot be reset
 * by a noninteractive shell's trap builtin. This helper is test-only. */
int main(int argc, char **argv) {
    struct sigaction action = {0};
    if (argc < 2) return 2;
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask) || sigaction(SIGINT, &action, NULL) ||
        sigaction(SIGQUIT, &action, NULL)) return 1;
    if (close_inherited()) return 1;
    execvp(argv[1], argv + 1);
    perror("shell test exec");
    return 127;
}
