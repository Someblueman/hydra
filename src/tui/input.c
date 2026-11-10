#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
/* One interaction model everywhere: Tab / Shift-Tab move between tabs (or
 * panes inside the workspace), arrows select, Enter opens, Esc steps back.
 * The loop delivers decoded events; input_event sends each to the layer that
 * owns it, and the views' single-character key handlers are reached through
 * one translation at the end of this file. */

static void handle_key(struct app *app, char key);

/* Views that load their own data when opened. */
static void enter_view_refresh(struct app *app, int view) {
    if (view == 9) native_attention_tick(app, true);
    if (view == 7) (void)native_workspace_init(app);
    if (view == 6) native_setup_hosts_refresh(app);
}

/* View changes go through here so refreshes and stale flags stay consistent. */
void enter_view(struct app *app, int view) {
    if (view < 0 || view > 9) return;
    if (view == 8) { if (app->view != 8) statistics_toggle(app); return; }
    if (app->view == 8 && app->statistics) app->statistics->detail = false;
    if (view != app->view) app->previous_view = app->view;
    if (view != 0 && view != 4 && view != 5) outline_release_run(app);
    app->view = view;
    app->diagnostics = false; app->help = false; app->result_open = false;
    if (view == 5) { app->graph_follow = true; if (!app->fleet) (void)refresh_workflows(app, NULL); }
    enter_view_refresh(app, view);
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
        case 0: case 4:
            if (item > MAX_HEADS) { app->run_row = true; app->workflow_run = item - MAX_HEADS - 1U; }
            else { app->run_row = false; app->selected = item; }
            break;
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

/* values are a report's button, column and row, the latter two counted from
 * one as the terminal sends them. */
static void handle_mouse(struct app *app, const struct kt_event *e) {
    unsigned values[3];
    size_t index;
    values[0] = e->button; values[1] = (unsigned)e->x + 1U; values[2] = (unsigned)e->y + 1U;
    if (app->cols != app->hit_cols || app->rows != app->hit_height ||
        app->view != app->hit_view || app->cols < 40 || app->rows < 10) return;
    if (values[1] >= (unsigned)app->cols || values[2] > (unsigned)app->rows) return;
    if (!e->release && tab_click(app, values)) return;
    if (app->view == 7 && !app->help && !app->diagnostics && !app->result_open) {
        native_workspace_mouse(app, e->button, e->x, e->y, e->release);
        return;
    }
    if (e->release) return;
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
/* PgUp/PgDn page read-only output: the result overlay or the Details
 * transcript (rows back from its newest output). */
static void page_output(struct app *app, bool up) {
    size_t *rows = app->result_open ? &app->result_scroll : app->view == 1 && app->preview ? &app->preview_back : NULL;
    size_t page = app->rows > 12 ? (size_t)app->rows / 2U : 6U;
    bool back = app->result_open ? !up : up;
    if (!rows) return;
    if (back) *rows += page;
    else *rows = *rows > page ? *rows - page : 0;
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
    if (app->view == 6) hosts_open_row(app);
    else if (app->view == 3) {
        if (app->model.recovery_count) recovery_check_action(app);
    } else if (app->view == 7) {
        /* Enter in a non-navigation pane has no target; navigation handles its own Enter. */
    } else if (app->view == 1 || app->view == 2) {
        copy_text(app->notice, sizeof(app->notice), "a talks to the agent, Esc goes back");
    } else if (app->run_row && (app->view == 0 || app->view == 4)) (void)outline_toggle(app, 0);
    else if (selected_head(app) != NULL) enter_view(app, 1);
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

/* Hosts: A adds a host, e / x edit or remove an unfinished setup; head
 * actions do not apply to host rows. */
static bool hosts_key(struct app *app, char key) {
    size_t record;
    if (key == 'A') { native_setup_open_form(app); return true; }
    if ((key == 'e' || key == 'x') && hosts_setup_selected(app, &record)) {
        if (key == 'e') native_setup_edit(app, record);
        else native_setup_remove(app, record);
        return true;
    }
    if (!key || !strchr("/:pac xGd", key)) return false;
    copy_text(app->notice, sizeof(app->notice), "Select a row and press Enter; A adds a host");
    return true;
}

static bool view_key(struct app *app, char key) {
    if (key == 27 && app->view == 9) { go_back(app); return true; }
    if (app->view == 9 && native_attention_key(app, key)) return true;
    if (key == 'D') { statistics_toggle(app); return true; }
    if (app->view == 8 && statistics_key(app, key)) return true;
    if (app->view == 7 && workspace_key(app, key)) return true;
    if (app->view == 5 && workflow_key(app, key)) return true;
    if (select_view(app, key)) return true;
    return app->view == 6 && hosts_key(app, key);
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
        case 'l': case 'h':
            if ((app->view == 0 || app->view == 4) && !outline_toggle(app, key == 'l' ? 1 : -1))
                copy_text(app->notice, sizeof(app->notice), "h and l collapse and expand a run; select a run row first");
            break;
        case '\r': case '\n': open_selected(app); break;
        case '/': case ':': interactive_prompt(app, key); break;
        case 'n': new_task_action(app); break;
        case 'a': attach_selected(app); break;
        case 'c': if (app->view == 1 && selected_head(app)) enter_view(app, 2); break;
        case 'p': if (app->view != 1) enter_view(app, 1); app->preview = !app->preview; app->preview_back = 0; capture_preview(app); break;
        case 'd': if (app->view != 3 && app->view != 1) enter_view(app, 1); app->diagnostics = !app->diagnostics; break;
        case ' ': toggle_mark(app); break;
        case 'A': select_all_visible(app); break;
        case 'x': remove_key_action(app); break;
        case 'G': group_key(app); break;
        case 't': cycle_theme(app); break;
        case '?': app->help = !app->help; break;
        case 27: go_back(app); break;
        default: break;
    }
}

static void handle_key(struct app *app, char key) {
    if (overlay_key(app, key)) return;
    if (key == 'q') { app->running = false; return; }
    if (view_key(app, key)) return;
    if (fleet_key(app, key)) return;
    head_key(app, key);
}

/* Any key that arrives as an escape sequence closes help and does nothing
 * else; over the result overlay only the keys that scroll it act. */
static bool overlay_takes(struct app *app, const struct kt_event *e) {
    bool scrolls = e->key == KT_KEY_UP || e->key == KT_KEY_DOWN || e->key == KT_KEY_PAGE_UP || e->key == KT_KEY_PAGE_DOWN;
    if (app->help) { app->help = false; return true; }
    return app->result_open && !scrolls;
}

static void arrow_key(struct app *app, uint32_t key) {
    switch (key) {
        case KT_KEY_UP: selection_arrow(app, 'A', -1); break;
        case KT_KEY_DOWN: selection_arrow(app, 'B', 1); break;
        case KT_KEY_RIGHT: horizontal_arrow(app, 1); break;
        case KT_KEY_LEFT: horizontal_arrow(app, -1); break;
        case KT_KEY_PAGE_UP: page_output(app, true); break;
        case KT_KEY_PAGE_DOWN: page_output(app, false); break;
        default: break;
    }
}

/* Keys that arrive as an escape sequence: arrows, pages and Shift-Tab. */
static void named_key(struct app *app, const struct kt_event *e) {
    if (overlay_takes(app, e)) return;
    /* The statistics view drops its notice on any key, these included. */
    if (app->view == 8 && app->statistics && !app->result_open) app->notice[0] = '\0';
    if (kt_event_is(e, KT_KEY_TAB, KT_MOD_SHIFT)) {
        if (app->view == 7) native_workspace_focus_previous(app);
        else select_tab(app, -1);
    } else if (!e->mods) arrow_key(app, e->key);
}

/* An escape sequence that stalled after "ESC [" must not turn into keys when
 * its tail arrives: single bytes are dropped up to and including the one that
 * would have ended the sequence. */
static bool sequence_tail(struct app *app, const struct kt_event *e) {
    unsigned char byte = (unsigned char)e->bytes[0];
    if (!app->sequence_tail) return false;
    if (e->length != 1) { app->sequence_tail = false; return false; }
    if (byte >= 0x40U && byte <= 0x7eU) app->sequence_tail = false;
    return true;
}

/* The single boundary between key events and the views' key handlers, which
 * take one character: a key that is one byte is that character; Escape and a
 * key in quick succession (reported as the key with Alt) are Escape, then the
 * key; arrows, Shift-Tab and the page keys keep their own actions. */
static void view_key_event(struct app *app, const struct kt_event *e) {
    if (sequence_tail(app, e)) return;
    if (e->length == 1) { handle_key(app, e->bytes[0]); return; }
    if (e->mods == KT_MOD_ALT && e->length == 2 && e->bytes[0] == '\033') {
        if (e->bytes[1] == '[') { app->sequence_tail = true; return; }
        handle_key(app, 27);
        if (app->running) handle_key(app, e->bytes[1]);
        return;
    }
    if (e->mods == KT_MOD_ALT && e->bytes[0] == '\033' && e->key < KT_KEY_UP) { handle_key(app, 27); return; }
    if (e->key >= KT_KEY_UP && e->key < KT_KEY_SEQUENCE) named_key(app, e);
}

/* A key or mouse event from the terminal, sent to the layer that owns it: an
 * agent pane holding input (or a chord of Hydra's own keys), Ctrl-C, the
 * remote setup flow, then the views. */
void input_event(struct app *app, const struct kt_event *e) {
    bool setup = native_setup_active(app) && app->view == 6;
    if (native_terminal_wants(app, e)) { native_terminal_event(app, e); return; }
    if (e->type == KT_MOUSE) { if (!setup) handle_mouse(app, e); return; }
    if (e->type != KT_KEY) return;
    if (kt_event_is(e, 'c', KT_MOD_CTRL)) { terminal_request_stop(SIGINT); return; }
    if (setup) { native_setup_event(app, e); return; }
    view_key_event(app, e);
}

/* Pasted text is data for whoever takes text: the agent, a setup field. To
 * the views it is nothing, so it can never run a command. */
void input_paste(struct app *app, const char *bytes, size_t length, bool truncated) {
    app->sequence_tail = false;
    if (native_terminal_paste(app, bytes, length)) {
        if (truncated) copy_text(app->notice, sizeof(app->notice), "Paste too long: only its beginning was sent");
        return;
    }
    if (native_setup_active(app) && app->view == 6) { native_setup_paste(app, bytes, length); return; }
    copy_text(app->notice, sizeof(app->notice), "bracketed paste ignored");
}
