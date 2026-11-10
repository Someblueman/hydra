#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "program_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#elif defined(__linux__)
#include <dirent.h>
#endif

/* The real terminal: poll(2) over the input descriptor, a self-pipe and the
 * loop's descriptors; signals and kt_program_post arrive through the pipe;
 * children are forked into their own sessions. The terminal is put back the
 * way it was found on every way out, including exit() and fatal signals. */

extern char **environ;

enum { RECORD_WAKE = 0, RECORD_POST = 1 };
#define POST_RECORD (1 + sizeof(struct kt_msg))
/* A posted message must reach the pipe in one atomic write. */
typedef char kt_post_fits_pipe_buf[POST_RECORD <= _POSIX_PIPE_BUF ? 1 : -1];

static const int wake_signals[] = {SIGWINCH, SIGINT, SIGTERM, SIGHUP, SIGCHLD, SIGCONT, SIGTSTP};
static const unsigned wake_bits[] = {KT_WAKE_RESIZE, KT_WAKE_INTERRUPT, KT_WAKE_TERMINATE,
                                     KT_WAKE_HANGUP, KT_WAKE_CHILD, KT_WAKE_CONTINUE, KT_WAKE_STOP};
static const int fatal_signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
#define WAKE_COUNT (sizeof(wake_signals) / sizeof(*wake_signals))
#define FATAL_COUNT (sizeof(fatal_signals) / sizeof(*fatal_signals))
#define CONTINUE_SLOT 5
#define STOP_SLOT 6
#define PIPE_SLOT (WAKE_COUNT + FATAL_COUNT)
#define SIGNAL_SLOTS (PIPE_SLOT + 1)

struct kt_posix {
    struct kt_allocator allocator;
    int input_fd, output_fd, input_flags, output_flags;
    struct termios saved;
    volatile sig_atomic_t raw;
    short input_events;
    int wake[2];
    volatile sig_atomic_t post_fd;
    unsigned char records[4096 + POST_RECORD];
    size_t record_start, record_end;
    struct sigaction previous[SIGNAL_SLOTS];
    bool installed[SIGNAL_SLOTS], stop_ignored;
    /* Read by the exit and fatal-signal paths. */
    char restore[KT_LEAVE_MAX];
    volatile sig_atomic_t restore_length;
    pid_t *owned;
    size_t owned_count, owned_capacity;
};

/* One program holds the terminal at a time; signal handlers find it here. */
static struct kt_posix *volatile active;
static volatile sig_atomic_t caught[WAKE_COUNT];
static bool exit_registered;

/* ---- signals ------------------------------------------------------------ */

static void wake(const struct kt_posix *b) {
    unsigned char record = RECORD_WAKE;
    int fd = b ? (int)b->post_fd : -1;
    /* A full pipe already guarantees a wake-up. */
    if (fd >= 0 && write(fd, &record, 1) < 0) return;
}

static void on_wake(int number) {
    int saved = errno;
    size_t i;
    for (i = 0; i < WAKE_COUNT; i++) if (wake_signals[i] == number) caught[i] = 1;
    wake(active);
    errno = saved;
}

/* Writes what undoes the terminal modes in effect, once. */
static void posix_leave(void *context) {
    struct kt_posix *b = context;
    size_t length = (size_t)b->restore_length, done = 0;
    b->restore_length = 0;
    while (done < length) {
        ssize_t count = write(b->output_fd, b->restore + done, length - done);
        if (count > 0) done += (size_t)count;
        else if (count == 0 || errno != EINTR) break;
    }
}

/* Async-signal-safe: write, tcflush, tcsetattr and fcntl only. */
static void restore_now(struct kt_posix *b) {
    posix_leave(b);
    if (!b->raw) return;
    b->raw = 0;
    (void)tcflush(b->input_fd, TCIFLUSH);
    (void)tcsetattr(b->input_fd, TCSANOW, &b->saved);
    (void)fcntl(b->input_fd, F_SETFL, b->input_flags);
    (void)fcntl(b->output_fd, F_SETFL, b->output_flags);
}

/* The terminal comes back first; the signal then takes its default course,
 * so the exit status and any core dump are what they would have been. */
static void on_fatal(int number) {
    struct kt_posix *b = active;
    if (b) restore_now(b);
    (void)signal(number, SIG_DFL);
    (void)raise(number);
}

static void session_kill(pid_t session);

/* exit() from inside the loop skips kt_program_end. */
static void on_exit_restore(void) {
    struct kt_posix *b = active;
    size_t i;
    if (!b) return;
    restore_now(b);
    for (i = 0; i < b->owned_count; i++) {
        session_kill(b->owned[i]);
        (void)kill(-b->owned[i], SIGKILL);
        (void)kill(b->owned[i], SIGKILL);
    }
}

static void signal_install(struct kt_posix *b, size_t slot, int number, void (*handler)(int), int flags) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler; action.sa_flags = flags;
    (void)sigemptyset(&action.sa_mask);
    b->installed[slot] = sigaction(number, &action, &b->previous[slot]) == 0;
}

/* A signal the parent arranged to ignore (nohup, a shell without job
 * control) stays ignored. */
static bool keeps_ignored(int number) {
    struct sigaction current;
    if (number != SIGINT && number != SIGTERM && number != SIGHUP && number != SIGTSTP) return false;
    return sigaction(number, NULL, &current) == 0 && current.sa_handler == SIG_IGN;
}

static void signals_install(struct kt_posix *b) {
    size_t i;
    memset(b->installed, 0, sizeof(b->installed));
    b->stop_ignored = keeps_ignored(SIGTSTP);
    for (i = 0; i < WAKE_COUNT; i++) {
        int flags = wake_signals[i] == SIGCHLD ? SA_RESTART | SA_NOCLDSTOP : SA_RESTART;
        caught[i] = 0;
        if (!keeps_ignored(wake_signals[i])) signal_install(b, i, wake_signals[i], on_wake, flags);
    }
    for (i = 0; i < FATAL_COUNT; i++) signal_install(b, WAKE_COUNT + i, fatal_signals[i], on_fatal, 0);
    /* A vanished terminal is a failed write, not a silent death. */
    signal_install(b, PIPE_SLOT, SIGPIPE, SIG_IGN, 0);
}

static int slot_signal(size_t slot) {
    if (slot < WAKE_COUNT) return wake_signals[slot];
    return slot < PIPE_SLOT ? fatal_signals[slot - WAKE_COUNT] : SIGPIPE;
}

static void signals_restore(struct kt_posix *b) {
    size_t slot;
    for (slot = 0; slot < SIGNAL_SLOTS; slot++) {
        if (b->installed[slot]) (void)sigaction(slot_signal(slot), &b->previous[slot], NULL);
        b->installed[slot] = false;
    }
}

static unsigned signals_take(void) {
    unsigned taken = 0;
    size_t i;
    for (i = 0; i < WAKE_COUNT; i++) if (caught[i]) { caught[i] = 0; taken |= wake_bits[i]; }
    return taken;
}

/* ---- terminal ----------------------------------------------------------- */

static uint64_t posix_now(void *context) {
    struct timespec time;
    (void)context;
    (void)clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000U + (uint64_t)time.tv_nsec / 1000000U;
}

static bool attributes_set(int fd, const struct termios *attributes) {
    int result;
    do result = tcsetattr(fd, TCSAFLUSH, attributes); while (result != 0 && errno == EINTR);
    return result == 0;
}

static bool posix_raw(void *context, bool enable) {
    struct kt_posix *b = context;
    struct termios raw;
    bool restored;
    if (!enable) {
        if (!b->raw) return true;
        b->raw = 0;
        restored = attributes_set(b->input_fd, &b->saved);
        return fcntl(b->input_fd, F_SETFL, b->input_flags) == 0 &&
               fcntl(b->output_fd, F_SETFL, b->output_flags) == 0 && restored;
    }
    raw = b->saved;
    raw.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= (tcflag_t)~OPOST;
    raw.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= (tcflag_t)~(CSIZE | PARENB); raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
    if (!attributes_set(b->input_fd, &raw)) return false;
    b->raw = 1;
    return true;
}

static bool descriptor_private(int fd) {
    return fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

static void pair_close(int fds[2]) {
    if (fds[0] >= 0) (void)close(fds[0]);
    if (fds[1] >= 0) (void)close(fds[1]);
    fds[0] = fds[1] = -1;
}

/* Close-on-exec pipe; no child ever inherits it past its exec. */
static bool pair_open(int fds[2]) {
    if (pipe(fds) != 0) { fds[0] = fds[1] = -1; return false; }
    if (descriptor_private(fds[0]) && descriptor_private(fds[1])) return true;
    pair_close(fds);
    return false;
}

static bool wake_open(struct kt_posix *b) {
    if (!pair_open(b->wake)) return false;
    if (fcntl(b->wake[0], F_SETFL, O_NONBLOCK) != 0 || fcntl(b->wake[1], F_SETFL, O_NONBLOCK) != 0) {
        pair_close(b->wake);
        return false;
    }
    b->record_start = b->record_end = 0;
    b->post_fd = b->wake[1];
    return true;
}

static void posix_end(void *context) {
    struct kt_posix *b = context;
    b->restore_length = 0;
    (void)posix_raw(b, false);
    signals_restore(b);
    b->post_fd = -1;
    pair_close(b->wake);
    if (active == b) active = NULL;
}

static bool posix_begin(void *context) {
    struct kt_posix *b = context;
    if (active || !isatty(b->input_fd) || !isatty(b->output_fd)) return false;
    if (tcgetattr(b->input_fd, &b->saved) != 0) return false;
    b->input_flags = fcntl(b->input_fd, F_GETFL);
    b->output_flags = fcntl(b->output_fd, F_GETFL);
    if (b->input_flags < 0 || b->output_flags < 0 || !wake_open(b)) return false;
    b->restore_length = 0; b->input_events = 0; b->owned_count = 0;
    active = b;
    signals_install(b);
    if (!exit_registered) exit_registered = atexit(on_exit_restore) == 0;
    if (posix_raw(b, true)) return true;
    posix_end(b);
    return false;
}

static bool posix_size(void *context, int *columns, int *rows) {
    struct kt_posix *b = context;
    struct winsize size;
    if (ioctl(b->output_fd, TIOCGWINSZ, &size) != 0 || !size.ws_col || !size.ws_row) return false;
    *columns = size.ws_col; *rows = size.ws_row;
    return true;
}

/* One write per frame; more only when the terminal takes less than offered. */
static bool posix_write(void *context, const char *bytes, size_t length) {
    struct kt_posix *b = context;
    while (length) {
        ssize_t count = write(b->output_fd, bytes, length);
        if (count > 0) { bytes += count; length -= (size_t)count; }
        else if (count < 0 && errno == EAGAIN) {
            struct pollfd writable = {0, POLLOUT, 0};
            writable.fd = b->output_fd;
            (void)poll(&writable, 1, -1);
        } else if (count == 0 || errno != EINTR) return false;
    }
    return true;
}

/* In raw mode a read can return nothing without the terminal being gone;
 * only a hangup or error reported by poll makes an empty read the end. */
static long posix_read(void *context, void *buffer, size_t capacity) {
    struct kt_posix *b = context;
    ssize_t count = read(b->input_fd, buffer, capacity);
    if (count > 0) return (long)count;
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) return KT_READ_NONE;
    if (count == 0 && !(b->input_events & (POLLHUP | POLLERR | POLLNVAL))) return KT_READ_NONE;
    return KT_READ_END;
}

static void posix_restore(void *context, const char *bytes, size_t length) {
    struct kt_posix *b = context;
    b->restore_length = 0;
    if (length > sizeof(b->restore)) length = sizeof(b->restore);
    memcpy(b->restore, bytes, length);
    b->restore_length = (sig_atomic_t)length;
}

/* ---- wake-up pipe ------------------------------------------------------- */

static void records_read(struct kt_posix *b) {
    ssize_t count;
    memmove(b->records, b->records + b->record_start, b->record_end - b->record_start);
    b->record_end -= b->record_start; b->record_start = 0;
    count = read(b->wake[0], b->records + b->record_end, sizeof(b->records) - b->record_end);
    if (count > 0) b->record_end += (size_t)count;
}

static bool posix_wait(void *context, struct pollfd *fds, size_t count, int timeout_ms,
                       struct kt_ready *ready) {
    struct kt_posix *b = context;
    size_t i;
    int result;
    fds[0].fd = ready->input ? b->input_fd : -1; fds[0].events = POLLIN;
    fds[1].fd = b->wake[0]; fds[1].events = POLLIN;
    result = poll(fds, (nfds_t)count, timeout_ms);
    if (result < 0 && errno != EINTR) return false;
    if (result < 0) for (i = 0; i < count; i++) fds[i].revents = 0;
    if (fds[1].revents) records_read(b);
    b->input_events = fds[0].revents;
    ready->input = fds[0].revents != 0;
    ready->signals = signals_take();
    return true;
}

static bool posix_post(void *context, const struct kt_msg *msg) {
    struct kt_posix *b = context;
    unsigned char record[POST_RECORD];
    int fd = (int)b->post_fd, saved = errno;
    ssize_t count;
    if (fd < 0) return false;
    record[0] = RECORD_POST;
    memcpy(record + 1, msg, sizeof(*msg));
    do count = write(fd, record, sizeof(record)); while (count < 0 && errno == EINTR);
    errno = saved;
    return count == (ssize_t)sizeof(record);
}

static bool posix_posted(void *context, struct kt_msg *msg) {
    struct kt_posix *b = context;
    while (b->record_start < b->record_end && b->records[b->record_start] == RECORD_WAKE) b->record_start++;
    if (b->record_end - b->record_start < POST_RECORD) return false;
    memcpy(msg, b->records + b->record_start + 1, sizeof(*msg));
    b->record_start += POST_RECORD;
    return true;
}

/* ---- children ----------------------------------------------------------- */

/* Job-control shells put helpers in other process groups of the session.
 * Only an escalation to SIGKILL walks the process table for them. */
static void session_kill(pid_t session) {
#ifdef __APPLE__
    int size = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0), used, i;
    pid_t *pids;
    if (size <= 0 || size > INT_MAX - 4096) return;
    size += 4096;
    pids = malloc((size_t)size);
    if (!pids) return;
    used = proc_listpids(PROC_ALL_PIDS, 0, pids, size);
    for (i = 0; used > 0 && (size_t)i < (size_t)used / sizeof(*pids); i++)
        if (pids[i] > 0 && getsid(pids[i]) == session) (void)kill(pids[i], SIGKILL);
    free(pids);
#elif defined(__linux__)
    DIR *directory = opendir("/proc");
    struct dirent *entry;
    if (!directory) return;
    while ((entry = readdir(directory)) != NULL) {
        char *end;
        long pid;
        if (entry->d_name[0] < '1' || entry->d_name[0] > '9') continue;
        errno = 0;
        pid = strtol(entry->d_name, &end, 10);
        if (!errno && !*end && pid <= INT_MAX && getsid((pid_t)pid) == session)
            (void)kill((pid_t)pid, SIGKILL);
    }
    (void)closedir(directory);
#else
    (void)session;
#endif
}

static void owned_add(struct kt_posix *b, pid_t pid) {
    size_t capacity = b->owned_capacity ? b->owned_capacity * 2 : 8;
    pid_t *owned = b->owned;
    if (b->owned_count == b->owned_capacity) {
        owned = b->allocator.resize(b->allocator.context, b->owned, capacity * sizeof(*owned));
        if (!owned) return;
        b->owned = owned; b->owned_capacity = capacity;
    }
    owned[b->owned_count++] = pid;
}

static void owned_remove(struct kt_posix *b, pid_t pid) {
    size_t i;
    for (i = 0; i < b->owned_count; i++)
        if (b->owned[i] == pid) { b->owned[i] = b->owned[--b->owned_count]; return; }
}

/* Runs in the forked child: handlers do not survive exec but ignored signals
 * and the signal mask do. */
static void child_signals(void) {
    struct sigaction action;
    sigset_t none;
    size_t slot;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    for (slot = 0; slot < SIGNAL_SLOTS; slot++) (void)sigaction(slot_signal(slot), &action, NULL);
    (void)sigaction(SIGQUIT, &action, NULL);
    (void)sigemptyset(&none);
    (void)sigprocmask(SIG_SETMASK, &none, NULL);
}

static bool child_stdio(const struct kt_spawn *spawn, int out) {
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0 || dup2(out, STDOUT_FILENO) < 0 ||
        dup2(spawn->merge_stderr ? out : null_fd, STDERR_FILENO) < 0) return false;
    if (null_fd > STDERR_FILENO) (void)close(null_fd);
    return true;
}

/* Never returns. A failure before exec is reported through `report`, which
 * exec closes on success. */
static void child_run(const struct kt_spawn *spawn, int out, int report) {
    int error;
    if (setsid() >= 0 && child_stdio(spawn, out) && (!spawn->directory || chdir(spawn->directory) == 0)) {
        child_signals();
        if (spawn->envp) environ = (char **)spawn->envp;
        (void)execvp(spawn->argv[0], spawn->argv);
    }
    error = errno;
    if (write(report, &error, sizeof(error)) < 0) _exit(127);
    _exit(127);
}

/* Blocks for the short time between fork and exec, as posix_spawn does. */
static int start_error(int report) {
    int error = 0;
    ssize_t count;
    do count = read(report, &error, sizeof(error)); while (count < 0 && errno == EINTR);
    if (count == 0) return 0;
    return count == (ssize_t)sizeof(error) && error ? error : EIO;
}

static void reap_blocking(pid_t pid) {
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
}

static bool posix_start(void *context, const struct kt_spawn *spawn, uint64_t id, uint64_t tag,
                        pid_t *pid, int *fd) {
    struct kt_posix *b = context;
    int out[2], report[2], error;
    pid_t child;
    (void)id; (void)tag;
    if (!pair_open(out)) return false;
    if (!pair_open(report)) { pair_close(out); return false; }
    child = fork();
    if (child == 0) child_run(spawn, out[1], report[1]);
    (void)close(out[1]); (void)close(report[1]);
    out[1] = report[1] = -1;
    error = child < 0 ? errno : start_error(report[0]);
    pair_close(report);
    if (error || fcntl(out[0], F_SETFL, O_NONBLOCK) != 0) {
        pair_close(out);
        if (child > 0) { (void)kill(child, SIGKILL); reap_blocking(child); }
        errno = error;
        return false;
    }
    if (spawn->on_exit == KT_SPAWN_KILL) owned_add(b, child);
    *pid = child; *fd = out[0];
    return true;
}

static long posix_child_read(void *context, pid_t pid, int fd, void *buffer, size_t capacity) {
    ssize_t count = read(fd, buffer, capacity);
    (void)context; (void)pid;
    if (count >= 0) return (long)count;
    return errno == EAGAIN || errno == EINTR ? KT_READ_NONE : KT_READ_ERROR;
}

/* The whole process group, and on SIGKILL the whole session. The leader is
 * not reaped before its output closes, so the group id cannot be reused. */
static void posix_child_signal(void *context, pid_t pid, int signal_number) {
    (void)context;
    if (signal_number == SIGKILL) session_kill(pid);
    (void)kill(-pid, signal_number);
    (void)kill(pid, signal_number);
}

static int exit_status(int status) {
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static bool posix_child_reap(void *context, pid_t pid, int *status) {
    struct kt_posix *b = context;
    int raw = 0;
    pid_t result = waitpid(pid, &raw, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) return false;
    *status = result == pid ? exit_status(raw) : -1;
    owned_remove(b, pid);
    return true;
}

static void posix_child_close(void *context, pid_t pid, int fd) {
    (void)context; (void)pid;
    if (fd >= 0) (void)close(fd);
}

/* ---- foreground hand-off ------------------------------------------------ */

static void exec_child(const struct kt_posix *b, char *const argv[]) {
    child_signals();
    if (b->input_fd != STDIN_FILENO && dup2(b->input_fd, STDIN_FILENO) < 0) _exit(126);
    if (b->output_fd != STDOUT_FILENO && dup2(b->output_fd, STDOUT_FILENO) < 0) _exit(126);
    (void)execvp(argv[0], argv);
    _exit(127);
}

static void disposition(int number, void (*handler)(int), struct sigaction *previous) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(number, &action, previous);
}

/* The child shares the foreground process group, so the keyboard's signals
 * reach both. Like system(3), only the child acts on interrupt and quit; a
 * suspend stops both, so the shell sees its job stop, and both continue
 * together. */
static int posix_exec(void *context, char *const argv[]) {
    struct kt_posix *b = context;
    struct sigaction interrupt, quit, stop;
    int status = 0, result = -1;
    pid_t child, waited;
    disposition(SIGINT, SIG_IGN, &interrupt);
    disposition(SIGQUIT, SIG_IGN, &quit);
    disposition(SIGTSTP, b->stop_ignored ? SIG_IGN : SIG_DFL, &stop);
    child = fork();
    if (child == 0) exec_child(b, argv);
    if (child > 0) {
        do waited = waitpid(child, &status, 0); while (waited < 0 && errno == EINTR);
        if (waited == child) result = exit_status(status);
    }
    (void)sigaction(SIGINT, &interrupt, NULL);
    (void)sigaction(SIGQUIT, &quit, NULL);
    (void)sigaction(SIGTSTP, &stop, NULL);
    caught[CONTINUE_SLOT] = 0;
    return result;
}

/* Stops the whole foreground job, as the terminal's suspend key would.
 * Execution continues here after SIGCONT; that continue is this suspend's
 * own and is not reported a second time. */
static void posix_suspend(void *context) {
    struct kt_posix *b = context;
    struct sigaction previous;
    if (b->stop_ignored) return;
    disposition(SIGTSTP, SIG_DFL, &previous);
    (void)kill(0, SIGTSTP);
    (void)sigaction(SIGTSTP, &previous, NULL);
    caught[CONTINUE_SLOT] = 0; caught[STOP_SLOT] = 0;
}

static void posix_destroy(void *context) {
    struct kt_posix *b = context;
    struct kt_allocator allocator = b->allocator;
    (void)allocator.resize(allocator.context, b->owned, 0);
    (void)allocator.resize(allocator.context, b, 0);
}

void *kt_posix_backend_new(const struct kt_options *options) {
    struct kt_posix *b = options->allocator.resize(options->allocator.context, NULL, sizeof(*b));
    if (!b) return NULL;
    memset(b, 0, sizeof(*b));
    b->allocator = options->allocator;
    b->input_fd = options->input_fd; b->output_fd = options->output_fd;
    b->wake[0] = b->wake[1] = -1; b->post_fd = -1;
    return b;
}

const struct kt_backend kt_posix_backend = {
    posix_now, posix_begin, posix_end, posix_raw, posix_size, posix_write, posix_read,
    posix_wait, posix_restore, posix_leave, posix_post, posix_posted, posix_start, NULL,
    posix_child_read, posix_child_signal, posix_child_reap, posix_child_close,
    posix_exec, posix_suspend, posix_destroy
};
