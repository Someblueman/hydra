#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>

#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

static struct kt_session *active[16];
static void cleanup_sessions(void) {
    size_t i;
    for (i = 0; i < 16; i++)
        if (active[i])
            kt_abort(active[i]);
}
void kt_init(void) {
    CHECK(setlocale(LC_CTYPE, "") != NULL, "set locale");
    if (MB_CUR_MAX == 1)
        CHECK(setlocale(LC_CTYPE, "en_US.UTF-8") || setlocale(LC_CTYPE, "C.UTF-8"),
              "UTF-8 locale required");
    CHECK(atexit(cleanup_sessions) == 0, "register session cleanup");
}
void kt_fail(const char *file, int line, const char *message) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, message);
    exit(1);
}
double kt_now(void) {
    struct timespec t;
    CHECK(!clock_gettime(CLOCK_MONOTONIC, &t), "clock");
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
void kt_sleep(double seconds) {
    struct timespec t;
    t.tv_sec = (time_t)seconds;
    t.tv_nsec = (long)((seconds - (double)t.tv_sec) * 1e9);
    while (nanosleep(&t, &t) && errno == EINTR) {
    }
}
static bool poll_exit(struct kt_session *s) {
    int value;
    pid_t result;
    if (s->exited)
        return true;
    do {
        result = waitpid(s->pid, &value, WNOHANG);
    } while (result < 0 && errno == EINTR);
    CHECK(result >= 0, "waitpid");
    if (result) {
        s->exited = true;
        s->status = WIFEXITED(value) ? WEXITSTATUS(value) : -WTERMSIG(value);
    }
    return s->exited;
}
static bool controlling_terminal;
/* Runs in the child: a new session, which optionally adopts the PTY. */
static void session_begin(int slave) {
    (void)setsid();
    if (controlling_terminal && ioctl(slave, TIOCSCTTY, 0) != 0)
        _exit(125);
}
void kt_open_terminal(struct kt_session *s, const char *const argv[], int cols, int rows,
                      const char *cwd) {
    controlling_terminal = true;
    kt_open(s, argv, cols, rows, cwd);
    controlling_terminal = false;
}
void kt_open(struct kt_session *s, const char *const argv[], int cols, int rows, const char *cwd) {
    struct winsize size;
    const char *slave;
    size_t i;
    memset(s, 0, sizeof(*s));
    s->master = s->slave = -1;
    s->pid = -1;
    for (i = 0; i < 16 && active[i]; i++) {
    }
    CHECK(i < 16, "session bound");
    active[i] = s;
    kt_screen_reset(&s->screen, cols, rows, false);
    s->master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(s->master >= 0, "open PTY");
    CHECK(!grantpt(s->master) && !unlockpt(s->master), "grant PTY");
    slave = ptsname(s->master);
    CHECK(slave, "PTY name");
    s->slave = open(slave, O_RDWR | O_NOCTTY);
    CHECK(s->slave >= 0, "open slave");
    CHECK(!tcgetattr(s->slave, &s->original), "read termios");
    memset(&size, 0, sizeof(size));
    size.ws_row = (unsigned short)rows;
    size.ws_col = (unsigned short)cols;
    CHECK(!ioctl(s->slave, TIOCSWINSZ, &size), "PTY dimensions");
    s->pid = fork();
    CHECK(s->pid >= 0, "fork PTY child");
    if (!s->pid) {
        session_begin(s->slave);
        if (cwd && chdir(cwd))
            _exit(125);
        if (dup2(s->slave, 0) < 0 || dup2(s->slave, 1) < 0 || dup2(s->slave, 2) < 0)
            _exit(125);
        close(s->master);
        if (s->slave > 2)
            close(s->slave);
        setenv("ENV", "/dev/null", 1);
        setenv("PS1", "TV$ ", 1);
        setenv("TERM", "xterm-256color", 1);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    CHECK(fcntl(s->master, F_SETFL, O_NONBLOCK) >= 0, "nonblocking PTY");
}
size_t kt_pump(struct kt_session *s, double seconds) {
    size_t total = 0;
    double end = kt_now() + seconds;
    do {
        fd_set fds;
        struct timeval timeout;
        double left = end - kt_now();
        int ready;
        unsigned char chunk[65536];
        ssize_t n;
        if (left < 0)
            break;
        timeout.tv_sec = (time_t)left;
        timeout.tv_usec = (suseconds_t)((left - (double)timeout.tv_sec) * 1e6);
        FD_ZERO(&fds);
        CHECK(s->master >= 0 && s->master < FD_SETSIZE, "PTY descriptor bound");
        FD_SET(s->master, &fds);
        ready = select(s->master + 1, &fds, NULL, NULL, &timeout);
        if (ready < 0 && errno == EINTR)
            continue;
        CHECK(ready >= 0, "PTY select");
        if (!ready)
            break;
        n = read(s->master, chunk, sizeof(chunk));
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n < 0 && errno == EIO)
            break;
        CHECK(n >= 0, "PTY read");
        if (!n)
            break;
        if (s->raw_size + (size_t)n + 1 > s->raw_capacity) {
            size_t cap = (s->raw_size + (size_t)n + 1) * 2;
            char *next;
            CHECK(cap < 64 * 1024 * 1024, "raw PTY bound");
            next = realloc(s->raw, cap);
            CHECK(next, "raw allocation");
            s->raw = next;
            s->raw_capacity = cap;
        }
        memcpy(s->raw + s->raw_size, chunk, (size_t)n);
        s->raw_size += (size_t)n;
        s->raw[s->raw_size] = 0;
        kt_screen_feed(&s->screen, chunk, (size_t)n);
        total += (size_t)n;
    } while (kt_now() < end);
    return total;
}
void kt_send_n(struct kt_session *s, const void *bytes, size_t length) {
    const char *p = bytes;
    double deadline = kt_now() + 3;
    while (length) {
        ssize_t n = write(s->master, p, length);
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            CHECK(kt_now() < deadline, "PTY input deadline");
            kt_sleep(.005);
            continue;
        }
        CHECK(n > 0, "PTY write");
        p += n;
        length -= (size_t)n;
    }
}
void kt_send(struct kt_session *s, const char *text) { kt_send_n(s, text, strlen(text)); }
void kt_repeat(struct kt_session *s, const char *text, int count) {
    while (count-- > 0)
        kt_send(s, text);
}
void kt_until(struct kt_session *s, const char *marker, double timeout) {
    double end = kt_now() + timeout;
    while (!kt_contains(s, marker) && kt_now() < end)
        kt_pump(s, .05);
    if (!kt_contains(s, marker)) {
        fprintf(stderr, "Missing '%s':\n%s\n", marker, kt_text(s));
        CHECK(false, "screen marker");
    }
}
bool kt_match(struct kt_session *s, const char *pattern, char groups[][256], size_t count) {
    regex_t r;
    regmatch_t m[16];
    size_t i;
    int status;
    CHECK(count < 16, "regex groups");
    CHECK(!regcomp(&r, pattern, REG_EXTENDED), "regex compile");
    status = regexec(&r, kt_text(s), count + 1, m, 0);
    if (!status)
        for (i = 0; i < count; i++) {
            size_t n;
            CHECK(m[i + 1].rm_so >= 0, "regex group");
            n = (size_t)(m[i + 1].rm_eo - m[i + 1].rm_so);
            CHECK(n < 256, "regex capture");
            memcpy(groups[i], s->screen.text + m[i + 1].rm_so, n);
            groups[i][n] = 0;
        }
    regfree(&r);
    return status == 0;
}
void kt_until_match(struct kt_session *s, const char *pattern, char groups[][256], size_t count,
                    double timeout) {
    double end = kt_now() + timeout;
    while (!kt_match(s, pattern, groups, count) && kt_now() < end)
        kt_pump(s, .05);
    CHECK(kt_match(s, pattern, groups, count), "screen pattern");
}
void kt_resize(struct kt_session *s, int cols, int rows) {
    struct winsize size;
    double end;
    /* The size a terminal already has is not a resize: nothing is signalled
     * and nothing needs repainting. */
    if (s->screen.cols == cols && s->screen.rows == rows)
        return;
    kt_screen_reset(&s->screen, cols, rows, true);
    memset(&size, 0, sizeof(size));
    size.ws_col = (unsigned short)cols;
    size.ws_row = (unsigned short)rows;
    CHECK(!ioctl(s->slave, TIOCSWINSZ, &size), "resize");
    kt_pump(s, .25);
    end = kt_now() + 3;
    while (s->screen.awaiting_clear && kt_now() < end)
        kt_pump(s, .05);
    CHECK(!s->screen.awaiting_clear, "resize full redraw acknowledgement");
}
static void close_fd(int *fd) {
    if (*fd >= 0)
        close(*fd);
    *fd = -1;
}
void kt_abort(struct kt_session *s) {
    size_t i;
    if (s->closed)
        return;
    if (s->pid > 0 && !s->exited) {
        double end;
        kill(s->pid, SIGTERM);
        end = kt_now() + 2;
        while (!poll_exit(s) && kt_now() < end)
            kt_sleep(.02);
        if (!s->exited) {
            /* A child that exits with unread output can block closing its
             * terminal until the output drains (macOS); close our end so
             * waiting for it cannot hang. */
            close_fd(&s->master);
            kill(s->pid, SIGKILL);
            while (waitpid(s->pid, NULL, 0) < 0 && errno == EINTR) {
            }
            s->exited = true;
        }
    }
    close_fd(&s->master);
    close_fd(&s->slave);
    s->closed = true;
    free(s->screen.cells);
    free(s->screen.text);
    free(s->raw);
    s->screen.cells = NULL;
    s->screen.text = NULL;
    s->raw = NULL;
    for (i = 0; i < 16; i++)
        if (active[i] == s)
            active[i] = NULL;
}
void kt_close(struct kt_session *s, const char *keys, int signum, int expected) {
    struct termios after;
    if (!poll_exit(s)) {
        double end = kt_now() + 3;
        if (signum)
            kill(s->pid, signum);
        else if (keys)
            kt_send(s, keys);
        else
            kill(s->pid, SIGTERM);
        while (!poll_exit(s) && kt_now() < end)
            kt_pump(s, .05);
        CHECK(s->exited, "PTY child exit deadline");
    }
    CHECK(s->status == expected, "PTY child exit status");
    CHECK(!tcgetattr(s->slave, &after), "read restored termios");
    CHECK(after.c_iflag == s->original.c_iflag && after.c_oflag == s->original.c_oflag &&
              after.c_cflag == s->original.c_cflag && after.c_lflag == s->original.c_lflag &&
              memcmp(after.c_cc, s->original.c_cc, sizeof(after.c_cc)) == 0 &&
              cfgetispeed(&after) == cfgetispeed(&s->original) &&
              cfgetospeed(&after) == cfgetospeed(&s->original),
          "exact termios restoration");
    kt_abort(s);
}
