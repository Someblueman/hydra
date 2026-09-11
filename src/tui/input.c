#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"

int read_key(int timeout_ms, char *key) {
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

/* SGR mouse reports borrow the last rendered frame's hit map. No mutations. */
static bool mouse_hit(const struct app *app, const unsigned values[3], size_t index) {
    return values[2] >= (unsigned)app->hit_rows[index] && values[2] <= (unsigned)app->hit_bottom[index] &&
        values[1] >= (unsigned)app->hit_left[index] && values[1] <= (unsigned)app->hit_right[index];
}

static void select_mouse_item(struct app *app, size_t item) {
    switch (app->view) {
        case 0: case 4: app->selected = item; break;
        case 3: app->recovery_selected = item; break;
        case 5: app->workflow_node = item; app->graph_follow = true; break;
        case 6: app->host_selected = item; break;
        default: break;
    }
}

static void statistics_mouse(struct app *app, const unsigned values[3]) {
    size_t index;
    for (index = 0; index < app->hit_count; index++) {
        if (!mouse_hit(app, values, index)) continue;
        if (values[0] == 64 || values[0] == 65) statistics_move(app, values[0] == 64 ? -1 : 1);
        else if (values[0] == 0) {
            app->statistics->selected = app->hit_items[index];
            app->statistics->selected_id[0] = '\0'; statistics_visible(app);
        }
        return;
    }
}

static const char *parse_mouse(const char *sequence, unsigned values[3]) {
    const char *cursor = sequence + 1;
    size_t field;
    for (field = 0U; field < 3U; field++) {
        if (*cursor < '0' || *cursor > '9') return NULL;
        while (*cursor >= '0' && *cursor <= '9') {
            values[field] = values[field] * 10U + (unsigned)(*cursor++ - '0');
            if (values[field] > 65535U) return NULL;
        }
        if (field < 2U && *cursor++ != ';') return NULL;
    }
    return cursor;
}

static void handle_mouse(struct app *app, const char *sequence) {
    unsigned values[3] = {0U, 0U, 0U};
    const char *cursor = parse_mouse(sequence, values);
    size_t index;
    if (!cursor) return;
    if ((*cursor != 'M' && *cursor != 'm') || cursor[1] != '\0') return;
    update_size(app);
    if (app->cols != app->hit_cols || app->rows != app->hit_height ||
        app->view != app->hit_view || app->cols < 40 || app->rows < 10) return;
    if (values[1] == 0U || values[1] >= (unsigned)app->cols ||
        values[2] == 0U || values[2] > (unsigned)app->rows) return;
    if (app->view == 7 && !app->help && !app->diagnostics) {
        native_workspace_mouse(app, values[0], (int)values[1] - 1, (int)values[2] - 1, *cursor == 'm');
        return;
    }
    if (*cursor == 'm') return;
    if (app->view == 8 && !app->help && !app->diagnostics) {
        statistics_mouse(app, values);
        return;
    }
    if (values[0] == 0U && values[2] == 2U && app->hit_tabs) {
        static const unsigned starts[] = {1U, 10U, 21U, 37U, 50U, 62U, 75U};
        static const unsigned ends[] = {7U, 18U, 34U, 46U, 59U, 72U, 81U};
        for (index = 0U; index < (app->cols >= 100 ? 7U : 4U); index++) {
            if (values[1] >= starts[index] && values[1] <= ends[index]) {
                app->view = (int)index; app->help = false; app->diagnostics = false;
                if (index == 5 && !app->fleet) { app->graph_follow = true; (void)refresh_workflows(app, NULL); }
                return;
            }
        }
    }
    if (app->help || app->diagnostics || values[1] < 3U ||
        values[1] > (unsigned)(app->cols - 3)) return;
    for (index = 0U; index < app->hit_count; index++) {
        if (!mouse_hit(app, values, index)) continue;
        if (values[0] == 64U || values[0] == 65U) {
            move_selection(app, values[0] == 64U ? -1 : 1);
        } else if (values[0] == 0U) {
            select_mouse_item(app, app->hit_items[index]);
        }
        return;
    }
}


static bool escape_expired(const struct timespec *started) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec - started->tv_sec > 1 ||
        (now.tv_sec - started->tv_sec == 1 && now.tv_nsec >= started->tv_nsec);
}

static bool csi_final(char ch) {
    return (unsigned char)ch >= 0x40U && (unsigned char)ch <= 0x7eU;
}

static bool discard_complete(struct app *app, char ch) {
    static const char paste_end[] = "\033[201~";
    if (app->input_mode == INPUT_DISCARD_CSI) return csi_final(ch);
    if (ch == paste_end[app->paste_matched]) app->paste_matched++;
    else app->paste_matched = ch == paste_end[0] ? 1U : 0U;
    return app->paste_matched == sizeof(paste_end) - 1U;
}

/* A budget yields to the UI, never turns discarded bytes into key bindings.
 * The caller owns app and retains the terminator match across input batches. */
static void discard_input(struct app *app, char ch) {
    struct timespec started;
    size_t consumed = 0U;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    for (;;) {
        if (discard_complete(app, ch)) {
            app->input_mode = INPUT_KEYS;
            app->paste_matched = 0U;
            return;
        }
        if (++consumed >= 8192U || escape_expired(&started) || read_key(20, &ch) <= 0) return;
    }
}

static void workspace_arrow(struct app *app, char key) {
    if (app->view == 7) (void)native_workspace_key(app, key);
}
static void selection_arrow(struct app *app, char key, int direction) {
    if (app->view == 9) (void)native_attention_key(app, key);
    else move_selection(app, direction);
}
static void discard_legacy_mouse(char *key) {
    size_t count;
    for (count = 0U; count < 3U; count++) if (read_key(20, key) <= 0) break;
}
static void begin_paste_discard(struct app *app, char *key) {
    app->input_mode = INPUT_DISCARD_PASTE;
    app->paste_matched = 0U;
    copy_text(app->notice, sizeof(app->notice), "bracketed paste ignored");
    if (read_key(20, key) > 0) discard_input(app, *key);
}

static void dispatch_escape(struct app *app, const char *sequence) {
    char ch;
    if (sequence[0] == '<') handle_mouse(app, sequence);
    else if (strcmp(sequence, "A") == 0) selection_arrow(app, 'A', -1);
    else if (strcmp(sequence, "B") == 0) selection_arrow(app, 'B', 1);
    else if (strcmp(sequence, "C") == 0) workspace_arrow(app, 'l');
    else if (strcmp(sequence, "D") == 0) workspace_arrow(app, 'h');
    else if (strcmp(sequence, "M") == 0) {
        /* Legacy X10 carries three bytes after CSI M; never treat them as keys. */
        discard_legacy_mouse(&ch);
    }
    else if (strcmp(sequence, "200~") == 0) {
        begin_paste_discard(app, &ch);
    }
}

static void handle_escape(struct app *app) {
    char ch, sequence[64];
    size_t count = 0U, consumed = 0U;
    struct timespec started;
    bool complete = false;
    if (read_key(20, &ch) <= 0) {
        if (statistics_back(app)) return;
        app->view = 0; app->help = false; app->diagnostics = false;
        app->search[0] = '\0'; app->notice[0] = '\0';
        return;
    }
    if (ch != '[') return;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    while (consumed++ < 8192U && read_key(20, &ch) > 0) {
        if (count + 1U < sizeof(sequence)) sequence[count++] = ch;
        if (csi_final(ch)) { complete = true; break; }
        if (escape_expired(&started)) break;
    }
    if (!complete) { app->input_mode = INPUT_DISCARD_CSI; return; }
    if (consumed >= sizeof(sequence)) return;
    sequence[count] = '\0';
    dispatch_escape(app, sequence);
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

static bool fleet_key(struct app *app, char key) {
    if (!app->fleet) return false;
    if (app->view == 4 && native_control_key(app, key)) return true;
    switch (key) {
        case 'a': if (native_terminal_attach(app)) { app->view=7; native_workspace_show_terminal(app, true); } return true;
        case 'c': fleet_action(app); return true;
        case ':': case 'p': case ' ': case 'A': case 'x': case 'G':
            copy_text(app->notice, sizeof(app->notice), "Fleet: a attach, c interrupt, v views, / search, q quit"); return true;
        default: return false;
    }
}

static void open_selected(struct app *app) {
    if (app->view == 6 && app->host_selected < app->model.host_count) {
        copy_text(app->search, sizeof(app->search), app->model.hosts[app->host_selected].name);
        app->view = 0; retarget_selection(app);
    } else if (selected_head(app) != NULL) app->view = 1;
    else copy_text(app->notice, sizeof(app->notice), "no matching head selected");
}

static bool workspace_key(struct app *app, char key) {
    if (native_plan_key(app, key)) return true;
    if (native_control_key(app, key)) return true;
    if (native_workspace_key(app, key)) return true;
    if (key != 'a' || app->fleet) return false;
    if (native_terminal_attach(app)) native_workspace_show_terminal(app, true);
    return true;
}

static bool select_view(struct app *app, char key) {
    switch (key) {
        case 'v': app->view = (app->view + 1) % 10; break;
        case 'I': app->view = 9; break;
        case 'W': app->view = 7; break;
        case 'o': app->view = 4; break;
        case 'H': app->view = 6; break;
        case 'w': app->view = 5; break;
        default: return false;
    }
    app->diagnostics = false;
    if (app->view == 5) {
        app->graph_follow = true;
        if (!app->fleet) (void)refresh_workflows(app, NULL);
    }
    if (app->view == 8) (void)refresh_statistics(app, NULL);
    if (app->view == 9) native_attention_tick(app, true);
    return true;
}

static bool view_key(struct app *app, char key) {
    if (app->view == 9 && native_attention_key(app, key)) return true;
    if (key == 'D') { statistics_toggle(app); return true; }
    if (app->view == 8 && statistics_key(app, key)) return true;
    if (app->view == 7 && workspace_key(app, key)) return true;
    if (app->view == 5 && workflow_key(app, key)) return true;
    if (select_view(app, key)) return true;
    if (app->view == 6 && key && strchr("/:pac AxGd", key)) {
        copy_text(app->notice, sizeof(app->notice), "Select a host and press Enter to inspect its heads");
        return true;
    }
    return false;
}

static void handle_key(struct app *app, char key) {
    if (native_terminal_byte(app, (unsigned char)key)) return;
    if (key == 3) { terminal_request_stop(SIGINT); return; }
    if (key == 'q') { app->running = false; return; }
    if (view_key(app, key)) return;
    if (fleet_key(app, key)) return;
    switch (key) {
        case 'q': app->running = false; break;
        case 'j': move_selection(app, 1); break;
        case 'k': move_selection(app, -1); break;
        case '\r': case '\n': open_selected(app); break;
        case '/': case ':': interactive_prompt(app, key); break;
        case 'p': app->view = 1; app->preview = !app->preview; capture_preview(app); break;
        case 'd': if (app->view != 3) app->view = 1; app->diagnostics = !app->diagnostics; break;
        case ' ': toggle_mark(app); break;
        case 'A': select_all_visible(app); break;
        case 'x': if (app->marked_count > 0U) kill_marked_action(app); break;
        case 'G': if (app->marked_count > 0U) group_marked_action(app); break;
        case 't':
            app->theme = (app->theme + 1) % 3;
            snprintf(app->notice, sizeof(app->notice), "Theme: %s%s", theme_name(app->theme), app->no_color ? " (NO_COLOR)" : "");
            break;
        case '?': app->help = !app->help; break;
        case 27: handle_escape(app); break;
        default: break;
    }
}

static void handle_input(struct app *app, char key) {
    if (app->input_mode == INPUT_KEYS) handle_key(app, key);
    else discard_input(app, key);
}

static void refresh_observations(struct app *app, time_t *last_refresh) {
    time_t now = time(NULL);
    if (now - *last_refresh >= 2) {
        native_observations_tick(app,true);
        if (app->preview && !native_terminal_focused(app)) capture_preview(app);
        *last_refresh = now;
    } else native_observations_tick(app,false);
}

static bool terminal_ready(struct app *app) {
    const char *term = getenv("TERM");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("hydra-tui requires interactive stdin/stdout; use --headless-fixture for deterministic rendering\n", stderr);
        return false;
    }
    if (term == NULL || strcmp(term, "dumb") == 0) {
        fputs("hydra-tui cannot use TERM=dumb; run hydra tui --basic\n", stderr);
        return false;
    }
    update_size(app);
    if (app->cols < 40 || app->rows < 10) {
        fputs("hydra-tui requires at least 40 columns by 10 rows; run hydra tui --basic\n", stderr);
        return false;
    }
    return true;
}

int interactive_main(struct app *app) {
    time_t last_refresh;
    char key;
    if (!terminal_ready(app)) return 3;
    terminal_watch(app);
    copy_text(app->notice, sizeof(app->notice), "Loading snapshot...");

    refresh_current_session(app);
    if (enter_raw(app) != 0) return 4;
    native_observations_tick(app, true);
    capture_preview(app);
    last_refresh = time(NULL);
    app->running = true;
    while (app->running && !terminal_stopped()) {
        refresh_observations(app, &last_refresh);
        native_terminals_pump(app);
        update_size(app);
        render(app, 0U, false);
        if (read_key(app->terminals || app->observations ? 40 : 500, &key) <= 0) {
            native_terminal_flush_input(app);
            continue;
        }
        handle_input(app, key);
    }
    restore_terminal(app);
    return terminal_exit_status();
}
