#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
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
    bool slow_output;
};

static int tests;
static int failures;
static long long monotonic_ms(void);
static int open_session(struct session *, const char *, const char *, const char *, unsigned short, unsigned short);
static bool wait_for_raw(struct session *);
static bool wait_for_marker(struct session *, const char *, long);
static bool wait_for_marker_capture(struct session *, const char *, long, char *, size_t *);
static int wait_for_exit(struct session *);
static void close_session(struct session *);
static bool terminal_restored(struct session *);
static ssize_t read_marker_output(struct session *, char *, size_t);
static void drain_start_output(struct session *);
static bool measure_unsigned(const char *value, unsigned *result) {
    char *end; unsigned long parsed;
    if (!value || !*value) return false;
    errno=0; parsed=strtoul(value,&end,10);
    if (errno || *end || parsed>1000000UL) return false;
    *result=(unsigned)parsed; return true;
}

static bool result(bool ok, const char *message) {
    tests++;
    if (ok) printf("[PASS] %s\n", message);
    else { printf("[FAIL] %s\n", message); failures++; }
    return ok;
}

static void sleep_ms(long milliseconds) {
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000L;
    delay.tv_nsec = (milliseconds % 1000L) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) { }
}

static bool marker_file_exists(const char *path) {
    struct stat status;
    return path != NULL && stat(path, &status) == 0;
}

static bool write_marker_file(const char *path, const char *contents) {
    FILE *file;
    if (!path || !*path || marker_file_exists(path)) return false;
    file=fopen(path,"w");
    if (!file) return false;
    if (contents) fputs(contents,file);
    fclose(file);
    return true;
}

static bool wait_for_file(const char *path, long timeout_ms) {
    long long deadline=monotonic_ms()+timeout_ms;
    while (monotonic_ms()<deadline) {
        if (marker_file_exists(path)) return true;
        sleep_ms(10);
    }
    return marker_file_exists(path);
}

static bool wait_for_stop_drain(struct session *session, const char *path, long timeout_ms,
                                char *captured, size_t *used, long long *stopped_at) {
    long long deadline=monotonic_ms()+timeout_ms;
    while (monotonic_ms()<deadline && !marker_file_exists(path)) {
        char buffer[4096]; ssize_t length=read_marker_output(session,buffer,sizeof(buffer));
        if (length>0) {
            size_t available=*used<65535U ? 65535U-*used : 0U;
            size_t copy=(size_t)length<available ? (size_t)length : available;
            if (copy>0U) { memcpy(captured+*used,buffer,copy); *used+=copy; captured[*used]='\0'; }
        } else sleep_ms(10);
    }
    *stopped_at=monotonic_ms();
    return marker_file_exists(path);
}

static void capture_bytes(char *captured, size_t *used, const char *buffer, size_t length) {
    size_t available = *used < 65535U ? 65535U - *used : 0U;
    size_t copy = length < available ? length : available;
    if (copy > 0U) {
        memcpy(captured + *used, buffer, copy);
        *used += copy;
        captured[*used] = '\0';
    }
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
        fprintf(stderr, "short pseudo-terminal input write: %zu of %zu bytes\n", written, length);
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

static void child_session(const struct session *session, const char *tui, const char *hydra, const char *fake_bin) {
    char path[4096];
    const char *old_path = getenv("PATH");
    char *tui_path = strdup(tui), *hydra_path = strdup(hydra);
    if (!tui_path || !hydra_path) _exit(119);
    snprintf(path, sizeof(path), "%s:%s", fake_bin, old_path == NULL ? "" : old_path);
    close(session->master);
    if (setsid() < 0) _exit(120);
#ifdef TIOCSCTTY
    (void)ioctl(session->slave, TIOCSCTTY, 0);
#endif
    if (dup2(session->slave, STDIN_FILENO) < 0 || dup2(session->slave, STDOUT_FILENO) < 0 ||
        dup2(session->slave, STDERR_FILENO) < 0) _exit(121);
    if (session->slave > STDERR_FILENO) close(session->slave);
    setenv("TERM", "xterm-256color", 1);
    if (getenv("HYDRA_TEST_COLOR") != NULL) unsetenv("NO_COLOR");
    else setenv("NO_COLOR", "1", 1);
    setenv("PATH", path, 1);
    if (getenv("HYDRA_FIXTURE_REPO") != NULL) (void)chdir(getenv("HYDRA_FIXTURE_REPO"));
    if (getenv("HYDRA_TEST_FLEET_ATTACH")) execl(tui_path, tui_path, "--hydra", hydra_path, "--fleet", "--view", "heads", (char *)NULL);
    else if (getenv("HYDRA_TEST_FLEET_VIEW")) execl(tui_path, tui_path, "--hydra", hydra_path, "--fleet", "--view", "hosts", (char *)NULL);
    else if (getenv("HYDRA_TEST_OVERVIEW")) execl(tui_path, tui_path, "--hydra", hydra_path, "--view", "overview", (char *)NULL);
    else execl(tui_path, tui_path, "--hydra", hydra_path, "--view", "heads", (char *)NULL);
    _exit(127);
}

struct i1_measurement {
    struct session session;
    char tag[128];
    char transcript[65536];
    size_t transcript_used;
    bool workspace_ready;
    bool attached;
    bool handshake;
};

static bool measure_i1_start(struct i1_measurement *measurement, const char *tui,
                             const char *hydra) {
    measurement->transcript[0] = '\0';
    if (open_session(&measurement->session, tui, hydra, "/usr/bin", 120, 40) != 0) return false;
    if (!wait_for_raw(&measurement->session)) {
        close_session(&measurement->session);
        return false;
    }
    write_input(measurement->session.master, "W", 1U);
    measurement->workspace_ready = wait_for_marker_capture(&measurement->session, "HYDRA WORKSPACE", 3000,
                                                            measurement->transcript, &measurement->transcript_used);
    if (!measurement->workspace_ready) return true;
    write_input(measurement->session.master, "a", 1U);
    measurement->attached = wait_for_marker_capture(&measurement->session,
        "INPUT TO AGENT / Ctrl-B Tab Hydra", 5000, measurement->transcript,
        &measurement->transcript_used);
    if (measurement->attached && getenv("HYDRA_I1_READY_FILE") != NULL) {
        char ready[128];
        (void)snprintf(ready, sizeof(ready), "observer_pid=%ld\ntui_pid=%ld\n",
                       (long)getpid(), (long)measurement->session.pid);
        measurement->handshake = write_marker_file(getenv("HYDRA_I1_READY_FILE"), ready) &&
            wait_for_file(getenv("HYDRA_I1_GO_FILE"), 30000);
    }
    return true;
}

static void measure_i1_command(struct i1_measurement *measurement, const char *seed) {
    char command[256];
    if (!measurement->attached || (getenv("HYDRA_I1_READY_FILE") && !measurement->handshake)) return;
    (void)snprintf(command, sizeof(command), "printf 'I1-INPUT-'; printf '%s'; printf 'TAG'; printf '\\n'; sleep 1\n", seed);
    write_input(measurement->session.master, command, strlen(command));
}

static bool measure_i1_response(struct i1_measurement *measurement, const char *seed) {
    char expected[160];
    if (!measurement->attached || (getenv("HYDRA_I1_READY_FILE") && !measurement->handshake)) return false;
    (void)snprintf(expected, sizeof(expected), "I1-INPUT-%sTAG", seed);
    return wait_for_marker_capture(&measurement->session, expected, 3000,
                                   measurement->transcript, &measurement->transcript_used);
}

static void measure_i1_artifact(const struct i1_measurement *measurement, bool complete,
                                int exit_status, bool restored) {
    const char *artifact_dir = getenv("HYDRA_I1_ARTIFACT_DIR");
    char path[4096];
    FILE *file;
    if (!artifact_dir || !*artifact_dir) artifact_dir = "/tmp";
    if (complete && measurement->workspace_ready && measurement->attached && exit_status == 0 && restored) return;
    if (snprintf(path, sizeof(path), "%s/i1-pty-%ld.raw", artifact_dir, (long)getpid()) >= (int)sizeof(path)) return;
    file = fopen(path, "w");
    if (file) { (void)fwrite(measurement->transcript, 1, measurement->transcript_used, file); fclose(file); }
}

static int measure_i1_prepare(struct i1_measurement *measurement, char *hydra,
                              size_t hydra_size, char *cwd, size_t cwd_size,
                              const char *seed) {
    const char *root = getenv("HYDRA_SOURCE_ROOT");
    if (!root || !getenv("HYDRA_FIXTURE_REPO") || !getenv("HYDRA_HOME") || !seed || !*seed ||
        snprintf(hydra, hydra_size, "%s/bin/hydra", root) >= (int)hydra_size) return 2;
    if (snprintf(measurement->tag, sizeof(measurement->tag), "I1-INPUT-%s", seed) >= (int)sizeof(measurement->tag)) return 2;
    if (!getcwd(cwd, cwd_size) || chdir(getenv("HYDRA_FIXTURE_REPO")) ||
        setenv("HYDRA_NO_SWITCH", "1", 1) || setenv("HYDRA_NONINTERACTIVE", "1", 1) || chdir(cwd)) return 1;
    return 0;
}

static int measure_i1(const char *tui, unsigned heads, const char *seed) {
    struct i1_measurement measurement = {0};
    struct rusage before, after;
    long long sent, observed;
    char cwd[4096];
    bool complete = false, restored = false, stop_received = false;
    int exit_status = 254;
    char hydra[4096];
    int preparation = measure_i1_prepare(&measurement, hydra, sizeof(hydra), cwd, sizeof(cwd), seed);
    if (preparation != 0) return preparation;
    (void)getrusage(RUSAGE_SELF, &before);
    if (!measure_i1_start(&measurement, tui, hydra)) return 1;
    sent=monotonic_ms();
    measure_i1_command(&measurement, seed);
    complete = measure_i1_response(&measurement, seed);
    observed=monotonic_ms();
    long long interval_stop=observed;
    if (measurement.handshake) stop_received=wait_for_stop_drain(&measurement.session,getenv("HYDRA_I1_STOP_FILE"),6000,
                                                     measurement.transcript,&measurement.transcript_used,&interval_stop);
    /* Ctrl-B q is Hydra's documented terminal prefix and exits the observer
     * while closing its attachment clients. */
    write_input(measurement.session.master,"\002q",2U);
    exit_status=wait_for_exit(&measurement.session);
    restored=terminal_restored(&measurement.session);
    close_session(&measurement.session);
    (void)getrusage(RUSAGE_SELF, &after);
    measure_i1_artifact(&measurement, complete, exit_status, restored);
    printf("{\"schema_version\":1,\"benchmark\":\"native-tui-i1-attached-pty\",\"heads\":%u,\"clients\":1,\"view\":\"workspace-attached-pane\",\"tag\":\"%s\",\"observer_pid\":%ld,\"tui_pid\":%ld,\"measurement_start_ts_ms\":%lld,\"measurement_stop_ts_ms\":%lld,\"stop_received\":%s,\"outer_pty_bytes\":%zu,\"input_sent_ts_ms\":%lld,\"frame_observed_ts_ms\":%lld,\"frame_latency_ms\":%lld,\"pty_frame_completion\":%s,\"workspace_ready\":%s,\"attached\":%s,\"exit_status\":%d,\"terminal_restored\":%s,\"observer_cpu_ms\":%ld,\"tui_subtree_cpu_ms\":null}\n",
           heads,measurement.tag,(long)getpid(),(long)measurement.session.pid,sent,interval_stop,stop_received ? "true" : "false",measurement.transcript_used,sent,observed,observed-sent,complete ? "true" : "false",
           measurement.workspace_ready ? "true" : "false", measurement.attached ? "true" : "false", exit_status,
           restored ? "true" : "false",
           (long)(((after.ru_utime.tv_sec-before.ru_utime.tv_sec)*1000L +
                   (after.ru_utime.tv_usec-before.ru_utime.tv_usec)/1000L +
                   (after.ru_stime.tv_sec-before.ru_stime.tv_sec)*1000L +
                   (after.ru_stime.tv_usec-before.ru_stime.tv_usec)/1000L)));
    return complete && measurement.workspace_ready && measurement.attached && exit_status==0 && restored &&
        (!getenv("HYDRA_I1_READY_FILE") || stop_received) ? 0 : 1;
}

static int open_session(struct session *session, const char *tui, const char *hydra,
                        const char *fake_bin, unsigned short cols, unsigned short rows) {
    char *slave_name;
    struct winsize size;
    pid_t pid;
    *session = (struct session){.pid = -1, .master = -1, .slave = -1};
    if (!tui || !hydra || !fake_bin) return -1;
    session->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (session->master < 0 || grantpt(session->master) != 0 || unlockpt(session->master) != 0) goto failed;
    slave_name = ptsname(session->master);
    if (slave_name == NULL) goto failed;
    session->slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (session->slave < 0 || tcgetattr(session->slave, &session->original) != 0) goto failed;
    memset(&size, 0, sizeof(size));
    size.ws_col = cols; size.ws_row = rows;
    if (ioctl(session->master, TIOCSWINSZ, &size) != 0) goto failed;
    pid = fork();
    if (pid < 0) goto failed;
    if (pid == 0) child_session(session, tui, hydra, fake_bin);
    session->pid = pid;
    (void)fcntl(session->master, F_SETFL, fcntl(session->master, F_GETFL) | O_NONBLOCK);
    return 0;
failed:
    if (session->slave >= 0) close(session->slave);
    if (session->master >= 0) close(session->master);
    session->slave = session->master = -1;
    return -1;
}

static bool wait_for_raw(struct session *session) {
    int attempt;
    /* Real repositories with a ten-head inventory can take a few seconds for
     * the first hydra data refresh; do not confuse that with a failed PTY. */
    for (attempt = 0; attempt < 300; attempt++) {
        struct termios current;
        int status;
        drain_start_output(session);
        if (waitpid(session->pid, &status, WNOHANG) == session->pid) {
            if (getenv("HYDRA_I1_ARTIFACT_DIR") != NULL)
                fprintf(stderr, "i1: TUI exited before raw mode (status=%d)\n", status);
            return false;
        }
        if (tcgetattr(session->slave, &current) == 0 &&
            (current.c_lflag & (ICANON | ECHO)) == 0) return true;
        sleep_ms(20);
    }
    return false;
}

static void drain_start_output(struct session *session) {
    char buffer[4096];
    ssize_t length;
    if (getenv("HYDRA_I1_ARTIFACT_DIR") == NULL) { drain_output(session->master); return; }
    do {
        length = read(session->master, buffer, sizeof(buffer));
        if (length > 0) {
            char path[4096];
            FILE *file;
            if (snprintf(path, sizeof(path), "%s/i1-pty-start.raw", getenv("HYDRA_I1_ARTIFACT_DIR")) < (int)sizeof(path) &&
                (file = fopen(path, "a")) != NULL) {
                (void)fwrite(buffer, 1, (size_t)length, file);
                fclose(file);
            }
        }
    } while (length > 0);
}

static ssize_t read_marker_output(struct session *session, char *buffer, size_t size) {
    ssize_t length;
    if (session->slow_output && size > 64U) size = 64U;
    length = read(session->master, buffer, size);
    if (session->slow_output && length > 0) sleep_ms(2);
    return length;
}

/* Keep both observations: one PTY read can contain the entire frame. */
static bool wait_for_markers(struct session *session, const char *marker,
                             const char *second, long timeout_ms) {
    bool found = false, found_second = second == NULL;
    char captured[16384] = "";
    size_t used = 0U;
    long long deadline = monotonic_ms() + timeout_ms;
    while (monotonic_ms() < deadline) {
        ssize_t length = read_marker_output(session, captured + used, sizeof(captured) - used - 1U);
        if (length > 0) {
            used += (size_t)length;
            captured[used] = '\0';
            found = found || strstr(captured, marker) != NULL;
            found_second = found_second || (second != NULL && strstr(captured, second) != NULL);
            if (found && found_second) return true;
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

static bool wait_for_marker(struct session *session, const char *marker, long timeout_ms) {
    return wait_for_markers(session, marker, NULL, timeout_ms);
}

static bool wait_for_marker_capture(struct session *session, const char *marker, long timeout_ms,
                                    char *captured, size_t *used) {
    long long deadline = monotonic_ms() + timeout_ms;
    while (monotonic_ms() < deadline) {
        char buffer[4096];
        ssize_t length = read_marker_output(session, buffer, sizeof(buffer));
        if (length > 0) {
            capture_bytes(captured, used, buffer, (size_t)length);
            if (strstr(captured, marker) != NULL) return true;
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

#include "test_tui_mouse.inc"
#include "test_tui_themes.inc"
#include "test_tui_palette.inc"

static void test_session_failure(void) {
    pid_t pid = fork();
    int status = 0;
    if (pid == 0) {
        struct rlimit limit = {0, 0};
        struct session session;
        if (setrlimit(RLIMIT_NOFILE, &limit) != 0) _exit(2);
        int opened = open_session(&session, "/usr/bin/false", "/usr/bin/false", "", 80, 24);
        _exit(opened == -1 && session.pid == -1 && session.master == -1 && session.slave == -1 ? 0 : 1);
    }
    result(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
           "failed PTY setup leaves no live session handles");
}

static void test_small_list(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    if (!result(open_session(&session, tui, hydra, fake_bin, 40, 10) == 0, "open minimum-size terminal")) return;
    result(wait_for_raw(&session), "small list enters raw mode");
    write_input(session.master, "/", 1U);
    (void)wait_for_marker(&session, "Search heads:", 1000);
    write_input(session.master, "feature\n", 8U);
    (void)wait_for_marker(&session, "Search: feature", 1000);
    write_input(session.master, "jj", 2U);
    result(wait_for_marker(&session, ">  feature-unavail", 1000), "selection scrolls into view in a short filtered list");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "small list exits with exact terminal restoration");
    close_session(&session);
}

static void test_interaction(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    struct winsize size;
    const char paste[] = "\033[200~pasted-q-:kill\033[201~";
    char large_paste[8300];
    size_t paste_offset;
    const char mouse[] = "\033[<0;12;4M";
    (void)setenv("TMUX", "test", 1);
    (void)setenv("FAKE_TMUX_CURRENT_SESSION", "hydra-feature-live", 1);
    bool opened = result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0, "open real pseudo-terminal");
    (void)unsetenv("TMUX");
    (void)unsetenv("FAKE_TMUX_CURRENT_SESSION");
    if (!opened) return;
    result(wait_for_raw(&session), "interactive TUI enters raw mode");
    write_input(session.master, "j\r", 2U);
    result(wait_for_marker(&session, "HEAD DETAIL  feature-stale", 1000),
           "keyboard navigation opens the selected head detail");
    write_input(session.master, "\033[A\r", 4U);
    result(wait_for_marker(&session, "HEAD DETAIL  feature-live", 1000),
           "arrow-key navigation remains bounded and deterministic");
    write_input(session.master, "d", 1U);
    result(wait_for_marker(&session, "lifecycle source:", 1000), "diagnostics are reachable on demand");
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "[Heads]", 1000), "Escape returns to the head list");
    write_input(session.master, "/", 1U);
    result(wait_for_marker(&session, "Search heads:", 1000), "keyboard search prompt is reachable");
    write_input(session.master, "FEATURE-STALE\n", 14U);
    result(wait_for_marker(&session, "Search: FEATURE-STALE", 1000),
           "keyboard-only search is case-insensitive and returns to raw mode");
    write_input(session.master, "k\r", 2U);
    result(wait_for_marker(&session, "HEAD DETAIL  feature-stale", 1000),
           "filtered navigation and detail stay on the visible head");
    write_input(session.master, "/", 1U);
    (void)wait_for_marker(&session, "Search heads:", 1000);
    write_input(session.master, "does-not-exist\n", 15U);
    (void)wait_for_marker(&session, "Search: does-not-exist", 1000);
    write_input(session.master, "\r", 1U);
    result(wait_for_marker(&session, "no matching head selected", 1000),
           "head actions are disabled when the search has no match");
    write_input(session.master, "/", 1U);
    (void)wait_for_marker(&session, "Search heads:", 1000);
    write_input(session.master, "\n", 1U);
    result(wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000),
           "keyboard-only search can be cleared");
    write_input(session.master, paste, sizeof(paste) - 1U);
    sleep_ms(150);
    result(still_running(&session), "bracketed paste cannot inject quit or actions");
    memset(large_paste, '9', sizeof(large_paste));
    memcpy(large_paste, "\033[200~", 6U);
    memcpy(large_paste + sizeof(large_paste) - 7U, "q\033[201~", 7U);
    for (paste_offset = 0U; paste_offset < sizeof(large_paste); paste_offset += 100U) {
        write_input(session.master, large_paste + paste_offset, 100U);
        sleep_ms(2);
        drain_output(session.master);
    }
    write_input(session.master, "?", 1U);
    result(wait_for_marker(&session, "KEYBOARD HELP", 2000), "oversized paste cannot inject quit and preserves the next key");
    write_input(session.master, "?", 1U);
    result(wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000), "keyboard input resumes after oversized paste");
    write_input(session.master, mouse, sizeof(mouse) - 1U);
    sleep_ms(100);
    result(still_running(&session), "mouse over a non-list view is inert");
    write_input(session.master, "p", 1U);
    sleep_ms(150);
    result(still_running(&session), "untrusted pane output cannot enter the input parser");
    write_input(session.master, "?", 1U);
    result(wait_for_marker(&session, "KEYBOARD HELP", 1000), "keyboard help is available in-product");
    write_input(session.master, "?", 1U);
    result(wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000), "keyboard help closes in raw mode");
    write_input(session.master, ":", 1U);
    result(wait_for_marker(&session, "Action search:", 1000), "dashboard action search is reachable");
    write_input(session.master, "dashboard\n", 10U);
    result(wait_for_marker(&session, "FAKE DASHBOARD", 1000), "dashboard delegates to the shell CLI");
    write_input(session.master, "\n", 1U);
    result(wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000), "dashboard returns to native raw mode");
    write_input(session.master, ":", 1U);
    (void)wait_for_marker(&session, "Action search:", 1000);
    write_input(session.master, "spawn\n", 6U);
    (void)wait_for_marker(&session, "Branch to spawn:", 1000);
    write_input(session.master, "feature-native\n", 15U);
    (void)wait_for_marker(&session, "Profile (blank=project default, none=no agent):", 1000);
    write_input(session.master, "codex\n", 6U);
    (void)wait_for_marker(&session, "Template (optional):", 1000);
    write_input(session.master, "review\n", 7U);
    (void)wait_for_marker(&session, "Layout (blank=default, dev, full):", 1000);
    write_input(session.master, "full\n", 5U);
    result(wait_for_marker(&session, "FAKE SPAWN spawn feature-native --profile codex --template review --layout full", 1000),
           "spawn builds explicit profile, template, and layout argv");
    write_input(session.master, "\n", 1U);
    result(wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000), "spawn returns to native raw mode");
    write_input(session.master, "A", 1U);
    result(wait_for_marker(&session, "3 marked", 1000), "select-all marks every visible head");
    write_input(session.master, "G", 1U);
    (void)wait_for_marker(&session, "Group for selected heads:", 1000);
    write_input(session.master, "release\n", 8U);
    result(wait_for_marker(&session,
           "FAKE GROUP group create release feature-live feature-stale feature-unavailable", 1000),
           "bulk group assignment delegates explicit selected branches to the shell CLI");
    write_input(session.master, "\n", 1U);
    result(wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000), "bulk group returns to native raw mode");
    write_input(session.master, "A", 1U);
    (void)wait_for_marker(&session, "3 marked", 1000);
    write_input(session.master, "x", 1U);
    result(wait_for_marker(&session, "Bulk kill complete: 2 command(s), 1 current skipped", 1000),
           "bulk kill delegates non-current branches and preserves the current session");
    write_input(session.master, "\n", 1U);
    result(wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000), "bulk kill returns to native raw mode");
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

static void test_fleet_attach(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    char argv_path[128], captured[512] = "";
    FILE *argv_file;
    size_t length;
    snprintf(argv_path, sizeof(argv_path), "/tmp/hydra-i1-fleet-attach-%ld", (long)getpid());
    (void)unlink(argv_path);
    (void)setenv("HYDRA_TEST_FLEET_ATTACH", "1", 1);
    (void)setenv("HYDRA_TEST_FLEET_FRESH", "1", 1);
    (void)setenv("HYDRA_TEST_ATTACH_ARGV", argv_path, 1);
    if (!result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0,
                "open fleet attachment terminal")) {
        (void)unsetenv("HYDRA_TEST_FLEET_ATTACH");
        (void)unsetenv("HYDRA_TEST_FLEET_FRESH");
        (void)unsetenv("HYDRA_TEST_ATTACH_ARGV");
        return;
    }
    result(wait_for_raw(&session), "fleet view enters raw mode");
    write_input(session.master, "a", 1U);
    sleep_ms(200);
    argv_file = fopen(argv_path, "r");
    if (argv_file != NULL) {
        length = fread(captured, 1U, sizeof(captured) - 1U, argv_file);
        captured[length] = '\0';
        fclose(argv_file);
    }
    result(strstr(captured, "fleet\nattach\nbuilder\n--project\n/work/project\n--instance\ninstance_aaaaaaaaaaaaaaaaaaaa\n--\nfeature-build\n") != NULL,
           "fleet attach uses exact host, project, and instance argv");
    (void)kill(session.pid, SIGTERM);
    (void)wait_for_exit(&session);
    close_session(&session);
    (void)unlink(argv_path);
    (void)unsetenv("HYDRA_TEST_FLEET_FRESH");

    (void)unlink(argv_path);
    if (!result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0,
                "open unknown-freshness fleet attachment terminal")) {
        (void)unsetenv("HYDRA_TEST_FLEET_ATTACH");
        (void)unsetenv("HYDRA_TEST_ATTACH_ARGV");
        return;
    }
    result(wait_for_raw(&session), "unknown-freshness fleet view enters raw mode");
    result(wait_for_marker(&session, "builder", 1000), "unknown-freshness fleet row is visible");
    write_input(session.master, "a", 1U);
    result(wait_for_marker(&session, "Remote target is stale", 3000), "unknown host freshness shows refusal notice");
    result(still_running(&session), "unknown host freshness refuses a new attachment");
    result(access(argv_path, F_OK) != 0, "unknown host freshness does not invoke remote attach");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0, "unknown-freshness fleet view exits cleanly");
    close_session(&session);
    (void)unsetenv("HYDRA_TEST_FLEET_ATTACH");
    (void)unsetenv("HYDRA_TEST_ATTACH_ARGV");
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
    result(wait_for_marker(&session, "\033[?1000l\033[?1006l", 1000), "signal disables mouse reporting");
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
    if (!result(open_session(&session, dispatch, "/usr/bin/false", fake_bin, 80, 24) == 0,
                "open crash-fallback pseudo-terminal")) return;
    result(wait_for_marker(&session, "\033[?1000l\033[?1006l", 3000),
           "crash fallback disables mouse reporting");
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

#include "test_tui_visualization.inc"

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--i1-measure") == 0) {
        unsigned heads;
        if (!measure_unsigned(argv[2], &heads) || heads == 0U || heads > 10U) return 2;
        return measure_i1(getenv("HYDRA_TUI_BIN") ? getenv("HYDRA_TUI_BIN") : argv[0], heads, argv[3]);
    }
    if (argc == 5 && strcmp(argv[1], "--measure") == 0) {
        return measure_interactive(argv[2], argv[3], argv[4]);
    }
    if (argc != 4) {
        fprintf(stderr, "usage: test-tui-pty TUI FAKE_HYDRA FAKE_BIN\n");
        return 2;
    }
    printf("Running native TUI pseudo-terminal tests...\n");
    test_session_failure();
    test_themes(argv[1], argv[2], argv[3]);
    test_mouse(argv[1], argv[2], argv[3]);
    test_visualization(argv[1], argv[2], argv[3]);
    test_visualization_refresh(argv[1], argv[2], argv[3]);
    test_visualization_hosts(argv[1], argv[2], argv[3]);
    test_fleet_attach(argv[1], argv[2], argv[3]);
    test_small_list(argv[1], argv[2], argv[3]);
    test_interaction(argv[1], argv[2], argv[3]);
    test_palette(argv[1], argv[2], argv[3]);
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
    if (getenv("HYDRA_TEST_EOF_HYDRA") != NULL) {
        test_preflight_failure(argv[1], getenv("HYDRA_TEST_EOF_HYDRA"), argv[3], 80, 24, 4,
                               "adapter deadline remains bounded after stdout EOF");
    }
    test_crash_fallback(getenv("HYDRA_TEST_CRASH_DISPATCH"), argv[3]);
    printf("Tests: %d, Failed: %d\n", tests, failures);
    return failures == 0 ? 0 : 1;
}
