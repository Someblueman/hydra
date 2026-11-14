#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "attention.h"
#include "inbox.h"
/* Work: pick a piece of work, see what its agent is doing, talk to it, and
 * review its plan or run without leaving the list. The list is on the left,
 * the selected head's conversation in the centre and a side pane of pages
 * (Details, Plan, Run) on the right. Below 120 columns the conversation and
 * the side pane share one pane whose pages include the conversation; below
 * 70 columns one pane fills the screen and Tab changes it. Tab moves focus
 * between panes and [ ] change pages: there are no layouts to choose. */

#define WORK_WIDE 120
#define WORK_COMPACT_WIDTH 70
#define WORK_COMPACT_HEIGHT 12

/* One pane at a time below 70x12; the conversation and the side pane share
 * one below 120 columns. */
static void frame_sizes(const struct app *app, struct native_workspace *w) {
    w->compact = app->cols < WORK_COMPACT_WIDTH || app->rows < WORK_COMPACT_HEIGHT;
    w->merged = !w->compact && app->cols < WORK_WIDE;
}

bool native_workspace_init(struct app *app) {
    struct native_workspace *w;
    if (app->workspace) return true;
    w = calloc(1, sizeof(*w));
    if (!w) return false;
    kt_workspace_init(&w->layout, 8, 3);
    (void)kt_workspace_split(&w->layout, 0, KT_COLUMNS, 1);
    (void)kt_workspace_split(&w->layout, 2, KT_COLUMNS, 999);
    w->layout.focus = WORK_PANE_LIST;
    w->chosen = -1;
    w->theme = app->theme;
    /* Keys replayed before the first frame see the frame they will get. */
    frame_sizes(app, w);
    app->workspace = w;
    return true;
}

void native_workspace_destroy(struct app *app) {
    free(app->workspace);
    app->workspace = NULL;
}

/* ---- the selection and its pages ----------------------------------------- */

/* The selected row's identity: a head, a run or a remote task. */
static void selection_key(const struct app *app, char *out, size_t size) {
    const struct head *h = selected_head((struct app *)app);
    const struct task_observation *task = selected_task(app);
    if (app->run_row && app->workflows && app->workflow_run < app->workflows->run_count)
        snprintf(out, size, "run %s", app->workflows->runs[app->workflow_run].id);
    else if (task) snprintf(out, size, "task %.100s %.100s", task->host, task->task_id);
    else if (h) snprintf(out, size, "head %.200s", h->branch);
    else out[0] = '\0';
}

/* The run the Run page shows: the selected run, or the run of a selected run head. */
size_t work_run(const struct app *app) {
    const struct head *h;
    if (!app->workflows || !app->workflows->run_count) return SIZE_MAX;
    if (app->run_row) return app->workflow_run < app->workflows->run_count ? app->workflow_run : SIZE_MAX;
    h = selected_head((struct app *)app);
    return h ? head_owner_run(app, h, NULL) : SIZE_MAX;
}

/* A plan for the selected head: the one loaded for it, one its agent
 * published (Inbox lists it as Plan ready), or a run it launched. */
bool work_head_plan(const struct app *app, const struct head *h) {
    size_t i, index;
    if (!h || head_remote(h) || app->fleet) return false;
    if (app->plan && (!app->plan->proposal_head[0] || !strcmp(app->plan->proposal_head, h->branch))) return true;
    for (i = 0; app->inbox && i < app->inbox->plan_count; i++) if (!strcmp(app->inbox->plans[i].branch, h->branch)) return true;
    index = (size_t)(h - app->model.heads);
    for (i = 0; app->workflows && i < app->workflows->run_count; i++) if (run_owner_head(app, i) == index) return true;
    return false;
}

/* A plan imported from files belongs to no head: with no head selected
 * (none yet, or the list is empty) Work shows it on its Plan page. */
static bool unowned_plan(const struct app *app, const struct head *h) {
    return !h && !app->task_row && app->plan && !app->plan->proposal_head[0] && !app->fleet;
}

bool work_page_available(const struct app *app, int page) {
    const struct head *h = selected_head((struct app *)app);
    switch (page) {
    case WORK_CONVERSATION: case WORK_DETAILS: return true;
    case WORK_PLAN: return !app->run_row && (work_head_plan(app, h) || unowned_plan(app, h));
    case WORK_RUN: return work_run(app) != SIZE_MAX;
    default: return false;
    }
}

/* Run for a run, Plan for a head with a plan, else Details; the shared pane
 * of a narrow screen opens on the conversation of a head. */
static int default_page(const struct app *app, bool side) {
    if (app->run_row && work_page_available(app, WORK_RUN)) return WORK_RUN;
    if (unowned_plan(app, selected_head((struct app *)app))) return WORK_PLAN;
    if (!side) return WORK_CONVERSATION;
    if (work_page_available(app, WORK_PLAN)) return WORK_PLAN;
    if (work_page_available(app, WORK_RUN)) return WORK_RUN;
    return WORK_DETAILS;
}

/* The page shown in the side pane (side) or in the shared pane. A choice
 * holds for the selection it was made for, while that page applies. */
int work_page_shown(const struct app *app, bool side) {
    const struct native_workspace *w = app->workspace;
    char key[TEXT + 32];
    selection_key(app, key, sizeof(key));
    /* An empty chosen_for is a page asked for at start (--view): it holds
     * until the selection moves. */
    if (w && w->chosen >= 0 && (!w->chosen_for[0] || !strcmp(key, w->chosen_for)) && work_page_available(app, w->chosen) &&
        !(side && w->chosen == WORK_CONVERSATION)) return w->chosen;
    return default_page(app, side);
}

static bool work_merged(const struct app *app) {
    return app->workspace ? app->workspace->merged || app->workspace->compact : app->cols < WORK_WIDE;
}

/* The page of the pane that shows pages: the side pane, or the shared one. */
int work_page(const struct app *app) {
    return work_page_shown(app, !work_merged(app));
}

void work_selection_notice(struct app *app, const char *text) {
    copy_text(app->notice, sizeof(app->notice), text);
    copy_text(app->selection_notice, sizeof(app->selection_notice), text);
}

void work_selection_notice_clear(struct app *app) {
    if (app->selection_notice[0] && !strcmp(app->notice, app->selection_notice)) app->notice[0] = '\0';
    app->selection_notice[0] = '\0';
}

void work_set_page(struct app *app, int page) {
    struct native_workspace *w = app->workspace;
    if (!w || !work_page_available(app, page)) return;
    work_selection_notice_clear(app);
    selection_key(app, w->chosen_for, sizeof(w->chosen_for));
    w->chosen = page;
    w->scroll[page] = 0;
    if (page == WORK_RUN) native_attention_read(app);
}

/* [ ] go round the pages that apply to the selection. */
void work_page_step(struct app *app, int direction) {
    bool merged = work_merged(app);
    int first = merged ? WORK_CONVERSATION : WORK_DETAILS, count = WORK_PAGES - first;
    int page = work_page(app) - first, tries;
    for (tries = 0; tries < count; tries++) {
        page = (page + direction + count) % count;
        if (work_page_available(app, page + first)) break;
    }
    work_set_page(app, page + first);
}

/* ---- focus --------------------------------------------------------------- */

/* The panes Tab goes through in order: the list, the conversation (or the
 * shared pane), the side pane, and a second agent when two are shown. */
static size_t pane_order(const struct native_workspace *w, int *panes) {
    size_t count = 0;
    panes[count++] = WORK_PANE_LIST;
    if (w->agents.first) { panes[count++] = w->agents.first; panes[count++] = w->agents.first + 1; }
    else panes[count++] = WORK_PANE_CONVERSATION;
    if (!w->merged && !w->compact) panes[count++] = WORK_PANE_SIDE;
    return count;
}

void work_pane_step(struct app *app, int direction) {
    struct native_workspace *w = app->workspace;
    int panes[4];
    size_t count, i, at = 0;
    if (!w) return;
    count = pane_order(w, panes);
    for (i = 0; i < count; i++) if (panes[i] == w->layout.focus) at = i;
    w->layout.focus = panes[(at + (direction < 0 ? count - 1 : 1)) % count];
    native_workspace_sync_terminal(app);
}

void work_focus_list(struct app *app) {
    if (native_workspace_init(app)) app->workspace->layout.focus = WORK_PANE_LIST;
}

/* Shows a page and gives focus to the pane that shows it (the side pane, or
 * the shared one on a narrow screen), so its keys apply at once. */
void work_focus_page(struct app *app, int page) {
    if (!native_workspace_init(app) || !work_page_available(app, page)) return;
    work_set_page(app, page);
    app->workspace->layout.focus = work_merged(app) ? WORK_PANE_CONVERSATION : WORK_PANE_SIDE;
}


/* The page the focused pane shows, or -1 while the list has focus. */
int work_focused_page(const struct app *app) {
    const struct native_workspace *w = app->workspace;
    if (!w || w->layout.focus == WORK_PANE_LIST) return -1;
    if (w->layout.focus == WORK_PANE_SIDE && !work_merged(app)) return work_page_shown(app, true);
    if (w->agents.first && w->layout.focus >= w->agents.first) return WORK_CONVERSATION;
    return work_merged(app) ? work_page_shown(app, false) : WORK_CONVERSATION;
}

enum key_chain_id work_chain(const struct app *app) {
    switch (work_focused_page(app)) {
    case WORK_CONVERSATION: return KEY_CHAIN_WORK_CONVERSATION;
    case WORK_DETAILS: return KEY_CHAIN_WORK_DETAILS;
    case WORK_PLAN: return KEY_CHAIN_WORK_PLAN;
    case WORK_RUN: return KEY_CHAIN_WORK_RUN;
    default: return KEY_CHAIN_WORK_LIST;
    }
}

/* A new selection shows its own pages from the top and its latest output:
 * the preview is read now, one capture of that head, not one per row passed
 * over (a read in flight is followed by one more for the newest selection). */
void work_selection_changed(struct app *app) {
    struct native_workspace *w = app->workspace;
    app->preview_back = 0;
    work_selection_notice_clear(app);
    if (w) {
        w->run_auto = false;
        memset(w->scroll, 0, sizeof(w->scroll));
        if (!w->chosen_for[0]) w->chosen = -1;
    }
    if (app->view == 7) capture_preview(app);
}

/* Opens Work on its list with the selection made by the caller and a page
 * shown, as the jumps from Inbox and Stats do. */
void work_open(struct app *app, int page) {
    enter_view(app, 7);
    work_focus_list(app);
    work_selection_changed(app);
    if (page >= 0) work_set_page(app, page);
}

/* ---- Esc ------------------------------------------------------------------ */

/* Esc in Work: clear the filter, then the location filter, then bring focus
 * back to the list; false when there is nothing left here to undo. */
bool work_back(struct app *app) {
    struct native_workspace *w = app->workspace;
    if (app->search[0]) { app->search[0] = '\0'; retarget_selection(app); return true; }
    if (app->location[0] && !app->fleet) { location_set(app, ""); return true; }
    if (app->diagnostics) { app->diagnostics = false; return true; }
    if (w && w->zoom) { w->zoom = false; return true; }
    if (w && w->layout.focus != WORK_PANE_LIST) { w->layout.focus = WORK_PANE_LIST; return true; }
    return false;
}

/* ---- the layout ---------------------------------------------------------- */

/* The frame for this size (frame_sizes); focus and the shared pane's page
 * follow a change of it, and a pane that is not shown cannot hold focus. */
void work_frame_mode(struct app *app) {
    struct native_workspace *w = app->workspace;
    bool was_shared = w->compact || w->merged, shared;
    int side = work_page_shown(app, true);
    frame_sizes(app, w);
    shared = w->compact || w->merged;
    /* Narrowing with focus on the side pane: the shared pane shows its page
     * with focus; widening again, focus follows that page back. */
    if (shared && w->layout.focus == WORK_PANE_SIDE) {
        w->layout.focus = WORK_PANE_CONVERSATION;
        selection_key(app, w->chosen_for, sizeof(w->chosen_for));
        w->chosen = side;
    } else if (shared && !was_shared && w->layout.focus != WORK_PANE_LIST) {
        /* Narrowing with focus on the conversation: the shared pane keeps it. */
        selection_key(app, w->chosen_for, sizeof(w->chosen_for));
        w->chosen = WORK_CONVERSATION;
    } else if (!shared && was_shared && w->layout.focus == WORK_PANE_CONVERSATION && !w->agents.first &&
               work_page_shown(app, false) != WORK_CONVERSATION) w->layout.focus = WORK_PANE_SIDE;
}

/* The list's width: the agent column fits from 30 columns, and where remote
 * work is listed (from 95 columns) a WHERE column joins it. */
static int list_width(const struct app *app) {
    int width = app->cols, list = width >= 160 ? 36 : width >= WORK_WIDE ? 32 : width >= 90 ? 28 : 24;
    if (width >= 95 && remote_listed(app)) list += 12;
    return list;
}

/* The shared pane: the right side is one leaf for this frame, then the
 * conversation's subtree (two agents, if split) fills it. */
static void arrange_shared(struct kt_workspace *l, struct kt_rect area) {
    struct kt_pane saved[3];
    int root = l->root;
    l->panes[2].split = KT_LEAF; l->panes[2].min_width = 24;
    (void)kt_workspace_layout(l, area);
    l->panes[2].split = KT_COLUMNS;
    memcpy(saved, l->panes, sizeof(saved));
    l->root = WORK_PANE_CONVERSATION;
    (void)kt_workspace_layout(l, saved[2].bounds);
    l->root = root;
    memcpy(l->panes, saved, sizeof(saved));
    l->bounds = area;
}

/* Lays the panes out in area. A divider the user dragged keeps its place;
 * otherwise the list and the side pane take their default widths. */
void work_arrange(struct app *app, struct kt_rect area) {
    struct native_workspace *w = app->workspace;
    struct kt_workspace *l = &w->layout;
    int root = l->root;
    l->panes[1].min_width = w->compact ? 8 : list_width(app);
    l->panes[3].min_width = 24;
    l->panes[4].min_width = app->cols >= 160 ? 52 : 39;
    if (!w->dragged) { l->panes[0].ratio = 1; l->panes[2].ratio = 999; }
    if (w->compact || w->zoom) {
        l->root = l->focus;
        (void)kt_workspace_layout(l, area);
        l->root = root;
    } else if (w->merged) arrange_shared(l, area);
    else (void)kt_workspace_layout(l, area);
}

/* ---- page strip hits ------------------------------------------------------ */

void work_page_hits_clear(struct app *app) {
    if (app->workspace) app->workspace->page_hit_count = 0;
}

void work_page_hit(struct app *app, int page, int x, int y, int width) {
    struct native_workspace *w = app->workspace;
    if (!w || w->page_hit_count >= sizeof(w->page_hits) / sizeof(w->page_hits[0])) return;
    w->page_hits[w->page_hit_count++] = (struct work_page_hit){x, y, width, page};
}

/* A click on a page's name in a strip shows that page. */
bool work_page_click(struct app *app, int x, int y) {
    struct native_workspace *w = app->workspace;
    size_t i;
    for (i = 0; w && i < w->page_hit_count; i++) {
        const struct work_page_hit *h = &w->page_hits[i];
        if (y != h->y || x < h->x || x >= h->x + h->width) continue;
        work_set_page(app, h->page);
        return true;
    }
    return false;
}
