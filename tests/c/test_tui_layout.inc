/* Planning workspace geometry: the agent conversation gets full height and
 * most of the width beside the plan review; at 80x24 the side that holds focus
 * gets the space and a visible hint reveals the other. The fake agent reports
 * the rows and columns of the pane it is drawn in. */

static void layout_resize(struct session *session, unsigned short cols, unsigned short rows) {
    struct winsize size;
    memset(&size, 0, sizeof(size));
    size.ws_col = cols; size.ws_row = rows;
    (void)ioctl(session->master, TIOCSWINSZ, &size);
}

/* Enter follows the selected head into its full details from whichever
 * Hydra pane holds focus, as the SELECTED WORK pane says; Esc returns to the
 * workspace with the selection kept. */
static void test_workspace_enter(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    if (!result(open_session(&session, tui, hydra, fake_bin, 100, 30) == 0, "open workspace Enter terminal")) return;
    result(wait_for_raw(&session), "workspace Enter terminal enters raw mode");
    (void)wait_for_model(&session);
    write_input(session.master, "W", 1U);
    result(wait_for_marker(&session, "Enter  full details", 3000), "the selected-work pane offers Enter for full details");
    write_input(session.master, "\t", 1U);
    sleep_ms(100);
    write_input(session.master, "\r", 1U);
    result(wait_for_markers(&session, "Details: feature-live", "HYDRA / DETAILS", 3000), "Enter from the selected-work pane opens the head's details");
    write_input(session.master, "\033", 1U);
    result(wait_for_marker(&session, "PLAN TOGETHER", 3000), "Esc returns from the details to the workspace");
    write_input(session.master, "\r", 1U);
    result(wait_for_marker(&session, "Details: feature-live", 3000), "Enter from the project pane opens the same head's details");
    write_input(session.master, "\033", 1U);
    (void)wait_for_marker(&session, "PLAN TOGETHER", 3000);
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "workspace Enter terminal exits and restores the terminal");
    close_session(&session);
}

static void test_workspace_layout(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    if (!result(open_session(&session, tui, hydra, fake_bin, 140, 40) == 0, "open planning layout terminal")) return;
    result(wait_for_raw(&session), "planning layout enters raw mode");
    (void)wait_for_model(&session);
    write_input(session.master, "W", 1U);
    sleep_ms(100);
    write_input(session.master, "a", 1U);
    /* 140x40: navigation 20, agent 72, review 45 columns; the agent pane spans
     * the 36 workspace rows and its terminal gets 70x33 inside the frame. */
    result(wait_for_screen_text(&session, "AGENT SIZE 33 70", 140, 40, 3000),
           "140x40 gives the agent conversation full height and the majority of the width");
    /* 80x24: the review cannot fit beside a 50-column agent, so the agent keeps
     * all 20 workspace rows and the 60 columns after navigation (58x17 inside). */
    layout_resize(&session, 80, 24);
    result(wait_for_screen_text(&session, "AGENT SIZE 17 58", 80, 24, 3000),
           "80x24 keeps 18 navigation columns and gives the agent every remaining column");
    write_input(session.master, "\002B", 2U);
    result(wait_for_screen_text(&session, "PLAN REVIEW", 80, 24, 2000),
           "80x24 shows the plan review in place of the agent after Ctrl-B B");
    write_input(session.master, "A", 1U);
    result(wait_for_screen_text(&session, "Ctrl-B B plan", 80, 24, 2000),
           "80x24 conversation names the key that reveals the hidden plan review");
    layout_resize(&session, 140, 40);
    result(wait_for_screen_text(&session, "DEPENDENCIES", 140, 40, 2000),
           "140x40 shows the review beside the agent again");
    write_input(session.master, "\002B", 2U);
    result(wait_for_screen_text(&session, "AGENT SIZE 33 48", 140, 40, 3000),
           "the plan overview keeps the agent beside a wider plan review");
    write_input(session.master, "\002q", 2U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "planning layout exits and restores the terminal");
    close_session(&session);
}
