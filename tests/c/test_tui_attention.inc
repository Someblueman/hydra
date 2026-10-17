static void test_attention(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    if (!result(open_session(&session, tui, hydra, fake_bin, 100, 30) == 0, "open attention terminal")) return;
    result(wait_for_raw(&session), "attention terminal enters raw mode");
    (void)wait_for_model(&session);
    write_input(session.master, "I", 1U);
    result(wait_for_marker(&session, "ATTENTION  2 need you", 3000),
           "attention view counts the items that need the user");
    result(wait_for_marker(&session, "Approval needed", 1000), "attention rows read as sentences");
    result(wait_for_marker(&session, "NEW", 1000), "new attention item is visible");
    write_input(session.master, "\r", 1U);
    result(wait_for_marker(&session, "Route: workflow-request  navigable=1", 1000),
           "attention detail shows exact route kind");
    write_input(session.master, "s", 1U);
    result(wait_for_marker(&session, "Marked seen", 1000), "marking seen gives visible feedback in the detail view");
    result(wait_for_marker(&session, "Seen, but still needs your decision", 1000), "a seen approval still needs a decision");
    result(still_running(&session), "marking seen is read-only and keeps TUI alive");
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "ATTENTION  2 need you", 1000), "Escape leaves the detail for the list; the approval still counts");
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "HYDRA / ", 1000), "attention Escape returns to heads");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "attention exits with terminal restored");
    close_session(&session);
}

static bool attention_paths(char *path, size_t path_size, char *fail, size_t fail_size) {
    return snprintf(path, path_size, "/tmp/hydra-attention-pty-%ld.tsv", (long)getpid()) >= (int)path_size ||
        snprintf(fail, fail_size, "/tmp/hydra-attention-fail-%ld", (long)getpid()) >= (int)fail_size;
}
static bool write_attention_fixture(const char *path, const char *contents) {
    char temporary[320]; FILE *file; int written;
    written = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(temporary)) return false;
    file = fopen(temporary, "w");
    if (!file) return false;
    if (fputs(contents, file) == EOF || fclose(file) != 0) { unlink(temporary); return false; }
    if (rename(temporary, path) != 0) { unlink(temporary); return false; }
    return true;
}
static bool write_reordered_fixture(const char *path, const char *contents) {
    const char *end = strstr(contents, "END\t3\t0\t0\n");
    const char *extra = "ITEM\tfleet\tunknown\tprovider\t/project\tremote\tT-3\tR-3\tS-3\t1\thead-3\tinstance-3\t-\t-\t3333333333333333333333333333333333333333333333333333333333333333\t3333333333333333333333333333333333333333333333333333333333333333\tunknown\tunavailable\t0\n";
    char merged[8192]; size_t prefix;
    if (!end) return false;
    prefix = (size_t)(end - contents);
    if (prefix + strlen(extra) + strlen("END\t4\t0\t0\n") >= sizeof(merged)) return false;
    memcpy(merged, contents, prefix);
    memcpy(merged + prefix, extra, strlen(extra));
    memcpy(merged + prefix + strlen(extra), "END\t4\t0\t0\n", strlen("END\t4\t0\t0\n") + 1U);
    return write_attention_fixture(path, merged);
}
static void attention_expect_stale(struct session *session, const char *path, const char *contents, const char *label) {
    const char *good = "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tknown-good\t/project\tlocal\tGOOD\tR\tS\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t9999999999999999999999999999999999999999999999999999999999999999\tfresh\tworkflow-request\t1\nEND\t1\t0\t0\n";
    (void)write_attention_fixture(path, good); drain_output(session); write_input(session->master, "I", 1U);
    if (!result(wait_for_markers(session, "ATTENTION  1 need you", "known-good", 3000), "valid snapshot accepted before negative case")) return;
    if (!write_attention_fixture(path, contents)) { result(false, label); return; }
    drain_output(session); write_input(session->master, "I", 1U);
    result(wait_for_markers(session, "STALE: last good", "known-good", 3000), label);
}
static bool write_attention_many_item(FILE *file, size_t i, bool unique) {
    char line[512]; int written;
    written = snprintf(line, sizeof(line), "ITEM\tworkflow\tapproval\tcap\t/project\tlocal\tT-%zu\tR\tS\t1\thead\tinstance\trequest\tbinding\t%064zx\t%064zx\tfresh\tworkflow-request\t1\n", unique ? i : 0U, unique ? i + 1U : 1U, unique ? i + 1001U : 1U);
    return written >= 0 && (size_t)written < sizeof(line) && fputs(line, file) != EOF;
}
static bool write_attention_many(const char *path, size_t count, bool unique) {
    char temporary[320], line[512]; FILE *file; size_t i; int written;
    written = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(temporary) || !(file = fopen(temporary, "w"))) return false;
    if (fputs("HYDRA_ATTENTION\t1\n", file) == EOF) { fclose(file); unlink(temporary); return false; }
    for (i = 0U; i < count; i++) {
        if (!write_attention_many_item(file, i, unique)) { fclose(file); unlink(temporary); return false; }
    }
    written = snprintf(line, sizeof(line), "END\t%zu\t0\t0\n", count);
    if (written < 0 || (size_t)written >= sizeof(line) || fputs(line, file) == EOF || fclose(file) != 0 || rename(temporary, path) != 0) { unlink(temporary); return false; }
    return true;
}
static void attention_capacity_cases(struct session *session, const char *path) {
    bool wrote;
    wrote = write_attention_many(path, 512U, true); if (wrote) { drain_output(session); write_input(session->master, "I", 1U); result(wait_for_marker(session, "ATTENTION  512 need you", 3000), "512 unique items are accepted"); } else result(false, "512 unique items fixture writes atomically");
    wrote = write_attention_many(path, 513U, true); if (wrote) { drain_output(session); write_input(session->master, "I", 1U); result(wait_for_marker(session, "STALE: last good", 3000), "513 unique items reject atomically"); } else result(false, "513 unique items fixture writes atomically");
    wrote = write_attention_many(path, 1024U, false); if (wrote) { drain_output(session); write_input(session->master, "I", 1U); result(wait_for_marker(session, "ATTENTION  1 need you", 3000), "1024 duplicate wire items are accepted"); } else result(false, "1024 duplicate wire fixture writes atomically");
    wrote = write_attention_many(path, 1025U, false); if (wrote) { drain_output(session); write_input(session->master, "I", 1U); result(wait_for_markers(session, "STALE: last good", "cap", 3000), "1025 wire items reject atomically"); } else result(false, "1025 wire fixture writes atomically");
}
/* Every client also refreshes on its own every two seconds from the same
 * fixture and seen store, and a refresh requested while a load is in flight
 * joins that load, which may predate the latest change. Refresh until the
 * expected screen appears, within one bounded deadline. */
static bool refresh_until(struct session *session, const char *marker, const char *second, long timeout_ms) {
    bool shown = false; long long end = monotonic_ms() + timeout_ms;
    while (!shown && monotonic_ms() < end) {
        drain_output(session); write_input(session->master, "I", 1U);
        shown = wait_for_markers(session, marker, second, 1000);
    }
    return shown;
}
/* A seen result collapses into the Seen group and leaves the count; another
 * client of the same user shows it after its next refresh. */
static void attention_seen_group(struct session *left, struct session *right, const char *done) {
    drain_output(left); write_input(left->master, "j", 1U); write_input(left->master, "s", 1U);
    result(wait_for_markers(left, "ATTENTION  1 need you", "Seen (1)", 1500), "a seen result moves to the collapsed Seen group");
    write_input(left->master, "j", 1U); write_input(left->master, "\r", 1U);
    result(wait_for_marker(left, "Enter collapses", 1000), "Enter expands the Seen group");
    /* The left client persists the marker asynchronously. */
    unlink(done);
    result(refresh_until(right, "ATTENTION  1 need you", "Seen (1)", 8000),
           "another client of the same user shows the marker after refresh");
}
/* Seen markers persist per user: a new client starts with them. */
static void attention_restart(const char *tui, const char *hydra, const char *fake_bin) {
    struct session third;
    if (!result(open_session(&third, tui, hydra, fake_bin, 100, 30) == 0, "open attention client after restart")) return;
    (void)wait_for_raw(&third); write_input(third.master, "I", 1U);
    result(wait_for_markers(&third, "ATTENTION  1 need you", "Seen (1)", 3000), "seen markers survive a TUI restart");
    write_input(third.master, "q", 1U); result(wait_for_exit(&third) == 0 && terminal_restored(&third), "restarted attention client restores terminal"); close_session(&third);
}
static void test_attention_clients(const char *tui, const char *hydra, const char *fake_bin) {
    struct session left, right;
    char path[256], fail[256], done[256], seen[256]; FILE *file;
    const char *initial = "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tdecision-needed\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead-1\tinstance-1\trequest-1\tspec-1\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nITEM\tworkflow\tresult\tverification-ready\t/project\tlocal\tT-2\tR-2\tS-2\t1\thead-2\tinstance-2\t-\tspec-2\t2222222222222222222222222222222222222222222222222222222222222222\t2222222222222222222222222222222222222222222222222222222222222222\tfresh\tworkflow-evidence\t1\nITEM\tfleet\tunknown\tprovider\t/project\tremote\tT-3\tR-3\tS-3\t1\thead-3\tinstance-3\t-\t-\t3333333333333333333333333333333333333333333333333333333333333333\t3333333333333333333333333333333333333333333333333333333333333333\tunknown\tunavailable\t0\nEND\t3\t0\t0\n";
    const char *reordered = "HYDRA_ATTENTION\t1\nITEM\tworkflow\tresult\tverification-ready\t/project\tlocal\tT-2\tR-2\tS-2\t1\thead-2\tinstance-2\t-\tspec-2\t2222222222222222222222222222222222222222222222222222222222222222\t2222222222222222222222222222222222222222222222222222222222222222\tfresh\tworkflow-evidence\t1\nITEM\tworkflow\tapproval\tdecision-needed\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead-1\tinstance-1\trequest-1\tspec-1\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nITEM\tworkflow\tapproval\tdecision-needed\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead-1\tinstance-1\trequest-1\tspec-1\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t3\t0\t0\n";
    const char *mutated = "HYDRA_ATTENTION\t1\nITEM\tworkflow\tresult\tverification-ready\t/project\tlocal\tT-2\tR-2\tS-2\t1\thead-2\tinstance-2\t-\tspec-2\t2222222222222222222222222222222222222222222222222222222222222222\t2222222222222222222222222222222222222222222222222222222222222222\tfresh\tworkflow-evidence\t1\nITEM\tworkflow\tapproval\trevision-mutated\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead-1\tinstance-1\trequest-1\tspec-1\t4444444444444444444444444444444444444444444444444444444444444444\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t2\t0\t0\n";
    if (attention_paths(path, sizeof(path), fail, sizeof(fail))) return;
    if (!write_attention_fixture(path, initial)) return;
    if (snprintf(done, sizeof(done), "%s.done", path) >= (int)sizeof(done)) return;
    if (snprintf(seen, sizeof(seen), "%s.seen", path) >= (int)sizeof(seen)) return;
    unlink(seen);
    setenv("HYDRA_TEST_SEEN_FILE", seen, 1);
    setenv("HYDRA_ATTENTION_FIXTURE", path, 1);
    setenv("HYDRA_ATTENTION_DONE_FILE", done, 1);
    (void)setenv("HYDRA_TEST_ATTENTION_FAIL_FILE", fail, 1);
    if (!result(open_session(&left, tui, hydra, fake_bin, 100, 30) == 0, "open first attention client")) goto cleanup;
    if (!result(open_session(&right, tui, hydra, fake_bin, 100, 30) == 0, "open second attention client")) { close_session(&left); goto cleanup; }
    result(wait_for_raw(&left) && wait_for_raw(&right), "both attention clients enter raw mode"); write_input(left.master, "I", 1U); write_input(right.master, "I", 1U);
    result(wait_for_marker(&left, "ATTENTION  2 need you", 3000) && wait_for_marker(&right, "ATTENTION  2 need you", 3000), "both clients see two items that need the user");
    write_input(left.master, "s", 1U); result(wait_for_markers(&left, "seen, still needs a decision", "ATTENTION  2 need you", 1500), "a seen approval stays listed and counted until decided"); result(wait_for_marker(&right, "ATTENTION  2 need you", 1500), "second client is unchanged until it refreshes");
    drain_output(&left); unlink(done); write_reordered_fixture(path, reordered); write_input(left.master, "I", 1U); result(wait_for_attention_order(&left), "pure reorder applies with unchanged count"); write_input(left.master, "\r", 1U); result(wait_for_marker(&left, "Task: T-1", 1000), "pure reorder retains selected LEFT identity"); write_input(left.master, "k", 1U); result(wait_for_marker(&left, "Task: T-2", 1000), "pure reorder applies new row order"); write_input(left.master, "\r", 1U);
    drain_output(&left); unlink(done); write_attention_fixture(path, mutated); write_input(left.master, "I", 1U); result(wait_for_markers(&left, "ATTENTION  2 need you", "revision-mutated", 3000) && wait_for_marker(&left, "NEW", 1000), "semantic revision mutation is unseen in applied refresh");
    write_attention_fixture(path, "HYDRA_ATTENTION\t1\nITEM\tbroken\n"); drain_output(&left); unlink(done); write_input(left.master, "I", 1U); result(wait_for_marker(&left, "STALE: last good", 3000), "malformed refresh preserves last good snapshot");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tbad\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\n", "missing END rejects atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nEND\t0\t0\t0\ntrailing\n", "trailing bytes reject atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tbad\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\tZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t1\t0\t0\n", "bad digest rejects atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tbad\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nITEM\tworkflow\tapproval\tother\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t2\t0\t0\n", "conflicting duplicate rejects atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nEND\t0\t0\t1\n", "contradictory END flags reject atomically");
    attention_capacity_cases(&left, path);
    drain_output(&left); write_attention_fixture(path, "HYDRA_ATTENTION\t1\nEND\t0\t0\t0\n"); write_input(left.master, "I", 1U); result(wait_for_markers(&left, "ATTENTION  0 need you", "Nothing needs your attention", 3000), "empty complete snapshot is accepted");
    write_attention_fixture(path, initial); file = fopen(fail, "w"); if (file) fclose(file); drain_output(&left); unlink(done); write_input(left.master, "I", 1U); result(wait_for_marker(&left, "STALE: last good", 3000), "failed reconnect is explicit unavailable"); unlink(fail);
    result(refresh_until(&right, "ATTENTION  2 need you", NULL, 8000), "second client recovers the restored snapshot at count two");
    drain_output(&left); unlink(done); write_input(left.master, "I", 1U); result(wait_for_markers(&left, "ATTENTION  2 need you", "verification-ready", 3000), "successful LEFT reconnect keeps the seen approval listed");
    attention_seen_group(&left, &right, done);
    write_input(left.master, "q", 1U); write_input(right.master, "q", 1U); result(wait_for_exit(&left) == 0 && terminal_restored(&left), "first attention client restores terminal"); result(wait_for_exit(&right) == 0 && terminal_restored(&right), "second attention client restores terminal"); close_session(&left); close_session(&right);
    attention_restart(tui, hydra, fake_bin);
cleanup: unlink(path); unlink(done); unlink(seen); unsetenv("HYDRA_ATTENTION_FIXTURE"); unsetenv("HYDRA_ATTENTION_DONE_FILE"); unsetenv("HYDRA_TEST_ATTENTION_FAIL_FILE"); unsetenv("HYDRA_TEST_SEEN_FILE");
}
/* A failed check leads the list as a sentence naming the plan, the step and the
 * requirements it decides; marking it seen keeps it listed and counted until the
 * run is resolved. Version 3 appends the presentation detail. */
static void test_attention_failure(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    char path[256], fail[256], seen[256];
    const char *fixture = "HYDRA_ATTENTION\t3\n"
        "ITEM\tworkflow records\tresult\tresult_ready\tproject_1\t-\t-\trun_1\timplement\tattempt-1\thead_1\tinstance_1\t-\t-\t5555555555555555555555555555555555555555555555555555555555555555\t6666666666666666666666666666666666666666666666666666666666666666\tfresh\tworkflow-evidence\t1\tplan-x\t-\n"
        "ITEM\tworkflow records\tfailure\tcheck_failed\tproject_1\t-\t-\trun_1\trepo-check\tattempt-1\thead_1\tinstance_1\t-\t-\t7777777777777777777777777777777777777777777777777777777777777777\t8888888888888888888888888888888888888888888888888888888888888888\tfresh\tworkflow-evidence\t1\tplan-x\treq-a, req-b\n"
        "END\t2\t0\t0\n";
    if (attention_paths(path, sizeof(path), fail, sizeof(fail))) return;
    if (snprintf(seen, sizeof(seen), "%s.failure-seen", path) >= (int)sizeof(seen)) return;
    if (!result(write_attention_fixture(path, fixture), "failure attention fixture writes")) return;
    unlink(seen);
    setenv("HYDRA_TEST_SEEN_FILE", seen, 1);
    setenv("HYDRA_ATTENTION_FIXTURE", path, 1);
    if (!result(open_session(&session, tui, hydra, fake_bin, 120, 30) == 0, "open failure attention terminal")) goto cleanup;
    result(wait_for_raw(&session), "failure attention terminal enters raw mode");
    write_input(session.master, "I", 1U);
    result(wait_for_markers(&session, "ATTENTION  2 need you", "1 failed check or step needs a decision", 3000),
           "a failed check counts as needing the user and is counted in the header");
    result(wait_for_markers(&session, "> Check failed", "repo-check", 1000), "the failed check leads the list and names its step");
    result(wait_for_markers(&session, "req-a, req-b; review the log", "send the plan back or retry", 1000),
           "the failure names its requirements and what the user can do");
    result(wait_for_marker(&session, "failed its check. Press r to see the failed requirement", 1000),
           "the failure explains itself in plain language");
    write_input(session.master, "s", 1U);
    result(wait_for_markers(&session, "Marked seen", "ATTENTION  2 need you", 1500), "a seen failure stays listed and counted");
    write_input(session.master, "\r", 1U);
    result(wait_for_markers(&session, "until the run is resolved", "Next: r reviews the failed requirement", 1000),
           "the detail says a seen failure still needs a decision and names the next actions");
    result(wait_for_marker(&session, "Route: workflow-evidence  navigable=1", 1000), "a failure routes to its evidence review");
    write_input(session.master, "\033", 1U);
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "failure attention exits with terminal restored");
    close_session(&session);
cleanup:
    unlink(path); unlink(seen); unsetenv("HYDRA_ATTENTION_FIXTURE"); unsetenv("HYDRA_TEST_SEEN_FILE");
}
