#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

struct session {
    pid_t pid;
    int master;
    int slave;
    struct termios original;
};

static int tests;
static int failures;

static void result(bool ok, const char *message) {
    tests++;
    if (ok) printf("[PASS] %s\n", message);
    else { printf("[FAIL] %s\n", message); failures++; }
}

static void sleep_ms(long milliseconds) {
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000L;
    delay.tv_nsec = (milliseconds % 1000L) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) { }
}

static long long monotonic_ms(void) {
    struct timespec value;
    (void)clock_gettime(CLOCK_MONOTONIC, &value);
    return (long long)value.tv_sec * 1000LL + value.tv_nsec / 1000000LL;
}

static void write_input(int fd, const char *data, size_t length) {
    size_t written = 0U;
    long long deadline = monotonic_ms() + 1000LL;
    while (written < length) {
        ssize_t result = write(fd, data + written, length - written);
        if (result > 0) {
            written += (size_t)result;
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fd_set writefds;
            struct timeval timeout;
            long long remaining = deadline - monotonic_ms();
            int ready;
            if (remaining <= 0LL) break;
            FD_ZERO(&writefds);
            FD_SET(fd, &writefds);
            timeout.tv_sec = (time_t)(remaining / 1000LL);
            timeout.tv_usec = (suseconds_t)((remaining % 1000LL) * 1000LL);
            ready = select(fd + 1, NULL, &writefds, NULL, &timeout);
            if (ready > 0 || (ready < 0 && errno == EINTR)) continue;
            break;
        } else {
            break;
        }
    }
    if (written != length) {
        fprintf(stderr, "failed to write pseudo-terminal input: %s\n",
                errno == 0 ? "short write" : strerror(errno));
        exit(1);
    }
}

static void drain_output(int fd) {
    char buffer[4096];
    while (read(fd, buffer, sizeof(buffer)) > 0) { }
}

static bool same_terminal(const struct termios *left, const struct termios *right) {
    return left->c_iflag == right->c_iflag && left->c_oflag == right->c_oflag &&
           left->c_cflag == right->c_cflag && left->c_lflag == right->c_lflag &&
           memcmp(left->c_cc, right->c_cc, sizeof(left->c_cc)) == 0;
}

static int open_session(struct session *session, const char *tui, const char *hydra,
                        const char *fake_bin, unsigned short cols, unsigned short rows) {
    char *slave_name;
    struct winsize size;
    pid_t pid;
    session->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (session->master < 0 || grantpt(session->master) != 0 || unlockpt(session->master) != 0) return -1;
    slave_name = ptsname(session->master);
    if (slave_name == NULL) return -1;
    session->slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (session->slave < 0 || tcgetattr(session->slave, &session->original) != 0) return -1;
    memset(&size, 0, sizeof(size));
    size.ws_col = cols; size.ws_row = rows;
    if (ioctl(session->master, TIOCSWINSZ, &size) != 0) return -1;
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char path[4096];
        const char *old_path = getenv("PATH");
        close(session->master);
        if (setsid() < 0) _exit(120);
#ifdef TIOCSCTTY
        (void)ioctl(session->slave, TIOCSCTTY, 0);
#endif
        if (dup2(session->slave, STDIN_FILENO) < 0 || dup2(session->slave, STDOUT_FILENO) < 0 ||
            dup2(session->slave, STDERR_FILENO) < 0) _exit(121);
        if (session->slave > STDERR_FILENO) close(session->slave);
        setenv("TERM", "xterm-256color", 1);
        setenv("NO_COLOR", "1", 1);
        snprintf(path, sizeof(path), "%s:%s", fake_bin, old_path == NULL ? "" : old_path);
        setenv("PATH", path, 1);
        execl(tui, tui, "--hydra", hydra, (char *)NULL);
        _exit(127);
    }
    session->pid = pid;
    (void)fcntl(session->master, F_SETFL, fcntl(session->master, F_GETFL) | O_NONBLOCK);
    return 0;
}

static bool wait_for_raw(struct session *session) {
    int attempt;
    for (attempt = 0; attempt < 100; attempt++) {
        struct termios current;
        int status;
        drain_output(session->master);
        if (waitpid(session->pid, &status, WNOHANG) == session->pid) return false;
        if (tcgetattr(session->slave, &current) == 0 &&
            (current.c_lflag & (ICANON | ECHO)) == 0) return true;
        sleep_ms(20);
    }
    return false;
}

static bool wait_for_marker(struct session *session, const char *marker, long timeout_ms) {
    char captured[16384] = "";
    size_t used = 0U;
    long long deadline = monotonic_ms() + timeout_ms;
    while (monotonic_ms() < deadline) {
        ssize_t length = read(session->master, captured + used, sizeof(captured) - used - 1U);
        if (length > 0) {
            used += (size_t)length;
            captured[used] = '\0';
            if (strstr(captured, marker) != NULL) return true;
            if (used > sizeof(captured) / 2U) {
                memmove(captured, captured + used / 2U, used - used / 2U);
                used -= used / 2U;
                captured[used] = '\0';
            }
        } else {
            sleep_ms(10);
        }
    }
    return false;
}

static bool still_running(struct session *session) {
    int status;
    drain_output(session->master);
    return waitpid(session->pid, &status, WNOHANG) == 0;
}

static int wait_for_exit(struct session *session) {
    int attempt, status = 0;
    for (attempt = 0; attempt < 150; attempt++) {
        pid_t waited;
        drain_output(session->master);
        waited = waitpid(session->pid, &status, WNOHANG);
        if (waited == session->pid) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
            return 255;
        }
        sleep_ms(20);
    }
    kill(session->pid, SIGKILL);
    (void)waitpid(session->pid, &status, 0);
    return 254;
}

static bool terminal_restored(struct session *session) {
    struct termios current;
    if (tcgetattr(session->slave, &current) != 0 && tcgetattr(session->master, &current) != 0) {
        printf("  tcgetattr failed: %s\n", strerror(errno));
        return false;
    }
    if (!same_terminal(&session->original, &current)) {
        printf("  terminal before iflag=%lu oflag=%lu cflag=%lu lflag=%lu VMIN=%u VTIME=%u\n",
               (unsigned long)session->original.c_iflag, (unsigned long)session->original.c_oflag,
               (unsigned long)session->original.c_cflag, (unsigned long)session->original.c_lflag,
               session->original.c_cc[VMIN], session->original.c_cc[VTIME]);
        printf("  terminal after  iflag=%lu oflag=%lu cflag=%lu lflag=%lu VMIN=%u VTIME=%u\n",
               (unsigned long)current.c_iflag, (unsigned long)current.c_oflag,
               (unsigned long)current.c_cflag, (unsigned long)current.c_lflag,
               current.c_cc[VMIN], current.c_cc[VTIME]);
        return false;
    }
    return true;
}

static void close_session(struct session *session) {
    close(session->master);
    close(session->slave);
}

static void test_interaction(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    struct winsize size;
    const char paste[] = "\033[200~pasted-q-:kill\033[201~";
    const char mouse[] = "\033[<0;12;4M";
    result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0, "open real pseudo-terminal");
    result(wait_for_raw(&session), "interactive TUI enters raw mode");
    write_input(session.master, "j\r", 2U);
    result(wait_for_marker(&session, "HEAD DETAIL  feature-stale", 1000),
           "keyboard navigation opens the selected head detail");
    write_input(session.master, "\033[A\r", 4U);
    result(wait_for_marker(&session, "HEAD DETAIL  feature-live", 1000),
           "arrow-key navigation remains bounded and deterministic");
    write_input(session.master, "/", 1U);
    result(wait_for_marker(&session, "Search heads:", 1000), "keyboard search prompt is reachable");
    write_input(session.master, "feature-stale\n", 14U);
    result(wait_for_marker(&session, "search: feature-stale", 1000),
           "keyboard-only search returns to raw mode");
    write_input(session.master, "k\r", 2U);
    result(wait_for_marker(&session, "HEAD DETAIL  feature-stale", 1000),
           "filtered navigation and detail stay on the visible head");
    write_input(session.master, "/", 1U);
    (void)wait_for_marker(&session, "Search heads:", 1000);
    write_input(session.master, "does-not-exist\n", 15U);
    (void)wait_for_marker(&session, "search: does-not-exist", 1000);
    write_input(session.master, "\r", 1U);
    result(wait_for_marker(&session, "notice: no matching head selected", 1000),
           "head actions are disabled when the search has no match");
    write_input(session.master, "/", 1U);
    (void)wait_for_marker(&session, "Search heads:", 1000);
    write_input(session.master, "\n", 1U);
    result(wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000),
           "keyboard-only search can be cleared");
    write_input(session.master, paste, sizeof(paste) - 1U);
    sleep_ms(150);
    result(still_running(&session), "bracketed paste cannot inject quit or actions");
    write_input(session.master, mouse, sizeof(mouse) - 1U);
    sleep_ms(100);
    result(still_running(&session), "disabled mouse input is bounded and ignored");
    write_input(session.master, "p", 1U);
    sleep_ms(150);
    result(still_running(&session), "untrusted pane output cannot enter the input parser");
    memset(&size, 0, sizeof(size));
    size.ws_col = 41; size.ws_row = 10;
    (void)ioctl(session.master, TIOCSWINSZ, &size);
    size.ws_col = 120; size.ws_row = 40;
    (void)ioctl(session.master, TIOCSWINSZ, &size);
    sleep_ms(100);
    result(still_running(&session), "resize race remains interactive at the minimum layout");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0, "normal exit succeeds");
    result(terminal_restored(&session), "normal exit restores exact terminal state");
    close_session(&session);
}

static void test_signal(const char *tui, const char *hydra, const char *fake_bin,
                        int signal_number, const char *name) {
    struct session session;
    char message[128];
    if (open_session(&session, tui, hydra, fake_bin, 80, 24) != 0) {
        result(false, name); return;
    }
    snprintf(message, sizeof(message), "%s reaches interactive raw mode", name);
    result(wait_for_raw(&session), message);
    (void)kill(session.pid, signal_number);
    snprintf(message, sizeof(message), "%s exits with the signal category", name);
    result(wait_for_exit(&session) == 128 + signal_number, message);
    snprintf(message, sizeof(message), "%s restores exact terminal state", name);
    result(terminal_restored(&session), message);
    close_session(&session);
}

static void test_preflight_failure(const char *tui, const char *hydra, const char *fake_bin,
                                   unsigned short cols, unsigned short rows, int expected,
                                   const char *message) {
    struct session session;
    if (open_session(&session, tui, hydra, fake_bin, cols, rows) != 0) {
        result(false, message); return;
    }
    result(wait_for_exit(&session) == expected, message);
    result(terminal_restored(&session), "preflight failure preserves terminal state");
    close_session(&session);
}

static void test_crash_fallback(const char *dispatch, const char *fake_bin) {
    struct session session;
    if (dispatch == NULL || dispatch[0] == '\0') return;
    result(open_session(&session, dispatch, "/usr/bin/false", fake_bin, 80, 24) == 0,
           "open crash-fallback pseudo-terminal");
    result(wait_for_marker(&session, "Hydra TUI", 3000),
           "native crash restores the terminal before basic fallback");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0, "basic fallback exits cleanly after native crash");
    result(terminal_restored(&session), "native crash and basic fallback restore exact terminal state");
    close_session(&session);
}

static int measure_interactive(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    struct tms before, after;
    long ticks = sysconf(_SC_CLK_TCK);
    long long started, ready, ended;
    int status;
    (void)times(&before);
    started = monotonic_ms();
    if (open_session(&session, tui, hydra, fake_bin, 120, 40) != 0) return 1;
    if (!wait_for_marker(&session, "HYDRA MISSION CONTROL", 5000)) {
        kill(session.pid, SIGKILL); (void)waitpid(session.pid, NULL, 0); close_session(&session); return 1;
    }
    ready = monotonic_ms();
    sleep_ms(2200);
    drain_output(session.master);
    write_input(session.master, "q", 1U);
    status = wait_for_exit(&session);
    ended = monotonic_ms();
    (void)times(&after);
    close_session(&session);
    if (status != 0 || ticks <= 0) return 1;
    printf("{\"schema_version\":1,\"benchmark\":\"native-tui-interactive\","
           "\"startup_ms\":%lld,\"window_ms\":%lld,\"cpu_ms\":%ld}\n",
           ready - started, ended - ready,
           (long)(((after.tms_cutime - before.tms_cutime) + (after.tms_cstime - before.tms_cstime)) * 1000L / ticks));
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 5 && strcmp(argv[1], "--measure") == 0) {
        return measure_interactive(argv[2], argv[3], argv[4]);
    }
    if (argc != 4) {
        fprintf(stderr, "usage: test-tui-pty TUI FAKE_HYDRA FAKE_BIN\n");
        return 2;
    }
    printf("Running native TUI pseudo-terminal tests...\n");
    test_interaction(argv[1], argv[2], argv[3]);
    test_signal(argv[1], argv[2], argv[3], SIGINT, "SIGINT");
    test_signal(argv[1], argv[2], argv[3], SIGTERM, "SIGTERM");
    test_signal(argv[1], argv[2], argv[3], SIGHUP, "SIGHUP");
    test_preflight_failure(argv[1], argv[2], argv[3], 39, 9, 3, "narrow terminal fails before raw mode");
    test_preflight_failure(argv[1], "/usr/bin/false", argv[3], 80, 24, 4,
                           "adapter failure exits without entering raw mode");
    if (getenv("HYDRA_TEST_SLOW_HYDRA") != NULL) {
        test_preflight_failure(argv[1], getenv("HYDRA_TEST_SLOW_HYDRA"), argv[3], 80, 24, 4,
                               "hung adapter is terminated by the bounded refresh timeout");
    }
    test_crash_fallback(getenv("HYDRA_TEST_CRASH_DISPATCH"), argv[3]);
    printf("Tests: %d, Failed: %d\n", tests, failures);
    return failures == 0 ? 0 : 1;
}
