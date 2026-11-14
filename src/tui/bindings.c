#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "../kettle/components.h"
/* Scope chains, key dispatch, the footer and help, all read from the one
 * binding table in keys.c. What a footer offers is what its keys do because
 * both come from the same row; within one chain no key has two meanings,
 * which key_chain_check enforces for the built-in table and every keys file. */

#define CHAIN_BASE (KEY_SCOPE_TERMINAL | KEY_SCOPE_GLOBAL)
#define CHAIN_VIEW (CHAIN_BASE | KEY_SCOPE_JUMPS | KEY_SCOPE_VIEWS | KEY_SCOPE_VIEW_ARROWS)
/* Every chain includes the agent layer's chords: they are Hydra's in every
 * view, so no key of a view may start one of them. */
const uint32_t key_chains[KEY_CHAINS] = {
    [KEY_CHAIN_HEADS] = CHAIN_VIEW | KEY_SCOPE_HEADS,
    [KEY_CHAIN_DETAIL] = CHAIN_VIEW | KEY_SCOPE_DETAIL,
    [KEY_CHAIN_OVERVIEW] = CHAIN_VIEW | KEY_SCOPE_OVERVIEW,
    [KEY_CHAIN_RECOVERY] = CHAIN_VIEW | KEY_SCOPE_RECOVERY,
    [KEY_CHAIN_WORKFLOWS] = CHAIN_BASE | KEY_SCOPE_JUMPS | KEY_SCOPE_VIEWS | KEY_SCOPE_WORKFLOWS,
    [KEY_CHAIN_HOSTS] = CHAIN_VIEW | KEY_SCOPE_HOSTS,
    [KEY_CHAIN_WORKSPACE] = CHAIN_BASE | KEY_SCOPE_JUMPS | KEY_SCOPE_WORKSPACE,
    [KEY_CHAIN_MONITOR] = CHAIN_BASE | KEY_SCOPE_JUMPS | KEY_SCOPE_WORKSPACE | KEY_SCOPE_MONITOR,
    [KEY_CHAIN_STATISTICS] = CHAIN_VIEW | KEY_SCOPE_STATISTICS,
    [KEY_CHAIN_ATTENTION] = CHAIN_VIEW | KEY_SCOPE_ATTENTION,
    [KEY_CHAIN_REVIEW] = CHAIN_BASE | KEY_SCOPE_REVIEW,
    [KEY_CHAIN_HELP] = KEY_SCOPE_TERMINAL | KEY_SCOPE_HELP,
    [KEY_CHAIN_RESULT] = KEY_SCOPE_TERMINAL | KEY_SCOPE_RESULT,
};

static enum key_chain_id view_chain(const struct app *app) {
    static const enum key_chain_id views[] = {
        KEY_CHAIN_HEADS, KEY_CHAIN_DETAIL, KEY_CHAIN_DETAIL, KEY_CHAIN_RECOVERY, KEY_CHAIN_OVERVIEW,
        KEY_CHAIN_WORKFLOWS, KEY_CHAIN_HOSTS, KEY_CHAIN_WORKSPACE, KEY_CHAIN_STATISTICS, KEY_CHAIN_ATTENTION};
    if (app->view < 0 || app->view > 9) return KEY_CHAIN_HEADS;
    if (app->view == 7 && native_workspace_monitoring((struct app *)app)) return KEY_CHAIN_MONITOR;
    if (app->view == 9 && native_review_active(app)) return KEY_CHAIN_REVIEW;
    return views[app->view];
}

enum key_chain_id key_chain(const struct app *app) {
    if (app->result_open) return KEY_CHAIN_RESULT;
    if (app->help) return KEY_CHAIN_HELP;
    return view_chain(app);
}

/* ---- the table's consistency -------------------------------------------- */

/* Each chain is checked as a table of its own: the bindings live in it, in
 * one scope, which kettle refuses when two share a chord or one begins
 * another, naming them (and the keys-file line that set the later one). */
bool key_chain_check(const struct kt_binding *bindings, size_t count, char *error, size_t size) {
    struct kt_binding rows[KEY_ACTIONS];
    struct kt_keymap map;
    size_t c, i;
    if (size) error[0] = '\0';
    if (count > KEY_ACTIONS) count = KEY_ACTIONS;
    for (c = 0; c < KEY_CHAINS; c++) {
        map.bindings = rows; map.count = 0;
        for (i = 0; i < count; i++) {
            if (!(bindings[i].scope & key_chains[c])) continue;
            rows[map.count] = bindings[i];
            rows[map.count++].scope = 1;
        }
        if (!kt_keymap_check(&map, error, size)) return false;
    }
    return true;
}

/* ---- what applies to the selection -------------------------------------- */

static bool head_selected(const struct app *app) {
    return !app->run_row && selected_head((struct app *)app) != NULL;
}

static bool remote_head_selected(const struct app *app) {
    const struct head *h = app->run_row ? NULL : selected_head((struct app *)app);
    return h && head_remote(h);
}

/* Y/N/R/X on a remote task row in Work, Details or Overview; r observes the
 * selected remote work's host again, or every host when remote work is
 * shown at all. */
static bool remote_applies(const struct app *app, enum key_action action) {
    bool task = (app->view == 0 || app->view == 1 || app->view == 4) && selected_task((struct app *)app);
    if (action != KEY_HEADS_RETRY) return task;
    return task || remote_head_selected(app) || app->model.host_count || app->remote_stale;
}

static bool plan_applies(const struct app *app, enum key_action action) {
    const struct native_plan *p = app->plan;
    if (action == KEY_PLAN_PROPOSAL) return !p || p->state != PLAN_VALIDATING;
    if (!p) return false;
    if (action == KEY_PLAN_EXECUTE) return p->state == PLAN_READY;
    if (action == KEY_PLAN_VALIDATE) return p->state != PLAN_RETURNED && p->state != PLAN_VALIDATING;
    return p->state != PLAN_RETURNED;
}

static bool run_ended(const struct app *app) {
    const char *state;
    if (!app->workflows || app->workflow_run >= app->workflows->run_count) return true;
    state = app->workflows->runs[app->workflow_run].state;
    return !strcmp(state, "succeeded") || !strcmp(state, "failed") || !strcmp(state, "cancelled");
}

static bool workspace_applies(const struct app *app, enum key_action action) {
    const struct native_workspace *w = app->workspace;
    bool navigation = w && w->layout.focus == 1;
    switch (action) {
    case KEY_WS_COLLAPSE: case KEY_WS_EXPAND: return navigation;
    /* Enter opens the selected head's details from any pane. */
    case KEY_WS_OPEN: return navigation || head_selected(app);
    case KEY_WS_TALK: case KEY_WS_ATTACH: return head_selected(app) || (w && !w->run_selected && selected_head((struct app *)app));
    case KEY_RUN_RESUME: case KEY_RUN_CANCEL: return !run_ended(app);
    default: return true;
    }
}

static bool recovery_removable(const struct app *app) {
    return app->recovery_selected < app->model.recovery_count &&
        !strcmp(app->model.recovery[app->recovery_selected].kind, "orphan-worktree");
}

static bool runs_page(const struct app *app) {
    return app->statistics && app->statistics->model && !statistics_sessions_shown(app);
}

static bool heads_applies(const struct app *app, enum key_action action) {
    bool list = app->view == 0 || app->view == 4;
    switch (action) {
    case KEY_HEADS_COLLAPSE: case KEY_HEADS_EXPAND: return list && app->run_row;
    case KEY_HEADS_OPEN: return list && (app->run_row || selected_head((struct app *)app) || selected_task((struct app *)app));
    case KEY_HEADS_PAGE_UP: case KEY_HEADS_PAGE_DOWN: return app->view == 1 && app->preview;
    case KEY_HEAD_GROUP: return app->marked_count > 0;
    case KEY_HEAD_REMOVE: return app->marked_count > 0 || head_selected(app);
    case KEY_HEADS_COORDINATION: return app->view == 1 && head_selected(app);
    case KEY_HEAD_MARK: case KEY_HEAD_MARK_ALL: return app->model.head_count > 0;
    default: return head_selected(app);
    }
}

/* r on Hosts observes the hosts again; L filters the Work and Overview lists. */
static bool location_applies(const struct app *app, enum key_action action) {
    if (action == KEY_HOSTS_RETRY) return app->model.host_count || app->remote_stale;
    return app->view == 0 || app->view == 4;
}

bool key_applies(const struct app *app, enum key_action action) {
    size_t record;
    if (action >= KEY_WS_UP && action <= KEY_RUN_CANCEL) {
        if (action >= KEY_PLAN_PROPOSAL && action <= KEY_PLAN_EXECUTE) return plan_applies(app, action);
        return workspace_applies(app, action);
    }
    if ((action >= KEY_HEADS_COLLAPSE && action <= KEY_HEAD_TECHNICAL) && action != KEY_TASK_NEW)
        return heads_applies(app, action);
    if ((action >= KEY_REMOTE_APPROVE && action <= KEY_REMOTE_CANCEL) || action == KEY_HEADS_RETRY)
        return remote_applies(app, action);
    switch (action) {
    case KEY_RECOVERY_REMOVE: return recovery_removable(app);
    case KEY_ATTENTION_PAGE_DOWN: case KEY_ATTENTION_PAGE_UP: return native_attention_detail_open(app);
    case KEY_RECOVERY_CHECK: case KEY_RECOVERY_EXPLAIN: return app->model.recovery_count > 0;
    case KEY_HOSTS_EDIT: case KEY_HOSTS_REMOVE: return hosts_setup_selected(app, &record);
    case KEY_STATS_GRAPH: case KEY_STATS_METRIC: case KEY_STATS_WORKFLOW_PREVIOUS: case KEY_STATS_WORKFLOW_NEXT:
    case KEY_STATS_COHORTS: case KEY_STATS_LOCATION:
        return runs_page(app);
    case KEY_HOSTS_RETRY: case KEY_HEADS_LOCATION: return location_applies(app, action);
    case KEY_STATS_PAGE:
        return !app->fleet && app->statistics && app->statistics->model &&
            (app->statistics->model->session_count || statistics_sessions_shown(app));
    case KEY_BACK: return app->view != 0 || app->search[0] || app->location[0] || app->preview || app->diagnostics;
    default: return true;
    }
}

/* ---- dispatch ------------------------------------------------------------ */

/* The key each action stands for in the views' own handlers (input.c). */
#define NAMED(key) (0x200000U + (uint32_t)(key))
#define SHIFT_TAB NAMED(1)
static uint32_t action_key(enum key_action action) {
    static const uint32_t keys[KEY_ACTIONS] = {
        [KEY_RESULT_UP] = 'k', [KEY_RESULT_DOWN] = 'j', [KEY_RESULT_PAGE_UP] = KT_KEY_PAGE_UP,
        [KEY_RESULT_PAGE_DOWN] = KT_KEY_PAGE_DOWN, [KEY_RESULT_CLOSE] = 27, [KEY_RESULT_QUIT] = 'q',
        [KEY_HELP_UP] = 'k', [KEY_HELP_DOWN] = 'j', [KEY_HELP_PAGE_UP] = KT_KEY_PAGE_UP,
        [KEY_HELP_PAGE_DOWN] = KT_KEY_PAGE_DOWN, [KEY_HELP_CLOSE] = 27,
        [KEY_REVIEW_UP] = 'k', [KEY_REVIEW_DOWN] = 'j', [KEY_REVIEW_PAGE_DOWN] = ' ', [KEY_REVIEW_PAGE_UP] = 'b',
        [KEY_REVIEW_TOP] = 'g', [KEY_REVIEW_END] = 'G', [KEY_REVIEW_NEXT_SECTION] = 'n',
        [KEY_REVIEW_PREVIOUS_SECTION] = 'N', [KEY_REVIEW_REFERENCES] = 'f', [KEY_REVIEW_IDENTITY] = 'i',
        [KEY_REVIEW_OPEN] = '\r', [KEY_REVIEW_LOG] = 'L', [KEY_REVIEW_SUPPLY_LOG] = 'l',
        [KEY_REVIEW_SUPPLY_TRANSCRIPT] = 't', [KEY_REVIEW_SUPPLY_PR] = 'p', [KEY_REVIEW_PREVIOUS_ITEM] = '[',
        [KEY_REVIEW_NEXT_ITEM] = ']', [KEY_REVIEW_RELOAD] = 'r',
        [KEY_ATTENTION_UP] = 'k', [KEY_ATTENTION_DOWN] = 'j', [KEY_ATTENTION_OPEN] = '\r',
        [KEY_ATTENTION_REVIEW] = 'r', [KEY_ATTENTION_SEEN] = 's', [KEY_ATTENTION_PAGE_DOWN] = ' ', [KEY_ATTENTION_PAGE_UP] = 'b',
        [KEY_STATS_UP] = 'k', [KEY_STATS_DOWN] = 'j', [KEY_STATS_OPEN] = '\r', [KEY_STATS_PAGE] = 's',
        [KEY_STATS_GRAPH] = 'g', [KEY_STATS_RANGE] = 'T', [KEY_STATS_ATTENTION] = '!', [KEY_STATS_RESET] = '0',
        [KEY_STATS_METRIC] = 'M', [KEY_STATS_WORKFLOW_PREVIOUS] = '[', [KEY_STATS_WORKFLOW_NEXT] = ']',
        [KEY_STATS_FIND] = '/', [KEY_STATS_COHORTS] = 'C', [KEY_STATS_LOCATION] = 'L', [KEY_STATS_REFRESH] = 'r',
        [KEY_HOSTS_UP] = 'k', [KEY_HOSTS_DOWN] = 'j', [KEY_HOSTS_OPEN] = '\r', [KEY_HOSTS_ADD] = 'A',
        [KEY_HOSTS_EDIT] = 'e', [KEY_HOSTS_REMOVE] = 'x', [KEY_HOSTS_RETRY] = 'r', [KEY_HOSTS_ONBOARDING] = 'O',
        [KEY_RECOVERY_UP] = 'k', [KEY_RECOVERY_DOWN] = 'j', [KEY_RECOVERY_CHECK] = '\r',
        [KEY_RECOVERY_REMOVE] = 'x', [KEY_RECOVERY_EXPLAIN] = 'd',
        [KEY_FLOW_UP] = 'k', [KEY_FLOW_DOWN] = 'j', [KEY_FLOW_PREVIOUS_RUN] = '[', [KEY_FLOW_NEXT_RUN] = ']',
        [KEY_FLOW_LEFT] = 'h', [KEY_FLOW_RIGHT] = 'l', [KEY_FLOW_PAN_UP] = 'K', [KEY_FLOW_PAN_DOWN] = 'J',
        [KEY_FLOW_FOLLOW] = '\r',
        [KEY_WS_UP] = 'k', [KEY_WS_DOWN] = 'j', [KEY_WS_COLLAPSE] = 'h', [KEY_WS_EXPAND] = 'l',
        [KEY_WS_OPEN] = '\r', [KEY_WS_PANE_NEXT] = '\t', [KEY_WS_PANE_PREVIOUS] = SHIFT_TAB, [KEY_WS_TALK] = 'a',
        [KEY_WS_ATTACH] = 'a', [KEY_WS_CONVERSATION] = 'A', [KEY_WS_PLAN] = 'B', [KEY_WS_MONITOR] = 'C',
        [KEY_WS_ZOOM] = 'z', [KEY_WS_SPLIT] = 'S',
        [KEY_PLAN_PROPOSAL] = 'P', [KEY_PLAN_VALIDATE] = 'V', [KEY_PLAN_RETURN] = 'F', [KEY_PLAN_EXECUTE] = 'E',
        [KEY_MONITOR_PREVIOUS_RUN] = '[', [KEY_MONITOR_NEXT_RUN] = ']',
        [KEY_RUN_APPROVE] = 'Y', [KEY_RUN_REJECT] = 'N', [KEY_RUN_RESUME] = 'R', [KEY_RUN_CANCEL] = 'X',
        [KEY_REMOTE_APPROVE] = 'Y', [KEY_REMOTE_REJECT] = 'N', [KEY_REMOTE_RESUME] = 'R', [KEY_REMOTE_CANCEL] = 'X',
        [KEY_HEADS_UP] = 'k', [KEY_HEADS_DOWN] = 'j', [KEY_HEADS_COLLAPSE] = 'h', [KEY_HEADS_EXPAND] = 'l',
        [KEY_HEADS_OPEN] = '\r', [KEY_HEADS_TALK] = 'a', [KEY_HEADS_ATTACH] = 'a', [KEY_HEADS_INTERRUPT] = 'c',
        [KEY_HEADS_COORDINATION] = 'c', [KEY_HEADS_PAGE_UP] = KT_KEY_PAGE_UP, [KEY_HEADS_PAGE_DOWN] = KT_KEY_PAGE_DOWN,
        [KEY_TASK_NEW] = 'n', [KEY_HEAD_REMOVE] = 'x', [KEY_HEAD_MARK] = ' ', [KEY_HEAD_MARK_ALL] = 'A',
        [KEY_HEAD_GROUP] = 'G', [KEY_HEAD_OUTPUT] = 'p', [KEY_HEAD_TECHNICAL] = 'd', [KEY_HEADS_SEARCH] = '/',
        [KEY_HEADS_LOCATION] = 'L', [KEY_HEADS_RETRY] = 'r',
        [KEY_VIEWS_NEXT] = '\t', [KEY_VIEWS_PREVIOUS] = SHIFT_TAB, [KEY_VIEWS_LEFT] = KT_KEY_LEFT,
        [KEY_VIEWS_RIGHT] = KT_KEY_RIGHT,
        [KEY_VIEW_1] = '1', [KEY_VIEW_2] = '2', [KEY_VIEW_3] = '3', [KEY_VIEW_4] = '4', [KEY_VIEW_5] = '5',
        [KEY_VIEW_6] = '6', [KEY_VIEW_7] = '7', [KEY_VIEW_8] = '8', [KEY_VIEW_9] = '9',
        [KEY_VIEW_CYCLE] = 'v', [KEY_VIEW_WORKSPACE] = 'W', [KEY_VIEW_OVERVIEW] = 'o', [KEY_VIEW_WORKFLOWS] = 'w',
        [KEY_VIEW_HOSTS] = 'H', [KEY_VIEW_ATTENTION] = 'I', [KEY_VIEW_STATS] = 'D', [KEY_THEME] = 't',
        [KEY_PALETTE] = ':', [KEY_BACK] = 27, [KEY_HELP] = '?', [KEY_QUIT] = 'q'};
    return action < KEY_ACTIONS ? keys[action] : 0;
}

/* ctrl+r reads every source again now; the attention list and statistics
 * when they are shown. */
static void refresh_all(struct app *app) {
    size_t source;
    for (source = 0; source < OBSERVE_SOURCES; source++)
        if (source != OBSERVE_STATISTICS && source != OBSERVE_ATTENTION) native_observations_request(app, source);
    if (app->view == 9) native_attention_tick(app, true);
    if (app->view == 8) statistics_request(app);
    copy_text(app->notice, sizeof(app->notice), "Reading everything again");
}

void key_perform(struct app *app, enum key_action action) {
    uint32_t key = action_key(action);
    if (action == KEY_REFRESH) { refresh_all(app); return; }
    if (action == KEY_PALETTE) { palette_open(app); return; }
    if (key == SHIFT_TAB) input_named(app, KT_KEY_TAB, KT_MOD_SHIFT);
    else if (key >= KT_KEY_UP) input_named(app, key, 0);
    else if (key) input_character(app, (char)key);
}

/* A key the help leaves alone closes it and then acts where help was open,
 * as any key did before help had keys of its own. */
bool key_dispatch(struct app *app, const struct kt_event *e) {
    struct native_keys *k = native_keys(app);
    const struct kt_binding *binding = NULL;
    uint32_t chain = key_chains[key_chain(app)] & ~(uint32_t)KEY_SCOPE_TERMINAL;
    struct kt_event line_feed;
    enum kt_key_match match;
    /* A line feed (Ctrl-J, what some terminals and scripts send for Return)
     * is Enter, as it always was here. */
    if (e->length == 1 && e->bytes[0] == '\n') {
        line_feed = *e; line_feed.key = KT_KEY_ENTER; line_feed.mods = 0; e = &line_feed;
    }
    match = kt_keymap_feed(&k->map, &k->view_state, chain, e, &binding);
    if (match == KT_KEY_PENDING || match == KT_KEY_ABANDONED) return true;
    if (match == KT_KEY_ACTION) { key_perform(app, (enum key_action)binding->action); return true; }
    if (!app->help) return false;
    app->help = false; app->help_scroll = 0;
    return key_dispatch(app, e);
}

/* ---- footer and help ----------------------------------------------------- */

/* The table with every binding that does not apply now taken out of scope. */
static void applying(const struct app *app, struct kt_binding *rows) {
    size_t i;
    memcpy(rows, native_keys((struct app *)app)->bindings, sizeof(struct kt_binding) * KEY_ACTIONS);
    for (i = KEY_VIEW_ACTIONS; i < KEY_ACTIONS; i++)
        if (!key_applies(app, (enum key_action)i)) rows[i].scope = 0;
    /* x interrupts a selected remote head's agent instead of removing it;
     * Enter in the workspace opens a selected head's details, or a run's
     * evidence. */
    if (remote_head_selected(app) && !app->marked_count) rows[KEY_HEAD_REMOVE].label = "interrupt";
    if (app->view == 7 && head_selected(app) && !(app->workspace && app->workspace->run_selected))
        rows[KEY_WS_OPEN].label = "details";
}

uint32_t key_applying(const struct app *app, struct kt_binding rows[KEY_ACTIONS]) {
    applying(app, rows);
    return key_chains[key_chain(app)] & ~(uint32_t)KEY_SCOPE_TERMINAL;
}

static struct kt_help_style hint_style(void) {
    struct kt_help_style s;
    s.key = KT_STRONG; s.label = KT_MUTED; s.separator = KT_BORDER; s.heading = KT_TITLE;
    return s;
}

void key_footer(struct app *app, struct kt_canvas *canvas, struct kt_rect area) {
    struct kt_binding rows[KEY_ACTIONS];
    struct kt_help_source source;
    struct kt_help_style s = hint_style();
    applying(app, rows);
    source.bindings = rows; source.count = KEY_ACTIONS;
    source.scope = key_chains[key_chain(app)] & ~(uint32_t)KEY_SCOPE_TERMINAL;
    (void)kt_help_line(canvas, area, &source, 1, NULL, NULL, &s);
}

/* The help of the view under it: the view's chain, then the chords that
 * work while typing to an agent. Help is never about itself. */
int key_help(struct app *app, struct kt_canvas *canvas, struct kt_rect area, int skip) {
    struct kt_binding rows[KEY_ACTIONS];
    struct kt_help help;
    struct kt_help_source sources[2];
    bool open = app->help, result = app->result_open;
    int height;
    app->help = false; app->result_open = false;
    applying(app, rows);
    sources[0].bindings = rows; sources[0].count = KEY_ACTIONS;
    sources[0].scope = key_chains[key_chain(app)] & ~(uint32_t)KEY_SCOPE_TERMINAL;
    sources[1] = sources[0]; sources[1].scope = KEY_SCOPE_TERMINAL;
    app->help = open; app->result_open = result;
    kt_help_init(&help);
    help.style = hint_style();
    (void)kt_help_set(&help, sources, 2);
    height = kt_help_full_height(&help, area.width);
    if (canvas) (void)kt_help_full_view(&help, canvas, (struct kt_rect){area.x, area.y - skip, area.width, area.height + skip});
    return height;
}
