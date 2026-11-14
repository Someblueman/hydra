#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "../kettle/components.h"
#include "attention.h"
#include "inbox.h"
/* Scope chains, key dispatch, the footer and help, all read from the one
 * binding table in keys.c. What a footer offers is what its keys do because
 * both come from the same row; within one chain no key has two meanings,
 * which key_chain_check enforces for the built-in table and every keys file. */

#define CHAIN_BASE (KEY_SCOPE_TERMINAL | KEY_SCOPE_GLOBAL)
#define CHAIN_VIEW (CHAIN_BASE | KEY_SCOPE_JUMPS | KEY_SCOPE_VIEWS | KEY_SCOPE_VIEW_ARROWS)
#define CHAIN_WORK (CHAIN_BASE | KEY_SCOPE_JUMPS | KEY_SCOPE_WORK)
/* Every chain includes the agent layer's chords: they are Hydra's in every
 * view, so no key of a view may start one of them. Work's chains are its
 * panes: the list, the conversation and the side pane's pages. */
const uint32_t key_chains[KEY_CHAINS] = {
    [KEY_CHAIN_WORK_LIST] = CHAIN_WORK | KEY_SCOPE_WORK_LIST,
    [KEY_CHAIN_WORK_CONVERSATION] = CHAIN_WORK | KEY_SCOPE_WORK_CONVERSATION | KEY_SCOPE_WORK_VIEWPORT,
    [KEY_CHAIN_WORK_DETAILS] = CHAIN_WORK | KEY_SCOPE_WORK_VIEWPORT,
    [KEY_CHAIN_WORK_PLAN] = CHAIN_WORK | KEY_SCOPE_WORK_PLAN | KEY_SCOPE_WORK_VIEWPORT,
    [KEY_CHAIN_WORK_RUN] = CHAIN_WORK | KEY_SCOPE_WORK_RUN,
    [KEY_CHAIN_HOSTS] = CHAIN_VIEW | KEY_SCOPE_HOSTS,
    /* Stats: Tab moves between its panes and w is its workflow filter. */
    [KEY_CHAIN_STATISTICS] = CHAIN_BASE | KEY_SCOPE_JUMPS | KEY_SCOPE_VIEW_ARROWS | KEY_SCOPE_STATISTICS,
    [KEY_CHAIN_INBOX] = CHAIN_BASE | KEY_SCOPE_INBOX | KEY_SCOPE_INBOX_PAGES,
    [KEY_CHAIN_REVIEW] = CHAIN_BASE | KEY_SCOPE_REVIEW | KEY_SCOPE_INBOX_PAGES,
    [KEY_CHAIN_HELP] = KEY_SCOPE_TERMINAL | KEY_SCOPE_HELP,
    [KEY_CHAIN_RESULT] = KEY_SCOPE_TERMINAL | KEY_SCOPE_RESULT,
    [KEY_CHAIN_STATS_FILTER] = KEY_SCOPE_TERMINAL | KEY_SCOPE_STATS_FILTER,
};

static enum key_chain_id view_chain(const struct app *app) {
    if (app->view == 9) return inbox_chain(app);
    if (app->view == 8) return statistics_typing(app) ? KEY_CHAIN_STATS_FILTER : KEY_CHAIN_STATISTICS;
    if (app->view == 6) return KEY_CHAIN_HOSTS;
    return work_chain(app);
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

/* r observes the selected remote work's host again, or every host when
 * remote work is shown at all. */
static bool retry_applies(const struct app *app) {
    return selected_task((struct app *)app) || remote_head_selected(app) || app->model.host_count || app->remote_stale;
}

static bool heads_applies(const struct app *app, enum key_action action) {
    switch (action) {
    case KEY_HEAD_GROUP: return app->marked_count > 0;
    case KEY_HEAD_REMOVE: return app->marked_count > 0 || head_selected(app);
    case KEY_HEAD_MARK: case KEY_HEAD_MARK_ALL: return app->model.head_count > 0;
    case KEY_HEADS_RETRY: return retry_applies(app);
    case KEY_HEAD_TECHNICAL: return head_selected(app) || selected_task((struct app *)app);
    default: return true;
    }
}

/* Inbox and Work decide their own rows; false when action is not one of theirs. */
static bool owner_applies(const struct app *app, enum key_action action, bool *applies) {
    if (action >= KEY_REVIEW_UP && action <= KEY_INBOX_LAST) { *applies = inbox_applies(app, action); return true; }
    return work_applies(app, action, applies);
}

bool key_applies(const struct app *app, enum key_action action) {
    size_t record;
    bool applies;
    if (owner_applies(app, action, &applies)) return applies;
    if (action >= KEY_HEAD_REMOVE && action <= KEY_HEAD_TECHNICAL) return heads_applies(app, action);
    if (action >= KEY_STATS_UP && action <= KEY_STATS_FILTER_CLEAR) return statistics_applies(app, action);
    switch (action) {
    case KEY_HOSTS_EDIT: case KEY_HOSTS_REMOVE: return hosts_setup_selected(app, &record);
    case KEY_HOSTS_RETRY: return app->model.host_count || app->remote_stale;
    case KEY_BACK: return app->view != 7 || app->jump_back || app->search[0] || app->location[0] || app->diagnostics ||
        (app->workspace && app->workspace->layout.focus != WORK_PANE_LIST) || (app->inbox && app->inbox->jumped) ||
        (app->statistics && (app->statistics->graph_open || app->statistics->head_open));
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
        [KEY_HOSTS_UP] = 'k', [KEY_HOSTS_DOWN] = 'j', [KEY_HOSTS_OPEN] = '\r', [KEY_HOSTS_ADD] = 'A',
        [KEY_HOSTS_EDIT] = 'e', [KEY_HOSTS_REMOVE] = 'x', [KEY_HOSTS_RETRY] = 'r', [KEY_HOSTS_ONBOARDING] = 'O',
        [KEY_TASK_NEW] = 'n', [KEY_HEAD_REMOVE] = 'x', [KEY_HEAD_MARK] = ' ', [KEY_HEAD_MARK_ALL] = 'A',
        [KEY_HEAD_GROUP] = 'G', [KEY_HEAD_TECHNICAL] = 'd', [KEY_HEADS_SEARCH] = '/',
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
    if (action >= KEY_VIEW_1 && action <= KEY_VIEW_4) { enter_screen(app, (int)(action - KEY_VIEW_1)); return; }
    if (work_perform(app, action)) return;
    if (action >= KEY_STATS_UP && action <= KEY_STATS_FILTER_CLEAR) { statistics_perform(app, action); return; }
    if (inbox_perform(app, action)) return;
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
     * Enter talks to a head and expands or collapses a run. */
    if (remote_head_selected(app) && !app->marked_count) rows[KEY_HEAD_REMOVE].label = "interrupt";
    rows[KEY_HEADS_OPEN].label = work_open_label(app);
    rows[KEY_STATS_GRAPH].label = statistics_graph_label(app);
    rows[KEY_STATS_PAGE_PREVIOUS].label = rows[KEY_STATS_PAGE_NEXT].label = statistics_page_label(app);
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

/* While typing to an agent the chords are the footer; the help lists all. */
void key_terminal_footer(struct app *app, struct kt_canvas *canvas, struct kt_rect area) {
    static const struct { enum key_action action; int priority; } shown[] = {
        {KEY_TERMINAL_LEAVE, 100}, {KEY_TERMINAL_CLOSE, 9}, {KEY_TERMINAL_NEXT, 8}, {KEY_TERMINAL_HISTORY, 6},
        {KEY_SCREEN_INBOX, 5}, {KEY_APP_QUIT, 100}};
    struct kt_binding rows[KEY_ACTIONS];
    struct kt_help_source source;
    struct kt_help_style s = hint_style();
    size_t i;
    memcpy(rows, native_keys(app)->bindings, sizeof(rows));
    for (i = 0; i < KEY_ACTIONS; i++) rows[i].scope = 0;
    for (i = 0; i < sizeof(shown) / sizeof(shown[0]); i++) {
        rows[shown[i].action].scope = KEY_SCOPE_TERMINAL; rows[shown[i].action].priority = shown[i].priority;
    }
    rows[KEY_TERMINAL_CLOSE].label = "close"; rows[KEY_SCREEN_INBOX].label = "Inbox";
    /* Where the conversation takes the shared pane, the chord that shows the
     * selection's plan there instead. */
    if (app->workspace && (app->workspace->merged || app->workspace->compact) && work_page_available(app, WORK_PLAN)) {
        rows[KEY_LAYOUT_PLAN].scope = KEY_SCOPE_TERMINAL; rows[KEY_LAYOUT_PLAN].priority = 10; rows[KEY_LAYOUT_PLAN].label = "Plan";
    }
    source.bindings = rows; source.count = KEY_ACTIONS; source.scope = KEY_SCOPE_TERMINAL;
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
