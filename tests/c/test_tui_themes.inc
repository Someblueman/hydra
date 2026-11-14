#define THEME_FRAME_BEGIN "\033[?2026h"
#define THEME_FRAME_END "\033[?2026l"

static size_t theme_count(const char *text, const char *part) {
    size_t found = 0;
    for (text = strstr(text, part); text; text = strstr(text + 1, part)) found++;
    return found;
}

/* The update that names the theme leaves the terminal's default rendition
 * behind: the last rendition change before its end is a reset, or it never
 * left the default. */
static bool theme_ends_reset(const char *frame, const char *named) {
    const char *end = strstr(named, THEME_FRAME_END), *last = NULL, *at;
    for (at = strstr(frame, "\033["); at && at < end; at = strstr(at + 1, "\033[")) {
        const char *final = at + 2;
        while (*final && (*final == ';' || (*final >= '0' && *final <= '9'))) final++;
        if (*final == 'm') last = at;
    }
    return !last || !strncmp(last, "\033[0m", 4);
}

/* The rows a frame starts painting at their first column. */
static int theme_rows_painted(const char *frame, int rows) {
    int row, painted = 0;
    for (row = 1; row <= rows; row++) {
        char address[16];
        snprintf(address, sizeof(address), "\033[%d;1H", row);
        painted += strstr(frame, address) != NULL;
    }
    return painted;
}

/* Presses t and reads what the terminal is sent until the new theme is named
 * and the update that names it has ended. A theme switch changes the colours
 * of cells whose text stays the same: every cell is painted again, starting
 * from a reset into the new base colours, inside one synchronized update,
 * and the display is never erased (an erase would flash the default
 * background). The update ends by returning the rendition to the default. */
static bool theme_switch(struct session *session, const char *base, const char *name) {
    char frame[65536] = "";
    size_t used = 0U;
    long long deadline = monotonic_ms() + 2000;
    const char *named = NULL;
    mouse_input(session, "t");
    while (monotonic_ms() < deadline && !(named && strstr(named, THEME_FRAME_END))) {
        char buffer[4096];
        ssize_t length = read_marker_output(session, buffer, sizeof(buffer));
        if (length > 0) capture_bytes(frame, &used, buffer, (size_t)length);
        else sleep_ms(5);
        named = strstr(frame, name);
    }
    if (!named || !strstr(named, THEME_FRAME_END)) return false;
    return !strncmp(frame, THEME_FRAME_BEGIN, strlen(THEME_FRAME_BEGIN)) &&
        theme_count(frame, THEME_FRAME_BEGIN) == theme_count(frame, THEME_FRAME_END) &&
        !strstr(frame, "\033[2J") && theme_rows_painted(frame, 24) == 24 &&
        strstr(frame, base) != NULL && strstr(frame, base) < strstr(frame, "\xe2\x97\x86 hydra") &&
        theme_ends_reset(frame, named);
}

static void test_themes(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    (void)setenv("HYDRA_TEST_COLOR", "1", 1);
    (void)setenv("HYDRA_TUI_THEME", "dark", 1);
    if (!result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0, "open themed terminal")) goto done;
    /* The screen is taken over once, then painted in the theme's colours;
     * observe the raw stream before draining it with wait_for_model. */
    /* The first cell is painted from a reset; after it only the difference
     * from the rendition in effect is written, here both colours of a
     * selection. */
    result(wait_for_markers(&session, "\033[?1049h\033[H\033[2J", "\033[1;1H\033[0;38;5;252;48;5;234m", 2000) &&
           wait_for_marker(&session, "\033[38;5;16;48;5;117m", 2000),
           "dark startup paints background and contrasting emphasis");
    (void)wait_for_model(&session);
    result(theme_switch(&session, "\033[0;38;5;16;48;5;255m", "Theme: light"),
           "light theme repaints every cell in one synchronized update without erasing, and names the choice");
    mouse_input(&session, "j\r");
    result(wait_for_marker(&session, "Details: feature-stale", 1000), "theme change preserves keyboard navigation and detail");
    result(theme_switch(&session, "\033[0m", "Theme: terminal"),
           "terminal theme repaints every cell with the default background, without erasing, and names the choice");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "color session restores terminal state");
    close_session(&session);
    (void)unsetenv("HYDRA_TEST_COLOR");
    if (!result(open_session(&session, tui, hydra, fake_bin, 40, 10) == 0, "open NO_COLOR themed terminal")) goto done;
    /* Without colour the attributes still carry the meaning: the brand is
     * bold and the active tab is bold reverse video, in every theme. */
    result(wait_for_markers(&session, "\033[1;1H \033[1m\xe2\x97\x86 hydra", "\033[1;7m 1 Work ", 2000),
           "NO_COLOR startup writes attributes only, from the first cell on");
    (void)wait_for_model(&session);
    /* One key at a time, so each row is drawn again as the selection moves. */
    mouse_input(&session, "j");
    (void)wait_for_marker(&session, "\xe2\x96\x8c  feature-stale", 1000);
    mouse_input(&session, "k");
    result(wait_for_marker(&session, "\xe2\x96\x8c  feature-live", 1000), "NO_COLOR retains the selection bar with a configured theme");
    mouse_input(&session, "t");
    result(wait_for_marker(&session, "Theme: light (NO_COLOR)", 1000), "NO_COLOR remains authoritative when cycling themes");
    result(!wait_for_marker(&session, "38;5;", 600) && !wait_for_marker(&session, "48;5;", 100),
           "NO_COLOR emits no palette escapes in any theme");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0 && terminal_restored(&session), "NO_COLOR session restores terminal state");
    close_session(&session);
done:
    (void)unsetenv("HYDRA_TEST_COLOR");
    (void)unsetenv("HYDRA_TUI_THEME");
}
