static void test_themes(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    (void)setenv("HYDRA_TEST_COLOR", "1", 1);
    (void)setenv("HYDRA_TUI_THEME", "dark", 1);
    result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0, "open themed terminal");
    result(wait_for_marker(&session, "\033[0;38;5;252;48;5;234m\033[H\033[2J", 1000), "dark startup theme paints background before clearing");
    result(wait_for_marker(&session, "\033[0;38;5;16;48;5;117m", 1000), "dark palette has explicit contrasting emphasis");
    mouse_input(&session, "t");
    result(wait_for_marker(&session, "\033[0;38;5;16;48;5;255m\033[H\033[2J", 1000), "theme cycle repaints light background");
    result(wait_for_marker(&session, "Theme: light", 1000), "theme choice is named in the notice");
    mouse_input(&session, "j\r");
    result(wait_for_marker(&session, "HEAD DETAIL  feature-stale", 1000), "theme change preserves keyboard navigation and detail");
    mouse_input(&session, "t");
    result(wait_for_marker(&session, "\033[0m\033[H\033[2J", 1000), "terminal palette restores user default background");
    result(wait_for_marker(&session, "Theme: terminal", 1000), "theme cycle returns to terminal palette");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "color session restores terminal state");
    close_session(&session);
    (void)unsetenv("HYDRA_TEST_COLOR");
    result(open_session(&session, tui, hydra, fake_bin, 40, 10) == 0, "open NO_COLOR themed terminal");
    result(wait_for_marker(&session, ">  feature-live", 1000), "NO_COLOR retains textual selection with a configured theme");
    mouse_input(&session, "t");
    result(wait_for_marker(&session, "Theme: light (NO_COLOR)", 1000), "NO_COLOR remains authoritative when cycling themes");
    result(!wait_for_marker(&session, "\033[0;38;5;16;48;5;255m", 600), "NO_COLOR emits no light palette escapes");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "NO_COLOR session restores terminal state");
    close_session(&session);
    (void)unsetenv("HYDRA_TUI_THEME");
}
