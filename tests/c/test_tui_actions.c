/* Production actions with real spawn, pipes, clocks, signals and waits.
 * There is no loop here, so a captured action waits by polling its own pipe,
 * as it does once the control centre is shutting down. */
#include "../../src/tui/actions.c"
#include "../../src/tui/action_capture.c"
#include "../../src/tui/setup_flow.h"

static int check(bool ok, const char *name) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    return ok ? 0 : 1;
}

static int timeout_case(struct app *app, const char *script, const char *name) {
    char out[512];
    char *argv[] = {"sh", "-c", (char *)script, NULL};
    struct timespec started;
    int status, child_status;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    status = run_captured(app, argv, out, sizeof(out), 50L);
    long elapsed = action_elapsed(&started);
    int failures = check(status == 124 && elapsed < 2000 && strstr(out, "outcome unknown"), name);
    failures += check(!app->action_pid && waitpid(-1, &child_status, WNOHANG) == -1 && errno == ECHILD,
                      "timed-out action leader is reaped");
    return failures;
}

/* How a child was started, as its own report: this program again, as
 * `--signals [FILE]`. Dispositions and the mask are read directly, since a
 * shell may clear the mask it was started with. */
#define SIGNAL_EXPECTED "pipe=default term=open stop=ignored"
static int signal_report(const char *path) {
    struct sigaction broken, stop;
    sigset_t mask;
    FILE *out = path ? fopen(path, "w") : stdout;
    if (!out || sigaction(SIGPIPE, NULL, &broken) || sigaction(SIGTSTP, NULL, &stop) ||
        sigprocmask(SIG_SETMASK, NULL, &mask)) return 2;
    fprintf(out, "pipe=%s term=%s stop=%s", broken.sa_handler == SIG_IGN ? "ignored" : "default",
            sigismember(&mask, SIGTERM) ? "blocked" : "open", stop.sa_handler == SIG_IGN ? "ignored" : "default");
    return fclose(out) ? 2 : 0;
}

/* The state a child must not inherit: SIGPIPE ignored, as the loop leaves it,
 * and a blocked signal; and the one it must: an ignored stop signal. */
static void signal_state(bool set, struct sigaction previous[2], sigset_t *mask) {
    struct sigaction ignore;
    sigset_t blocked;
    if (!set) {
        (void)sigaction(SIGPIPE, &previous[0], NULL); (void)sigaction(SIGTSTP, &previous[1], NULL);
        (void)sigprocmask(SIG_SETMASK, mask, NULL);
        return;
    }
    memset(&ignore, 0, sizeof(ignore));
    ignore.sa_handler = SIG_IGN;
    (void)sigemptyset(&ignore.sa_mask);
    (void)sigaction(SIGPIPE, &ignore, &previous[0]); (void)sigaction(SIGTSTP, &ignore, &previous[1]);
    (void)sigemptyset(&blocked); (void)sigaddset(&blocked, SIGTERM);
    (void)sigprocmask(SIG_BLOCK, &blocked, mask);
}

static void signal_read(FILE *file, char *report, size_t size) {
    size_t length = file ? fread(report, 1U, size - 1U, file) : 0U;
    report[length] = '\0';
    if (file) fclose(file);
}

static void signal_observed(char *const argv[], char *report, size_t size) {
    struct native_capture capture;
    const struct timespec pause = {0, 10000000L};
    report[0] = '\0';
    if (!native_capture_start(&capture, argv, 5000L)) return;
    while (!native_capture_step(&capture)) (void)nanosleep(&pause, NULL);
    signal_read(native_capture_take(&capture), report, size);
}

static void signal_setup(char *const argv[], char *report, size_t size) {
    struct setup_capture capture;
    const struct timespec pause = {0, 10000000L};
    size_t length = 0;
    int status;
    char *text;
    report[0] = '\0';
    if (!setup_capture_start(&capture, argv, 5000L)) return;
    while (!setup_capture_step(&capture)) (void)nanosleep(&pause, NULL);
    text = setup_capture_finish(&capture, &length, &status);
    if (text) copy_text(report, size, text);
    free(text);
}

/* A detached child has no output: it reports into the file it is given. */
static void signal_detached(char *self, char *report, size_t size) {
    char path[] = "/tmp/hydra-tui-actions-signals-XXXXXX";
    char *argv[] = {self, "--signals", path, NULL};
    int fd = mkstemp(path), status;
    pid_t pid = 0;
    report[0] = '\0';
    if (fd < 0) return;
    close(fd);
    if (native_detached_start(&pid, argv, -1)) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
        signal_read(fopen(path, "r"), report, size);
    }
    (void)unlink(path);
}

static int signal_cases(struct app *app, char *self) {
    char *argv[] = {self, "--signals", NULL};
    struct sigaction previous[2];
    sigset_t mask;
    char report[4][128];
    int failed = 0;
    signal_state(true, previous, &mask);
    (void)run_captured(app, argv, report[0], sizeof(report[0]), 5000L);
    signal_observed(argv, report[1], sizeof(report[1]));
    signal_detached(self, report[2], sizeof(report[2]));
    signal_setup(argv, report[3], sizeof(report[3]));
    signal_state(false, previous, &mask);
    failed += check(!strcmp(report[0], SIGNAL_EXPECTED), "a captured action starts with default signals and an empty mask");
    failed += check(!strcmp(report[1], SIGNAL_EXPECTED), "a captured observation starts with default signals and an empty mask");
    failed += check(!strcmp(report[2], SIGNAL_EXPECTED), "a detached owner starts with default signals and an empty mask");
    failed += check(!strcmp(report[3], SIGNAL_EXPECTED), "a setup command starts with default signals and an empty mask");
    if (failed) printf("  expected \"" SIGNAL_EXPECTED "\"; got \"%s\", \"%s\", \"%s\", \"%s\"\n", report[0], report[1], report[2], report[3]);
    return failed;
}

/* This program again, as `--hold FILE`: a descendant of a capture's producer
 * that leaves the producer's session and keeps its output open, as a daemon
 * started by a `hydra` command would. The producer itself exits at once. The
 * descendant records its PID and lives for HOLD_MS at most, so nothing is
 * left behind and a capture that waits for it fails its case instead of
 * hanging it. */
#define HOLD_MS 20000L
#define HOLD_BOUND_MS 10000L
static int hold_output(const char *path) {
    const struct timespec life = {HOLD_MS / 1000L, 0};
    pid_t child = fork();
    FILE *file;
    if (child) return child < 0 ? 2 : 0;
    if (setsid() < 0 || !(file = fopen(path, "w"))) _exit(2);
    fprintf(file, "%ld\n", (long)getpid());
    if (fclose(file)) _exit(2);
    (void)nanosleep(&life, NULL);
    _exit(0);
}

/* The PID the holder recorded once it had left the session, or 0. */
static pid_t hold_started(const char *path) {
    const struct timespec pause = {0, 10000000L};
    struct timespec started;
    long pid = 0;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    while (pid <= 0 && action_elapsed(&started) < HOLD_BOUND_MS) {
        FILE *file = fopen(path, "r");
        if (file && (fscanf(file, "%ld", &pid) != 1 || fgetc(file) != '\n')) pid = 0;
        if (file) fclose(file);
        if (pid <= 0) (void)nanosleep(&pause, NULL);
    }
    return (pid_t)pid;
}

/* A capture whose output a descendant outside its session keeps open ends
 * within a bound: when it is destroyed while running (quit), and when it is
 * left to run into its budget. The descendant is not signalled. */
static int hold_case(char *self, bool destroy, const char *name) {
    char path[] = "/tmp/hydra-tui-actions-hold-XXXXXX";
    char *argv[] = {self, "--hold", path, NULL};
    const struct timespec pause = {0, 10000000L};
    struct native_capture capture;
    struct timespec started;
    int fd = mkstemp(path), failed;
    pid_t holder = 0;
    bool ended = false;
    if (fd >= 0) close(fd);
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    if (fd >= 0 && native_capture_start(&capture, argv, destroy ? 60000L : 100L)) {
        holder = hold_started(path);
        (void)clock_gettime(CLOCK_MONOTONIC, &started);
        while (!destroy && !native_capture_step(&capture) && action_elapsed(&started) < HOLD_MS + 5000L)
            (void)nanosleep(&pause, NULL);
        ended = destroy || (capture.reaped && capture.failed);
        native_capture_destroy(&capture);
    }
    failed = check(holder > 0 && ended && action_elapsed(&started) < HOLD_BOUND_MS, name);
    failed += check(holder > 0 && kill(holder, 0) == 0, "the descendant that left the session is not signalled");
    if (holder > 0) (void)kill(holder, SIGKILL);
    if (fd >= 0) (void)unlink(path);
    return failed;
}

/* Captures that resist SIGTERM, destroyed together, share one grace period:
 * the time is that of one escalation to SIGKILL, not of four in a row. */
#define RESISTANT 4U
static int shared_grace_case(void) {
    char *argv[] = {"sh", "-c", "trap '' TERM; printf ready; exec sleep 20", NULL};
    const struct timespec pause = {0, 10000000L};
    struct native_capture captures[RESISTANT], *all[RESISTANT];
    struct timespec started;
    size_t i, ready = 0;
    long elapsed;
    for (i = 0; i < RESISTANT; i++) {
        all[i] = &captures[i];
        if (!native_capture_start(all[i], argv, 60000L)) return check(false, "resistant captures start");
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    while (ready < RESISTANT && action_elapsed(&started) < HOLD_BOUND_MS) {
        for (i = ready = 0; i < RESISTANT; i++) { (void)native_capture_step(all[i]); ready += captures[i].bytes > 0; }
        (void)nanosleep(&pause, NULL);
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    native_captures_destroy(all, RESISTANT);
    elapsed = action_elapsed(&started);
    if (ready < RESISTANT || elapsed < 700 || elapsed >= 2400) printf("  %zu ready, destroyed in %ld ms\n", ready, elapsed);
    return check(ready == RESISTANT && elapsed >= 700 && elapsed < 2400,
                 "captures destroyed together share one grace period");
}

int main(int argc, char **argv) {
    struct app *app;
    char output[128], *ok[] = {"sh", "-c", "printf complete; exit 7", NULL};
    struct removal r = {0};
    char targets[1][TEXT] = {"current-head"};
    int failed = 0;
    if (argc >= 2 && !strcmp(argv[1], "--signals")) return signal_report(argc > 2 ? argv[2] : NULL);
    if (argc == 3 && !strcmp(argv[1], "--hold")) return hold_output(argv[2]);
    app = calloc(1U, sizeof(*app));
    if (!app) return 1;
    failed += check(run_captured(app, ok, output, sizeof(output), 1000L) == 7 && !strcmp(output, "complete"),
                    "normal action preserves output and nonzero status");
    failed += timeout_case(app, "exec 1>&- 2>&-; exec sleep 20", "pipe EOF does not bypass action deadline");
    failed += timeout_case(app, "while :; do printf flood; done", "continuous output does not bypass action deadline");
    failed += timeout_case(app, "trap '' TERM; exec sleep 20", "TERM resistance reaches bounded KILL escalation");
    failed += timeout_case(app, "sleep 20 & exit 0", "inherited output does not bypass action deadline");
    failed += signal_cases(app, argv[0]);
    failed += hold_case(argv[0], true, "destroying a capture does not wait for output held outside its session");
    failed += hold_case(argv[0], false, "a capture past its budget ends although its output is held outside its session");
    failed += shared_grace_case();
    r.count = 1;
    app->model.head_count = 1;
    copy_text(app->model.heads[0].branch, TEXT, targets[0]);
    copy_text(app->model.heads[0].session, TEXT, "current-session");
    copy_text(app->current_session, TEXT, "current-session");
    remove_one(app, targets[0], 0, &r);
    removal_notice(app, targets, &r);
    failed += check(!r.removed && r.skipped == 1 && strstr(app->notice, "Skipped current-head"),
                    "protected current head reports skipped removal");
    free(app);
    return failed ? 1 : 0;
}
