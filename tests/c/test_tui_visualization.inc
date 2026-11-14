/* User-level input probes. Coordinates are taken from the rendered layout. */
/* --view overview opens Work on the Run page of the active run: its graph,
 * its steps and the selected step's evidence; runs are rows of the list. At
 * 140x40 the list's rows start on row 4 of the terminal: feature-stale is
 * row 5 and the run visualization-proof row 7. */
static void test_visualization(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    struct winsize size;
    setenv("HYDRA_TEST_OVERVIEW", "1", 1);
    result(open_session(&session, tui, hydra, fake_bin, 140, 40) == 0, "open visualization terminal");
    unsetenv("HYDRA_TEST_OVERVIEW");
    (void)wait_for_model(&session);
    result(wait_for_markers(&session, "run visualization-proof   running", "STEP", 6000),
           "the overview view name opens the active run's Run page");
    mouse_input(&session, "\033[<0;10;5M");
    result(wait_for_marker(&session, "feature-stale   terminal gone", 1000), "a click selects the head row");
    mouse_input(&session, "\033[<0;110;12M");
    result(wait_for_marker(&session, "Session     terminal gone", 1000) && wait_for_marker(&session, "feature-stale   terminal gone", 300),
           "a click in the side pane focuses it and cannot retarget the list");
    mouse_input(&session, "\033[<0;10;7M");
    result(wait_for_marker(&session, "STEP", 1000), "a click on a run row shows its Run page");
    write_input(session.master, "\t\tj", 3);
    result(wait_for_marker(&session, "Kind    exec, attempt 1", 1000), "j on the Run page selects a step and shows its evidence");
    write_input(session.master, "j", 1);
    result(wait_for_marker(&session, "Kind    exec, attempt 2", 1000), "the next step's attempts are its own");
    write_input(session.master, "\033j", 2);
    result(wait_for_marker(&session, "run second-run", 1000), "another recorded run is a row of the list");
    write_input(session.master, "k", 1);
    (void)wait_for_marker(&session, "run visualization-proof   running", 1000);
    memset(&size, 0, sizeof(size)); size.ws_col = 40; size.ws_row = 10;
    result(ioctl(session.master, TIOCSWINSZ, &size) == 0, "resize the Run page to a narrow terminal");
    write_input(session.master, "\t", 1);
    result(wait_for_marker(&session, "STEP", 1000), "one pane at a time: Tab shows the run's steps at 40x10");
    size.ws_col = 140; size.ws_row = 40;
    (void)ioctl(session.master, TIOCSWINSZ, &size);
    write_input(session.master, "\tllll\r", 7);
    result(wait_for_marker(&session, "Kind    exec", 1000) && still_running(&session), "panning and Enter keep the selected step after resize");
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
    (void)wait_for_marker(&session, "STEP", 1000);
    fd = open(marker, O_WRONLY | O_CREAT | O_EXCL, 0600);
    result(fd >= 0, "inject a failed subsequent observation");
    if (fd >= 0) close(fd);
    /* Input activity must not indefinitely postpone refresh. */
    for (i = 0; i < 25; i++) { write_input(session.master, "j", 1); sleep_ms(100); }
    result(wait_for_marker(&session, "shell data adapter failed; showing last good snapshot", 2000),
           "continuous input still refreshes and labels the retained snapshot");
    unlink(marker);
    result(wait_for_marker(&session, "\xe2\x97\x8f live", 4000), "successful observation clears stale state");
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
    result(wait_for_marker(&session, "Showing host empty", 1000), "Enter follows an empty host's work, filtered to that host");
    result(wait_for_marker(&session, "No host empty to show", 1000), "empty host does not borrow another host's head");
    write_input(session.master, "\033", 1);
    result(wait_for_marker(&session, "List response: responded", 2000), "Esc returns to Hosts from the host's work");
    write_input(session.master, "H", 1);
    result(wait_for_marker(&session, "Observed 3", 2000), "Hosts lists the observed hosts again");
    mouse_input(&session, "\033[<0;10;7M");
    result(wait_for_marker(&session, "List response: failed", 1000), "mouse selects failed host and its response details");
    write_input(session.master, "q", 1);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "host view restores terminal settings");
    close_session(&session);
}
