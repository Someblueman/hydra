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
/* A child that closes both streams and exits a little later is reaped when
 * it exits, not a whole polling tick (50 ms) after the streams closed. The
 * fastest of several runs is judged, so a busy machine does not fail the
 * case; with a fixed tick every run takes at least that tick. */
static void capture_prompt_reap(void) {
    char *command[] = {(char *)"sh", (char *)"-c", (char *)"exec 1>&- 2>&-; sleep 0.01; exit 5", NULL};
    long best = -1;
    int i;
    for (i = 0; i < 10; i++) {
        struct f_capture cap = {0};
        long started = capture_clock_ms(), elapsed;
        assert(!f_run(command, NULL, 0, 30, &cap));
        elapsed = capture_clock_ms() - started;
        assert(cap.status == 5 && !cap.timeout && !cap.out_bytes && !cap.err_bytes && cap.measurement_complete);
        f_capture_free(&cap);
        if (best < 0 || elapsed < best) best = elapsed;
    }
    if (best >= 40) fprintf(stderr, "capture: the fastest of 10 runs of a child that exits 10 ms after closing its streams took %ld ms\n", best);
    assert(best < 40);
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
