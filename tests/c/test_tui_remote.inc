/* One workspace across local and remote execution (U11): a local and a
 * remote observation side by side, the location filter, acting on a remote
 * item through the public CLI with exact argv, a stale and a disconnected
 * remote observation, and 80x24 / 140x40 fits. The fake hydra answers
 * `tui --remote-data` with a fixture and can lose or change it on request. */
static void remote_fixture_path(char *out, size_t size, const char *hydra, const char *name) {
    const char *slash = strrchr(hydra, '/');
    snprintf(out, size, "%.*s/%s", slash ? (int)(slash - hydra) : 1, slash ? hydra : ".", name);
}

static void remote_env_clear(void) {
    (void)unsetenv("HYDRA_TEST_REMOTE_FIXTURE"); (void)unsetenv("HYDRA_TEST_REMOTE_FAIL_FILE");
    (void)unsetenv("HYDRA_TEST_REMOTE_SWITCH_FILE"); (void)unsetenv("HYDRA_TEST_ATTACH_ARGV");
    (void)unsetenv("HYDRA_TEST_TASK_ARGV"); (void)unsetenv("HYDRA_TEST_CANCEL_ARGV");
}

/* Debug aid: the persistent screen's rows, for a failing expectation. */
static void remote_dump(struct session *session) {
    const struct tv_canvas *canvas;
    struct measure_screen *screen = session->screen;
    int y, x;
    if (!getenv("HYDRA_TEST_REMOTE_DUMP") || !screen) return;
    session_drain(session);
    canvas = screen->terminal.alternate_active ? &screen->terminal.alternate.canvas : &screen->terminal.primary.canvas;
    for (y = 0; y < canvas->height; y++) {
        char line[MEASURE_COLUMNS + 1];
        for (x = 0; x < canvas->width; x++) {
            uint32_t glyph = canvas->cells[(size_t)y * (size_t)canvas->stride + (size_t)x].glyph;
            line[x] = glyph >= 32 && glyph <= 126 ? (char)glyph : glyph ? '?' : ' ';
        }
        line[canvas->width] = '\0';
        fprintf(stderr, "%02d|%s\n", y, line);
    }
}

static bool read_small_file(const char *path, char *out, size_t size) {
    FILE *file = fopen(path, "r");
    size_t length;
    out[0] = '\0';
    if (!file) return false;
    length = fread(out, 1U, size - 1U, file);
    out[length] = '\0';
    fclose(file);
    return true;
}

/* Local heads and a remote head and task in one list; L cycles the location. */
static void test_remote_side_by_side(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    char fixture[4096];
    remote_fixture_path(fixture, sizeof(fixture), hydra, "remote-v3.tsv");
    (void)setenv("HYDRA_TEST_REMOTE_FIXTURE", fixture, 1);
    if (!result(open_session(&session, tui, hydra, fake_bin, 140, 40) == 0, "open unified workspace terminal")) { remote_env_clear(); return; }
    (void)wait_for_raw(&session);
    (void)wait_for_model(&session);
    result(wait_for_marker(&session, "builder/feature-build", 6000), "a remote head is listed beside local heads");
    result(wait_for_marker(&session, "task_build", 2000), "a remote task is listed beside local heads");
    result(wait_for_marker(&session, "WHERE", 1000), "the list names where each row runs at 140 columns");
    result(wait_for_marker(&session, "Remote live", 3000), "the footer reports the remote observation beside the local one");
    write_input(session.master, "L", 1U);
    result(wait_for_marker(&session, "Showing local work", 1000), "L narrows to local work");
    write_input(session.master, "L", 1U);
    result(wait_for_marker(&session, "Showing remote work", 1000), "L narrows to remote work");
    write_input(session.master, "L", 1U);
    result(wait_for_marker(&session, "Showing host builder", 1000), "L narrows to one host");
    result(wait_for_marker(&session, "Heads: host builder", 1000), "the panel title names the host filter");
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "Showing all work", 1000), "Esc shows all work again");
    {
        struct winsize size;
        memset(&size, 0, sizeof(size)); size.ws_col = 80; size.ws_row = 24;
        result(ioctl(session.master, TIOCSWINSZ, &size) == 0, "resize the unified list to 80x24");
    }
    write_input(session.master, "t", 1U);
    result(wait_for_screen_text(&session, "builder/feature-build", 80, 24, 3000), "the unified list fits 80x24");
    write_input(session.master, "t", 1U);
    if (!result(wait_for_screen_text(&session, "? help  q quit", 80, 24, 3000), "hints stay visible at 80x24")) remote_dump(&session);
    write_input(session.master, "I", 1U);
    result(wait_for_marker(&session, "need you", 6000), "attention reads the one local-and-remote stream");
    write_input(session.master, "\033", 1U);
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "unified workspace exits and restores the terminal");
    close_session(&session);
    remote_env_clear();
}

/* The dispatch notice is replaced by the control's completion notice as soon
 * as the detached child is reaped; a fast child can finish before a frame
 * shows the first one, so either notice proves this dispatch. */
static bool wait_for_dispatch(struct session *session, const char *dispatched, const char *finished, long timeout_ms) {
    long long deadline = monotonic_ms() + timeout_ms;
    do {
        if (wait_for_marker(session, dispatched, 50) || wait_for_marker(session, finished, 50)) return true;
    } while (monotonic_ms() < deadline);
    return false;
}

/* Acting on remote work: attach through fleet attach, interrupt through
 * fleet cancel and a task decision through fleet task, each with the exact
 * host, project, instance and task identity. */
static void test_remote_actions(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    char fixture[4096], attach_path[128], cancel_path[128], task_path[128], captured[1024];
    remote_fixture_path(fixture, sizeof(fixture), hydra, "remote-v3.tsv");
    snprintf(attach_path, sizeof(attach_path), "/tmp/hydra-u11-attach-%ld", (long)getpid());
    snprintf(cancel_path, sizeof(cancel_path), "/tmp/hydra-u11-cancel-%ld", (long)getpid());
    snprintf(task_path, sizeof(task_path), "/tmp/hydra-u11-task-%ld", (long)getpid());
    (void)unlink(attach_path); (void)unlink(cancel_path); (void)unlink(task_path);
    (void)setenv("HYDRA_TEST_REMOTE_FIXTURE", fixture, 1);
    (void)setenv("HYDRA_TEST_ATTACH_ARGV", attach_path, 1);
    (void)setenv("HYDRA_TEST_CANCEL_ARGV", cancel_path, 1);
    (void)setenv("HYDRA_TEST_TASK_ARGV", task_path, 1);
    if (!result(open_session(&session, tui, hydra, fake_bin, 100, 30) == 0, "open remote action terminal")) { remote_env_clear(); return; }
    (void)wait_for_raw(&session);
    (void)wait_for_model(&session);
    result(wait_for_marker(&session, "task_build", 6000), "remote rows are present before acting");
    write_input(session.master, "jjj", 3U);
    result(wait_for_marker(&session, "On builder", 2000), "the remote head's summary names its host");
    write_input(session.master, "x", 1U);
    result(wait_for_marker(&session, "Interrupt feature-build on builder?", 2000), "x on a remote head asks to interrupt, naming host and branch");
    write_input(session.master, "yes\n", 4U);
    result(wait_for_marker(&session, "FAKE REMOTE CANCEL", 4000), "a confirmed interrupt runs fleet cancel");
    write_input(session.master, "\n", 1U);
    (void)wait_for_marker(&session, "HYDRA / ", 2000);
    result(read_small_file(cancel_path, captured, sizeof(captured)) &&
           strstr(captured, "fleet\ncancel\nbuilder\n--project\n/work/project\n--instance\ninstance_aaaaaaaaaaaaaaaaaaaa\n--\nfeature-build\n") != NULL,
           "interrupt carries the exact host, project, instance and branch");
    write_input(session.master, "jj", 2U);
    result(wait_for_marker(&session, "owner recorded", 2000), "the task row can be selected after the heads");
    write_input(session.master, "Y", 1U);
    result(wait_for_marker(&session, "Type approve to dispatch", 2000), "Y on a remote task asks for a typed confirmation");
    write_input(session.master, "approve\n", 8U);
    result(wait_for_dispatch(&session, "approve dispatched for task_build", "dispatch finished; outcome unconfirmed / refresh task evidence: task:approve task_build", 4000),
           "a confirmed decision is dispatched");
    sleep_ms(500);
    result(read_small_file(task_path, captured, sizeof(captured)) &&
           strstr(captured, "fleet\ntask\ndecide\nbuilder\n--id\ntask_build\n--request\nreq-1\n--decision\napprove\n--trust-spec\n0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n") != NULL,
           "the decision carries the exact task, request and bound spec");
    write_input(session.master, "kk", 2U);
    (void)wait_for_marker(&session, "desired running", 2000);
    write_input(session.master, "a", 1U);
    result(wait_for_screen_text(&session, "FAKE REMOTE ATTACH", 100, 30, 4000), "a on a remote head attaches through fleet attach in the workspace");
    result(read_small_file(attach_path, captured, sizeof(captured)) &&
           strstr(captured, "fleet\nattach\nbuilder\n--project\n/work/project\n--instance\ninstance_aaaaaaaaaaaaaaaaaaaa\n--\nfeature-build\n") != NULL,
           "attach carries the exact host, project, instance and branch");
    write_input(session.master, "\002\t", 2U);
    result(wait_for_marker(&session, "Keys go to Hydra", 2000), "Ctrl-B Tab leaves remote input the same way as local input");
    write_input(session.master, "\002q", 2U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "remote action terminal exits and restores the terminal");
    close_session(&session);
    (void)unlink(attach_path); (void)unlink(cancel_path); (void)unlink(task_path);
    remote_env_clear();
}

/* A lost remote observation is STALE with its age, refuses actions and keeps
 * the rows; a host that stops answering keeps its rows as the last
 * observation, disconnected, until r and a fresh observation bring it back. */
static bool copy_file(const char *from, const char *to) {
    char bytes[8192];
    FILE *in = fopen(from, "r"), *out = in ? fopen(to, "w") : NULL;
    size_t length;
    if (!in || !out) { if (in) fclose(in); return false; }
    while ((length = fread(bytes, 1U, sizeof(bytes), in)) > 0) (void)fwrite(bytes, 1U, length, out);
    fclose(in);
    return fclose(out) == 0;
}

static void test_remote_stale_and_disconnected(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    char fixture[4096], switched[4096], fail_path[128], attach_path[128], switch_path[128];
    int fd;
    remote_fixture_path(fixture, sizeof(fixture), hydra, "remote-v3.tsv");
    remote_fixture_path(switched, sizeof(switched), hydra, "remote-v3-disconnected.tsv");
    snprintf(fail_path, sizeof(fail_path), "/tmp/hydra-u11-fail-%ld", (long)getpid());
    snprintf(attach_path, sizeof(attach_path), "/tmp/hydra-u11-stale-attach-%ld", (long)getpid());
    snprintf(switch_path, sizeof(switch_path), "/tmp/hydra-u11-switch-%ld", (long)getpid());
    (void)unlink(fail_path); (void)unlink(attach_path); (void)unlink(switch_path);
    (void)setenv("HYDRA_TEST_REMOTE_FIXTURE", fixture, 1);
    (void)setenv("HYDRA_TEST_REMOTE_FAIL_FILE", fail_path, 1);
    (void)setenv("HYDRA_TEST_REMOTE_SWITCH_FILE", switch_path, 1);
    (void)setenv("HYDRA_TEST_ATTACH_ARGV", attach_path, 1);
    if (!result(open_session(&session, tui, hydra, fake_bin, 100, 30) == 0, "open stale remote terminal")) { remote_env_clear(); return; }
    (void)wait_for_raw(&session);
    (void)wait_for_model(&session);
    result(wait_for_marker(&session, "builder/feature-build", 6000), "remote rows arrive");
    write_input(session.master, "jjj", 3U);
    (void)wait_for_marker(&session, "On builder", 2000);
    fd = open(fail_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    result(fd >= 0, "inject a lost remote observation");
    if (fd >= 0) close(fd);
    result(wait_for_marker(&session, "Remote: STALE, last good observation", 12000), "a lost remote observation is STALE with the age of the last good one");
    if (!result(wait_for_marker(&session, "observation stale, last good observation", 3000), "the remote head's status is stale, not its desired state")) remote_dump(&session);
    write_input(session.master, "a", 1U);
    result(wait_for_marker(&session, "Remote target is stale", 3000), "a stale observation refuses to attach");
    result(access(attach_path, F_OK) != 0, "a stale observation never invokes fleet attach");
    write_input(session.master, "x", 1U);
    result(wait_for_marker(&session, "Remote target is stale", 3000), "a stale observation refuses to interrupt");
    (void)unlink(fail_path);
    result(copy_file(switched, switch_path), "switch the host to one that stops answering");
    write_input(session.master, "r", 1U);
    result(wait_for_marker(&session, "Reconnecting to builder", 2000), "r asks for a fresh observation of the host");
    if (!result(wait_for_marker(&session, "retained from an observation", 12000), "a host that stops answering keeps its head as a retained row with its age")) remote_dump(&session);
    result(wait_for_marker(&session, "host disconnected", 2000), "the retained row says its host is disconnected");
    write_input(session.master, "a", 1U);
    result(wait_for_marker(&session, "host builder is disconnected", 3000), "a disconnected host refuses to attach");
    result(access(attach_path, F_OK) != 0, "a disconnected host never invokes fleet attach");
    write_input(session.master, "H", 1U);
    result(wait_for_marker(&session, "disconnected", 2000), "Hosts shows the host as disconnected");
    result(wait_for_marker(&session, "retained row", 2000), "Hosts counts the retained rows");
    (void)unlink(switch_path);
    write_input(session.master, "r", 1U);
    result(wait_for_marker(&session, "Reconnecting to", 2000), "r on a host row retries that host");
    if (!result(wait_for_marker(&session, "1 heads  reachable", 12000), "a host that answers again is connected with a fresh observation")) remote_dump(&session);
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "stale remote terminal exits and restores the terminal");
    close_session(&session);
    (void)unlink(fail_path); (void)unlink(attach_path); (void)unlink(switch_path);
    remote_env_clear();
}
