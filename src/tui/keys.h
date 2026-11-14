#ifndef HYDRA_TUI_KEYS_H
#define HYDRA_TUI_KEYS_H
#include "model.h"
#include "../kettle/keymap.h"
#include <stdio.h>
/* Hydra's keys. One binding table (keys.c) drives matching, the footer, the
 * help screen and the user's keys file: a key does what the footer says
 * because both read the same row. */
struct app;
struct kt_canvas;
struct kt_rect;
struct kt_event;
/* Where a binding is live; each scope is one bit, so a binding can belong to
 * several. The focused element's scope chain (key_chain) is a set of these
 * bits, and within one chain no key has two meanings (key_chain_conflict).
 * TERMINAL: Hydra's own commands while an agent pane may hold input.
 * VIEWS: Tab changing screens (retired: Tab moves between panes);
 * VIEW_ARROWS: the arrows change screens (Stats).
 * JUMPS: the letters that open a screen, which Inbox and its review leave to
 * their own keys. STATS_FILTER: the keys while a Stats filter is typed.
 * INBOX: the Inbox list; INBOX_PAGES: its detail pages, there and on the
 * review pages (REVIEW). WORK: every pane of Work; WORK_LIST its list,
 * WORK_CONVERSATION the conversation, WORK_VIEWPORT a pane that scrolls
 * (the conversation and the Details and Plan pages), WORK_PLAN and WORK_RUN
 * the Plan and Run pages of the side pane. HOSTS: the Hosts list;
 * HOSTS_SETUP: the open setup in its detail pane; HOSTS_PANES: Tab and the
 * page keys of both. */
enum key_scope {
    KEY_SCOPE_TERMINAL = 1U << 0, KEY_SCOPE_GLOBAL = 1U << 1, KEY_SCOPE_VIEWS = 1U << 2,
    KEY_SCOPE_VIEW_ARROWS = 1U << 3, KEY_SCOPE_WORK_LIST = 1U << 4, KEY_SCOPE_WORK_RUN = 1U << 5,
    KEY_SCOPE_WORK_VIEWPORT = 1U << 6, KEY_SCOPE_WORK = 1U << 7, KEY_SCOPE_WORK_PLAN = 1U << 8,
    KEY_SCOPE_INBOX_PAGES = 1U << 9, KEY_SCOPE_WORK_CONVERSATION = 1U << 10, KEY_SCOPE_HOSTS = 1U << 11,
    KEY_SCOPE_STATISTICS = 1U << 12, KEY_SCOPE_INBOX = 1U << 13, KEY_SCOPE_REVIEW = 1U << 14,
    KEY_SCOPE_HELP = 1U << 15, KEY_SCOPE_RESULT = 1U << 16, KEY_SCOPE_JUMPS = 1U << 17,
    KEY_SCOPE_STATS_FILTER = 1U << 19, KEY_SCOPE_HOSTS_SETUP = 1U << 20, KEY_SCOPE_HOSTS_PANES = 1U << 21
};
/* Rows follow this order; the first fourteen are the chords of the keys
 * file's first version and keep their names. Footers list entries in this
 * order, so the keys of a place come before the ones that work everywhere. */
enum key_action {
    KEY_TERMINAL_LEAVE, KEY_TERMINAL_CLOSE, KEY_TERMINAL_NEXT, KEY_TERMINAL_RECONNECT,
    KEY_TERMINAL_HISTORY, KEY_TERMINAL_LIVE, KEY_APP_QUIT, KEY_VIEW_STATISTICS,
    KEY_LAYOUT_CONVERSATION, KEY_LAYOUT_PLAN, KEY_LAYOUT_MONITOR,
    KEY_WORKSPACE_ZOOM, KEY_WORKSPACE_SPLIT, KEY_TERMINAL_LITERAL,
    KEY_SCREEN_WORK, KEY_SCREEN_INBOX, KEY_SCREEN_HOSTS, KEY_SCREEN_STATS,
    /* overlays */
    KEY_RESULT_UP, KEY_RESULT_DOWN, KEY_RESULT_PAGE_UP, KEY_RESULT_PAGE_DOWN, KEY_RESULT_CLOSE, KEY_RESULT_QUIT,
    KEY_HELP_UP, KEY_HELP_DOWN, KEY_HELP_PAGE_UP, KEY_HELP_PAGE_DOWN, KEY_HELP_CLOSE,
    /* Inbox: the Review and References pages, then the list (inbox.c) */
    KEY_REVIEW_UP, KEY_REVIEW_DOWN, KEY_REVIEW_PAGE_DOWN, KEY_REVIEW_PAGE_UP, KEY_REVIEW_TOP, KEY_REVIEW_END,
    KEY_REVIEW_NEXT_SECTION, KEY_REVIEW_PREVIOUS_SECTION, KEY_REVIEW_OPEN, KEY_REVIEW_REFERENCE, KEY_REVIEW_LOG, KEY_REVIEW_RELOAD,
    KEY_INBOX_UP, KEY_INBOX_DOWN, KEY_INBOX_OPEN, KEY_INBOX_APPROVE, KEY_INBOX_REJECT, KEY_INBOX_REVIEW,
    KEY_INBOX_SEEN, KEY_INBOX_RESUME, KEY_INBOX_RETURN, KEY_INBOX_LAND, KEY_INBOX_GOTO, KEY_INBOX_CHECK,
    KEY_INBOX_REMOVE, KEY_INBOX_PANE, KEY_INBOX_PANE_BACK, KEY_INBOX_PAGE_PREVIOUS, KEY_INBOX_PAGE_NEXT,
    /* statistics */
    KEY_STATS_UP, KEY_STATS_DOWN, KEY_STATS_OPEN, KEY_STATS_PAGE_PREVIOUS, KEY_STATS_PAGE_NEXT, KEY_STATS_RANGE,
    KEY_STATS_FILTER, KEY_STATS_GRAPH, KEY_STATS_PANE, KEY_STATS_WORKFLOW, KEY_STATS_METRIC, KEY_STATS_COHORTS,
    KEY_STATS_LOCATION, KEY_STATS_ATTENTION, KEY_STATS_RESET, KEY_STATS_FILTER_UP, KEY_STATS_FILTER_DOWN,
    KEY_STATS_FILTER_KEEP, KEY_STATS_FILTER_CLEAR,
    /* statistics rows of 3.0 whose jobs moved: [ ] and the filter keys */
    KEY_STATS_PAGE, KEY_STATS_WORKFLOW_PREVIOUS, KEY_STATS_WORKFLOW_NEXT, KEY_STATS_FIND, KEY_STATS_REFRESH,
    /* Hosts: the list, its panes and pages, then the open setup (hosts.c) */
    KEY_HOSTS_UP, KEY_HOSTS_DOWN, KEY_HOSTS_OPEN, KEY_HOSTS_ADD, KEY_HOSTS_EDIT, KEY_HOSTS_REMOVE,
    KEY_HOSTS_RETRY, KEY_HOSTS_ONBOARDING, KEY_HOSTS_FILTER, KEY_HOSTS_PANE, KEY_HOSTS_PANE_BACK,
    KEY_HOSTS_PAGE_PREVIOUS, KEY_HOSTS_PAGE_NEXT,
    KEY_SETUP_UP, KEY_SETUP_DOWN, KEY_SETUP_PAGE_UP, KEY_SETUP_PAGE_DOWN, KEY_SETUP_CONTINUE, KEY_SETUP_CANCEL,
    KEY_SETUP_BACKGROUND,
    /* Work: the list, the panes that scroll, the Run and Plan pages, then
     * what acts on the selection and on the screen (work.c) */
    KEY_HEADS_UP, KEY_HEADS_DOWN, KEY_HEADS_OPEN, KEY_HEADS_TALK, KEY_HEADS_COLLAPSE, KEY_HEADS_EXPAND,
    KEY_WS_UP, KEY_WS_DOWN, KEY_HEADS_PAGE_UP, KEY_HEADS_PAGE_DOWN, KEY_WS_OPEN,
    KEY_FLOW_UP, KEY_FLOW_DOWN, KEY_FLOW_LEFT, KEY_FLOW_RIGHT, KEY_FLOW_PAN_UP, KEY_FLOW_PAN_DOWN, KEY_FLOW_FOLLOW,
    KEY_FLOW_PAGE_UP, KEY_FLOW_PAGE_DOWN,
    KEY_PLAN_PROPOSAL, KEY_PLAN_VALIDATE, KEY_PLAN_RETURN, KEY_PLAN_EXECUTE, KEY_PLAN_LAND, KEY_PLAN_REVIEW,
    KEY_PLAN_STATUS,
    KEY_RUN_APPROVE, KEY_RUN_REJECT, KEY_RUN_RESUME, KEY_RUN_CANCEL,
    KEY_HEAD_REMOVE, KEY_HEAD_MARK, KEY_HEAD_MARK_ALL, KEY_HEAD_GROUP, KEY_TASK_NEW, KEY_HEADS_SEARCH,
    KEY_HEADS_LOCATION, KEY_HEADS_RETRY, KEY_HEAD_TECHNICAL,
    KEY_WS_PANE_NEXT, KEY_WS_PANE_PREVIOUS, KEY_WORK_PAGE_PREVIOUS, KEY_WORK_PAGE_NEXT, KEY_WS_ZOOM, KEY_WS_SPLIT,
    /* Work rows of earlier versions whose jobs moved: pages replaced the
     * layouts and the Workflows run keys, a and x took over attach and
     * interrupt, run.* act on remote tasks too, the conversation shows the
     * output */
    KEY_WS_COLLAPSE, KEY_WS_EXPAND, KEY_WS_TALK, KEY_WS_ATTACH, KEY_WS_CONVERSATION, KEY_WS_PLAN, KEY_WS_MONITOR,
    KEY_MONITOR_PREVIOUS_RUN, KEY_MONITOR_NEXT_RUN, KEY_FLOW_PREVIOUS_RUN, KEY_FLOW_NEXT_RUN,
    KEY_HEADS_ATTACH, KEY_HEADS_INTERRUPT, KEY_HEADS_COORDINATION, KEY_HEAD_OUTPUT,
    KEY_REMOTE_APPROVE, KEY_REMOTE_REJECT, KEY_REMOTE_RESUME, KEY_REMOTE_CANCEL,
    /* views */
    KEY_VIEWS_NEXT, KEY_VIEWS_PREVIOUS, KEY_VIEWS_LEFT, KEY_VIEWS_RIGHT,
    /* everywhere */
    KEY_VIEW_1, KEY_VIEW_2, KEY_VIEW_3, KEY_VIEW_4, KEY_VIEW_5, KEY_VIEW_6, KEY_VIEW_7, KEY_VIEW_8, KEY_VIEW_9,
    KEY_VIEW_CYCLE, KEY_VIEW_WORKSPACE, KEY_VIEW_OVERVIEW, KEY_VIEW_WORKFLOWS, KEY_VIEW_HOSTS, KEY_VIEW_ATTENTION,
    KEY_VIEW_STATS, KEY_THEME, KEY_PALETTE, KEY_REFRESH, KEY_BACK, KEY_HELP, KEY_QUIT,
    KEY_ACTIONS
};
#define KEY_INBOX_LAST KEY_INBOX_PAGE_NEXT
#define KEY_HOSTS_LAST KEY_SETUP_BACKGROUND
/* The first action that is not a Ctrl-chord command of the agent layer. */
#define KEY_VIEW_ACTIONS KEY_RESULT_UP
struct native_keys {
    struct kt_binding bindings[KEY_ACTIONS];
    struct kt_keymap map;
    /* The TERMINAL-scope chord being entered, and a chord of the views. */
    struct kt_keymap_state state, view_state;
    /* The keys file read at startup (or where it would be), and why it was
     * not applied exactly as written; problem is empty when it was. */
    char path[4096], problem[TEXT];
    /* The notice shown for the last chord that matched nothing. */
    char unbound[TEXT];
    /* bindings is the table for this mode: the control centre, or its fleet
     * entry, whose statistics are the hosts' own (key_mode). */
    bool ready, fleet;
};
/* The app's keys, with the built-in defaults until native_keys_load runs. */
struct native_keys *native_keys(struct app *app);
/* Reads the keys file: HYDRA_TUI_KEYS, else $HYDRA_HOME/tui/keys.conf (or
 * ~/.hydra). A missing default file means defaults. A refused line keeps that
 * action's default, as does unbinding terminal.leave (input could never
 * return to Hydra); keys that conflict within a scope chain put every key
 * back to its default. Either way keys->problem says which file, line and
 * why. default_file is false for deterministic rendering, which reads only an
 * explicit file. Call it after the mode (app->fleet) is known. */
void native_keys_load(struct app *app, bool default_file);
/* Writes the effective bindings in the keys-file syntax. */
void native_keys_print(struct app *app, FILE *out);
/* Display text of an action's first chord, such as "Ctrl-]"; false and empty
 * when the action is unbound. */
bool key_text(struct app *app, enum key_action action, char *out, size_t size);
/* Builds hint text from a template of segments separated by '|', written
 * joined by separator. "{terminal.leave}" becomes that action's chord. A
 * segment naming an unbound action is left out, or replaced by the text after
 * '~' when the segment has one: "a or {terminal.reconnect} reconnect~a reconnect". */
void key_hint(struct app *app, char *out, size_t size, const char *separator, const char *template_text);
/* The first template whose text fits width; the last one when none fits. */
void key_hint_fit(struct app *app, char *out, size_t size, const char *separator,
                  const char *const templates[], size_t count, int width);
/* While a chord is partly entered: its keys so far and what may follow, such
 * as "Ctrl-\ ...  x close / n next agent". False when no chord is pending. */
bool key_pending_text(struct app *app, char *out, size_t size);

/* ---- scope chains (bindings.c) ------------------------------------------ */

/* Every scope chain the control centre can be in, for checking the table. */
enum key_chain_id {
    KEY_CHAIN_WORK_LIST, KEY_CHAIN_WORK_CONVERSATION, KEY_CHAIN_WORK_DETAILS, KEY_CHAIN_WORK_PLAN, KEY_CHAIN_WORK_RUN,
    KEY_CHAIN_HOSTS, KEY_CHAIN_HOSTS_SETUP, KEY_CHAIN_STATISTICS, KEY_CHAIN_INBOX,
    KEY_CHAIN_REVIEW, KEY_CHAIN_HELP, KEY_CHAIN_RESULT, KEY_CHAIN_STATS_FILTER, KEY_CHAINS
};
extern const uint32_t key_chains[KEY_CHAINS];
/* The chain of the focused element now. */
enum key_chain_id key_chain(const struct app *app);
/* Whether a binding does something for the current selection; one that does
 * not is left out of the footer and help and its keys do nothing. */
bool key_applies(const struct app *app, enum key_action action);
/* Refuses a table in which two bindings live together in one chain share a
 * chord, or one chord begins another; error names them. */
bool key_chain_check(const struct kt_binding *bindings, size_t count, char *error, size_t size);
/* Runs an action as its keys would. */
void key_perform(struct app *app, enum key_action action);
/* The table with the bindings that do not apply now taken out of scope, and
 * the focused chain's scopes (the agent layer's left out). */
uint32_t key_applying(const struct app *app, struct kt_binding rows[KEY_ACTIONS]);
/* Matches a key event against the focused chain and runs the action it is
 * bound to. False when the key is bound to nothing there. */
bool key_dispatch(struct app *app, const struct kt_event *e);
/* The footer: the bindings of the focused chain that apply, fitted to the
 * area (lowest priority dropped first; help and quit stay). */
void key_footer(struct app *app, struct kt_canvas *canvas, struct kt_rect area);
/* The footer while typing to an agent: the chords that leave, close, move
 * between agents, scroll, change screen and quit. */
void key_terminal_footer(struct app *app, struct kt_canvas *canvas, struct kt_rect area);
/* Help for where the user is: every binding of the chain that applies,
 * grouped, then the agent-input chords. Returns the rows it takes at width;
 * draws them when canvas is not NULL, scrolled by skip rows. */
int key_help(struct app *app, struct kt_canvas *canvas, struct kt_rect area, int skip);
#endif
