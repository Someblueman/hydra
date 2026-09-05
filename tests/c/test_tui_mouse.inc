/* Interaction tests share the PTY harness, not the implementation's hit map. */
static void mouse_input(struct session *session, const char *report) {
    drain_output(session->master);
    write_input(session->master, report, strlen(report));
}

static void test_mouse(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    struct winsize size;
    char overlong[240];
    result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0, "open mouse terminal");
    result(wait_for_marker(&session, "\033[?1006h", 1000), "raw mode enables SGR mouse reporting");
    (void)wait_for_marker(&session, "Click row", 1000);
    mouse_input(&session, "\033[<0;8;6M");
    result(wait_for_marker(&session, ">  feature-stale", 1000), "click selects the painted row");
    mouse_input(&session, "\033[<0;12;2M");
    result(wait_for_marker(&session, "HEAD DETAIL  feature-stale", 1000), "click Details opens selected head");
    mouse_input(&session, "\033[<0;3;2M");
    (void)wait_for_marker(&session, "[Heads]", 1000);
    mouse_input(&session, "\033[<65;8;6M");
    result(wait_for_marker(&session, ">  feature-unavailable", 1000), "wheel down has keyboard selection parity");
    mouse_input(&session, "\033[<64;8;6M");
    result(wait_for_marker(&session, ">  feature-stale", 1000), "wheel up has keyboard selection parity");
    mouse_input(&session, "\033[<0;8;5m\033[<32;8;5M\033[<4;8;5M\033[<0;0;5M\033[<0;8;25M\033[<0;-1;5M\033[<0;999999999999;5M\033[Mqqq");
    result(wait_for_marker(&session, "Row 2 of 3", 1000), "release, drag, modifiers and invalid coordinates do not select");
    memset(overlong, '9', sizeof(overlong));
    memcpy(overlong, "\033[<0;", 5U);
    memcpy(overlong + sizeof(overlong) - 5U, ";5M", 4U);
    overlong[sizeof(overlong) - 1U] = '\0';
    mouse_input(&session, overlong);
    write_input(session.master, "k", 1U);
    result(wait_for_marker(&session, ">  feature-live", 1000), "overlong mouse report drains without breaking the next key");
    mouse_input(&session, "\033[<0;40;2M");
    result(wait_for_marker(&session, "RECOVERY BOARD", 1000), "click Recovery changes view");
    mouse_input(&session, "\033[<0;8;6M");
    result(wait_for_marker(&session, "> stale-lock", 1000), "recovery row click selects finding");
    write_input(session.master, "d", 1U);
    result(wait_for_marker(&session, "Inspect:", 1000), "clicked recovery finding retains keyboard diagnostics");
    write_input(session.master, "\033", 1U);
    (void)wait_for_marker(&session, "[Heads]", 1000);
    write_input(session.master, "/", 1U);
    result(wait_for_marker(&session, "\033[?1006l", 1000), "search prompt disables mouse reporting");
    (void)wait_for_marker(&session, "Search heads:", 1000);
    write_input(session.master, "unavailable\n", 12U);
    (void)wait_for_marker(&session, "Search: unavailable", 1000);
    mouse_input(&session, "\033[<0;8;5M");
    write_input(session.master, "\r", 1U);
    result(wait_for_marker(&session, "HEAD DETAIL  feature-unavailable", 1000), "filtered click maps to the original head index");
    write_input(session.master, "\033", 1U);
    (void)wait_for_marker(&session, "[Heads]", 1000);
    memset(&size, 0, sizeof(size)); size.ws_col = 40; size.ws_row = 10;
    (void)ioctl(session.master, TIOCSWINSZ, &size);
    (void)wait_for_marker(&session, "Enter open", 1500);
    mouse_input(&session, "\033[<0;8;5M");
    result(wait_for_marker(&session, ">  feature-stale", 1000), "minimum-size scrolled click selects the visible row");
    mouse_input(&session, "\033[<65;8;5M");
    result(wait_for_marker(&session, ">  feature-unavail", 1000), "wheel scroll keeps selection visible after resize");
    write_input(session.master, "q", 1U);
    result(wait_for_marker(&session, "\033[?1000l\033[?1006l", 1000), "quit disables both mouse modes");
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "mouse session restores exact terminal state");
    close_session(&session);
}
