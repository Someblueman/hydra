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

static struct tv_session *active[16];
static void cleanup_sessions(void) {
    size_t i;
    for (i = 0; i < 16; i++)
        if (active[i])
            tv_abort(active[i]);
}
void tv_init(void) {
    CHECK(setlocale(LC_CTYPE, "") != NULL, "set locale");
    if (MB_CUR_MAX == 1)
        CHECK(setlocale(LC_CTYPE, "en_US.UTF-8") || setlocale(LC_CTYPE, "C.UTF-8"),
              "UTF-8 locale required");
    CHECK(atexit(cleanup_sessions) == 0, "register session cleanup");
}
void tv_fail(const char *file, int line, const char *message) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, message);
    exit(1);
}
double tv_now(void) {
    struct timespec t;
    CHECK(!clock_gettime(CLOCK_MONOTONIC, &t), "clock");
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
void tv_sleep(double seconds) {
    struct timespec t;
    t.tv_sec = (time_t)seconds;
    t.tv_nsec = (long)((seconds - (double)t.tv_sec) * 1e9);
    while (nanosleep(&t, &t) && errno == EINTR) {
    }
}
static bool poll_exit(struct tv_session *s) {
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
void tv_open(struct tv_session *s, const char *const argv[], int cols, int rows, const char *cwd) {
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
    tv_screen_reset(&s->screen, cols, rows, false);
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
        (void)setsid();
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
size_t tv_pump(struct tv_session *s, double seconds) {
    size_t total = 0;
    double end = tv_now() + seconds;
    do {
        fd_set fds;
        struct timeval timeout;
        double left = end - tv_now();
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
        tv_screen_feed(&s->screen, chunk, (size_t)n);
        total += (size_t)n;
    } while (tv_now() < end);
    return total;
}
void tv_send_n(struct tv_session *s, const void *bytes, size_t length) {
    const char *p = bytes;
    double deadline = tv_now() + 3;
    while (length) {
        ssize_t n = write(s->master, p, length);
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            CHECK(tv_now() < deadline, "PTY input deadline");
            tv_sleep(.005);
            continue;
        }
        CHECK(n > 0, "PTY write");
        p += n;
        length -= (size_t)n;
    }
}
void tv_send(struct tv_session *s, const char *text) { tv_send_n(s, text, strlen(text)); }
void tv_repeat(struct tv_session *s, const char *text, int count) {
    while (count-- > 0)
        tv_send(s, text);
}
void tv_until(struct tv_session *s, const char *marker, double timeout) {
    double end = tv_now() + timeout;
    while (!tv_contains(s, marker) && tv_now() < end)
        tv_pump(s, .05);
    if (!tv_contains(s, marker)) {
        fprintf(stderr, "Missing '%s':\n%s\n", marker, tv_text(s));
        CHECK(false, "screen marker");
    }
}
bool tv_match(struct tv_session *s, const char *pattern, char groups[][256], size_t count) {
    regex_t r;
    regmatch_t m[16];
    size_t i;
    int status;
    CHECK(count < 16, "regex groups");
    CHECK(!regcomp(&r, pattern, REG_EXTENDED), "regex compile");
    status = regexec(&r, tv_text(s), count + 1, m, 0);
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
void tv_until_match(struct tv_session *s, const char *pattern, char groups[][256], size_t count,
                    double timeout) {
    double end = tv_now() + timeout;
    while (!tv_match(s, pattern, groups, count) && tv_now() < end)
        tv_pump(s, .05);
    CHECK(tv_match(s, pattern, groups, count), "screen pattern");
}
void tv_resize(struct tv_session *s, int cols, int rows) {
    struct winsize size;
    double end;
    tv_screen_reset(&s->screen, cols, rows, true);
    memset(&size, 0, sizeof(size));
    size.ws_col = (unsigned short)cols;
    size.ws_row = (unsigned short)rows;
    CHECK(!ioctl(s->slave, TIOCSWINSZ, &size), "resize");
    tv_pump(s, .25);
    end = tv_now() + 3;
    while (s->screen.awaiting_clear && tv_now() < end)
        tv_pump(s, .05);
    CHECK(!s->screen.awaiting_clear, "resize full redraw acknowledgement");
}
void tv_abort(struct tv_session *s) {
    size_t i;
    if (s->closed)
        return;
    if (s->pid > 0 && !s->exited) {
        double end;
        kill(s->pid, SIGTERM);
        end = tv_now() + 2;
        while (!poll_exit(s) && tv_now() < end)
            tv_sleep(.02);
        if (!s->exited) {
            kill(s->pid, SIGKILL);
            while (waitpid(s->pid, NULL, 0) < 0 && errno == EINTR) {
            }
            s->exited = true;
        }
    }
    if (s->master >= 0)
        close(s->master);
    if (s->slave >= 0)
        close(s->slave);
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
void tv_close(struct tv_session *s, const char *keys, int signum, int expected) {
    struct termios after;
    if (!poll_exit(s)) {
        double end = tv_now() + 3;
        if (signum)
            kill(s->pid, signum);
        else if (keys)
            tv_send(s, keys);
        else
            kill(s->pid, SIGTERM);
        while (!poll_exit(s) && tv_now() < end)
            tv_pump(s, .05);
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
    tv_abort(s);
}
