/* Work's geometry: the agent conversation gets the full height of the screen
 * and the width between the list and the side pane; below 120 columns the
 * side pane's pages share the conversation's pane. The fake agent reports the
 * rows and columns of the pane it is drawn in. */

static void remote_dump(struct session *session);

static void layout_resize(struct session *session, unsigned short cols, unsigned short rows) {
    struct winsize size;
    memset(&size, 0, sizeof(size));
    size.ws_col = cols; size.ws_row = rows;
    (void)ioctl(session->master, TIOCSWINSZ, &size);
}

/* [ ] change the page of the pane that shows pages, from the list or from
 * that pane; Tab moves focus to it and Esc brings focus back to the list. */
static void test_workspace_enter(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    if (!result(open_session(&session, tui, hydra, fake_bin, 100, 30) == 0, "open Work pages terminal")) return;
    result(wait_for_raw(&session), "Work pages terminal enters raw mode");
    (void)wait_for_model(&session);
    if (!result(wait_for_marker(&session, "latest output   enter to talk", 3000), "the shared pane opens on the conversation")) remote_dump(&session);
    write_input(session.master, "]", 1U);
    result(wait_for_marker(&session, "Session     running", 3000), "] shows the selected head's Details page from the list");
    write_input(session.master, "\t[", 2U);
    result(wait_for_marker(&session, "latest output   enter to talk", 3000), "Tab focuses the shared pane, where [ goes back a page");
    result(wait_for_marker(&session, "scroll", 3000), "the focused conversation's footer offers scrolling");
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "x remove", 3000), "Esc brings focus back to the list and its keys");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "Work pages terminal exits and restores the terminal");
    close_session(&session);
}

static void test_workspace_layout(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    if (!result(open_session(&session, tui, hydra, fake_bin, 140, 40) == 0, "open Work layout terminal")) return;
    result(wait_for_raw(&session), "Work layout enters raw mode");
    (void)wait_for_model(&session);
    write_input(session.master, "a", 1U);
    /* 140x40: list 32, side pane 39; the conversation's terminal gets the
     * 63 columns between them and the 33 rows under its rule and header. */
    result(wait_for_screen_text(&session, "AGENT SIZE 33 63", 140, 40, 3000),
           "140x40 gives the agent conversation full height and the width between the list and the side pane");
    /* 80x24: list 24 and one shared pane; the agent keeps its 18 rows and
     * the 51 columns after the list. */
    layout_resize(&session, 80, 24);
    result(wait_for_screen_text(&session, "AGENT SIZE 18 51", 80, 24, 3000),
           "80x24 shares one pane between the agent and the side pane's pages");
    write_input(session.master, "\034A", 2U);
    result(wait_for_screen_text(&session, "Session     running", 80, 24, 2000),
           "80x24 shows the Details page in that pane after Ctrl-\\ A");
    write_input(session.master, "[", 1U);
    result(wait_for_screen_text(&session, "AGENT SIZE 18 51", 80, 24, 2000),
           "[ brings the conversation back to that pane");
    layout_resize(&session, 140, 40);
    result(wait_for_screen_text(&session, "Session     running", 140, 40, 2000),
           "140x40 shows the side pane beside the agent again");
    result(wait_for_screen_text(&session, "AGENT SIZE 33 63", 140, 40, 3000),
           "the agent gets its wide size back");
    write_input(session.master, "\034q", 2U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "Work layout exits and restores the terminal");
    close_session(&session);
}
