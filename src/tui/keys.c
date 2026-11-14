#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* The binding table: every command Hydra takes from the keyboard, its name
 * in the keys file, the scopes it is live in, its label (footer and help),
 * the help group it is listed under and the footer priority (higher stays
 * longer; negative: help only). Chords come from default_keys, written in the
 * syntax of that file. Rows follow enum key_action; footers list them in this
 * order. */
#define KEYS_FILE_LIMIT (64U * 1024U)

#define G_AGENT "While typing to an agent"
#define G_HEADS "Heads"
#define G_WORKSPACE "Workspace"
#define G_PLAN "Planning"
#define G_RUNS "Runs"
#define G_ALL "Everywhere"
#define G_STATS "Stats"
#define G_FILTER "Filter"
#define ROW(id, key_scope, key_name, text, key_group, rank) \
    [id] = {.name = key_name, .label = text, .action = id, .scope = key_scope, .group = key_group, .priority = rank}
#define T KEY_SCOPE_TERMINAL
#define HEADVIEWS (KEY_SCOPE_HEADS | KEY_SCOPE_DETAIL | KEY_SCOPE_OVERVIEW)
#define WORKVIEWS (HEADVIEWS | KEY_SCOPE_WORKSPACE)
static const struct kt_binding key_rows[KEY_ACTIONS] = {
    ROW(KEY_TERMINAL_LEAVE, T, "terminal.leave", "leave input", G_AGENT, -1),
    ROW(KEY_TERMINAL_CLOSE, T, "terminal.close", "close view", G_AGENT, -1),
    ROW(KEY_TERMINAL_NEXT, T, "terminal.next", "next agent", G_AGENT, -1),
    ROW(KEY_TERMINAL_RECONNECT, T, "terminal.reconnect", "reconnect", G_AGENT, -1),
    ROW(KEY_TERMINAL_HISTORY, T, "terminal.history", "scroll", G_AGENT, -1),
    ROW(KEY_TERMINAL_LIVE, T, "terminal.live", "live output", G_AGENT, -1),
    ROW(KEY_APP_QUIT, T, "app.quit", "quit", G_AGENT, -1),
    ROW(KEY_VIEW_STATISTICS, T, "view.statistics", "statistics", G_AGENT, -1),
    ROW(KEY_LAYOUT_CONVERSATION, T, "workspace.layout.conversation", "conversation", G_AGENT, -1),
    ROW(KEY_LAYOUT_PLAN, T, "workspace.layout.plan", "plan", G_AGENT, -1),
    ROW(KEY_LAYOUT_MONITOR, T, "workspace.layout.monitor", "monitor", G_AGENT, -1),
    ROW(KEY_WORKSPACE_ZOOM, T, "workspace.zoom", "zoom", G_AGENT, -1),
    ROW(KEY_WORKSPACE_SPLIT, T, "workspace.split", "two agents", G_AGENT, -1),
    ROW(KEY_TERMINAL_LITERAL, T, "terminal.literal", "send that key to the agent", G_AGENT, -1),

    ROW(KEY_RESULT_UP, KEY_SCOPE_RESULT, "output.up", "scroll", "Output", 5),
    ROW(KEY_RESULT_DOWN, KEY_SCOPE_RESULT, "output.down", "scroll", "Output", 5),
    ROW(KEY_RESULT_PAGE_UP, KEY_SCOPE_RESULT, "output.page-up", "page", "Output", 3),
    ROW(KEY_RESULT_PAGE_DOWN, KEY_SCOPE_RESULT, "output.page-down", "page", "Output", 3),
    ROW(KEY_RESULT_CLOSE, KEY_SCOPE_RESULT, "output.close", "close", "Output", 9),
    ROW(KEY_RESULT_QUIT, KEY_SCOPE_RESULT, "output.quit", "quit", "Output", 8),
    ROW(KEY_HELP_UP, KEY_SCOPE_HELP, "help.up", "scroll", "Help", 5),
    ROW(KEY_HELP_DOWN, KEY_SCOPE_HELP, "help.down", "scroll", "Help", 5),
    ROW(KEY_HELP_PAGE_UP, KEY_SCOPE_HELP, "help.page-up", "page", "Help", 3),
    ROW(KEY_HELP_PAGE_DOWN, KEY_SCOPE_HELP, "help.page-down", "page", "Help", 3),
    ROW(KEY_HELP_CLOSE, KEY_SCOPE_HELP, "help.close", "close help", "Help", 9),

    ROW(KEY_REVIEW_UP, KEY_SCOPE_REVIEW, "review.up", "scroll", "Review", 9),
    ROW(KEY_REVIEW_DOWN, KEY_SCOPE_REVIEW, "review.down", "scroll", "Review", 9),
    ROW(KEY_REVIEW_PAGE_DOWN, KEY_SCOPE_REVIEW, "review.page-down", "page", "Review", 2),
    ROW(KEY_REVIEW_PAGE_UP, KEY_SCOPE_REVIEW, "review.page-up", "page", "Review", 2),
    ROW(KEY_REVIEW_TOP, KEY_SCOPE_REVIEW, "review.top", "top/end", "Review", -1),
    ROW(KEY_REVIEW_END, KEY_SCOPE_REVIEW, "review.end", "top/end", "Review", -1),
    ROW(KEY_REVIEW_NEXT_SECTION, KEY_SCOPE_REVIEW, "review.next-section", "section", "Review", 3),
    ROW(KEY_REVIEW_PREVIOUS_SECTION, KEY_SCOPE_REVIEW, "review.previous-section", "section", "Review", 3),
    ROW(KEY_REVIEW_REFERENCES, KEY_SCOPE_REVIEW, "review.references", "references", "Review", 6),
    ROW(KEY_REVIEW_IDENTITY, KEY_SCOPE_REVIEW, "review.identity", "IDs", "Review", 7),
    ROW(KEY_REVIEW_OPEN, KEY_SCOPE_REVIEW, "review.open", "open reference", "Review", 4),
    ROW(KEY_REVIEW_LOG, KEY_SCOPE_REVIEW, "review.log", "full log", "Review", 5),
    ROW(KEY_REVIEW_SUPPLY_LOG, KEY_SCOPE_REVIEW, "review.supply-log", "add log/transcript/PR", "Review", 5),
    ROW(KEY_REVIEW_SUPPLY_TRANSCRIPT, KEY_SCOPE_REVIEW, "review.supply-transcript", "add log/transcript/PR", "Review", 5),
    ROW(KEY_REVIEW_SUPPLY_PR, KEY_SCOPE_REVIEW, "review.supply-pr", "add log/transcript/PR", "Review", 5),
    ROW(KEY_REVIEW_PREVIOUS_ITEM, KEY_SCOPE_REVIEW, "review.previous-item", "item", "Review", -1),
    ROW(KEY_REVIEW_NEXT_ITEM, KEY_SCOPE_REVIEW, "review.next-item", "item", "Review", -1),
    ROW(KEY_REVIEW_RELOAD, KEY_SCOPE_REVIEW, "review.reload", "read again", "Review", -1),

    ROW(KEY_ATTENTION_UP, KEY_SCOPE_ATTENTION, "attention.up", "select", "Attention", 9),
    ROW(KEY_ATTENTION_DOWN, KEY_SCOPE_ATTENTION, "attention.down", "select", "Attention", 9),
    ROW(KEY_ATTENTION_OPEN, KEY_SCOPE_ATTENTION, "attention.open", "details", "Attention", 8),
    ROW(KEY_ATTENTION_REVIEW, KEY_SCOPE_ATTENTION, "attention.review", "review", "Attention", 7),
    ROW(KEY_ATTENTION_SEEN, KEY_SCOPE_ATTENTION, "attention.seen", "mark seen", "Attention", 6),
    ROW(KEY_ATTENTION_PAGE_DOWN, KEY_SCOPE_ATTENTION, "attention.page-down", "scroll", "Attention", 4),
    ROW(KEY_ATTENTION_PAGE_UP, KEY_SCOPE_ATTENTION, "attention.page-up", "scroll", "Attention", 4),

    ROW(KEY_STATS_UP, KEY_SCOPE_STATISTICS, "statistics.up", "select", G_STATS, 9),
    ROW(KEY_STATS_DOWN, KEY_SCOPE_STATISTICS, "statistics.down", "select", G_STATS, 9),
    ROW(KEY_STATS_OPEN, KEY_SCOPE_STATISTICS, "statistics.open", "evidence", G_STATS, 8),
    ROW(KEY_STATS_PAGE_PREVIOUS, KEY_SCOPE_STATISTICS, "statistics.page-previous", "sessions/runs", G_STATS, 7),
    ROW(KEY_STATS_PAGE_NEXT, KEY_SCOPE_STATISTICS, "statistics.page-next", "sessions/runs", G_STATS, 7),
    ROW(KEY_STATS_RANGE, KEY_SCOPE_STATISTICS, "statistics.range", "range", G_STATS, 6),
    ROW(KEY_STATS_FILTER, KEY_SCOPE_STATISTICS, "statistics.filter", "filter", G_STATS, 5),
    ROW(KEY_STATS_GRAPH, KEY_SCOPE_STATISTICS, "statistics.graph", "graph", G_STATS, 5),
    ROW(KEY_STATS_PANE, KEY_SCOPE_STATISTICS, "statistics.pane", "pane", G_STATS, 4),
    ROW(KEY_STATS_WORKFLOW, KEY_SCOPE_STATISTICS, "statistics.workflow", "workflow", G_STATS, 4),
    ROW(KEY_STATS_METRIC, KEY_SCOPE_STATISTICS, "statistics.metric", "metric", G_STATS, 4),
    ROW(KEY_STATS_COHORTS, KEY_SCOPE_STATISTICS, "statistics.cohorts", "cohorts", G_STATS, 3),
    ROW(KEY_STATS_LOCATION, KEY_SCOPE_STATISTICS, "statistics.location", "location", G_STATS, 3),
    ROW(KEY_STATS_ATTENTION, KEY_SCOPE_STATISTICS, "statistics.attention", "needs you only", G_STATS, 2),
    ROW(KEY_STATS_RESET, KEY_SCOPE_STATISTICS, "statistics.reset", "reset filters", G_STATS, 2),
    ROW(KEY_STATS_FILTER_UP, KEY_SCOPE_STATS_FILTER, "statistics.filter-up", "select", G_FILTER, 5),
    ROW(KEY_STATS_FILTER_DOWN, KEY_SCOPE_STATS_FILTER, "statistics.filter-down", "select", G_FILTER, 5),
    ROW(KEY_STATS_FILTER_KEEP, KEY_SCOPE_STATS_FILTER, "statistics.filter-keep", "keep filter", G_FILTER, 9),
    ROW(KEY_STATS_FILTER_CLEAR, KEY_SCOPE_STATS_FILTER, "statistics.filter-clear", "clear filter", G_FILTER, 8),
    ROW(KEY_STATS_PAGE, KEY_SCOPE_STATISTICS, "statistics.page", "sessions/runs", G_STATS, -1),
    ROW(KEY_STATS_WORKFLOW_PREVIOUS, KEY_SCOPE_STATISTICS, "statistics.workflow-previous", "workflow", G_STATS, -1),
    ROW(KEY_STATS_WORKFLOW_NEXT, KEY_SCOPE_STATISTICS, "statistics.workflow-next", "workflow", G_STATS, -1),
    ROW(KEY_STATS_FIND, KEY_SCOPE_STATISTICS, "statistics.find", "find", G_STATS, -1),
    ROW(KEY_STATS_REFRESH, KEY_SCOPE_STATISTICS, "statistics.refresh", "refresh", G_STATS, -1),

    ROW(KEY_HOSTS_UP, KEY_SCOPE_HOSTS, "hosts.up", "select", "Hosts", 9),
    ROW(KEY_HOSTS_DOWN, KEY_SCOPE_HOSTS, "hosts.down", "select", "Hosts", 9),
    ROW(KEY_HOSTS_OPEN, KEY_SCOPE_HOSTS, "hosts.open", "open", "Hosts", 8),
    ROW(KEY_HOSTS_ADD, KEY_SCOPE_HOSTS, "hosts.add", "add a host", "Hosts", 7),
    ROW(KEY_HOSTS_EDIT, KEY_SCOPE_HOSTS, "hosts.edit", "edit", "Hosts", 6),
    ROW(KEY_HOSTS_REMOVE, KEY_SCOPE_HOSTS, "hosts.remove", "remove", "Hosts", 6),
    ROW(KEY_HOSTS_RETRY, KEY_SCOPE_HOSTS, "hosts.retry", "observe again", "Hosts", 5),
    ROW(KEY_HOSTS_ONBOARDING, KEY_SCOPE_HOSTS, "hosts.onboarding", "onboarding", "Hosts", 5),

    ROW(KEY_RECOVERY_UP, KEY_SCOPE_RECOVERY, "recovery.up", "select", "Recovery", 9),
    ROW(KEY_RECOVERY_DOWN, KEY_SCOPE_RECOVERY, "recovery.down", "select", "Recovery", 9),
    ROW(KEY_RECOVERY_CHECK, KEY_SCOPE_RECOVERY, "recovery.check", "run the check", "Recovery", 8),
    ROW(KEY_RECOVERY_REMOVE, KEY_SCOPE_RECOVERY, "recovery.remove", "remove", "Recovery", 7),
    ROW(KEY_RECOVERY_EXPLAIN, KEY_SCOPE_RECOVERY, "recovery.explain", "explain", "Recovery", 6),

    ROW(KEY_FLOW_UP, KEY_SCOPE_WORKFLOWS, "workflows.up", "step", "Workflows", 9),
    ROW(KEY_FLOW_DOWN, KEY_SCOPE_WORKFLOWS, "workflows.down", "step", "Workflows", 9),
    ROW(KEY_FLOW_PREVIOUS_RUN, KEY_SCOPE_WORKFLOWS, "workflows.previous-run", "run", "Workflows", 8),
    ROW(KEY_FLOW_NEXT_RUN, KEY_SCOPE_WORKFLOWS, "workflows.next-run", "run", "Workflows", 8),
    ROW(KEY_FLOW_LEFT, KEY_SCOPE_WORKFLOWS, "workflows.pan-left", "pan", "Workflows", 6),
    ROW(KEY_FLOW_RIGHT, KEY_SCOPE_WORKFLOWS, "workflows.pan-right", "pan", "Workflows", 6),
    ROW(KEY_FLOW_PAN_UP, KEY_SCOPE_WORKFLOWS, "workflows.pan-up", "pan up/down", "Workflows", 5),
    ROW(KEY_FLOW_PAN_DOWN, KEY_SCOPE_WORKFLOWS, "workflows.pan-down", "pan up/down", "Workflows", 5),
    ROW(KEY_FLOW_FOLLOW, KEY_SCOPE_WORKFLOWS, "workflows.follow", "recentre", "Workflows", 4),

    ROW(KEY_WS_UP, KEY_SCOPE_WORKSPACE, "workspace.up", "select", G_WORKSPACE, 9),
    ROW(KEY_WS_DOWN, KEY_SCOPE_WORKSPACE, "workspace.down", "select", G_WORKSPACE, 9),
    ROW(KEY_WS_COLLAPSE, KEY_SCOPE_WORKSPACE, "workspace.collapse", "collapse/expand", G_WORKSPACE, -1),
    ROW(KEY_WS_EXPAND, KEY_SCOPE_WORKSPACE, "workspace.expand", "collapse/expand", G_WORKSPACE, -1),
    ROW(KEY_WS_OPEN, KEY_SCOPE_WORKSPACE, "workspace.open", "open", G_WORKSPACE, 9),
    ROW(KEY_WS_PANE_NEXT, KEY_SCOPE_WORKSPACE, "workspace.pane-next", "pane", G_WORKSPACE, 10),
    ROW(KEY_WS_PANE_PREVIOUS, KEY_SCOPE_WORKSPACE, "workspace.pane-previous", "pane", G_WORKSPACE, 10),
    ROW(KEY_WS_TALK, KEY_SCOPE_WORKSPACE, "workspace.talk", "talk to agent", G_WORKSPACE, 9),
    ROW(KEY_WS_ATTACH, KEY_SCOPE_WORKSPACE, "workspace.attach", "attach", G_WORKSPACE, 9),
    ROW(KEY_WS_CONVERSATION, KEY_SCOPE_WORKSPACE, "workspace.conversation", "conversation", G_WORKSPACE, 3),
    ROW(KEY_WS_PLAN, KEY_SCOPE_WORKSPACE, "workspace.plan", "plan review", G_WORKSPACE, 5),
    ROW(KEY_WS_MONITOR, KEY_SCOPE_WORKSPACE, "workspace.monitor", "monitor", G_WORKSPACE, 4),
    ROW(KEY_WS_ZOOM, KEY_SCOPE_WORKSPACE, "workspace.zoom-pane", "zoom", G_WORKSPACE, 2),
    ROW(KEY_WS_SPLIT, KEY_SCOPE_WORKSPACE, "workspace.two-agents", "two agents", G_WORKSPACE, -1),
    ROW(KEY_PLAN_PROPOSAL, KEY_SCOPE_WORKSPACE, "plan.proposal", "review proposal", G_PLAN, 7),
    ROW(KEY_PLAN_VALIDATE, KEY_SCOPE_WORKSPACE, "plan.validate", "validate", G_PLAN, 7),
    ROW(KEY_PLAN_RETURN, KEY_SCOPE_WORKSPACE, "plan.return", "request changes", G_PLAN, 7),
    ROW(KEY_PLAN_EXECUTE, KEY_SCOPE_WORKSPACE, "plan.execute", "execute", G_PLAN, 8),
    ROW(KEY_MONITOR_PREVIOUS_RUN, KEY_SCOPE_MONITOR, "monitor.previous-run", "run", G_RUNS, 6),
    ROW(KEY_MONITOR_NEXT_RUN, KEY_SCOPE_MONITOR, "monitor.next-run", "run", G_RUNS, 6),
    ROW(KEY_RUN_APPROVE, KEY_SCOPE_MONITOR, "run.approve", "approve", G_RUNS, 8),
    ROW(KEY_RUN_REJECT, KEY_SCOPE_MONITOR, "run.reject", "reject", G_RUNS, 8),
    ROW(KEY_RUN_RESUME, KEY_SCOPE_MONITOR, "run.resume", "resume", G_RUNS, 7),
    ROW(KEY_RUN_CANCEL, KEY_SCOPE_MONITOR, "run.cancel", "cancel", G_RUNS, 7),

    ROW(KEY_HEADS_UP, HEADVIEWS, "heads.up", "select", G_HEADS, 9),
    ROW(KEY_HEADS_DOWN, HEADVIEWS, "heads.down", "select", G_HEADS, 9),
    ROW(KEY_HEADS_COLLAPSE, KEY_SCOPE_HEADS | KEY_SCOPE_OVERVIEW, "heads.collapse", "collapse/expand", G_HEADS, 8),
    ROW(KEY_HEADS_EXPAND, KEY_SCOPE_HEADS | KEY_SCOPE_OVERVIEW, "heads.expand", "collapse/expand", G_HEADS, 8),
    ROW(KEY_HEADS_OPEN, HEADVIEWS, "heads.open", "details", G_HEADS, 8),
    ROW(KEY_HEADS_TALK, HEADVIEWS, "heads.talk", "talk to agent", G_HEADS, 8),
    ROW(KEY_HEADS_ATTACH, HEADVIEWS, "heads.attach", "attach", G_HEADS, 8),
    ROW(KEY_HEADS_INTERRUPT, WORKVIEWS, "heads.interrupt", "interrupt", G_HEADS, 7),
    ROW(KEY_HEADS_COORDINATION, KEY_SCOPE_DETAIL, "heads.coordination", "coordination", G_HEADS, 4),
    ROW(KEY_HEADS_PAGE_UP, KEY_SCOPE_DETAIL, "heads.page-up", "page output", G_HEADS, -1),
    ROW(KEY_HEADS_PAGE_DOWN, KEY_SCOPE_DETAIL, "heads.page-down", "page output", G_HEADS, -1),
    ROW(KEY_TASK_NEW, WORKVIEWS, "task.new", "new task", G_HEADS, 7),
    ROW(KEY_HEAD_REMOVE, WORKVIEWS, "head.remove", "remove", G_HEADS, 6),
    ROW(KEY_HEAD_MARK, WORKVIEWS, "head.mark", "mark", G_HEADS, -1),
    ROW(KEY_HEAD_MARK_ALL, HEADVIEWS, "head.mark-all", "mark all", G_HEADS, -1),
    ROW(KEY_HEAD_GROUP, WORKVIEWS, "head.group", "group marked", G_HEADS, 5),
    ROW(KEY_HEAD_OUTPUT, WORKVIEWS, "head.output", "output", G_HEADS, 5),
    ROW(KEY_HEAD_TECHNICAL, WORKVIEWS, "head.technical", "technical", G_HEADS, 3),
    ROW(KEY_REMOTE_APPROVE, HEADVIEWS, "remote.approve", "approve task", G_HEADS, 8),
    ROW(KEY_REMOTE_REJECT, HEADVIEWS, "remote.reject", "reject task", G_HEADS, 8),
    ROW(KEY_REMOTE_RESUME, HEADVIEWS, "remote.resume", "resume task", G_HEADS, 7),
    ROW(KEY_REMOTE_CANCEL, HEADVIEWS, "remote.cancel", "cancel task", G_HEADS, 7),
    ROW(KEY_HEADS_LOCATION, KEY_SCOPE_HEADS | KEY_SCOPE_OVERVIEW, "heads.location", "location", G_HEADS, 5),
    ROW(KEY_HEADS_RETRY, HEADVIEWS, "heads.retry", "observe host again", G_HEADS, 4),
    ROW(KEY_HEADS_SEARCH, WORKVIEWS, "heads.search", "search", G_HEADS, 4),

    ROW(KEY_VIEWS_NEXT, KEY_SCOPE_VIEWS, "views.next", "views", G_ALL, 2),
    ROW(KEY_VIEWS_PREVIOUS, KEY_SCOPE_VIEWS, "views.previous", "views", G_ALL, 2),
    ROW(KEY_VIEWS_LEFT, KEY_SCOPE_VIEW_ARROWS, "views.left", "previous/next view", G_ALL, -1),
    ROW(KEY_VIEWS_RIGHT, KEY_SCOPE_VIEW_ARROWS, "views.right", "previous/next view", G_ALL, -1),

    ROW(KEY_VIEW_1, KEY_SCOPE_GLOBAL, "view.1", "view by number", G_ALL, -1),
    ROW(KEY_VIEW_2, KEY_SCOPE_GLOBAL, "view.2", "view by number", G_ALL, -1),
    ROW(KEY_VIEW_3, KEY_SCOPE_GLOBAL, "view.3", "view by number", G_ALL, -1),
    ROW(KEY_VIEW_4, KEY_SCOPE_GLOBAL, "view.4", "view by number", G_ALL, -1),
    ROW(KEY_VIEW_5, KEY_SCOPE_GLOBAL, "view.5", "view by number", G_ALL, -1),
    ROW(KEY_VIEW_6, KEY_SCOPE_GLOBAL, "view.6", "view by number", G_ALL, -1),
    ROW(KEY_VIEW_7, KEY_SCOPE_GLOBAL, "view.7", "view by number", G_ALL, -1),
    ROW(KEY_VIEW_8, KEY_SCOPE_GLOBAL, "view.8", "view by number", G_ALL, -1),
    ROW(KEY_VIEW_9, KEY_SCOPE_GLOBAL, "view.9", "view by number", G_ALL, -1),
    ROW(KEY_VIEW_CYCLE, KEY_SCOPE_JUMPS, "view.cycle", "next view", G_ALL, -1),
    ROW(KEY_VIEW_WORKSPACE, KEY_SCOPE_JUMPS, "view.workspace", "workspace", G_ALL, -1),
    ROW(KEY_VIEW_OVERVIEW, KEY_SCOPE_JUMPS, "view.overview", "overview", G_ALL, -1),
    ROW(KEY_VIEW_WORKFLOWS, KEY_SCOPE_FLOW_JUMP, "view.workflows", "workflows", G_ALL, -1),
    ROW(KEY_VIEW_HOSTS, KEY_SCOPE_JUMPS, "view.hosts", "hosts", G_ALL, -1),
    ROW(KEY_VIEW_ATTENTION, KEY_SCOPE_JUMPS, "view.attention", "attention", G_ALL, -1),
    ROW(KEY_VIEW_STATS, KEY_SCOPE_JUMPS, "view.stats", "statistics", G_ALL, -1),
    ROW(KEY_THEME, KEY_SCOPE_JUMPS, "app.theme", "theme", G_ALL, -1),
    ROW(KEY_PALETTE, KEY_SCOPE_GLOBAL, "app.actions", "more actions", G_ALL, 3),
    ROW(KEY_REFRESH, KEY_SCOPE_GLOBAL, "app.refresh", "refresh", G_ALL, -1),
    ROW(KEY_BACK, KEY_SCOPE_GLOBAL, "app.back", "back", G_ALL, 1),
    ROW(KEY_HELP, KEY_SCOPE_GLOBAL, "app.help", "help", G_ALL, 100),
    ROW(KEY_QUIT, KEY_SCOPE_GLOBAL, "app.exit", "quit", G_ALL, 100),
};
#undef T

/* Which mode a row belongs to: both, the local control centre only (the
 * fleet entry's statistics are the hosts' own), or neither. Local and remote
 * work share every other key, so a retired row is one whose job another row
 * now does: a (heads.talk) attaches to remote heads too, x (head.remove)
 * interrupts a selected remote head. A row that is not live (scope 0) keeps
 * its name, so keys files of earlier versions still load. */
enum { MODE_BOTH, MODE_LOCAL, MODE_RETIRED };
static const unsigned char key_modes[KEY_ACTIONS] = {
    [KEY_REVIEW_SUPPLY_LOG] = MODE_BOTH,
    [KEY_STATS_PAGE_PREVIOUS] = MODE_LOCAL, [KEY_STATS_PAGE_NEXT] = MODE_LOCAL, [KEY_STATS_GRAPH] = MODE_LOCAL,
    [KEY_STATS_RANGE] = MODE_LOCAL, [KEY_STATS_METRIC] = MODE_LOCAL, [KEY_STATS_WORKFLOW] = MODE_LOCAL,
    [KEY_STATS_COHORTS] = MODE_LOCAL, [KEY_STATS_LOCATION] = MODE_LOCAL,
    /* Stats pages are [ ], its filter is typed after /, ctrl+r reads again. */
    [KEY_STATS_PAGE] = MODE_RETIRED, [KEY_STATS_WORKFLOW_PREVIOUS] = MODE_RETIRED, [KEY_STATS_WORKFLOW_NEXT] = MODE_RETIRED,
    [KEY_STATS_FIND] = MODE_RETIRED, [KEY_STATS_REFRESH] = MODE_RETIRED,
    [KEY_WS_ATTACH] = MODE_RETIRED, [KEY_HEADS_ATTACH] = MODE_RETIRED, [KEY_HEADS_INTERRUPT] = MODE_RETIRED,
};

/* Ctrl-] alone leaves agent input; every other agent-layer command follows
 * Ctrl-\. Neither is tmux's prefix, so the tmux an agent runs in keeps its
 * own, and the terminal.literal chords send either key on to the agent. */
static const char default_keys[] =
    "terminal.leave = ctrl+], ctrl+\\ tab\n"
    "terminal.close = ctrl+\\ x\n"
    "terminal.next = ctrl+\\ n\n"
    "terminal.reconnect = ctrl+\\ r\n"
    "terminal.history = ctrl+\\ [\n"
    "terminal.live = ctrl+\\ ]\n"
    "app.quit = ctrl+\\ q\n"
    "view.statistics = ctrl+\\ D\n"
    "workspace.layout.conversation = ctrl+\\ A\n"
    "workspace.layout.plan = ctrl+\\ B\n"
    "workspace.layout.monitor = ctrl+\\ C\n"
    "workspace.zoom = ctrl+\\ z\n"
    "workspace.split = ctrl+\\ S\n"
    "terminal.literal = ctrl+\\ ctrl+\\, ctrl+\\ ctrl+]\n"
    "output.up = up, k\noutput.down = down, j\noutput.page-up = pgup\noutput.page-down = pgdn\n"
    "output.close = enter, esc\noutput.quit = q\n"
    "help.up = up, k\nhelp.down = down, j\nhelp.page-up = pgup, b\nhelp.page-down = pgdn, space\nhelp.close = ?, esc\n"
    "review.up = up, k\nreview.down = down, j\nreview.page-down = space, pgdn\nreview.page-up = b, pgup\n"
    "review.top = g\nreview.end = G\nreview.next-section = n\nreview.previous-section = N\n"
    "review.references = f\nreview.identity = i\nreview.open = enter, o\nreview.log = L\n"
    "review.supply-log = l\nreview.supply-transcript = t\nreview.supply-pr = p\n"
    "review.previous-item = [\nreview.next-item = ]\nreview.reload = r\n"
    "attention.up = up, k\nattention.down = down, j\nattention.open = enter\nattention.review = r\nattention.seen = s\n"
    "attention.page-down = space, pgdn\nattention.page-up = b, pgup\n"
    "statistics.up = up, k\nstatistics.down = down, j\nstatistics.open = enter\n"
    "statistics.page-previous = [\nstatistics.page-next = ]\nstatistics.range = T\nstatistics.filter = /\n"
    "statistics.graph = g\nstatistics.pane = tab, shift+tab\nstatistics.workflow = w\nstatistics.metric = M\n"
    "statistics.cohorts = C\nstatistics.location = L\nstatistics.attention = !\nstatistics.reset = 0\n"
    "statistics.filter-up = up\nstatistics.filter-down = down\nstatistics.filter-keep = enter\n"
    "statistics.filter-clear = esc\n"
    "statistics.page = s\nstatistics.workflow-previous = [\nstatistics.workflow-next = ]\n"
    "statistics.find = /\nstatistics.refresh = r\n"
    "hosts.up = up, k\nhosts.down = down, j\nhosts.open = enter\nhosts.add = A\nhosts.edit = e\nhosts.remove = x\n"
    "hosts.retry = r\nhosts.onboarding = O\n"
    "recovery.up = up, k\nrecovery.down = down, j\nrecovery.check = enter\nrecovery.remove = x\nrecovery.explain = d\n"
    "workflows.up = up, k\nworkflows.down = down, j\nworkflows.previous-run = [\nworkflows.next-run = ]\n"
    "workflows.pan-left = left, h\nworkflows.pan-right = right, l\nworkflows.pan-up = K\nworkflows.pan-down = J\n"
    "workflows.follow = enter\n"
    "workspace.up = up, k\nworkspace.down = down, j\nworkspace.collapse = left, h\nworkspace.expand = right, l\n"
    "workspace.open = enter\nworkspace.pane-next = tab\nworkspace.pane-previous = shift+tab\n"
    "workspace.talk = a\nworkspace.attach = a\nworkspace.conversation = A\nworkspace.plan = B\n"
    "workspace.monitor = C\nworkspace.zoom-pane = z\nworkspace.two-agents = S\n"
    "plan.proposal = P\nplan.validate = V\nplan.return = F\nplan.execute = E\n"
    "monitor.previous-run = [\nmonitor.next-run = ]\n"
    "run.approve = Y\nrun.reject = N\nrun.resume = R\nrun.cancel = X\n"
    "remote.approve = Y\nremote.reject = N\nremote.resume = R\nremote.cancel = X\n"
    "heads.location = L\nheads.retry = r\n"
    "heads.up = up, k\nheads.down = down, j\nheads.collapse = h\nheads.expand = l\n"
    "heads.open = enter\nheads.talk = a\nheads.attach = a\nheads.interrupt = c\nheads.coordination = c\n"
    "heads.page-up = pgup\nheads.page-down = pgdn\n"
    "task.new = n\nhead.remove = x\nhead.mark = space\nhead.mark-all = A\nhead.group = G\nhead.output = p\n"
    "head.technical = d\nheads.search = /\n"
    "views.next = tab\nviews.previous = shift+tab\nviews.left = left\nviews.right = right\n"
    "view.1 = 1\nview.2 = 2\nview.3 = 3\nview.4 = 4\nview.5 = 5\nview.6 = 6\nview.7 = 7\nview.8 = 8\nview.9 = 9\n"
    "view.cycle = v\nview.workspace = W\nview.overview = o\nview.workflows = w\nview.hosts = H\n"
    "view.attention = I\nview.stats = D\napp.theme = t\napp.actions = :\napp.refresh = ctrl+r\n"
    "app.back = esc\napp.help = ?\napp.exit = q\n";

/* Fills rows with every action and its default chords. */
static void default_bindings(struct kt_binding rows[KEY_ACTIONS]) {
    struct kt_keymap map = {rows, KEY_ACTIONS};
    char error[TEXT];
    size_t i;
    memcpy(rows, key_rows, sizeof(key_rows));
    (void)kt_keymap_configure(&map, default_keys, sizeof(default_keys) - 1U, error, sizeof(error));
    for (i = 0; i < KEY_ACTIONS; i++) rows[i].line = 0;
}

/* Rows of the other mode keep their names and chords but are live nowhere. */
static void keys_mode(struct native_keys *k) {
    size_t i;
    for (i = 0; i < KEY_ACTIONS; i++)
        if ((key_modes[i] == MODE_LOCAL && k->fleet) || key_modes[i] == MODE_RETIRED) k->bindings[i].scope = 0;
        else k->bindings[i].scope = key_rows[i].scope;
}

/* What --print-keys says about a row that is not live in this mode. */
static const char *mode_note(const struct native_keys *k, size_t row) {
    if (k->bindings[row].scope) return "";
    if (key_modes[row] == MODE_RETIRED) return " (no longer used)";
    return k->fleet ? " (local only)" : "";
}

static void keys_defaults(struct native_keys *k) {
    default_bindings(k->bindings);
    k->map.bindings = k->bindings; k->map.count = KEY_ACTIONS;
    keys_mode(k);
}

/* Without a chord for terminal.leave nothing typed would ever return input to
 * Hydra, so a keys file cannot unbind it: false after restoring its default. */
static bool keep_leave(struct native_keys *k) {
    struct kt_binding rows[KEY_ACTIONS];
    if (k->bindings[KEY_TERMINAL_LEAVE].chord_count) return true;
    default_bindings(rows);
    k->bindings[KEY_TERMINAL_LEAVE] = rows[KEY_TERMINAL_LEAVE];
    return false;
}

struct native_keys *native_keys(struct app *app) {
    struct native_keys *k = &app->keys;
    if (!k->ready || k->fleet != app->fleet) {
        memset(k, 0, sizeof(*k));
        k->fleet = app->fleet;
        keys_defaults(k);
        k->ready = true;
    }
    k->map.bindings = k->bindings;
    return k;
}

/* HYDRA_TUI_KEYS names the file outright; otherwise it lives in Hydra's home. */
static bool keys_path(char *out, size_t size, bool *named) {
    const char *file = getenv("HYDRA_TUI_KEYS"), *home = getenv("HYDRA_HOME"), *user = getenv("HOME");
    int n;
    *named = file && file[0];
    out[0] = '\0';
    if (*named) n = snprintf(out, size, "%s", file);
    else if (home && home[0]) n = snprintf(out, size, "%s/tui/keys.conf", home);
    else if (user && user[0]) n = snprintf(out, size, "%s/.hydra/tui/keys.conf", user);
    else return false;
    if (n > 0 && (size_t)n < size) return true;
    out[0] = '\0';
    return false;
}

/* Records why the keys file was not applied as written, naming it. The file
 * name and reason come first so a narrow status line still shows them. */
static void keys_problem(struct native_keys *k, const char *reason, const char *outcome) {
    const char *name = strrchr(k->path, '/');
    name = name ? name + 1 : k->path;
    if (snprintf(k->problem, sizeof(k->problem), "%s %s; %s (%s)", name, reason, outcome, k->path) >= (int)sizeof(k->problem))
        (void)snprintf(k->problem, sizeof(k->problem), "%.40s %.170s; %.30s", name, reason, outcome);
}

/* Reads the whole file; NULL with a reason, or with none when it is absent. */
static char *keys_read(const char *path, size_t *length, char *reason, size_t size) {
    FILE *in = fopen(path, "rb");
    char *text;
    reason[0] = '\0';
    if (!in) {
        if (errno != ENOENT) snprintf(reason, size, "cannot be read: %s", strerror(errno));
        return NULL;
    }
    text = malloc(KEYS_FILE_LIMIT + 1U);
    *length = text ? fread(text, 1, KEYS_FILE_LIMIT + 1U, in) : 0;
    if (!text || ferror(in)) snprintf(reason, size, "cannot be read");
    else if (*length > KEYS_FILE_LIMIT) snprintf(reason, size, "is larger than 64 KiB");
    (void)fclose(in);
    if (!reason[0]) return text;
    free(text);
    return NULL;
}

void native_keys_load(struct app *app, bool default_file) {
    struct native_keys *k = native_keys(app);
    char reason[TEXT], conflict[TEXT + 32], *text;
    size_t length = 0;
    bool named;
    k->problem[0] = '\0';
    if (!keys_path(k->path, sizeof(k->path), &named) || (!named && !default_file)) return;
    text = keys_read(k->path, &length, reason, sizeof(reason));
    if (!text) {
        if (reason[0]) keys_problem(k, reason, "using the default keys");
        else if (named) keys_problem(k, "does not exist", "using the default keys");
        return;
    }
    if (!kt_keymap_configure(&k->map, text, length, reason, sizeof(reason)))
        keys_problem(k, reason, "other lines applied");
    free(text);
    if (!keep_leave(k) && !k->problem[0]) keys_problem(k, "leaves terminal.leave unbound", "its default is kept");
    keys_mode(k);
    if (key_chain_check(k->bindings, KEY_ACTIONS, reason, sizeof(reason))) return;
    keys_defaults(k);
    snprintf(conflict, sizeof(conflict), "has conflicting keys: %s", reason);
    keys_problem(k, conflict, "using the default keys");
}

void native_keys_print(struct app *app, FILE *out) {
    struct native_keys *k = native_keys(app);
    size_t i;
    const char *group = NULL;
    fputs("# Hydra TUI keys: every key of the control centre, by action. The first group\n"
          "# works while you type to an attached agent; the others are the keys of a view.\n"
          "# One \"action = chord[, chord]\" per line; \"none\" unbinds; \"#\" starts a comment.\n"
          "# A chord is one to four keys pressed in order, such as ctrl+] or ctrl+\\ x.\n"
          "# Save as $HYDRA_HOME/tui/keys.conf (default ~/.hydra) or name a file in HYDRA_TUI_KEYS.\n", out);
    for (i = 0; i < KEY_ACTIONS; i++) {
        char line[256];
        if (!group || strcmp(group, key_rows[i].group)) { group = key_rows[i].group; fprintf(out, "# %s\n", group); }
        (void)kt_binding_config(&k->bindings[i], line, sizeof(line));
        fprintf(out, "%-46s # %s%s\n", line, k->bindings[i].label, mode_note(k, i));
    }
}

bool key_text(struct app *app, enum key_action action, char *out, size_t size) {
    const struct kt_binding *b = &native_keys(app)->bindings[action];
    if (size) out[0] = '\0';
    if (!b->chord_count) return false;
    (void)kt_chord_format(&b->chords[0], out, size);
    return true;
}

/* Appends at most the room that is left; out stays terminated. */
static void hint_append(char *out, size_t size, const char *text, size_t length) {
    size_t used = strlen(out);
    if (used + 1U >= size) return;
    if (length > size - used - 1U) length = size - used - 1U;
    memcpy(out + used, text, length);
    out[used + length] = '\0';
}

/* Appends the first chord of the action named in braces at text; false when
 * the name is unknown or the action is unbound. Returns the reference's end. */
static bool hint_reference(struct app *app, const char *text, const char *end, const char **next, char *out, size_t size) {
    const char *close = memchr(text, '}', (size_t)(end - text));
    const struct kt_binding *b;
    char name[64], chord[96];
    if (!close || (size_t)(close - text) > sizeof(name)) return false;
    memcpy(name, text + 1, (size_t)(close - text) - 1U);
    name[close - text - 1] = '\0';
    b = kt_keymap_find(&native_keys(app)->map, name);
    if (!b || !b->chord_count) return false;
    (void)kt_chord_format(&b->chords[0], chord, sizeof(chord));
    hint_append(out, size, chord, strlen(chord));
    *next = close + 1;
    return true;
}

/* Expands one segment into out; false when it names an unbound action. */
static bool hint_segment(struct app *app, const char *text, const char *end, char *out, size_t size) {
    out[0] = '\0';
    while (text < end) {
        const char *brace = memchr(text, '{', (size_t)(end - text));
        hint_append(out, size, text, (size_t)((brace ? brace : end) - text));
        if (!brace) break;
        if (!hint_reference(app, brace, end, &text, out, size)) return false;
    }
    return true;
}

/* One segment's text: its expansion, or the alternative after '~' when it
 * names an unbound action. False when the segment is left out. */
static bool hint_text(struct app *app, const char *segment, const char *end, char *out, size_t size) {
    const char *alternative = memchr(segment, '~', (size_t)(end - segment));
    if (hint_segment(app, segment, alternative ? alternative : end, out, size)) return true;
    if (!alternative) return false;
    out[0] = '\0';
    hint_append(out, size, alternative + 1, (size_t)(end - alternative) - 1U);
    return true;
}

void key_hint(struct app *app, char *out, size_t size, const char *separator, const char *template_text) {
    const char *segment = template_text;
    if (!size) return;
    out[0] = '\0';
    while (*segment) {
        const char *end = strchr(segment, '|');
        char text[512];
        if (!end) end = segment + strlen(segment);
        if (hint_text(app, segment, end, text, sizeof(text))) {
            if (out[0]) hint_append(out, size, separator, strlen(separator));
            hint_append(out, size, text, strlen(text));
        }
        segment = *end ? end + 1 : end;
    }
}

void key_hint_fit(struct app *app, char *out, size_t size, const char *separator,
                  const char *const templates[], size_t count, int width) {
    size_t i;
    if (size) out[0] = '\0';
    for (i = 0; i < count; i++) {
        key_hint(app, out, size, separator, templates[i]);
        if ((int)strlen(out) <= width) return;
    }
}

/* The keys of chord after its first skip, as a chord of their own. */
static void chord_rest(const struct kt_chord *chord, unsigned skip, struct kt_chord *rest) {
    unsigned i;
    memset(rest, 0, sizeof(*rest));
    for (i = skip; i < chord->count; i++) rest->keys[rest->count++] = chord->keys[i];
}

bool key_pending_text(struct app *app, char *out, size_t size) {
    struct native_keys *k = native_keys(app);
    const struct kt_chord *pending = &k->state.pending;
    char text[96];
    const char *join = " ";
    size_t i;
    unsigned c;
    if (!pending->count) return false;
    (void)kt_chord_format(pending, text, sizeof(text));
    snprintf(out, size, "%s %s ", text, app->ascii ? "..." : "\xe2\x80\xa6");
    for (i = 0; i < KEY_ACTIONS; i++) {
        const struct kt_binding *b = &k->bindings[i];
        for (c = 0; c < b->chord_count; c++) {
            struct kt_chord rest;
            if (!(b->scope & KEY_SCOPE_TERMINAL) || !kt_chord_extends(&b->chords[c], pending)) continue;
            chord_rest(&b->chords[c], pending->count, &rest);
            (void)kt_chord_format(&rest, text, sizeof(text));
            text_append(out, size, "%s%s %s", join, text, b->label);
            join = dot(app);
        }
    }
    return true;
}
