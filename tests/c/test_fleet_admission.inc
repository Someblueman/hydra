/* The wait for an admission slot claims again after a pause that starts short
 * and doubles up to a cap, not every 200 ms; it never runs past the queue
 * deadline and a stop request ends it within one slice. The shell CLI is
 * replaced by a script that counts its claims and answers "queued" until a
 * chosen claim, so no admission state is needed. */
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
static void admission_wait(const char *home) {
    char dir[F_PATH], script[F_PATH]; json_object *state; long claims; double elapsed, late; int status; const char *saved = f_hydra;
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

    /* A slot that frees up after two claims is found by the third: the first
     * retries are short (20 ms and 40 ms), not 200 ms each. */
    state = json_object_new_object(); f_string_add(state, "admission_id", "x");
    status = admission_run(dir, 3, 30, 0, state, &claims, &elapsed, &late);
    assert(!status && claims == 3 && !strcmp(f_string(f_field(state, "admission"), "state"), "reserved") && !f_string(state, "failure"));
    assert(elapsed >= 0.05 && elapsed < 0.35);
    json_object_put(state);

    /* A slot that never frees up: the pauses double (20, 40, 80, 160, 320,
     * 640, 1000 ms), so a wait of up to 3 s takes 5 to 7 claims where 200 ms
     * pauses took 8 or more, and it ends at the deadline, not after it. */
    state = json_object_new_object(); f_string_add(state, "admission_id", "x");
    status = admission_run(dir, 1000000, 3, 0, state, &claims, &elapsed, &late);
    if (claims < 5 || claims > 7 || late < 0 || late > 0.25) fprintf(stderr, "admission: claims %ld, %.3f s past the deadline\n", claims, late);
    assert(status == -1 && !strcmp(f_string(state, "failure"), "queue_deadline"));
    assert(claims >= 5 && claims <= 7 && late >= 0 && late < 0.25);
    json_object_put(state);

    /* A stop request ends a long pause within a slice, not after it. */
    state = json_object_new_object(); f_string_add(state, "admission_id", "x");
    status = admission_run(dir, 1000000, 30, 1.5, state, &claims, &elapsed, &late);
    if (elapsed >= 2.0) fprintf(stderr, "admission: stopped after %.3f s\n", elapsed);
    assert(status == -1 && !f_string(state, "failure") && elapsed >= 1.5 && elapsed < 2.0);
    json_object_put(state);

    f_hydra = saved;
    assert(!f_remove_tree(dir));
}
