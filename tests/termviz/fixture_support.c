#define _DARWIN_C_SOURCE

#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

void tv_format(char *out, size_t capacity, const char *format, ...) {
    va_list args;
    int n;
    va_start(args, format);
    n = vsnprintf(out, capacity, format, args);
    va_end(args);
    CHECK(n >= 0 && (size_t)n < capacity, "formatted buffer capacity");
}
void tv_mkdir(const char *path) {
    char copy[4096];
    char *p;
    tv_format(copy, sizeof(copy), "%s", path);
    for (p = copy + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            CHECK(!mkdir(copy, 0700) || errno == EEXIST, "mkdir parent");
            *p = '/';
        }
    CHECK(!mkdir(copy, 0700) || errno == EEXIST, "mkdir");
}
static void write_mode(const char *path, const char *text, const char *mode) {
    FILE *f = fopen(path, mode);
    CHECK(f, "write file open");
    CHECK(fwrite(text, 1, strlen(text), f) == strlen(text), "write file bytes");
    CHECK(!fclose(f), "write file close");
}
void tv_write(const char *path, const char *text) { write_mode(path, text, "w"); }
void tv_append(const char *path, const char *text) { write_mode(path, text, "a"); }
void tv_read(const char *path, char *out, size_t capacity) {
    FILE *f = fopen(path, "rb");
    size_t n;
    CHECK(f, "read file open");
    CHECK(capacity > 0, "read capacity");
    n = fread(out, 1, capacity - 1, f);
    CHECK(!ferror(f), "read file bytes");
    CHECK(feof(f) || fgetc(f) == EOF, "read file bound");
    CHECK(!ferror(f), "read file final byte");
    CHECK(n < capacity, "read byte count bound");
    /* Callers pass sizeof their output array; fread reads at most capacity - 1
     * bytes and the explicit count check above reserves this terminator. */
    out[n] = 0; // NOLINT(clang-analyzer-security.ArrayBound)
    CHECK(!fclose(f), "read file close");
}
bool tv_exists(const char *path) {
    struct stat st;
    return !stat(path, &st);
}
bool tv_file_equals(const char *path, const char *text) {
    FILE *f = fopen(path, "rb");
    size_t i;
    if (!f)
        return false;
    for (i = 0; text[i]; i++) {
        int byte = fgetc(f);
        if (byte == EOF || byte != (unsigned char)text[i]) {
            fclose(f);
            return false;
        }
    }
    if (fgetc(f) != EOF) {
        fclose(f);
        return false;
    }
    if (ferror(f)) {
        fclose(f);
        return false;
    }
    return fclose(f) == 0;
}
void tv_temp(char *out, size_t capacity, const char *prefix) {
    const char *tmp = getenv("TMPDIR");
    tv_format(out, capacity, "%s/%s.XXXXXX", tmp ? tmp : "/tmp", prefix);
    CHECK(mkdtemp(out), "temporary fixture");
}
int tv_command(const char *cwd, const char *input, char *out, size_t capacity, double timeout,
               const char *const argv[]) {
    int read_pipe[2], write_pipe[2], status = 0;
    pid_t pid;
    struct sigaction ignored, previous;
    bool exited = false, eof = false;
    size_t used = 0, sent = 0, length = input ? strlen(input) : 0;
    double end = tv_now() + timeout;
    CHECK(capacity > 0, "command output capacity");
    out[0] = 0;
    CHECK(!pipe(read_pipe) && !pipe(write_pipe), "command pipes");
    memset(&ignored, 0, sizeof(ignored));
    ignored.sa_handler = SIG_IGN;
    CHECK(!sigemptyset(&ignored.sa_mask) && !sigaction(SIGPIPE, &ignored, &previous),
          "command SIGPIPE handling");
    pid = fork();
    CHECK(pid >= 0, "command fork");
    if (!pid) {
        if (sigaction(SIGPIPE, &previous, NULL))
            _exit(125);
        setpgid(0, 0);
        close(read_pipe[0]);
        close(write_pipe[1]);
        if (cwd && chdir(cwd))
            _exit(125);
        if (dup2(write_pipe[0], 0) < 0 || dup2(read_pipe[1], 1) < 0 || dup2(read_pipe[1], 2) < 0)
            _exit(125);
        close(write_pipe[0]);
        close(read_pipe[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(read_pipe[1]);
    close(write_pipe[0]);
    CHECK(fcntl(read_pipe[0], F_SETFL, O_NONBLOCK) >= 0 &&
              fcntl(write_pipe[1], F_SETFL, O_NONBLOCK) >= 0,
          "command nonblocking");
    while (!exited || !eof) {
        struct pollfd fds[2];
        int ready;
        ssize_t n;
        if (tv_now() > end) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
            }
            fprintf(stderr, "Command timed out: %s\n%s\n", argv[0], out);
            CHECK(false, "command timeout");
        }
        if (write_pipe[1] >= 0 && sent == length) {
            close(write_pipe[1]);
            write_pipe[1] = -1;
        }
        fds[0].fd = read_pipe[0];
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = write_pipe[1];
        fds[1].events = POLLOUT;
        fds[1].revents = 0;
        ready = poll(fds, 2, 20);
        if (ready < 0 && errno == EINTR)
            continue;
        CHECK(ready >= 0, "command poll");
        if (fds[1].revents & POLLOUT) {
            n = write(write_pipe[1], input + sent, length - sent);
            if (n > 0)
                sent += (size_t)n;
            else if (errno == EPIPE) {
                close(write_pipe[1]);
                write_pipe[1] = -1;
            } else
                CHECK(errno == EINTR || errno == EAGAIN, "command input");
        }
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            char chunk[8192];
            n = read(read_pipe[0], chunk, sizeof(chunk));
            if (n == 0)
                eof = true;
            else if (n > 0) {
                CHECK(used + (size_t)n < capacity, "command output bound");
                memcpy(out + used, chunk, (size_t)n);
                used += (size_t)n;
                out[used] = 0;
            } else
                CHECK(errno == EAGAIN || errno == EINTR, "command output");
        }
        if (!exited) {
            pid_t result;
            do {
                result = waitpid(pid, &status, WNOHANG);
            } while (result < 0 && errno == EINTR);
            CHECK(result >= 0, "command wait");
            exited = result == pid;
        }
    }
    close(read_pipe[0]);
    if (write_pipe[1] >= 0)
        close(write_pipe[1]);
    CHECK(!sigaction(SIGPIPE, &previous, NULL), "restore SIGPIPE handling");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
void tv_command_ok(const char *cwd, const char *const argv[]) {
    char out[262144];
    int status = tv_command(cwd, NULL, out, sizeof(out), 60, argv);
    if (status)
        fprintf(stderr, "Command %s returned %d:\n%s\n", argv[0], status, out);
    CHECK(!status, "command success");
}
void tv_copy(const char *source, const char *destination, bool recursive) {
    const char *args[] = {"cp", recursive ? "-R" : "-p", source, destination, NULL};
    tv_command_ok(NULL, args);
}
void tv_paths(char *root, size_t root_capacity, char *build, size_t build_capacity) {
    const char *selected = getenv("BUILD_DIR");
    CHECK(getcwd(root, root_capacity), "repository working directory");
    if (selected && selected[0] == '/')
        tv_format(build, build_capacity, "%s", selected);
    else
        tv_format(build, build_capacity, "%s/%s", root, selected ? selected : "build");
}
