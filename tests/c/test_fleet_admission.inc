/* The wait for an admission slot claims again after a pause that starts short
 * and doubles up to a cap, not every 200 ms; it never runs past the queue
 * deadline and a stop request ends it within one slice. The shell CLI is
 * replaced by a script that counts its claims and answers "queued" until a
 * chosen claim, so no admission state is needed.
 *
 * The schedule itself is checked exactly, on the pure function that makes it.
 * The waits are checked by what the pauses guarantee (a floor on the time, a
 * ceiling on the number of claims, the deadline and the stop request) and
 * never by how fast a claim runs: the ceilings that depend on it are given
 * the cost of a claim measured on this machine, so a slow runner (a claim of
 * 300 ms) passes as a fast one does. */
static double admission_now(clockid_t clock) {
    struct timespec now;
    assert(!clock_gettime(clock, &now));
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}
static double admission_started;
static double admission_stop_after;
static bool admission_stop(void *context) {
    (void)context;
    return admission_stop_after > 0 && admission_now(CLOCK_MONOTONIC) - admission_started >= admission_stop_after;
}
static long admission_claims(const char *dir) {
    char path[F_PATH], *text; long count;
    assert(!f_path(path, sizeof(path), dir, "claims"));
    text = f_read(path, 32);
    if (!text) return 0;
    count = atol(text); free(text); return count;
}
/* Waits with the fake CLI: returns task_admission_wait's status, leaves the
 * claims it took in *claims, the time it took in *elapsed and the time it
 * ended past the queue deadline in *late. */
static int admission_run(const char *dir, long reserve_at, int64_t seconds, double stop_after, json_object *state, long *claims, double *elapsed, double *late) {
    struct task_control control; char path[F_PATH]; double deadline; int status;
    json_object *record = json_object_new_object();
    assert(!f_path(path, sizeof(path), dir, "claims")); (void)unlink(path);
    assert(!f_path(path, sizeof(path), dir, "reserve-at"));
    { char number[32]; (void)snprintf(number, sizeof(number), "%ld\n", reserve_at); assert(!f_write(path, number, strlen(number), true)); }
    memset(&control, 0, sizeof(control)); control.process.stop = admission_stop;
    f_string_add(record, "state", "queued"); json_object_object_add(record, "requested_at", json_object_new_int64(1));
    json_object_object_add(state, "admission", record);
    deadline = (double)(time(NULL) + seconds);
    json_object_object_add(state, "admission_deadline", json_object_new_int64((int64_t)deadline));
    admission_stop_after = stop_after; admission_started = admission_now(CLOCK_MONOTONIC);
    status = task_admission_wait(dir, state, &control);
    *elapsed = admission_now(CLOCK_MONOTONIC) - admission_started;
    *late = admission_now(CLOCK_REALTIME) - deadline;
    *claims = admission_claims(dir);
    return status;
}
/* 20, 40, 80, 160, 320, 640 ms, then 1000 ms at the cap, from the first
 * pause on; a bad start or a huge value stays within the cap. */
static void admission_schedule(void) {
    static const long expected[] = {20, 40, 80, 160, 320, 640, 1000, 1000, 1000};
    long pause = 0; size_t i;
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        pause = task_admission_pause_ms(pause);
        assert(pause == expected[i]);
    }
    assert(task_admission_pause_ms(-5) == 20 && task_admission_pause_ms(501) == 1000 && task_admission_pause_ms(1L << 40) == 1000);
}
/* What one claim of the fake CLI costs here: twice the slowest of three. */
static double admission_claim_cost(const char *script) {
    char *argv[] = {(char *)script, NULL}; double worst = 0; int i;
    for (i = 0; i < 3; i++) {
        struct f_capture cap = {0}; double started = admission_now(CLOCK_MONOTONIC), took;
        assert(!f_run(argv, NULL, 0, 5, &cap));
        f_capture_free(&cap);
        took = admission_now(CLOCK_MONOTONIC) - started;
        if (took > worst) worst = took;
    }
    return 2 * worst;
}
static void admission_never_frees(const char *dir, double cost) {
    json_object *state = json_object_new_object(); long claims; double elapsed, late; int status;
    /* A slot that never frees up: the claims come at 20, 60, 140, 300, 620,
     * 1260 and 2260 ms at the earliest, so a wait of up to 3 s takes at most
     * 7 (200 ms pauses took 14); it ends at the deadline, not before it, and
     * not more than one claim after it. */
    f_string_add(state, "admission_id", "x");
    status = admission_run(dir, 1000000, 3, 0, state, &claims, &elapsed, &late);
    if (claims < 1 || claims > 7 || late < 0 || late > cost + 0.35) fprintf(stderr, "admission: claims %ld, %.3f s past the deadline (a claim costs %.3f s)\n", claims, late, cost);
    assert(status == -1 && !strcmp(f_string(state, "failure"), "queue_deadline"));
    assert(claims >= 1 && claims <= 7 && late >= 0 && late <= cost + 0.35);
    json_object_put(state);
}
static void admission_stopped(const char *dir, double cost) {
    json_object *state = json_object_new_object(); long claims; double elapsed, late; int status;
    /* A stop request ends a long pause within a slice (100 ms) and the claim
     * that may be running, not after the pause. */
    f_string_add(state, "admission_id", "x");
    status = admission_run(dir, 1000000, 30, 1.5, state, &claims, &elapsed, &late);
    if (elapsed >= 1.5 + cost + 0.35) fprintf(stderr, "admission: stopped after %.3f s (a claim costs %.3f s)\n", elapsed, cost);
    assert(status == -1 && !f_string(state, "failure") && elapsed >= 1.5 && elapsed < 1.5 + cost + 0.35);
    json_object_put(state);
}
static void admission_wait(const char *home) {
    char dir[F_PATH], script[F_PATH]; json_object *state; long claims; double elapsed, late, cost; int status; const char *saved = f_hydra;
    static const char fake[] =
        "#!/bin/sh\n"
        "d=$(dirname \"$0\")\n"
        "n=$(cat \"$d/claims\" 2>/dev/null || echo 0); n=$((n + 1)); echo \"$n\" > \"$d/claims\"\n"
        "state=queued; [ \"$n\" -lt \"$(cat \"$d/reserve-at\")\" ] || state=reserved\n"
        "printf '{\"schema_version\":1,\"ok\":true,\"command\":\"admission\",\"data\":{\"id\":\"x\",\"project\":\"p\",\"state\":\"%s\","
        "\"requested_at\":1,\"deadline\":9999999999,\"sequence\":1,\"required_labels\":\"-\",\"reason\":\"host_limit\",\"age_seconds\":0}}\\n' \"$state\"\n";
    assert(!f_path(dir, sizeof(dir), home, "admission-wait") && !mkdir(dir, 0700) && !f_path(script, sizeof(script), dir, "hydra"));
    assert(!f_write(script, fake, sizeof(fake) - 1, false) && !chmod(script, 0700));
    f_hydra = script;

    admission_schedule();

    /* A slot that frees up after two claims is found by the third, after two
     * pauses of at least 20 and 40 ms. How long the claims took is not the
     * test's business, so there is no ceiling. */
    state = json_object_new_object(); f_string_add(state, "admission_id", "x");
    status = admission_run(dir, 3, 30, 0, state, &claims, &elapsed, &late);
    assert(!status && claims == 3 && !strcmp(f_string(f_field(state, "admission"), "state"), "reserved") && !f_string(state, "failure"));
    assert(elapsed >= 0.05);
    json_object_put(state);
    cost = admission_claim_cost(script);

    admission_never_frees(dir, cost);
    admission_stopped(dir, cost);

    f_hydra = saved;
    assert(!f_remove_tree(dir));
}
