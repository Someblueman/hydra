#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Work's actions from the binding table: talking to the selected agent,
 * moving through the list, the panes and the pages, scrolling, the planning
 * actions of the Plan page and the run controls. The keys that act on heads
 * (n, x, space, G, /, L, r, d) keep their handlers in input.c. */

/* Enter or a: talk to the selected head's agent. A run row expands or
 * collapses instead; a remote task has no terminal here. */
static void talk(struct app *app) {
    char refusal[TEXT];
    const struct head *h = selected_head(app);
    if (app->run_row) { (void)outline_toggle(app, 0); return; }
    if (selected_task(app)) { work_selection_notice(app, "A remote task has no terminal here; Y/N/R/X are its controls"); return; }
    if (!h) { work_selection_notice(app, "Select a head first; n starts a task"); return; }
    if (remote_action_refusal(app, h, refusal, sizeof(refusal))) { toast(app, KT_TOAST_ERROR, refusal); return; }
    if (head_headless(h)) { work_selection_notice(app, "A headless head has no terminal; its step output is in the conversation"); return; }
    if (native_terminal_attach(app)) native_workspace_show_terminal(app, true);
}

static void scroll_terminal(struct native_terminal *t, int rows) {
    size_t amount = (size_t)(rows < 0 ? -rows : rows);
    t->scrolling = true;
    if (rows < 0) t->scroll = t->scroll + amount > t->screen->history_count ? t->screen->history_count : t->scroll + amount;
    else t->scroll = t->scroll > amount ? t->scroll - amount : 0;
}

/* j / k and the page keys scroll the focused pane: the conversation's
 * output (or the agent's own history), the Details and Plan pages. */
static void scroll(struct app *app, int rows) {
    struct native_workspace *w = app->workspace;
    struct native_terminal *t = work_terminal(app);
    int page = work_focused_page(app);
    size_t *value;
    if (page == WORK_CONVERSATION && t && t->screen) { scroll_terminal(t, rows); return; }
    value = page == WORK_CONVERSATION ? &app->preview_back : &w->scroll[page < 0 ? WORK_DETAILS : page];
    if (page == WORK_CONVERSATION) rows = -rows;
    if (rows < 0) *value = (size_t)-rows > *value ? 0 : *value - (size_t)-rows;
    else *value += (size_t)rows;
}

/* A page of the focused text: the Run page pages through its evidence. */
static int page_rows(const struct app *app) {
    const struct native_workspace *w = app->workspace;
    if (w && work_focused_page(app) == WORK_RUN && w->evidence_rows > 1) return w->evidence_rows - 1;
    return app->rows > 12 ? app->rows / 2 : 6;
}

static void zoom(struct app *app) {
    struct native_workspace *w = app->workspace;
    w->zoom = !w->zoom;
    copy_text(app->notice, sizeof(app->notice), w->zoom ? "Focused pane fills the screen; z restores the panes" : "Panes restored");
}

/* The Plan page's actions, through plan_actions.c. A plan the agent
 * published but nobody opened yet is read and acted on in one step. */
static void plan_key(struct app *app, enum key_action action) {
    const struct head *h = selected_head(app);
    char branch[TEXT] = "", worker[TEXT];
    const char *run = work_plan_run(app);
    bool ready = work_head_plan_ready(app, h);
    if (h) copy_text(branch, sizeof(branch), h->branch);
    switch (action) {
    case KEY_PLAN_PROPOSAL: plan_action_load_proposal(app); break;
    case KEY_PLAN_VALIDATE: plan_action_validate(app); break;
    case KEY_PLAN_RETURN: if (ready) plan_action_return_published(app, branch); else plan_action_request_changes(app); break;
    case KEY_PLAN_EXECUTE: if (ready) plan_action_approve_published(app, branch); else plan_action_approve(app); break;
    case KEY_PLAN_LAND: if (work_land_worker(app, worker, sizeof(worker))) work_land(app); break;
    case KEY_PLAN_REVIEW: if (run) plan_action_review(app, run); break;
    case KEY_PLAN_STATUS: if (run) plan_action_status(app, run); break;
    default: break;
    }
}

/* Land the selected head's finished run (the palette's "land worker" too). */
void work_land(struct app *app) {
    char worker[TEXT];
    if (!work_land_worker(app, worker, sizeof(worker))) {
        work_selection_notice(app, "Nothing to land: select the planning head of a finished run whose worker is kept");
        return;
    }
    plan_action_land(app, worker);
    native_observations_tick(app, true);
}

static void list_move(struct app *app, int direction) {
    outline_move(app, direction, false);
    work_selection_changed(app);
}

static bool pane_perform(struct app *app, enum key_action action) {
    switch (action) {
    case KEY_WS_PANE_NEXT: work_pane_step(app, 1); return true;
    case KEY_WS_PANE_PREVIOUS: work_pane_step(app, -1); return true;
    case KEY_WORK_PAGE_PREVIOUS: work_page_step(app, -1); return true;
    case KEY_WORK_PAGE_NEXT: work_page_step(app, 1); return true;
    case KEY_WS_ZOOM: zoom(app); return true;
    case KEY_WS_SPLIT: native_workspace_split_agents(app); return true;
    case KEY_WS_UP: scroll(app, -1); return true;
    case KEY_WS_DOWN: scroll(app, 1); return true;
    case KEY_HEADS_PAGE_UP: case KEY_FLOW_PAGE_UP: scroll(app, -page_rows(app)); return true;
    case KEY_HEADS_PAGE_DOWN: case KEY_FLOW_PAGE_DOWN: scroll(app, page_rows(app)); return true;
    default: return false;
    }
}

static bool list_perform(struct app *app, enum key_action action) {
    switch (action) {
    case KEY_HEADS_UP: list_move(app, -1); return true;
    case KEY_HEADS_DOWN: list_move(app, 1); return true;
    case KEY_HEADS_OPEN: case KEY_HEADS_TALK: case KEY_WS_OPEN: talk(app); return true;
    case KEY_HEADS_COLLAPSE: case KEY_HEADS_EXPAND:
        if (!outline_toggle(app, action == KEY_HEADS_EXPAND ? 1 : -1))
            work_selection_notice(app, "Left and right collapse and expand a run; select a run first");
        work_selection_changed(app);
        return true;
    default: return false;
    }
}

/* The run keys: Y, N, R and X on a selected run or remote task. */
static void run_control(struct app *app, enum key_action action) {
    static const char keys[] = {'Y', 'N', 'R', 'X'};
    if (!native_control_key(app, keys[action - KEY_RUN_APPROVE]))
        work_selection_notice(app, "Y/N/R/X act on a selected run or remote task");
}

bool work_perform(struct app *app, enum key_action action) {
    static const char graph_keys[] = {'k', 'j', 'h', 'l', 'K', 'J', '\r'};
    if (app->view != 7 || !native_workspace_init(app)) return false;
    if (pane_perform(app, action) || list_perform(app, action)) return true;
    if (action >= KEY_PLAN_PROPOSAL && action <= KEY_PLAN_STATUS) plan_key(app, action);
    else if (action >= KEY_FLOW_UP && action <= KEY_FLOW_FOLLOW) (void)workflow_key(app, graph_keys[action - KEY_FLOW_UP]);
    else if (action >= KEY_RUN_APPROVE && action <= KEY_RUN_CANCEL) run_control(app, action);
    else return false;
    return true;
}

/* ---- what applies ---------------------------------------------------------- */

static bool plan_mine(const struct app *app, const struct head *h) {
    const struct native_plan *p = app->plan;
    return p && h && (!p->proposal_head[0] || !strcmp(p->proposal_head, h->branch));
}

static bool plan_applies(const struct app *app, enum key_action action) {
    const struct native_plan *p = app->plan;
    const struct head *h = selected_head((struct app *)app);
    char worker[TEXT];
    bool ready = work_head_plan_ready(app, h), mine = plan_mine(app, h);
    switch (action) {
    case KEY_PLAN_PROPOSAL: return h && !head_remote(h) && !(p && p->state == PLAN_VALIDATING);
    case KEY_PLAN_VALIDATE: return mine && p->state != PLAN_RETURNED && p->state != PLAN_VALIDATING;
    case KEY_PLAN_RETURN: return ready || (mine && p->state != PLAN_RETURNED);
    case KEY_PLAN_EXECUTE: return ready || (mine && p->state != PLAN_RETURNED && p->state != PLAN_VALIDATING);
    case KEY_PLAN_LAND: return work_land_worker(app, worker, sizeof(worker));
    default: return work_plan_run(app) != NULL;
    }
}

static bool run_controls_apply(const struct app *app, enum key_action action) {
    const char *state;
    size_t run = work_run(app);
    if (selected_task(app)) return true;
    if (run == SIZE_MAX) return false;
    state = app->workflows->runs[run].state;
    if (action == KEY_RUN_APPROVE || action == KEY_RUN_REJECT) {
        char request[80], step[80];
        return !strcmp(state, "waiting-approval") || !strcmp(state, "running") ||
            native_attention_run_request(app, app->workflows->runs[run].id, request, sizeof(request), step, sizeof(step));
    }
    return strcmp(state, "succeeded") && strcmp(state, "failed") && strcmp(state, "cancelled");
}

static bool list_applies(const struct app *app, enum key_action action) {
    const struct head *h = app->run_row ? NULL : selected_head((struct app *)app);
    if (action == KEY_HEADS_COLLAPSE || action == KEY_HEADS_EXPAND) return app->run_row || (h && head_owner_run(app, h, NULL) != SIZE_MAX);
    /* Enter on a run row expands it; a talks only to a head. */
    if (action == KEY_HEADS_OPEN) return app->run_row || (h && !head_headless(h));
    return h && !head_headless(h);
}

/* Which of Work's rows do something for the selection now; false when the
 * action is not one of them. */
bool work_applies(const struct app *app, enum key_action action, bool *applies) {
    if (action >= KEY_PLAN_PROPOSAL && action <= KEY_PLAN_STATUS) *applies = plan_applies(app, action);
    else if (action >= KEY_RUN_APPROVE && action <= KEY_RUN_CANCEL) *applies = run_controls_apply(app, action);
    else if (action == KEY_HEADS_COLLAPSE || action == KEY_HEADS_EXPAND || action == KEY_HEADS_OPEN ||
             action == KEY_HEADS_TALK || action == KEY_WS_OPEN) *applies = list_applies(app, action);
    else if (action == KEY_WS_SPLIT) *applies = app->terminals != NULL;
    else return false;
    return true;
}

/* The label of the Enter row: talk to a head, expand or collapse a run. */
const char *work_open_label(const struct app *app) {
    if (app->run_row && app->workflows && app->workflow_run < app->workflows->run_count)
        return run_expanded(app, app->workflow_run) ? "collapse" : "expand";
    return "talk";
}

/* ---- the mouse ------------------------------------------------------------- */

static bool select_row(struct app *app, int x, int y) {
    size_t i;
    for (i = 0; i < app->hit_count; i++) {
        size_t item = app->hit_items[i];
        if (y + 1 != app->hit_rows[i] || x + 1 < app->hit_left[i] || x + 1 > app->hit_right[i]) continue;
        if (item > MAX_HEADS + WF_RUNS) { app->task_row = true; app->run_row = false; app->task_selected = item - MAX_HEADS - 1U - WF_RUNS; }
        else if (item > MAX_HEADS) { app->run_row = true; app->task_row = false; app->workflow_run = item - MAX_HEADS - 1U; }
        else { app->run_row = false; app->task_row = false; app->selected = item; }
        work_selection_changed(app);
        return true;
    }
    return false;
}

/* A click selects a row, a page or focuses a pane; the wheel scrolls the
 * pane under the pointer; the dividers drag. A click never changes work. */
/* A press or wheel turn on a pane focuses it: a press on a list row selects
 * the row, the wheel moves the list or scrolls the pane. */
static void pane_press(struct app *app, unsigned button, int x, int y) {
    struct native_workspace *w = app->workspace;
    if (!kt_workspace_press(&w->layout, x, y) || w->layout.drag >= 0) return;
    native_workspace_sync_terminal(app);
    if (button == 0) { if (w->layout.focus == WORK_PANE_LIST) (void)select_row(app, x, y); return; }
    if (w->layout.focus == WORK_PANE_LIST) list_move(app, button == 64 ? -1 : 1);
    else scroll(app, button == 64 ? -3 : 3);
}

void work_mouse(struct app *app, unsigned button, int x, int y, bool release) {
    struct native_workspace *w = app->workspace;
    if (!w) return;
    if (release) { kt_workspace_release(&w->layout); return; }
    if (button == 32) { if (kt_workspace_motion(&w->layout, x, y)) w->dragged = true; return; }
    if (button != 0 && button != 64 && button != 65) return;
    if (button == 0 && work_page_click(app, x, y)) return;
    pane_press(app, button, x, y);
}
