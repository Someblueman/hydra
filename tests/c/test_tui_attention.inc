static void test_attention(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    if (!result(open_session(&session, tui, hydra, fake_bin, 100, 30) == 0, "open attention terminal")) return;
    result(wait_for_raw(&session), "attention terminal enters raw mode");
    (void)wait_for_model(&session);
    write_input(session.master, "I", 1U);
    result(wait_for_marker(&session, "ATTENTION  2 current  0 stale  1 unknown", 3000),
           "attention view shows bounded freshness counts");
    result(wait_for_marker(&session, "NEW", 1000), "new attention item is visible");
    write_input(session.master, "\r", 1U);
    result(wait_for_marker(&session, "Route: workflow-request  navigable=1", 1000),
           "attention detail shows exact route kind");
    write_input(session.master, "s", 1U);
    result(still_running(&session), "acknowledgement is read-only and keeps TUI alive");
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000), "attention Escape returns to heads");
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
    (void)write_attention_fixture(path, good); drain_output(session->master); write_input(session->master, "I", 1U);
    if (!result(wait_for_markers(session, "ATTENTION  1 current", "known-good", 3000), "valid snapshot accepted before negative case")) return;
    if (!write_attention_fixture(path, contents)) { result(false, label); return; }
    drain_output(session->master); write_input(session->master, "I", 1U);
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
    wrote = write_attention_many(path, 512U, true); if (wrote) { drain_output(session->master); write_input(session->master, "I", 1U); result(wait_for_marker(session, "ATTENTION  512 current", 3000), "512 unique items are accepted"); } else result(false, "512 unique items fixture writes atomically");
    wrote = write_attention_many(path, 513U, true); if (wrote) { drain_output(session->master); write_input(session->master, "I", 1U); result(wait_for_marker(session, "STALE: last good", 3000), "513 unique items reject atomically"); } else result(false, "513 unique items fixture writes atomically");
    wrote = write_attention_many(path, 1024U, false); if (wrote) { drain_output(session->master); write_input(session->master, "I", 1U); result(wait_for_marker(session, "ATTENTION  1 current", 3000), "1024 duplicate wire items are accepted"); } else result(false, "1024 duplicate wire fixture writes atomically");
    wrote = write_attention_many(path, 1025U, false); if (wrote) { drain_output(session->master); write_input(session->master, "I", 1U); result(wait_for_markers(session, "STALE: last good", "cap", 3000), "1025 wire items reject atomically"); } else result(false, "1025 wire fixture writes atomically");
}
static void test_attention_clients(const char *tui, const char *hydra, const char *fake_bin) {
    struct session left, right;
    char path[256], fail[256], done[256]; FILE *file;
    const char *initial = "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tdecision-needed\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead-1\tinstance-1\trequest-1\tspec-1\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nITEM\tworkflow\tresult\tverification-ready\t/project\tlocal\tT-2\tR-2\tS-2\t1\thead-2\tinstance-2\t-\tspec-2\t2222222222222222222222222222222222222222222222222222222222222222\t2222222222222222222222222222222222222222222222222222222222222222\tfresh\tworkflow-evidence\t1\nITEM\tfleet\tunknown\tprovider\t/project\tremote\tT-3\tR-3\tS-3\t1\thead-3\tinstance-3\t-\t-\t3333333333333333333333333333333333333333333333333333333333333333\t3333333333333333333333333333333333333333333333333333333333333333\tunknown\tunavailable\t0\nEND\t3\t0\t0\n";
    const char *reordered = "HYDRA_ATTENTION\t1\nITEM\tworkflow\tresult\tverification-ready\t/project\tlocal\tT-2\tR-2\tS-2\t1\thead-2\tinstance-2\t-\tspec-2\t2222222222222222222222222222222222222222222222222222222222222222\t2222222222222222222222222222222222222222222222222222222222222222\tfresh\tworkflow-evidence\t1\nITEM\tworkflow\tapproval\tdecision-needed\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead-1\tinstance-1\trequest-1\tspec-1\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nITEM\tworkflow\tapproval\tdecision-needed\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead-1\tinstance-1\trequest-1\tspec-1\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t3\t0\t0\n";
    const char *mutated = "HYDRA_ATTENTION\t1\nITEM\tworkflow\tresult\tverification-ready\t/project\tlocal\tT-2\tR-2\tS-2\t1\thead-2\tinstance-2\t-\tspec-2\t2222222222222222222222222222222222222222222222222222222222222222\t2222222222222222222222222222222222222222222222222222222222222222\tfresh\tworkflow-evidence\t1\nITEM\tworkflow\tapproval\trevision-mutated\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead-1\tinstance-1\trequest-1\tspec-1\t4444444444444444444444444444444444444444444444444444444444444444\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t2\t0\t0\n";
    if (attention_paths(path, sizeof(path), fail, sizeof(fail))) return;
    if (!write_attention_fixture(path, initial)) return;
    if (snprintf(done, sizeof(done), "%s.done", path) >= (int)sizeof(done)) return;
    setenv("HYDRA_ATTENTION_FIXTURE", path, 1);
    setenv("HYDRA_ATTENTION_DONE_FILE", done, 1);
    (void)setenv("HYDRA_TEST_ATTENTION_FAIL_FILE", fail, 1);
    if (!result(open_session(&left, tui, hydra, fake_bin, 100, 30) == 0, "open first attention client")) goto cleanup;
    if (!result(open_session(&right, tui, hydra, fake_bin, 100, 30) == 0, "open second attention client")) { close_session(&left); goto cleanup; }
    result(wait_for_raw(&left) && wait_for_raw(&right), "both attention clients enter raw mode"); write_input(left.master, "I", 1U); write_input(right.master, "I", 1U);
    result(wait_for_marker(&left, "ATTENTION  2 current", 3000) && wait_for_marker(&right, "ATTENTION  2 current", 3000), "both clients see two unseen eligible items");
    write_input(left.master, "s", 1U); result(wait_for_marker(&left, "ATTENTION  1 current", 1500), "ack lowers only first client count"); result(wait_for_marker(&right, "ATTENTION  2 current", 1500), "second client count is isolated");
    drain_output(left.master); unlink(done); write_reordered_fixture(path, reordered); write_input(left.master, "I", 1U); result(wait_for_marker(&left, "ATTENTION  1 current  0 stale  1 unknown", 3000), "pure reorder applies with unchanged count"); write_input(left.master, "\r", 1U); result(wait_for_marker(&left, "Task: T-1", 1000), "pure reorder retains selected LEFT identity"); write_input(left.master, "k", 1U); result(wait_for_marker(&left, "Task: T-2", 1000), "pure reorder applies new row order"); write_input(left.master, "\r", 1U);
    drain_output(left.master); unlink(done); write_attention_fixture(path, mutated); write_input(left.master, "I", 1U); result(wait_for_markers(&left, "ATTENTION  2 current", "revision-mutated", 3000), "semantic revision mutation is unseen in applied refresh");
    write_attention_fixture(path, "HYDRA_ATTENTION\t1\nITEM\tbroken\n"); drain_output(left.master); unlink(done); write_input(left.master, "I", 1U); result(wait_for_marker(&left, "STALE: last good", 3000), "malformed refresh preserves last good snapshot");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tbad\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\n", "missing END rejects atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nEND\t0\t0\t0\ntrailing\n", "trailing bytes reject atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tbad\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\tZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t1\t0\t0\n", "bad digest rejects atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nITEM\tworkflow\tapproval\tbad\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nITEM\tworkflow\tapproval\tother\t/project\tlocal\tT-1\tR-1\tS-1\t1\thead\tinstance\trequest\tbinding\t1111111111111111111111111111111111111111111111111111111111111111\t1111111111111111111111111111111111111111111111111111111111111111\tfresh\tworkflow-request\t1\nEND\t2\t0\t0\n", "conflicting duplicate rejects atomically");
    attention_expect_stale(&left, path, "HYDRA_ATTENTION\t1\nEND\t0\t0\t1\n", "contradictory END flags reject atomically");
    attention_capacity_cases(&left, path);
    drain_output(left.master); write_attention_fixture(path, "HYDRA_ATTENTION\t1\nEND\t0\t0\t0\n"); write_input(left.master, "I", 1U); result(wait_for_marker(&left, "ATTENTION  0 current  0 stale  0 unknown", 3000), "empty complete snapshot is accepted");
    write_attention_fixture(path, initial); file = fopen(fail, "w"); if (file) fclose(file); drain_output(left.master); unlink(done); write_input(left.master, "I", 1U); result(wait_for_marker(&left, "STALE: last good", 3000), "failed reconnect is explicit unavailable"); unlink(fail);
    drain_output(right.master); result(wait_for_marker(&right, "ATTENTION  2 current", 1000), "untouched RIGHT client remains at count two");
    drain_output(left.master); unlink(done); write_input(left.master, "I", 1U); result(wait_for_markers(&left, "ATTENTION  1 current", "verification-ready", 3000), "successful LEFT reconnect retains ACKed LEFT while RIGHT remains unseen and visible"); drain_output(right.master); result(wait_for_marker(&right, "ATTENTION  2 current", 1000), "RIGHT remains at count two after LEFT reconnect");
    write_input(left.master, "q", 1U); write_input(right.master, "q", 1U); result(wait_for_exit(&left) == 0 && terminal_restored(&left), "first attention client restores terminal"); result(wait_for_exit(&right) == 0 && terminal_restored(&right), "second attention client restores terminal"); close_session(&left); close_session(&right);
cleanup: unlink(path); unlink(done); unsetenv("HYDRA_ATTENTION_FIXTURE"); unsetenv("HYDRA_ATTENTION_DONE_FILE"); unsetenv("HYDRA_TEST_ATTENTION_FAIL_FILE");
}
