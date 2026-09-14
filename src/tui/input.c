#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* One interaction model everywhere: Tab / Shift-Tab move between tabs (or
 * panes inside the workspace), arrows select, Enter opens, Esc steps back. */

static void handle_key(struct app *app, char key);

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

/* View changes go through here so refreshes and stale flags stay consistent. */
void enter_view(struct app *app, int view) {
    if (view < 0 || view > 9) return;
    if (view == 8) { if (app->view != 8) statistics_toggle(app); return; }
    if (app->view == 8 && app->statistics) app->statistics->detail = false;
    if (view != app->view) app->previous_view = app->view;
    app->view = view;
    app->diagnostics = false; app->help = false; app->result_open = false;
    if (view == 5) { app->graph_follow = true; if (!app->fleet) (void)refresh_workflows(app, NULL); }
    if (view == 9) native_attention_tick(app, true);
    if (view == 7) (void)native_workspace_init(app);
}

void select_tab(struct app *app, int direction) {
    int views[12];
    size_t count = tab_order(app, views), i, current = 0;
    for (i = 0; i < count; i++) if (views[i] == app->view) current = i;
    if (direction > 0) current = (current + 1) % count;
    else if (direction < 0) current = (current + count - 1) % count;
    enter_view(app, views[current]);
}

static void select_tab_index(struct app *app, size_t index) {
    int views[12];
    size_t count = tab_order(app, views);
    if (index < count) enter_view(app, views[index]);
}

static void go_back_view(struct app *app) {
    struct native_workspace *w = app->workspace;
    switch (app->view) {
        case 1:
            app->diagnostics = false;
            if (app->previous_view != 7) { app->search[0] = '\0'; }
            enter_view(app, app->previous_view == 7 ? 7 : 0);
            return;
        case 2: enter_view(app, 1); return;
        case 7:
            if (w && w->layout.focus != 1) { w->layout.focus = 1; return; }
            enter_view(app, 0); return;
        case 0:
            if (app->search[0]) { app->search[0] = '\0'; retarget_selection(app); }
            return;
        default: enter_view(app, app->previous_view == 7 ? 7 : 0); return;
    }
}

void go_back(struct app *app) {
    app->notice[0] = '\0';
    if (app->result_open) { app->result_open = false; return; }
    if (app->help) { app->help = false; return; }
    if (app->view == 9) { (void)native_attention_key(app, 27); return; }
    if (statistics_back(app)) return;
    /* Recovery keeps the finding explanation as an inner Esc layer; Details
     * closes diagnostics and returns to the list in one step. */
    if (app->diagnostics && app->view == 3) { app->diagnostics = false; return; }
    if (app->preview) { app->preview = false; return; }
    go_back_view(app);
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

static bool tab_click(struct app *app, const unsigned values[3]) {
    size_t index;
    if (values[0] != 0U || values[2] != 2U || app->help || app->result_open) return false;
    for (index = 0U; index < app->tab_count; index++) {
        if ((int)values[1] - 1 >= app->tab_left[index] && (int)values[1] - 1 <= app->tab_right[index]) {
            enter_view(app, app->tab_view[index]);
            return true;
        }
    }
    return false;
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
    if (*cursor == 'M' && tab_click(app, values)) return;
    if (app->view == 7 && !app->help && !app->diagnostics && !app->result_open) {
        native_workspace_mouse(app, values[0], (int)values[1] - 1, (int)values[2] - 1, *cursor == 'm');
        return;
    }
    if (*cursor == 'm') return;
    if (app->view == 8 && !app->help && !app->diagnostics && !app->result_open) {
        statistics_mouse(app, values);
        return;
    }
    if (app->help || app->diagnostics || app->result_open || values[1] < 3U ||
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

static void horizontal_arrow(struct app *app, int direction) {
    if (app->result_open || app->help) return;
    if (app->view == 7) { (void)native_workspace_key(app, direction > 0 ? 'l' : 'h'); return; }
    if (app->view == 5) { (void)workflow_key(app, direction > 0 ? 'l' : 'h'); return; }
    select_tab(app, direction);
}
static void selection_arrow(struct app *app, char key, int direction) {
    if (app->result_open) { if (direction < 0 && app->result_scroll) app->result_scroll--; else if (direction > 0) app->result_scroll++; return; }
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
    else if (strcmp(sequence, "C") == 0) horizontal_arrow(app, 1);
    else if (strcmp(sequence, "D") == 0) horizontal_arrow(app, -1);
    else if (strcmp(sequence, "Z") == 0) {
        if (app->view == 7 && !app->help && !app->result_open) native_workspace_focus_previous(app);
        else if (!app->help && !app->result_open) select_tab(app, -1);
    }
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
    if (read_key(20, &ch) <= 0) { go_back(app); return; }
    if (ch != '[') {
        /* A bare Escape acts immediately; a key that follows within the escape
         * window (Esc then q, or Esc then H) is still handled, never dropped. */
        if (app->view == 9) (void)native_attention_key(app, 27);
        else go_back(app);
        handle_key(app, ch);
        return;
    }
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
        /* A heads search filters the list; show it rather than a stale detail. */
        if (app->view == 1 || app->view == 2) app->view = 0;
        retarget_selection(app);
    }
    else execute_palette(app, query);
}

static bool fleet_key(struct app *app, char key) {
    if (!app->fleet) return false;
    if (app->view == 4 && native_control_key(app, key)) return true;
    switch (key) {
        case 'a':
            if (native_workspace_init(app) && native_terminal_attach(app)) {
                enter_view(app, 7); native_workspace_show_terminal(app, true);
            }
            return true;
        case 'c': fleet_action(app); return true;
        case ':': case 'p': case ' ': case 'A': case 'x': case 'G': case 'n':
            copy_text(app->notice, sizeof(app->notice), "Remote heads: a attach, c interrupt, / search; tasks start on their host"); return true;
        default: return false;
    }
}

static void open_selected(struct app *app) {
    if (app->view == 6 && app->host_selected < app->model.host_count) {
        copy_text(app->search, sizeof(app->search), app->model.hosts[app->host_selected].name);
        enter_view(app, 0); retarget_selection(app);
    } else if (app->view == 3) {
        if (app->model.recovery_count) recovery_check_action(app);
    } else if (app->view == 7) {
        /* Enter in a non-navigation pane has no target; navigation handles its own Enter. */
    } else if (app->view == 1 || app->view == 2) {
        copy_text(app->notice, sizeof(app->notice), "a talks to the agent, Esc goes back");
    } else if (selected_head(app) != NULL) enter_view(app, 1);
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
        case 'v': select_tab(app, 1); return true;
        case '\t': if (app->view != 7) { select_tab(app, 1); return true; } return false;
        case 'I': enter_view(app, 9); return true;
        case 'W': enter_view(app, 7); return true;
        case 'o': enter_view(app, 4); return true;
        case 'H': enter_view(app, 6); return true;
        case 'w': enter_view(app, 5); return true;
        default: break;
    }
    if (key >= '1' && key <= '9') { select_tab_index(app, (size_t)(key - '1')); return true; }
    return false;
}

static bool overlay_key(struct app *app, char key) {
    if (app->result_open) {
        if (key == '\r' || key == '\n' || key == ' ' || key == 27) { app->result_open = false; app->notice[0] = '\0'; }
        else if (key == 'j') app->result_scroll++;
        else if (key == 'k' && app->result_scroll) app->result_scroll--;
        else if (key == 'q') app->running = false;
        return true;
    }
    if (app->help) {
        app->help = false;
        return key == '?' || key == 27;
    }
    return false;
}

static bool view_key(struct app *app, char key) {
    if (key == 27 && app->view == 9) { handle_escape(app); return true; }
    if (app->view == 9 && native_attention_key(app, key)) return true;
    if (key == 'D') { statistics_toggle(app); return true; }
    if (app->view == 8 && statistics_key(app, key)) return true;
    if (app->view == 7 && workspace_key(app, key)) return true;
    if (app->view == 5 && workflow_key(app, key)) return true;
    if (select_view(app, key)) return true;
    if (app->view == 6 && key && strchr("/:pac AxGd", key)) {
        copy_text(app->notice, sizeof(app->notice), "Select a host and press Enter to see its heads");
        return true;
    }
    return false;
}

static void attach_selected(struct app *app) {
    if (native_workspace_init(app) && selected_head(app)) {
        enter_view(app, 7);
        if (native_terminal_attach(app)) native_workspace_show_terminal(app, true);
    } else copy_text(app->notice, sizeof(app->notice), "Select a head first");
}

static void group_key(struct app *app) {
    if (app->marked_count > 0U) group_marked_action(app);
    else copy_text(app->notice, sizeof(app->notice), "Mark heads with Space first, then G groups them");
}

static void cycle_theme(struct app *app) {
    app->theme = (app->theme + 1) % 3;
    snprintf(app->notice, sizeof(app->notice), "Theme: %s%s", theme_name(app->theme), app->no_color ? " (NO_COLOR)" : "");
}

/* Keys that act on the selected or marked heads in the list views. */
static void head_key(struct app *app, char key) {
    switch (key) {
        case 'j': move_selection(app, 1); break;
        case 'k': move_selection(app, -1); break;
        case '\r': case '\n': open_selected(app); break;
        case '/': case ':': interactive_prompt(app, key); break;
        case 'n': new_task_action(app); break;
        case 'a': attach_selected(app); break;
        case 'c': if (app->view == 1 && selected_head(app)) enter_view(app, 2); break;
        case 'p': if (app->view != 1) enter_view(app, 1); app->preview = !app->preview; capture_preview(app); break;
        case 'd': if (app->view != 3 && app->view != 1) enter_view(app, 1); app->diagnostics = !app->diagnostics; break;
        case ' ': toggle_mark(app); break;
        case 'A': select_all_visible(app); break;
        case 'x': remove_heads_action(app); break;
        case 'G': group_key(app); break;
        case 't': cycle_theme(app); break;
        case '?': app->help = !app->help; break;
        case 27: handle_escape(app); break;
        default: break;
    }
}

static void handle_key(struct app *app, char key) {
    if (native_terminal_byte(app, (unsigned char)key)) return;
    if (key == 3) { terminal_request_stop(SIGINT); return; }
    if (overlay_key(app, key)) return;
    if (key == 'q') { app->running = false; return; }
    if (view_key(app, key)) return;
    if (fleet_key(app, key)) return;
    head_key(app, key);
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
