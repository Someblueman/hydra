/* Capture buffers grow with the output: large output arrives whole and NUL
 * terminated on both streams, output that reaches F_LIMIT is reported as
 * truncated with status 125, and an endless writer is stopped there. */
static void capture_expect(const char *script, int status, size_t out_bytes, size_t err_bytes) {
    struct f_capture cap = {0};
    char *command[] = {(char *)"sh", (char *)"-c", (char *)script, NULL};
    assert(!f_run(command, NULL, 0, 30, &cap));
    if (cap.status != status || cap.out_bytes != out_bytes || cap.err_bytes != err_bytes)
        fprintf(stderr, "capture %.60s: status %d (want %d), out %zu (want %zu), err %zu (want %zu), timeout %d\n",
                script, cap.status, status, cap.out_bytes, out_bytes, cap.err_bytes, err_bytes, (int)cap.timeout);
    assert(cap.status == status);
    assert(cap.out_bytes == out_bytes && cap.err_bytes == err_bytes);
    assert(cap.out && strlen(cap.out) == out_bytes && cap.err && strlen(cap.err) == err_bytes);
    assert(!out_bytes || cap.out[out_bytes - 1] == 'a');
    assert(cap.measurement_complete == (status != 125));
    f_capture_free(&cap);
}
/* A file of n 'a' bytes. The output is written here rather than made with
 * head and tr, whose handling of /dev/zero and octal escapes differs between
 * coreutils implementations. */
static void write_as(const char *path, size_t n) {
    char block[4096];
    FILE *file = fopen(path, "wb");
    assert(file);
    memset(block, 'a', sizeof(block));
    while (n) {
        size_t chunk = n < sizeof(block) ? n : sizeof(block);
        assert(fwrite(block, 1, chunk, file) == chunk);
        n -= chunk;
    }
    assert(!fclose(file));
}
static void capture_growth(const char *dir) {
    char script[3 * F_PATH], out[F_PATH], err[F_PATH], under[F_PATH], at[F_PATH];
    assert(snprintf(out, sizeof(out), "%s/capture-out", dir) < (int)sizeof(out));
    assert(snprintf(err, sizeof(err), "%s/capture-err", dir) < (int)sizeof(err));
    assert(snprintf(under, sizeof(under), "%s/capture-under", dir) < (int)sizeof(under));
    assert(snprintf(at, sizeof(at), "%s/capture-at", dir) < (int)sizeof(at));
    write_as(out, 1048579U); write_as(err, 100003U); write_as(under, F_LIMIT - 1U); write_as(at, F_LIMIT);
    capture_expect("exit 0", 0, 0, 0);
    (void)snprintf(script, sizeof(script), "cat '%s'; cat '%s' >&2", out, err);
    capture_expect(script, 0, 1048579, 100003);
    (void)snprintf(script, sizeof(script), "cat '%s'", under);
    capture_expect(script, 0, F_LIMIT - 1U, 0);
    (void)snprintf(script, sizeof(script), "cat '%s'", at);
    capture_expect(script, 125, F_LIMIT, 0);
    capture_expect("while :; do printf aaaaaaaaaaaaaaaa; done", 125, F_LIMIT, 0);
    capture_expect("while :; do printf aaaaaaaaaaaaaaaa >&2; done", 125, 0, F_LIMIT);
    /* A writer that ignores SIGPIPE is killed before its stream is closed,
     * so it never reports a failed write on the other stream. */
    capture_expect("trap '' PIPE; while :; do printf aaaaaaaaaaaaaaaa; done", 125, F_LIMIT, 0);
    unlink(out); unlink(err); unlink(under); unlink(at);
}
static long capture_clock_ms(void) {
    struct timespec ts;
    assert(!clock_gettime(CLOCK_MONOTONIC, &ts));
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
/* The wait for a child that closed its streams starts short and doubles up
 * to the 50 ms tick, so a child that exits soon is reaped soon. */
static void capture_reap_schedule(void) {
    long pause = f_reap_pause_us(0), total = pause;
    int steps = 1;
    assert(pause > 0 && pause <= 200);
    while (pause < 50000L) {
        long next = f_reap_pause_us(pause);
        assert(next == pause * 2 || next == 50000L);
        pause = next; total += pause; steps++;
    }
    assert(steps >= 8 && total > 50000L);
    assert(f_reap_pause_us(50000L) == 50000L && f_reap_pause_us(-5) == f_reap_pause_us(0));
    assert(f_reap_pause_us(1L << 40) == 50000L);
}
/* Stands in for the child's exit: once the child reports that it closed both
 * streams it waits CAPTURE_EXIT_AFTER_MS, then releases it, and sends the
 * moment of the release down the pipe. The child blocks on the release, so
 * the time between the close and the exit is this wait whatever the machine
 * is doing, and it holds no program start after the close. */
#define CAPTURE_EXIT_AFTER_MS 4L
static void capture_release_child(const char *ready, const char *go, int report) {
    char byte;
    long released;
    int fd;
    alarm(30);
    fd = open(ready, O_RDONLY);
    if (fd < 0 || read(fd, &byte, 1) != 1) _exit(1);
    close(fd);
    (void)nanosleep(&(struct timespec){0, CAPTURE_EXIT_AFTER_MS * 1000000L}, NULL);
    fd = open(go, O_WRONLY);
    if (fd < 0) _exit(1);
    released = capture_clock_ms();
    if (write(fd, "x", 1) != 1 || write(report, &released, sizeof(released)) != (ssize_t)sizeof(released)) _exit(1);
    _exit(0);
}
/* How long after the child was released f_run returned, in ms. */
static long capture_reap_lag(const char *ready, const char *go) {
    char script[4 * F_PATH];
    char *command[] = {(char *)"sh", (char *)"-c", script, NULL};
    struct f_capture cap = {0};
    long released = -1, returned;
    int report[2], status;
    pid_t helper;
    assert(snprintf(script, sizeof(script), "exec 1>&- 2>&-; echo >'%s'; read x <'%s'; exit 5", ready, go) < (int)sizeof(script));
    assert(!pipe(report));
    helper = fork();
    assert(helper >= 0);
    if (!helper) { close(report[0]); capture_release_child(ready, go, report[1]); }
    close(report[1]);
    assert(!f_run(command, NULL, 0, 30, &cap));
    returned = capture_clock_ms();
    assert(read(report[0], &released, sizeof(released)) == (ssize_t)sizeof(released));
    close(report[0]);
    assert(waitpid(helper, &status, 0) == helper && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(cap.status == 5 && !cap.timeout && !cap.out_bytes && !cap.err_bytes && cap.measurement_complete);
    f_capture_free(&cap);
    return returned - released;
}
/* A child that closed both streams is reaped soon after it exits, not up to
 * a whole polling tick (50 ms) later. The child closes its streams, then
 * waits for a release that comes CAPTURE_EXIT_AFTER_MS later, so it is alive
 * when the streams close and exits that long after them; the time from the
 * release to f_run's return is the reap lag. The pauses double from 100 us,
 * so the lag is about the 2 ms left to the 6.3 ms mark plus what the machine
 * adds: a fixed 50 ms tick leaves about 46 ms. The fastest of several runs is
 * used, so a busy machine, whose delays only add to a run, does not fail the
 * case; a fixed tick adds the same delay to every run. */
static void capture_prompt_reap(const char *dir) {
    char ready[F_PATH], go[F_PATH];
    long best = -1;
    int i;
    capture_reap_schedule();
    assert(snprintf(ready, sizeof(ready), "%s/reap-ready", dir) < (int)sizeof(ready));
    assert(snprintf(go, sizeof(go), "%s/reap-go", dir) < (int)sizeof(go));
    for (i = 0; i < 10; i++) {
        long lag;
        assert(!mkfifo(ready, 0600) && !mkfifo(go, 0600));
        lag = capture_reap_lag(ready, go);
        unlink(ready); unlink(go);
        if (best < 0 || lag < best) best = lag;
    }
    if (best >= 20)
        fprintf(stderr, "capture: a child that closed its streams %ld ms before exiting was reaped %ld ms after it exited\n",
                CAPTURE_EXIT_AFTER_MS, best);
    assert(best < 20);
}
static bool capture_stop_second(void *context) {
    int *calls = context;
    return ++*calls > 1;
}
/* With both streams closed the deadline still ends the child with status
 * 124, a child that ignores SIGTERM is killed only after the 500 ms that
 * follow it, and a cancelled child is not reaped before its grace period
 * (less the 200 ms kept for SIGKILL) has run. */
static void capture_closed_guarantees(void) {
    char *quiet[] = {(char *)"sh", (char *)"-c", (char *)"exec 1>&- 2>&-; exec sleep 10", NULL};
    char *stubborn[] = {(char *)"sh", (char *)"-c", (char *)"trap '' TERM; exec 1>&- 2>&-; exec sleep 10", NULL};
    struct f_capture cap = {0};
    struct f_control control = {0};
    int calls = 0;
    long started = capture_clock_ms(), elapsed;
    assert(!f_run(quiet, NULL, 0, 1, &cap));
    elapsed = capture_clock_ms() - started;
    assert(cap.timeout && cap.status == 124 && !cap.cancelled && elapsed >= 990 && elapsed < 8000);
    f_capture_free(&cap);
    started = capture_clock_ms();
    assert(!f_run(stubborn, NULL, 0, 1, &cap));
    elapsed = capture_clock_ms() - started;
    assert(cap.timeout && cap.status == 124 && elapsed >= 1490 && elapsed < 8000);
    f_capture_free(&cap);
    control.log_fd[0] = control.log_fd[1] = -1;
    control.stop = capture_stop_second; control.context = &calls; control.grace_seconds = 1;
    started = capture_clock_ms();
    assert(!f_run_controlled(quiet, NULL, 0, 30, &cap, &control));
    elapsed = capture_clock_ms() - started;
    assert(cap.cancelled && cap.status == 130 && !cap.timeout && !cap.stop_unknown && !control.stop_unknown);
    assert(elapsed >= 790 && elapsed < 8000);
    f_capture_free(&cap);
}
