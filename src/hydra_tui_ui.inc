/* Terminal control, rendering, palette actions, and interactive input. */

static void write_terminal(const char *data, size_t length) {
    size_t written = 0U;
    while (written < length) {
        ssize_t result = write(STDOUT_FILENO, data + written, length - written);
        if (result > 0) {
            written += (size_t)result;
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

static void restore_terminal(struct app *app) {
    if (app != NULL && app->raw) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &app->saved);
        app->raw = false;
    }
    if (isatty(STDOUT_FILENO)) {
        static const char reset[] = "\033[?1000l\033[?1006l\033[0m\033[?25h\n";
        write_terminal(reset, sizeof(reset) - 1U);
    }
}

static void cleanup_terminal(void) { restore_terminal(active_app); }

static void signal_handler(int signal_number) {
    stop_signal = signal_number;
    stop_requested = 1;
}

static int enter_raw(struct app *app) {
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &app->saved) != 0) return -1;
    raw = app->saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    app->raw = true;
    {
        static const char modes[] = "\033[?25l\033[?1000h\033[?1006h";
        write_terminal(modes, sizeof(modes) - 1U);
    }
    return 0;
}

static void update_size(struct app *app) {
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0) {
        app->cols = size.ws_col;
        app->rows = size.ws_row;
    }
}

static void safe_print(const char *text, int width) {
    const unsigned char *cursor = (const unsigned char *)(text == NULL ? "" : text);
    int used = 0;
    while (*cursor != '\0' && used < width) {
        unsigned char ch = *cursor++;
        if (ch == '\t') ch = ' ';
        if (ch < 0x20U || ch == 0x7fU) ch = '?';
        if (ch >= 0x80U) ch = '?';
        putchar((int)ch);
        used++;
    }
}

static void linef(struct app *app, const char *format, ...) {
    char buffer[2048];
    va_list args;
    if (app->line >= app->limit) return;
    app->line++;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (app->boxed && app->cols >= 10) {
        int width = app->cols - 5;
        int used = (int)strlen(buffer);
        if (used > width) used = width;
        fputs("| ", stdout);
        safe_print(buffer, width);
        while (used++ < width) putchar(' ');
        fputs(" |", stdout);
    } else safe_print(buffer, app->cols > 1 ? app->cols - 1 : 1);
    if (!app->raw || app->line < app->rows) putchar('\n');
}

static const char *display_status(const struct head *head) {
    if (head->remote_host[0] != '\0') return "UNOBSERVED";
    if (strcmp(head->observed, "unavailable") == 0 || strcmp(head->liveness, "unavailable") == 0) return "UNAVAILABLE";
    if (strcmp(head->liveness, "stopped") == 0 &&
        (strcmp(head->observed, "running") == 0 || strcmp(head->observed, "idle") == 0)) return "STALE";
    if (strcmp(head->liveness, "live") == 0) return "LIVE";
    return "STOPPED";
}

static bool contains_folded(const char *text, const char *search) {
    size_t length = strlen(search);
    if (length == 0U) return true;
    while (*text != '\0') {
        if (strncasecmp(text, search, length) == 0) return true;
        text++;
    }
    return false;
}

static bool head_matches(const struct head *head, const char *search) {
    if (search[0] == '\0') return true;
    return contains_folded(head->branch, search) || contains_folded(head->session, search) ||
           contains_folded(head->group, search) || contains_folded(head->profile, search);
}

static struct head *selected_head(struct app *app) {
    if (app->selected >= app->model.head_count) return NULL;
    if (!head_matches(&app->model.heads[app->selected], app->search)) return NULL;
    return &app->model.heads[app->selected];
}

static void retarget_selection(struct app *app) {
    size_t index;
    if (selected_head(app) != NULL) return;
    for (index = 0U; index < app->model.head_count; index++) {
        if (head_matches(&app->model.heads[index], app->search)) {
            app->selected = index;
            return;
        }
    }
    app->selected = app->model.head_count;
}

static void move_selection(struct app *app, int direction) {
    size_t index;
    if (app->view == 3) {
        if (direction > 0 && app->recovery_selected + 1U < app->model.recovery_count) app->recovery_selected++;
        else if (direction < 0 && app->recovery_selected > 0U) app->recovery_selected--;
        return;
    }
    retarget_selection(app);
    if (selected_head(app) == NULL) return;
    if (direction > 0) {
        for (index = app->selected + 1U; index < app->model.head_count; index++) {
            if (head_matches(&app->model.heads[index], app->search)) {
                app->selected = index;
                return;
            }
        }
    } else {
        index = app->selected;
        while (index > 0U) {
            index--;
            if (head_matches(&app->model.heads[index], app->search)) {
                app->selected = index;
                return;
            }
        }
    }
}

static size_t marked_index(const struct app *app, const char *branch) {
    size_t index;
    for (index = 0U; index < app->marked_count; index++) {
        if (strcmp(app->marked[index], branch) == 0) return index;
    }
    return MAX_HEADS;
}

static void toggle_mark(struct app *app) {
    struct head *head = selected_head(app);
    size_t index;
    if (head == NULL) return;
    index = marked_index(app, head->branch);
    if (index < app->marked_count) {
        app->marked_count--;
        if (index < app->marked_count) {
            memmove(app->marked[index], app->marked[index + 1U],
                    (app->marked_count - index) * sizeof(app->marked[0]));
        }
    } else if (app->marked_count < MAX_HEADS) {
        copy_text(app->marked[app->marked_count++], TEXT, head->branch);
    }
}

static void select_all_visible(struct app *app) {
    size_t index;
    app->marked_count = 0U;
    for (index = 0U; index < app->model.head_count; index++) {
        if (head_matches(&app->model.heads[index], app->search)) {
            copy_text(app->marked[app->marked_count++], TEXT, app->model.heads[index].branch);
        }
    }
}

static const struct head *head_for_branch(const struct app *app, const char *branch) {
    size_t index;
    for (index = 0U; index < app->model.head_count; index++) {
        if (strcmp(app->model.heads[index].branch, branch) == 0) return &app->model.heads[index];
    }
    return NULL;
}

/* Store only rows actually painted in the last frame, including filtered indices. */
static void record_hit(struct app *app, size_t index) {
    if (app->line >= app->limit || app->hit_count >= MAX_HEADS) return;
    app->hit_rows[app->hit_count] = app->line + 1;
    app->hit_items[app->hit_count++] = index;
}

#include "hydra_tui_render.inc"

#include "hydra_tui_actions.inc"

static int read_key(int timeout_ms, char *key) {
    fd_set readfds;
    struct timeval timeout;
    int ready;
    FD_ZERO(&readfds); FD_SET(STDIN_FILENO, &readfds);
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
    if (ready <= 0) return ready;
    return read(STDIN_FILENO, key, 1U) == 1 ? 1 : -1;
}

#include "hydra_tui_mouse.inc"

static void handle_escape(struct app *app) {
    static const char paste_end[] = "\033[201~";
    char ch, sequence[64];
    size_t count = 0U, matched = 0U, consumed = 0U;
    struct timespec started, now;
    if (read_key(20, &ch) <= 0) {
        app->view = 0; app->help = false; app->diagnostics = false;
        app->search[0] = '\0'; app->notice[0] = '\0';
        return;
    }
    if (ch != '[') return;
    /* Drain overlong CSI reports through their terminator, with both bounds. */
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    while (consumed++ < 8192U && read_key(20, &ch) > 0) {
        if (count + 1U < sizeof(sequence)) sequence[count++] = ch;
        if ((unsigned char)ch >= 0x40U && (unsigned char)ch <= 0x7eU) break;
        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - started.tv_sec >= 1) break;
    }
    if (consumed >= sizeof(sequence)) return;
    consumed = 0U;
    sequence[count] = '\0';
    if (sequence[0] == '<') handle_mouse(app, sequence);
    else if (strcmp(sequence, "A") == 0) move_selection(app, -1);
    else if (strcmp(sequence, "B") == 0) move_selection(app, 1);
    else if (strcmp(sequence, "M") == 0) {
        /* Legacy X10 carries three bytes after CSI M; never treat them as keys. */
        for (count = 0U; count < 3U; count++) if (read_key(20, &ch) <= 0) break;
    }
    else if (strcmp(sequence, "200~") == 0) {
        (void)clock_gettime(CLOCK_MONOTONIC, &started);
        while (consumed++ < 8192U) {
            (void)clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec - started.tv_sec >= 1) break;
            if (read_key(20, &ch) <= 0) continue;
            if (ch == paste_end[matched]) matched++;
            else matched = ch == paste_end[0] ? 1U : 0U;
            if (matched == sizeof(paste_end) - 1U) break;
        }
        copy_text(app->notice, sizeof(app->notice), "bracketed paste ignored");
    }
}

static void interactive_prompt(struct app *app, char prefix) {
    char query[TEXT];
    if (prompt_text(app, prefix == '/' ? "Search heads: " : "Action search: ", query, sizeof(query)) != 0) return;
    if (prefix == '/') {
        app->notice[0] = '\0';
        copy_text(app->search, sizeof(app->search), query);
        retarget_selection(app);
    }
    else execute_palette(app, query);
}

static int interactive_main(struct app *app) {
    time_t last_refresh = 0;
    char key;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("hydra-tui requires interactive stdin/stdout; use --headless-fixture for deterministic rendering\n", stderr);
        return 3;
    }
    if (getenv("TERM") == NULL || strcmp(getenv("TERM"), "dumb") == 0) {
        fputs("hydra-tui cannot use TERM=dumb; run hydra tui --basic\n", stderr);
        return 3;
    }
    update_size(app);
    if (app->cols < 40 || app->rows < 10) {
        fputs("hydra-tui requires at least 40 columns by 10 rows; run hydra tui --basic\n", stderr);
        return 3;
    }
    active_app = app;
    atexit(cleanup_terminal);
    signal(SIGINT, signal_handler); signal(SIGTERM, signal_handler); signal(SIGHUP, signal_handler);
    signal(SIGPIPE, signal_handler);
    if (refresh_model(app) != 0 && app->model.head_count == 0U) return 4;

    refresh_current_session(app);
    if (enter_raw(app) != 0) return 4;
    app->running = true;
    while (app->running && !stop_requested) {
        time_t now = time(NULL);
        update_size(app);
        if (now - last_refresh >= 2) {
            (void)refresh_model(app);
            refresh_current_session(app);
            capture_preview(app);
            last_refresh = now;
        }
        render(app, 0U, false);
        if (read_key(500, &key) <= 0) continue;
        if (key == 'q') app->running = false;
        else if (key == 'j') move_selection(app, 1);
        else if (key == 'k') move_selection(app, -1);
        else if (key == 'v') { app->view = (app->view + 1) % 4; app->diagnostics = false; }
        else if (key == '\r' || key == '\n') {
            if (selected_head(app) != NULL) app->view = 1;
            else copy_text(app->notice, sizeof(app->notice), "no matching head selected");
        }
        else if (key == '/') interactive_prompt(app, '/');
        else if (app->fleet && key == 'a') fleet_action(app, true);
        else if (app->fleet && key == 'c') fleet_action(app, false);
        else if (app->fleet && (key == ':' || key == 'p' || key == ' ' || key == 'A' || key == 'x' || key == 'G')) copy_text(app->notice, sizeof(app->notice), "Fleet: a attach, c interrupt, v views, / search, q quit");
        else if (key == ':') interactive_prompt(app, ':');
        else if (key == 'p') { app->view = 1; app->preview = !app->preview; capture_preview(app); }
        else if (key == 'd') { if (app->view != 3) app->view = 1; app->diagnostics = !app->diagnostics; }
        else if (key == ' ') toggle_mark(app);
        else if (key == 'A') select_all_visible(app);
        else if (key == 'x' && app->marked_count > 0U) kill_marked_action(app);
        else if (key == 'G' && app->marked_count > 0U) group_marked_action(app);
        else if (key == '?') app->help = !app->help;
        else if (key == 27) handle_escape(app);
    }
    restore_terminal(app);
    return stop_signal == 0 ? 0 : 128 + stop_signal;
}
