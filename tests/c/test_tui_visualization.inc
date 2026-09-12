/* User-level input probes. Coordinates are taken from the rendered layout. */
static void test_visualization(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    struct winsize size;
    setenv("HYDRA_TEST_OVERVIEW", "1", 1);
    result(open_session(&session, tui, hydra, fake_bin, 140, 40) == 0, "open visualization terminal");
    unsetenv("HYDRA_TEST_OVERVIEW");
    (void)wait_for_model(&session);
    result(wait_for_marker(&session, "QUEUE DEPTH", 1000), "overview lays out real snapshot statistics");
    mouse_input(&session, "\033[<0;10;13M");
    result(wait_for_marker(&session, "Observed: STALE", 1000), "overview mouse selects the head row");
    mouse_input(&session, "\033[<0;110;12M");
    result(wait_for_marker(&session, "Observed: STALE", 1000), "detail panel click cannot retarget the head table");
    write_input(session.master, "w", 1);
    result(wait_for_marker(&session, "4 dependency edges", 1500), "workflow view consumes the read adapter");
    write_input(session.master, "j", 1);
    result(wait_for_marker(&session, "build [exec] / running / attempts 1", 1000), "keyboard selects graph nodes and exposes recorded details");
    mouse_input(&session, "\033[<0;34;16M");
    result(wait_for_marker(&session, "tests [exec] / failed / attempts 2", 1000), "click selects a dependency graph node");
    write_input(session.master, "]", 1);
    result(wait_for_marker(&session, "second-run", 1000), "run navigation reaches another recorded workflow");
    write_input(session.master, "[", 1);
    (void)wait_for_marker(&session, "visualization-proof", 1000);
    memset(&size, 0, sizeof(size)); size.ws_col = 40; size.ws_row = 10;
    result(ioctl(session.master, TIOCSWINSZ, &size) == 0, "resize graph to a narrow terminal");
    write_input(session.master, "j", 1);
    result(wait_for_marker(&session, "2/4 build [running]", 1000), "narrow graph becomes navigable step detail");
    size.ws_col = 140; size.ws_row = 40;
    (void)ioctl(session.master, TIOCSWINSZ, &size);
    write_input(session.master, "llll\r", 5);
    result(wait_for_marker(&session, "build [exec] / running", 1000), "Enter refocuses the selected node after panning and resize");
    write_input(session.master, "q", 1);
    result(wait_for_exit(&session) == 0, "visualization exits normally");
    result(terminal_restored(&session), "visualization preserves exact terminal settings");
    close_session(&session);
}

static void test_visualization_refresh(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    char marker[] = "/tmp/hydra-viz-refresh.XXXXXX";
    int fd = mkstemp(marker), i;
    result(fd >= 0, "create owned refresh-failure marker");
    if (fd < 0) return;
    close(fd); unlink(marker);
    setenv("HYDRA_TEST_FAIL_FILE", marker, 1);
    setenv("HYDRA_TEST_OVERVIEW", "1", 1);
    result(open_session(&session, tui, hydra, fake_bin, 140, 40) == 0, "open refresh visualization terminal");
    unsetenv("HYDRA_TEST_FAIL_FILE"); unsetenv("HYDRA_TEST_OVERVIEW");
    (void)wait_for_marker(&session, "QUEUE DEPTH", 1000);
    fd = open(marker, O_WRONLY | O_CREAT | O_EXCL, 0600);
    result(fd >= 0, "inject a failed subsequent observation");
    if (fd >= 0) close(fd);
    /* Input activity must not indefinitely postpone refresh. */
    for (i = 0; i < 25; i++) { write_input(session.master, "j", 1); sleep_ms(100); }
    result(wait_for_marker(&session, "shell data adapter failed; showing last good snapshot", 2000),
           "continuous input still refreshes and labels the retained snapshot");
    unlink(marker);
    result(wait_for_marker(&session, "Current snapshot", 4000), "successful observation clears stale state");
    write_input(session.master, "q", 1);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "refresh recovery preserves terminal state");
    close_session(&session);
}

static void test_visualization_hosts(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    setenv("HYDRA_TEST_FLEET_VIEW", "1", 1);
    result(open_session(&session, tui, hydra, fake_bin, 140, 40) == 0, "open host visualization terminal");
    unsetenv("HYDRA_TEST_FLEET_VIEW");
    result(wait_for_marker(&session, "CPU / memory / load: unavailable", 1000), "host detail distinguishes missing telemetry");
    write_input(session.master, "j\r", 2);
    result(wait_for_marker(&session, "Search: empty", 1000), "Enter filters heads for an empty host");
    result(wait_for_marker(&session, "No matching heads", 1000), "empty host does not borrow another host's head");
    write_input(session.master, "\033", 1);
    (void)wait_for_marker(&session, "[Heads]", 2000);
    write_input(session.master, "H", 1);
    result(wait_for_marker(&session, "HOST DETAIL", 2000), "return to hosts after clearing its head filter");
    mouse_input(&session, "\033[<0;10;8M");
    result(wait_for_marker(&session, "List response: failed", 1000), "mouse selects failed host and its response details");
    write_input(session.master, "q", 1);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "host view restores terminal settings");
    close_session(&session);
}
