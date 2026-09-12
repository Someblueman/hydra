struct review_fixture {
    char directory[128], attention[160], document[160], fail[160], delay[160];
    char started[160], calls[160], pids[160], opener[160], opened[160];
    char original[8192];
};

static volatile sig_atomic_t review_owner_stopped;
static void review_owner_signal(int signum) { review_owner_stopped = signum; }

/* A cooperative producer owns a TERM-resistant child in a separate group,
 * matching Fleet's nested f_run lifecycle and 500 ms escalation grace. */
static pid_t review_owner_child(const char *scratch) {
    pid_t child;
    if (mkdir(scratch, 0700) || (child = fork()) < 0) return -1;
    if (!child) {
        (void)setpgid(0, 0); signal(SIGTERM, SIG_IGN); signal(SIGINT, SIG_IGN); signal(SIGHUP, SIG_IGN);
        for (;;) pause();
    }
    (void)setpgid(child, child); return child;
}

static void review_owner_reap(pid_t child, bool grace) {
    int status = 0;
    if (child <= 0) return;
    if (grace) { (void)kill(-child, SIGTERM); sleep_ms(500); }
    (void)kill(-child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) { }
}

static bool review_owner_pids(const char *path, pid_t child) {
    FILE *file = fopen(path, "w"); bool ok;
    if (!file) return false;
    ok = fprintf(file, "%ld\n", (long)getpid()) >= 0;
    if (child > 0) ok = fprintf(file, "%ld\n", (long)child) >= 0 && ok;
    return fclose(file) == 0 && ok;
}

static int review_cancel_owner(const char *pids, const char *scratch, const char *cleaned, const char *mode) {
    bool nested = !strcmp(mode, "nested"); pid_t child = -1;
    signal(SIGTERM, nested ? review_owner_signal : SIG_IGN);
    signal(SIGINT, nested ? review_owner_signal : SIG_IGN);
    signal(SIGHUP, nested ? review_owner_signal : SIG_IGN);
    if (nested && (child = review_owner_child(scratch)) < 0) return 2;
    if (!review_owner_pids(pids, child)) { review_owner_reap(child, false); return 2; }
    while (!review_owner_stopped) sleep_ms(10);
    review_owner_reap(child, true);
    return rmdir(scratch) || !write_marker_file(cleaned, "nested child reaped; scratch removed\n") ? 2 : 0;
}

static bool review_read(const char *path, char *text, size_t capacity) {
    FILE *file = fopen(path, "rb"); size_t length;
    if (!file) return false;
    length = fread(text, 1U, capacity - 1U, file); text[length] = '\0';
    bool ok = !ferror(file) && fgetc(file) == EOF;
    fclose(file); return ok;
}

static bool review_header(const struct review_fixture *fixture, char *header, size_t capacity) {
    const char *end = strchr(fixture->original, '\n');
    size_t length;
    if (!end) return false;
    length = (size_t)(end - fixture->original) + 1U;
    if (length >= capacity) return false;
    memcpy(header, fixture->original, length);
    header[length] = '\0';
    return true;
}

static bool review_attention(struct review_fixture *fixture, char revision, const char *attempt) {
    char text[2048], digest[65];
    memset(digest, revision, 64U); digest[64] = '\0';
    snprintf(text, sizeof(text), "HYDRA_ATTENTION\t1\n"
        "ITEM\tworkflow records\tresult\treview-ready\t/project\t-\t-\tR-1\tS-1\t%s\thead-1\tinstance-1\t-\t1111111111111111111111111111111111111111\t%s\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-evidence\t1\n"
        "ITEM\tworkflow records\tresult\tother-candidate\t/project\t-\t-\tR-2\tS-2\tattempt-1\thead-2\tinstance-2\t-\t2222222222222222222222222222222222222222\t2222222222222222222222222222222222222222222222222222222222222222\t2222222222222222222222222222222222222222222222222222222222222222\tfresh\tworkflow-evidence\t1\nEND\t2\t0\t0\n", attempt, digest);
    return write_attention_fixture(fixture->attention, text);
}

static bool review_setup(struct review_fixture *fixture, const char *hydra) {
    char source[4096], *slash; FILE *file;
    char *paths[] = {fixture->attention, fixture->document, fixture->fail, fixture->delay,
        fixture->started, fixture->calls, fixture->pids, fixture->opener, fixture->opened};
    const char *names[] = {"attention", "review", "fail", "delay", "started", "calls", "pids",
#ifdef __APPLE__
        "open",
#else
        "xdg-open",
#endif
        "opened"};
    const char *env[] = {"HYDRA_ATTENTION_FIXTURE", "HYDRA_REVIEW_FIXTURE", "HYDRA_REVIEW_FAIL", "HYDRA_REVIEW_DELAY",
        "HYDRA_REVIEW_STARTED", "HYDRA_REVIEW_CALLS", "HYDRA_REVIEW_PIDS"};
    memset(fixture, 0, sizeof(*fixture));
    snprintf(fixture->directory, sizeof(fixture->directory), "/tmp/hydra-review-pty-XXXXXX");
    if (!mkdtemp(fixture->directory)) return false;
    for (size_t i = 0U; i < 9U; i++) snprintf(paths[i], 160U, "%s/%s", fixture->directory, names[i]);
    snprintf(source, sizeof(source), "%s", hydra); slash = strrchr(source, '/');
    if (!slash) return false;
    snprintf(slash + 1U, sizeof(source) - (size_t)(slash + 1U - source), "review-v1.tsv");
    if (!review_read(source, fixture->original, sizeof(fixture->original)) ||
        !write_attention_fixture(fixture->document, fixture->original) || !review_attention(fixture, '1', "attempt-1")) return false;
    for (size_t i = 0U; i < 7U; i++) setenv(env[i], paths[i], 1);
    file = fopen(fixture->opener, "w");
    if (!file) return false;
    fprintf(file, "#!/bin/sh\nprintf '%%s\\n' \"$@\" > '%s'\n", fixture->opened);
    if (fclose(file) || chmod(fixture->opener, 0700)) return false;
    return true;
}

static void review_cleanup(struct review_fixture *fixture) {
    const char *env[] = {"HYDRA_ATTENTION_FIXTURE", "HYDRA_REVIEW_FIXTURE", "HYDRA_REVIEW_FAIL", "HYDRA_REVIEW_DELAY",
        "HYDRA_REVIEW_STARTED", "HYDRA_REVIEW_CALLS", "HYDRA_REVIEW_PIDS"};
    const char *paths[] = {fixture->attention, fixture->document, fixture->fail, fixture->delay,
        fixture->started, fixture->calls, fixture->pids, fixture->opener, fixture->opened};
    for (size_t i = 0U; i < 7U; i++) unsetenv(env[i]);
    for (size_t i = 0U; i < 9U; i++) unlink(paths[i]);
    rmdir(fixture->directory);
}

static bool review_start(struct session *session, const char *tui, const char *hydra,
                         struct review_fixture *fixture, unsigned short width, unsigned short height) {
    if (!result(open_session(session, tui, hydra, fixture->directory, width, height) == 0, "open review PTY")) return false;
    result(wait_for_raw(session), "review terminal enters raw mode");
    write_input(session->master, "I", 1U);
    return result(wait_for_marker(session, "ATTENTION  2 current", 3000), "review candidates load");
}

static void review_stop(struct session *session) {
    write_input(session->master, "q", 1U);
    result(wait_for_exit(session) == 0 && terminal_restored(session), "review quit restores terminal");
    close_session(session);
}

static bool review_reload(struct session *session, struct review_fixture *fixture, const char *document, const char *marker) {
    if (!write_attention_fixture(fixture->document, document)) return false;
    drain_output(session->master); write_input(session->master, "r", 1U);
    return wait_for_marker(session, marker, 3000);
}

static void review_negative_cases(struct session *session, struct review_fixture *fixture) {
    const char *good = fixture->original; char bad[sizeof(fixture->original) + sizeof("trailing\n")]; char *end;
    snprintf(bad, sizeof(bad), "%s", good); end = strstr(bad, "END\t26"); if (end) *end = '\0';
    result(review_reload(session, fixture, bad, "Review unavailable"), "truncated review never displays evidence");
    int written = snprintf(bad, sizeof(bad), "%strailing\n", good);
    if (written < 0 || (size_t)written >= sizeof(bad)) { result(false, "trailing review fixture fits buffer"); return; }
    result(review_reload(session, fixture, bad, "Review unavailable"), "trailing review records rejected");
    snprintf(bad, sizeof(bad), "%s", good); end = strstr(bad, "END\t26"); if (end) memcpy(end + 4U, "27", 2U);
    result(review_reload(session, fixture, bad, "Review unavailable"), "wrong review counts rejected");
    snprintf(bad, sizeof(bad), "%s", good); end = strstr(bad, "\tR-1\t"); if (end) end[3] = '9';
    result(review_reload(session, fixture, bad, "Review unavailable"), "review for different run rejected");
    snprintf(bad, sizeof(bad), "%s", good); end = strstr(bad, "readiness: ready"); if (end) *end = 27;
    result(review_reload(session, fixture, bad, "Review unavailable"), "terminal control bytes rejected");
    snprintf(bad, sizeof(bad), "%s", good); end = strstr(bad, "https://"); if (end) memcpy(end, "file:///", 8U);
    result(review_reload(session, fixture, bad, "Review unavailable"), "unsupported reference scheme rejected");
    write_marker_file(fixture->fail, "fail");
    result(review_reload(session, fixture, good, "Review unavailable"), "failed public review is unavailable"); unlink(fixture->fail);
    result(review_reload(session, fixture, good, "readiness: ready"), "valid review reconnect succeeds");
}

static void review_references_test(struct session *session, struct review_fixture *fixture) {
    char opened[4096], calls[32768];
    write_input(session->master, "f\r", 2U);
    result(wait_for_marker(session, "EXPLICIT-LOCAL-PREVIEW", 1000), "selected local reference opens bounded data preview");
    drain_output(session->master); write_input(session->master, "\033", 1U);
    (void)wait_for_marker(session, "j/k selects; Enter follows reference", 1000); write_input(session->master, "j\r", 2U);
    result(wait_for_marker(session, "Reference inaccessible", 1000), "missing reference has explicit inaccessible state");
    drain_output(session->master); write_input(session->master, "\033", 1U);
    (void)wait_for_marker(session, "j/k selects; Enter follows reference", 1000); write_input(session->master, "j\r", 2U);
    result(wait_for_marker(session, "Opened in browser; content not verified", 2000), "explicit http reference dispatches read-only opener");
    result(review_read(fixture->opened, opened, sizeof(opened)) && !strcmp(opened, "https://example.invalid/review/123\n"), "opener receives only exact supplied URL as one argument");
    result(review_read(fixture->calls, calls, sizeof(calls)) && strstr(calls, "workflow\nreview-data\nresult\n/project\n-\n-\nR-1\nS-1\nattempt-1\nhead-1\ninstance-1\n-\n") != NULL &&
        !strstr(calls, "\napprove\n") && !strstr(calls, "\ncollect\n") && !strstr(calls, "\nintegrate\n"), "review/reference navigation uses exact public arguments without mutation commands");
}

static void review_supplied_reference_test(struct session *session, struct review_fixture *fixture) {
    char calls[32768];
    const char *locator = "/tmp/explicit \"quoted\" \\ $(literal)";
    write_input(session->master, "l", 1U);
    result(wait_for_marker(session, "log reference:", 1000), "explicit reference input opens as text form");
    write_input(session->master, locator, strlen(locator)); write_input(session->master, "\r", 1U);
    result(wait_for_marker(session, "Reference 1/3", 3000), "explicit reference reload returns selectable references");
    result(review_read(fixture->calls, calls, sizeof(calls)) &&
        strstr(calls, "[{\"kind\":\"log\",\"locator\":\"/tmp/explicit \\\"quoted\\\" \\\\ $(literal)\"}]\n"), "reference quotes/backslashes are encoded in one literal JSON argument");
    write_input(session->master, "p", 1U);
    (void)wait_for_marker(session, "pr reference:", 1000);
    write_input(session->master, "javascript:alert(1)\r", 20U);
    result(wait_for_marker(session, "Unsupported reference:", 1000), "user reference input refuses unsupported protocols");
}

static void review_bound_cases(struct session *session, struct review_fixture *fixture) {
    FILE *file; char text[8192]; size_t i;
    if (!review_header(fixture, text, sizeof(text))) { result(false, "bounded review header fits buffer"); return; }
    file = fopen(fixture->document, "wb");
    if (!file) { result(false, "binary review fixture opens"); return; }
    fputs(text, file); fputs("TEXT\tready", file); fputc(0, file); fputs("\nEND\t1\t0\t0\n", file); fclose(file);
    drain_output(session->master); write_input(session->master, "r", 1U);
    result(wait_for_marker(session, "Review unavailable", 3000), "embedded NUL cannot hide malformed evidence");
    file = fopen(fixture->document, "wb");
    if (!file) { result(false, "oversized review fixture opens"); return; }
    fputs(text, file); fputs("TEXT\t", file);
    for (i = 0U; i < 8192U; i++) fputc('x', file);
    fputs("\nEND\t1\t0\t0\n", file); fclose(file);
    drain_output(session->master); write_input(session->master, "r", 1U);
    result(wait_for_marker(session, "Review unavailable", 3000), "overlong review line refused before display");
    file = fopen(fixture->document, "wb");
    if (!file) { result(false, "document cap fixture opens"); return; }
    fputs(text, file); memset(text, 'x', 1024U); text[1024] = '\0';
    for (i = 0U; i < 1024U; i++) fprintf(file, "TEXT\t%s\n", text);
    fputs("END\t1024\t0\t0\n", file); fclose(file);
    drain_output(session->master); write_input(session->master, "r", 1U);
    result(wait_for_marker(session, "Review unavailable", 3000), "whole review byte cap enforced independently of line cap");
}

static bool review_exact_cap(struct review_fixture *fixture) {
    char header[1024], payload[1025]; FILE *file;
    const char *finish = "END\t1024\t0\t0\n";
    size_t bytes, count = 1024U, i;
    if (!review_header(fixture, header, sizeof(header))) return false;
    bytes = 1024U * 1024U - strlen(header) - strlen(finish) - 6U * count;
    file = fopen(fixture->document, "wb"); if (!file) return false;
    fputs(header, file);
    for (i = 0U; i < count; i++) {
        size_t length = bytes / count + (i < bytes % count ? 1U : 0U);
        memset(payload, 'x', length); payload[length] = '\0';
        if (!i) memcpy(payload, "CAP-FIRST", 9U);
        if (i + 1U == count) memcpy(payload + length - 8U, "CAP-LAST", 8U);
        fprintf(file, "TEXT\t%s\n", payload);
    }
    fputs(finish, file); return fclose(file) == 0;
}

static void review_revoked_cases(struct session *session, struct review_fixture *fixture) {
    const char *states[] = {"missing", "tampered", "expired"};
    char text[4096];
    for (size_t i = 0U; i < 3U; i++) {
        if (!review_header(fixture, text, sizeof(text))) { result(false, "revoked fixture header fits buffer"); return; }
        size_t length = strlen(text);
        int written = snprintf(text + length, sizeof(text) - length,
            "TEXT\treadiness: revoked\nTEXT\taccepted: false\nTEXT\tchecks: state: failed\nTEXT\tartifacts: answer: %s\nEND\t4\t0\t0\n", states[i]);
        if (written < 0 || (size_t)written >= sizeof(text) - length) { result(false, "revoked fixture fits buffer"); return; }
        result(review_reload(session, fixture, text, "readiness: revoked") &&
            wait_for_markers(session, "checks: state: failed", states[i], 1000), "revoked evidence shows failed checks and exact unavailable artifact state");
    }
}

static void test_review_navigation(const char *tui, const char *hydra) {
    struct review_fixture fixture; struct session session;
    if (!result(review_setup(&fixture, hydra), "prepare review fixtures")) return;
    if (!review_start(&session, tui, hydra, &fixture, 80, 24)) { close_session(&session); goto cleanup; }
    write_input(session.master, "r", 1U);
    result(wait_for_markers(&session, "readiness: ready", "accepted: false", 3000), "readiness is separate from acceptance");
    result(wait_for_markers(&session, "Long identifier: BEGIN-", "-END", 1000), "long identifier wraps through its exact suffix at 80x24");
    write_input(session.master, "i", 1U);
    result(wait_for_markers(&session, "Source: workflow records", "Project: /project", 1000), "selected identity view exposes exact source and project");
    write_input(session.master, "i", 1U);
    write_input(session.master, "G", 1U);
    result(wait_for_marker(&session, "FINAL-REVIEW-EVIDENCE", 1000), "long review scroll reaches final evidence at 80x24");
    write_input(session.master, "g", 1U);
    review_negative_cases(&session, &fixture);
    review_bound_cases(&session, &fixture);
    if (result(review_exact_cap(&fixture), "construct exact 1MiB valid document")) {
        drain_output(session.master); write_input(session.master, "r", 1U);
        result(wait_for_marker(&session, "CAP-FIRST", 3000), "exact 1MiB review is accepted");
        write_input(session.master, "G", 1U);
        result(wait_for_marker(&session, "CAP-LAST", 1000), "exact byte-cap review remains scrollable");
    }
    review_revoked_cases(&session, &fixture);
    result(review_reload(&session, &fixture, fixture.original, "readiness: ready"), "valid review resumes after byte-bound failures");
    review_references_test(&session, &fixture);
    review_supplied_reference_test(&session, &fixture);
    review_stop(&session);
cleanup: review_cleanup(&fixture);
}

static void test_review_stale(const char *tui, const char *hydra) {
    struct review_fixture fixture; struct session session;
    if (!review_setup(&fixture, hydra)) { result(false, "prepare slow review fixture"); return; }
    write_marker_file(fixture.delay, "4\n");
    if (!review_start(&session, tui, hydra, &fixture, 80, 24)) { close_session(&session); goto cleanup; }
    write_input(session.master, "r", 1U);
    result(wait_for_marker(&session, "Loading exact review", 1000) && wait_for_file(fixture.pids, 1000), "slow evidence runs as tracked asynchronous capture");
    write_input(session.master, "]", 1U);
    result(wait_for_marker(&session, "Selection or revision changed", 1000), "changing candidate invalidates pending review");
    result(pids_gone(fixture.pids), "candidate change removes review process and descendant");
    unlink(fixture.delay);
    result(review_reload(&session, &fixture, fixture.original, "Review unavailable"), "previous candidate response cannot satisfy new selection");
    write_input(session.master, "[", 1U);
    result(review_reload(&session, &fixture, fixture.original, "readiness: ready"), "return to original candidate loads exact evidence");
    review_attention(&fixture, '2', "attempt-2"); drain_output(session.master); write_input(session.master, "I", 1U);
    result(wait_for_marker(&session, "Selection or revision changed", 3000), "attempt/revision refresh invalidates displayed review");
    result(review_reload(&session, &fixture, fixture.original, "Review unavailable"), "old attempt/revision response rejected");
    review_stop(&session);
cleanup: review_cleanup(&fixture);
}

static void test_review_narrow(const char *tui, const char *hydra) {
    struct review_fixture fixture; struct session session;
    if (!review_setup(&fixture, hydra)) { result(false, "prepare narrow review fixture"); return; }
    if (!review_start(&session, tui, hydra, &fixture, 40, 10)) { close_session(&session); goto cleanup; }
    write_input(session.master, "r", 1U);
    result(wait_for_marker(&session, "readiness: ready", 3000), "review works at supported minimum terminal size");
    write_input(session.master, "\033[B", 3U);
    result(wait_for_marker(&session, "accepted: false", 1000), "arrow scrolling retains review at narrow size");
    write_input(session.master, "G", 1U);
    result(wait_for_marker(&session, "FINAL-REVIEW-EVIDENCE", 1000), "narrow review can scroll to final evidence");
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "ATTENTION  2 current", 1000), "opening/closing review does not acknowledge candidate");
    write_marker_file(fixture.delay, "4\n"); write_input(session.master, "r", 1U);
    result(wait_for_marker(&session, "Loading exact review", 1000), "narrow slow review stays responsive");
    write_input(session.master, "\033", 1U);
    review_stop(&session);
cleanup: review_cleanup(&fixture);
}

static bool review_separate_group(const char *path) {
    FILE *file = fopen(path, "r"); long owner = 0, child = 0;
    if (!file) return false;
    bool read = fscanf(file, "%ld\n%ld", &owner, &child) == 2;
    fclose(file);
    return read && owner > 0 && child > 0 && getpgid((pid_t)owner) == (pid_t)owner &&
        getpgid((pid_t)child) == (pid_t)child && owner != child;
}

static void cancel_active_review(struct session *session, struct review_fixture *fixture,
                                 const char *scratch, const char *cleaned, unsigned i) {
    const char *keys[] = {"]", "\033q", "q"};
    if (i < 2U) result(review_separate_group(fixture->pids), "nested capture child has its own process group");
    drain_output(session->master); long long started = monotonic_ms();
    write_input(session->master, keys[i], strlen(keys[i]));
    if (!i) result(wait_for_marker(session, "Selection or revision changed", 1500), "candidate change completes nested cleanup");
    else result(wait_for_exit(session) == 0 && terminal_restored(session), "cancelled capture quit restores terminal");
    long long elapsed = monotonic_ms() - started;
    result(elapsed < 1500 && pids_gone(fixture->pids), "capture cancellation is bounded and reaps owned processes");
    if (i < 2U) result(marker_file_exists(cleaned) && !marker_file_exists(scratch), "producer removes scratch after reaping its separate group");
    else result(elapsed >= 650, "TERM-resistant capture reaches bounded KILL escalation");
    if (!i) review_stop(session); else close_session(session);
}

static void review_cancellation_case(const char *tui, const char *hydra, const char *executable, unsigned i) {
    struct review_fixture fixture; struct session session; char scratch[160], cleaned[160];
    if (!review_setup(&fixture, hydra)) { result(false, "prepare cancellation fixture"); return; }
    snprintf(scratch, sizeof(scratch), "%s/scratch", fixture.directory);
    snprintf(cleaned, sizeof(cleaned), "%s/cleaned", fixture.directory);
    setenv("HYDRA_REVIEW_CANCEL_OWNER", executable, 1); setenv("HYDRA_REVIEW_CANCEL_SCRATCH", scratch, 1);
    setenv("HYDRA_REVIEW_CANCEL_CLEANED", cleaned, 1); setenv("HYDRA_REVIEW_CANCEL_MODE", i < 2U ? "nested" : "ignore", 1);
    if (!review_start(&session, tui, hydra, &fixture, 80, 24)) { close_session(&session); goto cleanup; }
    write_input(session.master, "r", 1U);
    result(wait_for_marker(&session, "Loading exact review", 1000) && wait_for_file(fixture.pids, 1000), "cancellable producer is live");
    cancel_active_review(&session, &fixture, scratch, cleaned, i);
cleanup:
    unsetenv("HYDRA_REVIEW_CANCEL_OWNER"); unsetenv("HYDRA_REVIEW_CANCEL_SCRATCH");
    unsetenv("HYDRA_REVIEW_CANCEL_CLEANED"); unsetenv("HYDRA_REVIEW_CANCEL_MODE");
    unlink(cleaned); rmdir(scratch); review_cleanup(&fixture);
}

static void test_review_cancellation(const char *tui, const char *hydra, const char *self) {
    char executable[PATH_MAX];
    if (!result(realpath(self, executable) != NULL, "resolve nested capture fixture")) return;
    for (unsigned i = 0U; i < 3U; i++) review_cancellation_case(tui, hydra, executable, i);
}

static int review_tests_only(const char *tui, const char *hydra, const char *self) {
    test_review_navigation(tui, hydra);
    test_review_stale(tui, hydra);
    test_review_narrow(tui, hydra);
    test_review_cancellation(tui, hydra, self);
    printf("Review tests: %d passed, %d failed\n", tests - failures, failures);
    return failures ? 1 : 0;
}
