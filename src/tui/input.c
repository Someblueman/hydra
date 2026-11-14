#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
#include "attention.h"
#include "inbox.h"
/* One interaction model everywhere: 1-4 change screens, Tab moves between
 * the panes of a screen, arrows select, Enter opens, Esc steps back. The
 * loop delivers decoded events; input_event sends each to the layer that
 * owns it. Keys for the screens go through the binding table (bindings.c):
 * a key acts only when the focused chain binds it, and the action reaches
 * its handler directly (work_perform, inbox_perform, statistics_perform) or
 * as the key it stands for (input_character and input_named). */

/* Screens that load their own data when opened. */
static void enter_view_refresh(struct app *app, int view) {
    if (view == 9) {
        native_attention_tick(app, true);
        if (app->inbox) app->inbox->jumped = false;
    }
    if (view == 7) {
        (void)native_workspace_init(app);
        native_observations_request(app, OBSERVE_WORKFLOWS);
        capture_preview(app);
    }
    if (view == 6) native_setup_hosts_refresh(app);
}

/* The views of earlier versions are pages of Work now: Details and
 * Coordination the Details page, Overview and Workflows the Run page;
 * Recovery is Inbox. */
static int view_page(int *view) {
    static const int pages[] = {-1, WORK_DETAILS, WORK_DETAILS, -1, WORK_RUN, WORK_RUN};
    int page;
    if (*view == 3) { *view = 9; return -1; }
    if (*view < 0 || *view > 5) return -1;
    page = pages[*view];
    *view = 7;
    return page;
}

/* Screen changes go through here so refreshes and stale flags stay consistent. */
void enter_view(struct app *app, int view) {
    int page = view_page(&view);
    if (view < 6 || view > 9) return;
    if (view == 8) { if (app->view != 8) statistics_toggle(app); return; }
    if (app->view == 8 && app->statistics) app->statistics->detail = false;
    if (view != app->view) app->previous_view = app->view;
    app->view = view;
    app->diagnostics = false; app->help = false; app->result_open = false;
    enter_view_refresh(app, view);
    if (page >= 0) {
        if (page == WORK_RUN && !work_page_available(app, WORK_RUN) && overview_run(app) != SIZE_MAX) { app->run_row = true; app->workflow_run = overview_run(app); }
        work_set_page(app, page);
    }
}

/* The four screens in tab order: Work, Inbox, Hosts, Stats. */
static const int screens[] = {7, 9, 6, 8};

size_t tab_order(const struct app *app, int *views) {
    size_t i;
    (void)app;
    for (i = 0; i < 4; i++) views[i] = screens[i];
    return 4;
}

void enter_screen(struct app *app, int index) {
    if (index < 0 || index >= 4) return;
    app->jump_back = 0;
    enter_view(app, screens[index]);
}

void select_tab(struct app *app, int direction) {
    int views[12];
    size_t count = tab_order(app, views), i, current = 0;
    for (i = 0; i < count; i++) if (views[i] == app->view) current = i;
    if (direction > 0) current = (current + 1) % count;
    else if (direction < 0) current = (current + count - 1) % count;
    enter_view(app, views[current]);
}

/* Esc after a screen's own steps: back to the screen a jump came from (o
 * from Inbox, g from Stats, the letters I, H and W), or nothing. */
static void jump_return(struct app *app) {
    int back = app->jump_back - 1;
    if (app->view == 7 && app->inbox && app->inbox->jumped) { app->inbox->jumped = false; enter_view(app, 9); return; }
    if (app->view == 7 && app->statistics && (app->statistics->graph_open || app->statistics->head_open)) {
        app->statistics->graph_open = app->statistics->head_open = false;
        app->view = 8;
        return;
    }
    app->jump_back = 0;
    if (back >= 0 && back != app->view) enter_view(app, back);
}

/* A letter that opens a screen is a jump: Esc there comes back. */
static void jump_to(struct app *app, int view) {
    if (view == app->view) return;
    app->jump_back = app->view + 1;
    enter_view(app, view);
}

void go_back(struct app *app) {
    app->notice[0] = '\0';
    if (app->result_open) { app->result_open = false; return; }
    if (app->help) { app->help = false; return; }
    if (app->view == 9 && inbox_back(app)) return;
    if (statistics_back(app)) return;
    if (app->view == 7 && work_back(app)) return;
    jump_return(app);
}

/* SGR mouse reports borrow the last rendered frame's hit map. No mutations. */
static bool mouse_hit(const struct app *app, const unsigned values[3], size_t index) {
    return values[2] >= (unsigned)app->hit_rows[index] && values[2] <= (unsigned)app->hit_bottom[index] &&
        values[1] >= (unsigned)app->hit_left[index] && values[1] <= (unsigned)app->hit_right[index];
}

static void select_mouse_item(struct app *app, size_t item) {
    if (app->view == 6) app->host_selected = item;
}

static void statistics_mouse(struct app *app, const unsigned values[3]) {
    size_t index;
    for (index = 0; index < app->hit_count; index++) {
        if (!mouse_hit(app, values, index)) continue;
        if (values[0] == 64 || values[0] == 65) statistics_move(app, values[0] == 64 ? -1 : 1);
        else if (values[0] == 0) {
            statistics_select(app, app->hit_items[index]);
        }
        return;
    }
}

static bool tab_click(struct app *app, const unsigned values[3]) {
    size_t index;
    if (values[0] != 0U || values[2] != 1U || app->help || app->result_open) return false;
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
    if (app->view == 7 && !app->help && !app->result_open) {
        work_mouse(app, e->button, e->x, e->y, e->release);
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
        if (app->view == 9) { inbox_click(app, app->hit_items[index], values[0]); return; }
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
    select_tab(app, direction);
}
/* Up and Down select; Inbox and Work bind them as their own actions. */
static void selection_arrow(struct app *app, int direction) {
    if (app->result_open) { if (direction < 0 && app->result_scroll) app->result_scroll--; else if (direction > 0) app->result_scroll++; return; }
    if (app->help) { help_scroll(app, direction); return; }
    move_selection(app, direction);
}
/* PgUp/PgDn page the output viewer. */
static void page_output(struct app *app, bool up) {
    size_t page = app->rows > 12 ? (size_t)app->rows / 2U : 6U;
    if (app->help) { help_scroll(app, up ? -(int)page : (int)page); return; }
    if (!app->result_open) return;
    if (!up) app->result_scroll += page;
    else app->result_scroll = app->result_scroll > page ? app->result_scroll - page : 0;
}

/* / filters the Work list as you type. */
static void interactive_prompt(struct app *app, char prefix) {
    char query[TEXT];
    if (prefix != '/') { palette_open(app); return; }
    if (prompt_text(app, "Filter heads: ", query, sizeof(query)) != 0) return;
    app->notice[0] = '\0';
    copy_text(app->search, sizeof(app->search), query);
    retarget_selection(app);
    work_selection_changed(app);
}

static bool select_view(struct app *app, char key) {
    switch (key) {
        case '\t': if (app->view == 6) { select_tab(app, 1); return true; } return false;
        case 'I': jump_to(app, 9); return true;
        case 'W': jump_to(app, 7); return true;
        case 'H': jump_to(app, 6); return true;
        default: return false;
    }
}

static void result_key(struct app *app, char key) {
    if (key == '\r' || key == '\n' || key == ' ' || key == 27) { app->result_open = false; app->notice[0] = '\0'; }
    else if (key == 'j') app->result_scroll++;
    else if (key == 'k' && app->result_scroll) app->result_scroll--;
    else if (key == 'q') app->running = false;
}

/* j / k scroll help; any other key closes it, and acts unless it was the
 * help key or Esc. */
static bool help_key(struct app *app, char key) {
    if (key == 'j' || key == 'k') { help_scroll(app, key == 'j' ? 1 : -1); return true; }
    app->help = false; app->help_scroll = 0;
    return key == '?' || key == 27;
}

static bool overlay_key(struct app *app, char key) {
    if (app->result_open) { result_key(app, key); return true; }
    return app->help && help_key(app, key);
}

/* Hosts: A adds a host, O opens onboarding, r retries one, e / x edit or
 * remove an unfinished setup; head actions do not apply to host rows. */
static bool hosts_key(struct app *app, char key) {
    size_t record;
    if (key == 'A') { native_setup_open_form(app); return true; }
    if (key == 'O') { native_onboarding_open(app); return true; }
    if (key == 'r') { hosts_retry_row(app); return true; }
    if ((key == 'e' || key == 'x') && hosts_setup_selected(app, &record)) {
        if (key == 'e') native_setup_edit(app, record);
        else native_setup_remove(app, record);
        return true;
    }
    if (!key || !strchr("/pac xGd", key)) return false;
    copy_text(app->notice, sizeof(app->notice), "Select a row and press Enter; A adds a host, O opens onboarding");
    return true;
}

static bool view_key(struct app *app, char key) {
    if (key == 'D') { statistics_toggle(app); return true; }
    if (select_view(app, key)) return true;
    return app->view == 6 && hosts_key(app, key);
}

/* x: a local head is removed; a remote head's agent is interrupted on its host. */
static void remove_or_interrupt(struct app *app) {
    const struct head *head = selected_head(app);
    if (head && head_remote(head)) { fleet_action(app); return; }
    if (selected_task(app)) { copy_text(app->notice, sizeof(app->notice), "Remote task: X cancels it through its receiver; x applies to heads"); return; }
    remove_key_action(app);
}

/* r: a fresh observation of the selected remote item's host. */
static void retry_selected(struct app *app) {
    const struct head *head = selected_head(app);
    const struct task_observation *task = selected_task(app);
    if (head && head_remote(head)) remote_retry(app, head->remote_host);
    else if (task) remote_retry(app, task->host);
    else if (app->model.host_count || app->remote_stale) remote_retry(app, NULL);
    else copy_text(app->notice, sizeof(app->notice), "r retries a remote host; select remote work or a host first");
}

static void group_key(struct app *app) {
    if (app->marked_count > 0U) group_marked_action(app);
    else copy_text(app->notice, sizeof(app->notice), "Mark heads with Space first, then G groups them");
}

static void cycle_theme(struct app *app) {
    app->theme = (app->theme + 1) % 3;
    snprintf(app->notice, sizeof(app->notice), "Theme: %s%s", theme_name(app->theme), app->no_color ? " (NO_COLOR)" : "");
}

/* d: the technical details of the selected head, on the Details page. */
static void technical(struct app *app) {
    app->diagnostics = !app->diagnostics;
    if (app->diagnostics) work_set_page(app, WORK_DETAILS);
}

/* Keys that act on the selected or marked heads in Work. */
static void head_key(struct app *app, char key) {
    switch (key) {
        case 'j': move_selection(app, 1); break;
        case 'k': move_selection(app, -1); break;
        case '\r': case '\n': if (app->view == 6) hosts_open_row(app); break;
        case '/': case ':': interactive_prompt(app, key); break;
        case 'n': new_task_action(app); break;
        case 'L': location_cycle(app); work_selection_changed(app); break;
        case 'r': retry_selected(app); break;
        case 'd': technical(app); break;
        case ' ': toggle_mark(app); break;
        case 'A': select_all_visible(app); break;
        case 'x': remove_or_interrupt(app); break;
        case 'G': group_key(app); break;
        case 't': cycle_theme(app); break;
        case '?': app->help = !app->help; app->help_scroll = 0; break;
        case 27: go_back(app); break;
        default: break;
    }
}

void input_character(struct app *app, char key) {
    if (overlay_key(app, key)) return;
    if (key == 'q') { app->running = false; return; }
    if (view_key(app, key)) return;
    head_key(app, key);
}

/* Any key that arrives as an escape sequence closes help and does nothing
 * else; over the result overlay only the keys that scroll it act. */
static bool overlay_takes(struct app *app, const struct kt_event *e) {
    bool scrolls = e->key == KT_KEY_UP || e->key == KT_KEY_DOWN || e->key == KT_KEY_PAGE_UP || e->key == KT_KEY_PAGE_DOWN;
    if (app->help && !(scrolls && !e->mods)) { app->help = false; app->help_scroll = 0; return true; }
    return app->result_open && !scrolls;
}

/* Fixture rendering replays keys without a terminal, each decoded as the
 * terminal's byte would be (a lone Escape is Escape), through the same path
 * as interactive input, so a fixture screen is the screen those keys reach. */
void headless_key(struct app *app, char key) {
    struct kt_input decoder;
    struct kt_event e;
    kt_input_init(&decoder);
    if (kt_input_feed(&decoder, (unsigned char)key, &e) || kt_input_flush(&decoder, &e)) input_event(app, &e);
}

static void arrow_key(struct app *app, uint32_t key) {
    switch (key) {
        case KT_KEY_UP: selection_arrow(app, -1); break;
        case KT_KEY_DOWN: selection_arrow(app, 1); break;
        case KT_KEY_RIGHT: horizontal_arrow(app, 1); break;
        case KT_KEY_LEFT: horizontal_arrow(app, -1); break;
        case KT_KEY_PAGE_UP: page_output(app, true); break;
        case KT_KEY_PAGE_DOWN: page_output(app, false); break;
        default: break;
    }
}

/* Keys that arrive as an escape sequence: arrows, pages and Shift-Tab. */
void input_named(struct app *app, uint32_t key, unsigned mods) {
    struct kt_event e;
    memset(&e, 0, sizeof(e));
    e.type = KT_KEY; e.key = key; e.mods = mods;
    if (overlay_takes(app, &e)) return;
    /* The statistics view drops its notice on any key, these included. */
    if (app->view == 8 && app->statistics && !app->result_open) app->notice[0] = '\0';
    if (kt_event_is(&e, KT_KEY_TAB, KT_MOD_SHIFT)) select_tab(app, -1);
    else if (!mods) arrow_key(app, key);
}

/* One key for the binding table: a single character as itself, Escape as
 * Escape. */
static void dispatch_byte(struct app *app, char byte) {
    struct kt_event e;
    memset(&e, 0, sizeof(e));
    e.type = KT_KEY; e.bytes[0] = byte; e.length = 1;
    e.key = byte == 27 ? KT_KEY_ESCAPE : (uint32_t)(unsigned char)byte;
    (void)key_dispatch(app, &e);
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
    if (e->mods == KT_MOD_ALT && e->length == 2 && e->bytes[0] == '\033') {
        if (e->bytes[1] == '[') { app->sequence_tail = true; return; }
        dispatch_byte(app, 27);
        if (app->running) dispatch_byte(app, e->bytes[1]);
        return;
    }
    if (e->mods == KT_MOD_ALT && e->bytes[0] == '\033' && e->key < KT_KEY_UP) { dispatch_byte(app, 27); return; }
    if (app->view == 8 && statistics_filter_event(app, e)) return;
    if (e->key < KT_KEY_SEQUENCE) (void)key_dispatch(app, e);
}

/* A key or mouse event from the terminal, sent to the layer that owns it: an
 * agent pane holding input (or a chord of Hydra's own keys), Ctrl-C, the
 * remote setup flow or the onboarding view, then the views. */
void input_event(struct app *app, const struct kt_event *e) {
    bool setup = native_setup_active(app) && app->view == 6;
    bool onboarding = !setup && native_onboarding_active(app) && app->view == 6;
    if (native_terminal_wants(app, e)) { native_terminal_event(app, e); return; }
    if (e->type == KT_MOUSE) { if (!setup && !onboarding) handle_mouse(app, e); return; }
    if (e->type != KT_KEY) return;
    if (kt_event_is(e, 'c', KT_MOD_CTRL)) { terminal_request_stop(SIGINT); return; }
    if (setup) { native_setup_event(app, e); return; }
    if (onboarding) { (void)native_onboarding_event(app, e); return; }
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
    if (native_onboarding_active(app) && app->view == 6) {
        struct kt_event e;
        memset(&e, 0, sizeof(e));
        e.type = KT_PASTE;
        e.length = length < sizeof(e.bytes) ? length : sizeof(e.bytes);
        memcpy(e.bytes, bytes, e.length);
        (void)native_onboarding_event(app, &e);
        return;
    }
    if (statistics_paste(app, bytes, length)) return;
    copy_text(app->notice, sizeof(app->notice), "bracketed paste ignored");
}
