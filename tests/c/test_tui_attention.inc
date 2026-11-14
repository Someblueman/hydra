/* Inbox over the attention read: approvals, results and items Hydra cannot
 * classify are listed with the head snapshot's gates, stopped terminals and
 * recovery findings (fake-hydra's native-v2.tsv: two heads with pending gates,
 * two agents to look at and three repairs besides the folded dead session). */
static void test_attention(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    if (!result(open_session(&session, tui, hydra, fake_bin, 100, 30) == 0, "open attention terminal")) return;
    result(wait_for_raw(&session), "attention terminal enters raw mode");
    (void)wait_for_model(&session);
    write_input(session.master, "2", 1U);
    result(wait_for_markers(&session, "Decisions 3", "Ready for review 1", 3000),
           "2 opens Inbox, which lists the approval with the gates and the result to review");
    result(wait_for_marker(&session, "Approve S-1: T-1", 1000), "Inbox rows read as sentences");
    result(wait_for_marker(&session, "attempt 1   new", 1000), "a new item says so");
    result(wait_for_markers(&session, "Agents 2", "Repairs 4", 1000), "stopped terminals and repairs are listed after decisions");
    write_input(session.master, "AB", 2U);
    sleep_ms(150);
    write_input(session.master, "[", 1U);
    result(wait_for_markers(&session, "Route       workflow-request", "navigable", 1000),
           "letters A and B do not move the selection; [ opens the first item's Technical page");
    write_input(session.master, "]", 1U);
    result(wait_for_marker(&session, "Y Approve    N Reject    r Review    s Seen", 1000), "] returns to the Summary with the item's keys");
    write_input(session.master, "s", 1U);
    result(wait_for_marker(&session, "Marked seen", 1000), "marking seen gives visible feedback");
    result(wait_for_marker(&session, "Seen, but still needs your decision", 1000), "a seen approval still needs a decision");
    result(wait_for_marker(&session, "Decisions 3", 1000), "a seen approval stays in Decisions");
    result(still_running(&session), "marking seen is read-only and keeps TUI alive");
    write_input(session.master, "\t", 1U);
    sleep_ms(100);
    write_input(session.master, "\033", 1U);
    sleep_ms(100);
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "Heads  ", 1000), "Esc leaves the detail, then Inbox for Work");
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
/* ctrl+r reads everything again, attention included. */
static void attention_refresh(struct session *session) { drain_output(session); write_input(session->master, "\022", 1U); }
static void attention_expect_stale(struct session *session, const char *path, const char *contents, const char *label) {
    const char *good = "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tknown-good\t/project\tlocal\tGOOD\tR\tS\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t9999999999999999999999999999999999999999999999999999999999999999\tfresh\tworkflow-request\t1\nEND\t1\t0\t0\n";
    (void)write_attention_fixture(path, good); attention_refresh(session);
    if (!result(wait_for_markers(session, "Decisions 3", "Approve S: GOOD", 3000), "valid snapshot accepted before negative case")) return;
    if (!write_attention_fixture(path, contents)) { result(false, label); return; }
    attention_refresh(session);
    result(wait_for_markers(session, "reading it failed", "Approve S: GOOD", 3000), label);
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
/* The two gates of native-v2.tsv are always among the decisions. */
static void attention_capacity_cases(struct session *session, const char *path) {
    bool wrote;
    wrote = write_attention_many(path, 512U, true); if (wrote) { attention_refresh(session); result(wait_for_marker(session, "Decisions 514", 3000), "512 unique items are accepted"); } else result(false, "512 unique items fixture writes atomically");
    wrote = write_attention_many(path, 513U, true); if (wrote) { attention_refresh(session); result(wait_for_marker(session, "reading it failed", 3000), "513 unique items reject atomically"); } else result(false, "513 unique items fixture writes atomically");
    wrote = write_attention_many(path, 1024U, false); if (wrote) { attention_refresh(session); result(wait_for_marker(session, "Decisions 3", 3000), "1024 duplicate wire items are accepted"); } else result(false, "1024 duplicate wire fixture writes atomically");
    wrote = write_attention_many(path, 1025U, false); if (wrote) { attention_refresh(session); result(wait_for_markers(session, "reading it failed", "Approve S: T-0", 3000), "1025 wire items reject atomically"); } else result(false, "1025 wire fixture writes atomically");
}
/* Every client also refreshes on its own every two seconds from the same
 * fixture and seen store, and a refresh requested while a load is in flight
 * joins that load, which may predate the latest change. Refresh until the
 * expected screen appears, within one bounded deadline. */
static bool refresh_until(struct session *session, const char *marker, const char *second, long timeout_ms) {
    bool shown = false; long long end = monotonic_ms() + timeout_ms;
    while (!shown && monotonic_ms() < end) {
        attention_refresh(session);
        shown = wait_for_markers(session, marker, second, 1000);
    }
    return shown;
}
/* A seen result collapses into the Seen group and leaves Ready for review;
 * another client of the same user shows it after its next refresh. */
static void attention_seen_group(struct session *left, struct session *right, const char *done) {
    /* From the top: the approval, the two gates, then the result. */
    drain_output(left); write_input(left->master, "kkkkkkkkjjj", 11U); write_input(left->master, "s", 1U);
    result(wait_for_markers(left, "Seen 1", "marked seen; enter shows them", 1500) && !wait_for_marker(left, "Ready for review 1", 300),
           "a seen result moves to the collapsed Seen group");
    drain_output(left); write_input(left->master, "jjjjjjjjjjjjjjjj", 16U); write_input(left->master, "\r", 1U);
    result(wait_for_marker(left, "enter hides them", 1000), "Enter expands the Seen group");
    /* The left client persists the marker asynchronously. */
    unlink(done);
    result(refresh_until(right, "Seen 1", "Approve S-1: T-1", 8000),
           "another client of the same user shows the marker after refresh");
}
/* Seen markers persist per user: a new client starts with them. */
static void attention_restart(const char *tui, const char *hydra, const char *fake_bin) {
    struct session third;
    if (!result(open_session(&third, tui, hydra, fake_bin, 100, 30) == 0, "open attention client after restart")) return;
    (void)wait_for_raw(&third); (void)wait_for_model(&third); write_input(third.master, "2", 1U);
    /* The seen store is read asynchronously, and the clients that just quit
     * may still hold its lock: refresh within one bounded deadline. */
    result(refresh_until(&third, "Seen 1", "Decisions 3", 8000), "seen markers survive a TUI restart");
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
    result(wait_for_raw(&left) && wait_for_raw(&right), "both attention clients enter raw mode"); write_input(left.master, "2", 1U); write_input(right.master, "2", 1U);
    result(wait_for_markers(&left, "Decisions 3", "Ready for review 1", 3000) && wait_for_markers(&right, "Decisions 3", "Ready for review 1", 3000), "both clients list the approval and the result");
    write_input(left.master, "s", 1U); result(wait_for_markers(&left, "Seen, but still needs your decision", "Decisions 3", 1500), "a seen approval stays listed until decided"); result(wait_for_marker(&right, "step S-1   attempt 1   new", 1500), "second client is unchanged until it refreshes");
    drain_output(&left); unlink(done); write_reordered_fixture(path, reordered); attention_refresh(&left); result(wait_for_attention_order(&left), "pure reorder applies with unchanged sections"); write_input(left.master, "[", 1U); result(wait_for_marker(&left, "Task        T-1", 1000), "pure reorder retains selected LEFT identity"); write_input(left.master, "]", 1U);
    drain_output(&left); unlink(done); write_attention_fixture(path, mutated); attention_refresh(&left); result(wait_for_markers(&left, "Repairs 3", "step S-1   attempt 1   new", 3000), "semantic revision mutation is unseen in applied refresh");
    write_attention_fixture(path, "HYDRA_ATTENTION\t1\nITEM\tbroken\n"); drain_output(&left); unlink(done); attention_refresh(&left); result(wait_for_marker(&left, "reading it failed", 3000), "malformed refresh preserves last good snapshot");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tbad\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\n", "missing END rejects atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nEND\t0\t0\t0\ntrailing\n", "trailing bytes reject atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tbad\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\tZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t1\t0\t0\n", "bad digest rejects atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tbad\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nITEM\tworkflow\tapproval\tother\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t2\t0\t0\n", "conflicting duplicate rejects atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nEND\t0\t0\t1\n", "contradictory END flags reject atomically");
    attention_capacity_cases(&left, path);
    drain_output(&left); write_attention_fixture(path, "HYDRA_ATTENTION\t1\nEND\t0\t0\t0\n"); attention_refresh(&left); result(wait_for_markers(&left, "Decisions 2", "Repairs 3", 3000), "empty complete snapshot is accepted");
    write_attention_fixture(path, initial); file = fopen(fail, "w"); if (file) fclose(file); drain_output(&left); unlink(done); attention_refresh(&left); result(wait_for_marker(&left, "reading it failed", 3000), "failed reconnect is explicit unavailable"); unlink(fail);
    result(refresh_until(&right, "Decisions 3", "Ready for review 1", 8000), "second client recovers the restored snapshot");
    drain_output(&left); unlink(done); attention_refresh(&left); result(wait_for_markers(&left, "Decisions 3", "seen   run R-1   step S-1", 3000), "successful LEFT reconnect keeps the seen approval listed");
    attention_seen_group(&left, &right, done);
    write_input(left.master, "q", 1U); write_input(right.master, "q", 1U); result(wait_for_exit(&left) == 0 && terminal_restored(&left), "first attention client restores terminal"); result(wait_for_exit(&right) == 0 && terminal_restored(&right), "second attention client restores terminal"); close_session(&left); close_session(&right);
    attention_restart(tui, hydra, fake_bin);
cleanup: unlink(path); unlink(done); unlink(seen); unsetenv("HYDRA_ATTENTION_FIXTURE"); unsetenv("HYDRA_ATTENTION_DONE_FILE"); unsetenv("HYDRA_TEST_ATTENTION_FAIL_FILE"); unsetenv("HYDRA_TEST_SEEN_FILE");
}
/* A failed check is listed under Failed as a sentence naming the plan, the
 * step and the requirements it decides; marking it seen keeps it listed until
 * the run is resolved. Version 3 appends the presentation detail. */
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
    write_input(session.master, "2", 1U);
    result(wait_for_markers(&session, "Failed 1", "Check failed: plan-x", 3000), "a failed check is listed under Failed, named by its plan");
    result(wait_for_markers(&session, "step repo-check", "req-a, req-b", 1000), "the failure names its step and the requirements it decides");
    result(screen_row_order(&session, "Failed 1", "Ready for review 1"), "failures are listed before results to review");
    drain_output(&session);
    write_input(session.master, "jj", 2U);
    result(wait_for_marker(&session, "failed its check; review the log", 1000),
           "the failure can be selected and explains itself in plain language");
    write_input(session.master, "s", 1U);
    result(wait_for_markers(&session, "Marked seen", "Failed 1", 1500), "s on a failure keeps it listed under Failed");
    result(wait_for_marker(&session, "Seen, but still needs your decision", 1000),
           "the detail says a seen failure still needs a decision");
    result(wait_for_marker(&session, "r reviews the failed requirement and its log", 1000), "the detail names the next actions");
    attention_refresh(&session);
    result(wait_for_markers(&session, "Failed 1", "Check failed: plan-x", 3000), "a seen failure is still listed after a refresh");
    write_input(session.master, "[", 1U);
    result(wait_for_markers(&session, "Route       workflow-evidence", "navigable", 1000), "a failure routes to its evidence review");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "failure attention exits with terminal restored");
    close_session(&session);
cleanup:
    unlink(path); unlink(seen); unsetenv("HYDRA_ATTENTION_FIXTURE"); unsetenv("HYDRA_TEST_SEEN_FILE");
}

/* Help longer than the screen scrolls: j and the arrows reach its last
 * group at 80x24 (the agent-layer chords), k and Page Up come back to its
 * first (the keys of the view). */
static void test_help_scroll(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    int i;
    if (!result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0, "open help terminal")) return;
    result(wait_for_raw(&session), "help terminal enters raw mode");
    (void)wait_for_model(&session);
    write_input(session.master, "1?", 2U);
    result(wait_for_markers(&session, "KEYBOARD HELP", "Heads", 1000) && !wait_for_marker(&session, "send that key to the agent", 300),
           "help opens on the view's own keys; at 80x24 the rest is below");
    for (i = 0; i < 40; i++) write_input(session.master, i % 2 ? "j" : "\033[B", i % 2 ? 1U : 3U);
    result(wait_for_marker(&session, "send that key to the agent", 1000), "scrolling reaches the last help line at 80x24");
    write_input(session.master, "\033[5~kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk", 46U);
    result(wait_for_marker(&session, "Heads", 1000), "scrolling back reaches the first help group");
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "\xe2\x97\x86 hydra", 1000), "Esc closes the scrolled help");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "help scroll exits with terminal restored");
    close_session(&session);
}

/* Records the argv of the Inbox's decisions and restarts. fake-hydra writes
 * one argument per line to the named file. */
static bool inbox_argv(const char *path, const char *expected, long timeout_ms) {
    char text[4096]; long long end = monotonic_ms() + timeout_ms;
    while (monotonic_ms() < end) {
        FILE *file = fopen(path, "r");
        if (file) {
            size_t length = fread(text, 1U, sizeof(text) - 1U, file);
            text[length] = '\0'; fclose(file);
            if (!strcmp(text, expected)) return true;
        }
        sleep_ms(20);
    }
    return false;
}

/* Y and N act in Inbox: the confirmation names the exact run, step and
 * request; its default (Enter) and n send nothing; y sends exactly that
 * request for that run to the workflow control owner. */
static void test_inbox_decisions(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    char argv_path[256];
    snprintf(argv_path, sizeof(argv_path), "/tmp/hydra-inbox-control-%ld", (long)getpid());
    unlink(argv_path);
    setenv("HYDRA_TEST_CONTROL_ARGV", argv_path, 1);
    if (!result(open_session(&session, tui, hydra, fake_bin, 120, 30) == 0, "open Inbox decision terminal")) goto cleanup;
    (void)wait_for_raw(&session);
    write_input(session.master, "2", 1U);
    result(wait_for_markers(&session, "Approve S-1: T-1", "Request     request-1", 3000), "the 2 tab opens Inbox on the approval and its request");
    write_input(session.master, "Y", 1U);
    result(wait_for_markers(&session, "Approve request", "Approve S-1?", 1000) && wait_for_markers(&session, "Run R-1", "Request request-1", 1000),
           "Y asks to approve, naming the exact run, step and request");
    write_input(session.master, "\r", 1U);
    result(wait_for_marker(&session, "Nothing was decided", 1000) && !marker_file_exists(argv_path), "Enter on the question is Cancel: nothing is sent");
    write_input(session.master, "Y", 1U);
    (void)wait_for_marker(&session, "Approve request", 1000);
    write_input(session.master, "y", 1U);
    result(inbox_argv(argv_path, "workflow\n--workspace-control\nR-1\napprove\nrequest-1\n", 3000),
           "y approves exactly that run's request through the workflow control owner");
    unlink(argv_path);
    write_input(session.master, "N", 1U);
    result(wait_for_markers(&session, "Reject request", "fails at this step and stops", 1000), "N asks to reject, saying what it does");
    write_input(session.master, "n", 1U);
    result(wait_for_marker(&session, "Nothing was decided", 1000) && !marker_file_exists(argv_path), "n declines a rejection: nothing is sent");
    write_input(session.master, "N", 1U);
    (void)wait_for_marker(&session, "Reject request", 1000);
    write_input(session.master, "y", 1U);
    result(inbox_argv(argv_path, "workflow\n--workspace-control\nR-1\nreject\nrequest-1\n", 3000),
           "y rejects exactly that run's request");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "Inbox decision terminal restores");
    close_session(&session);
cleanup:
    unlink(argv_path); unsetenv("HYDRA_TEST_CONTROL_ARGV");
}

/* R on a stopped terminal in Inbox restarts it with hydra resume --terminal
 * after a confirmation; nothing runs when it is declined. */
static void test_inbox_restart(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    char argv_path[256];
    int i;
    snprintf(argv_path, sizeof(argv_path), "/tmp/hydra-inbox-resume-%ld", (long)getpid());
    unlink(argv_path);
    setenv("HYDRA_TEST_RESUME_ARGV", argv_path, 1);
    if (!result(open_session(&session, tui, hydra, fake_bin, 120, 30) == 0, "open Inbox restart terminal")) goto cleanup;
    (void)wait_for_raw(&session);
    write_input(session.master, "2", 1U);
    (void)wait_for_marker(&session, "Terminal stopped: feature-stale", 3000);
    for (i = 0; i < 8 && !wait_for_marker(&session, "R Restart terminal", 300); i++) { drain_output(&session); write_input(session.master, "j", 1U); }
    result(wait_for_marker(&session, "R Restart terminal", 1000), "a stopped terminal offers R to restart it");
    write_input(session.master, "R", 1U);
    result(wait_for_markers(&session, "Restart terminal", "Restart the terminal of feature-stale?", 1000), "R asks first, naming the head");
    write_input(session.master, "n", 1U);
    result(wait_for_marker(&session, "Terminal not restarted; nothing changed", 1000) && !marker_file_exists(argv_path), "declining restarts nothing");
    write_input(session.master, "R", 1U);
    (void)wait_for_marker(&session, "Restart the terminal of feature-stale?", 1000);
    write_input(session.master, "y", 1U);
    result(inbox_argv(argv_path, "resume\n--terminal\nfeature-stale\n", 3000), "y runs hydra resume --terminal for exactly that head");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "Inbox restart terminal restores");
    close_session(&session);
cleanup:
    unlink(argv_path); unsetenv("HYDRA_TEST_RESUME_ARGV");
}

/* A plan run that succeeded with its worker head still listed is "Ready to
 * land" in Inbox; L asks first, n lands nothing, y runs hydra land with
 * exactly that worker. */
static void test_inbox_land(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    char argv_path[256], heads[4096], runs[4096];
    const char *slash = strrchr(hydra, '/');
    int i;
    snprintf(argv_path, sizeof(argv_path), "/tmp/hydra-inbox-land-%ld", (long)getpid());
    snprintf(heads, sizeof(heads), "%.*s/native-runs.tsv", slash ? (int)(slash - hydra) : 1, slash ? hydra : ".");
    snprintf(runs, sizeof(runs), "%.*s/workflow-runs.tsv", slash ? (int)(slash - hydra) : 1, slash ? hydra : ".");
    unlink(argv_path);
    setenv("HYDRA_TEST_LAND_ARGV", argv_path, 1);
    setenv("HYDRA_TUI_FIXTURE", heads, 1);
    setenv("HYDRA_TEST_WORKFLOW_FIXTURE", runs, 1);
    if (!result(open_session(&session, tui, hydra, fake_bin, 120, 30) == 0, "open Inbox land terminal")) goto cleanup;
    (void)wait_for_raw(&session);
    (void)wait_for_model(&session);
    write_input(session.master, "2", 1U);
    result(wait_for_marker(&session, "Ready to land: kill-dry-run-worker", 4000), "a finished plan run's worker is ready to land");
    for (i = 0; i < 8 && !wait_for_marker(&session, "L Land", 300); i++) { drain_output(&session); write_input(session.master, "j", 1U); }
    result(wait_for_markers(&session, "L Land", "Worker      kill-dry-run-worker", 1000), "the item offers L to land it");
    write_input(session.master, "L", 1U);
    result(wait_for_markers(&session, "Land result", "Land kill-dry-run-worker?", 1000), "L asks first, naming the worker");
    write_input(session.master, "n", 1U);
    result(wait_for_marker(&session, "Not landed; nothing was changed", 1000) && !marker_file_exists(argv_path), "declining lands nothing");
    write_input(session.master, "L", 1U);
    (void)wait_for_marker(&session, "Land kill-dry-run-worker?", 1000);
    write_input(session.master, "y", 1U);
    result(inbox_argv(argv_path, "land\nkill-dry-run-worker\n", 3000), "y runs hydra land for exactly that worker");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "Inbox land terminal restores");
    close_session(&session);
cleanup:
    unlink(argv_path); unsetenv("HYDRA_TEST_LAND_ARGV"); unsetenv("HYDRA_TUI_FIXTURE"); unsetenv("HYDRA_TEST_WORKFLOW_FIXTURE");
}
