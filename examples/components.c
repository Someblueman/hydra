/* components: every component in one small application.
 *
 *   tabs       Heads, Table, Log and Settings pages (tab, [ ], 1-4, click)
 *   list       heads in sections, filtered as you type with '/'
 *   viewport   the selected head's details, and a log that follows its end
 *   table      the same heads in sortable columns ('s', 'S', click a heading)
 *   form       settings inline, and a new task in a dialog ('n')
 *   confirm    removing a head ('x')
 *   palette    every command, fuzzy filtered (':')
 *   help       the footer, and every key in a dialog ('?')
 *   spinner, progress bar, toasts: a pretend job ('w') and what it reports
 *
 * 't' changes the theme and ctrl+c or 'q' quits. The footer is generated
 * from the binding tables that drive the keys.
 *
 *   components            interactive
 *   components --print    the first screen at 100x30 in each built-in theme */
#include "components.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- data ---------------------------------------------------------------- */

struct head { const char *name, *status, *agent, *report, *group, *icon; kt_color tone; bool section; };
static const struct head heads[] = {
    {"This project", NULL, NULL, NULL, NULL, NULL, KT_DEFAULT, true},
    {"feature-live", "running", "codex", "done", "alpha", "\xe2\x97\x8f", KT_COLOR_SUCCESS, false},
    {"feature-stale", "terminal gone", "claude", NULL, "alpha", "\xe2\x96\xb2", KT_COLOR_WARNING, false},
    {"feature-unavailable", "unknown", "shell", NULL, NULL, "?", KT_COLOR_MUTED, false},
    {"fix-login-redirect", "running", "codex", NULL, "beta", "\xe2\x97\x8f", KT_COLOR_SUCCESS, false},
    {"docs-components", "stopped", "aider", "done", NULL, "\xe2\x97\x8b", KT_COLOR_MUTED, false},
    {"Runs", NULL, NULL, NULL, NULL, NULL, KT_DEFAULT, true},
    {"run visualization-proof", "running", "codex", NULL, "alpha", "\xe2\x96\xb8", KT_COLOR_SUCCESS, false},
    {"run second-run", "stopped", "claude", NULL, NULL, "\xe2\x96\xb8", KT_COLOR_MUTED, false},
    {"Elsewhere", NULL, NULL, NULL, NULL, NULL, KT_DEFAULT, true},
    {"build-cache", "failed", "codex", NULL, NULL, "\xe2\x9c\x97", KT_COLOR_DANGER, false},
    {"release-notes", "running", "gemini", NULL, "gamma", "\xe2\x97\x8f", KT_COLOR_SUCCESS, false},
    {"perf-sweep", "stopped", "codex", "done", NULL, "\xe2\x97\x8b", KT_COLOR_MUTED, false},
};
#define HEADS (sizeof(heads) / sizeof(heads[0]))

static const char *const agents[] = {"codex", "claude", "aider", "gemini"};
static const char *const log_words[] = {"compiled 42 objects", "linked hydra-tui", "ran 118 checks",
                                        "wrote build/test-logs", "waiting for the agent", "pushed nothing"};

enum { TICK = 1, SPIN, TOAST };
enum { PAGE_HEADS, PAGE_TABLE, PAGE_LOG, PAGE_SETTINGS, PAGES };
enum { MODAL_NEW = 1, MODAL_REMOVE, MODAL_HELP, MODAL_PALETTE };
enum { APP_NEW, APP_REMOVE, APP_JOB, APP_COMMANDS, APP_WRAP, APP_THEME, APP_QUIT, APP_STOP, APP_ACTIONS };
/* Scopes: keys that are letters are not the application's while a field
 * takes typing, so they and their hints come and go with SCOPE_KEYS. */
enum { SCOPE_ALWAYS = 1, SCOPE_KEYS = 2, SCOPE_HEADS = 4, SCOPE_LOG = 8 };
#define LOG_MAX 400

static const struct kt_theme *const themes[] = {&kt_theme_dark, &kt_theme_light, &kt_theme_terminal};
#define THEMES (sizeof(themes) / sizeof(themes[0]))

static const struct kt_tab pages[PAGES] = {
    {"Heads", NULL, KT_DEFAULT}, {"Table", NULL, KT_DEFAULT}, {"Log", "3", KT_COLOR_WARNING}, {"Settings", NULL, KT_DEFAULT}};

static const struct kt_table_column columns[] = {
    {"HEAD", KT_MIN(16), KT_ALIGN_LEFT}, {"STATUS", KT_FIXED(15), KT_ALIGN_LEFT},
    {"AGENT", KT_FIXED(7), KT_ALIGN_LEFT}, {"GROUP", KT_FIXED(6), KT_ALIGN_LEFT}, {"REP", KT_FIXED(4), KT_ALIGN_RIGHT}};
#define COLUMNS (sizeof(columns) / sizeof(columns[0]))

struct model {
    size_t theme;
    bool removed[HEADS];
    size_t order[HEADS], rows;        /* table rows: heads that are not sections */
    struct kt_tabs tabs;
    struct kt_list list;
    struct kt_match matches[HEADS];
    struct kt_viewport detail, log;
    struct kt_table table;
    struct kt_help help;
    struct kt_modals modals;
    struct kt_confirm confirm;
    struct kt_form task, settings;
    struct kt_field task_fields[2], settings_fields[3];
    struct kt_textinput task_name, settings_name;
    struct kt_palette palette;
    struct kt_spinner spinner;
    struct kt_progress progress;
    struct kt_toasts toasts;
    struct kt_binding keys[APP_ACTIONS];
    struct kt_keymap_state chord;
    uint64_t job;
    size_t log_count;
    char toast[128];
    int columns, rows_on_screen;
};

/* ---- the application's own keys ----------------------------------------- */

#define APP_ROW(id, name, label, scope, group, priority, key) \
    {name, label, id, scope, {{{{(key), 0}}, 1}, {{{0, 0}}, 0}}, 1, 0, group, priority}
static const struct kt_binding app_keys[APP_ACTIONS] = {
    APP_ROW(APP_NEW, "app.new", "new task", SCOPE_HEADS, "Work", 5, 'n'),
    APP_ROW(APP_REMOVE, "app.remove", "remove", SCOPE_HEADS, "Work", 4, 'x'),
    APP_ROW(APP_JOB, "app.job", "run job", SCOPE_KEYS, "Work", 2, 'w'),
    APP_ROW(APP_COMMANDS, "app.commands", "commands", SCOPE_KEYS, "Work", 3, ':'),
    APP_ROW(APP_WRAP, "app.wrap", "wrap", SCOPE_LOG, "View", 1, 'W'),
    APP_ROW(APP_THEME, "app.theme", "theme", SCOPE_KEYS, "View", 1, 't'),
    APP_ROW(APP_QUIT, "app.quit", "quit", SCOPE_KEYS, "View", 9, 'q'),
    {"app.stop", "quit", APP_QUIT, SCOPE_ALWAYS, {{{{'c', KT_MOD_CTRL}}, 1}, {{{0, 0}}, 0}}, 1, 0, "View", 9},
};

/* Whether the page's component takes typed characters now. */
static bool typing(const struct model *m) {
    const struct kt_field *field = &m->settings_fields[m->settings.focus];
    if (m->tabs.active == PAGE_HEADS) return m->list.filtering;
    return m->tabs.active == PAGE_SETTINGS && field->kind == KT_FIELD_TEXT;
}

static uint32_t app_scope(const struct model *m) {
    uint32_t scope = SCOPE_ALWAYS;
    if (typing(m)) return scope;
    scope |= SCOPE_KEYS;
    if (m->tabs.active == PAGE_HEADS || m->tabs.active == PAGE_TABLE) scope |= SCOPE_HEADS;
    if (m->tabs.active == PAGE_LOG) scope |= SCOPE_LOG;
    return scope;
}

/* ---- sources for the components ----------------------------------------- */

static void head_item(void *context, size_t index, struct kt_list_item *item) {
    const struct model *m = context;
    const struct head *h = &heads[index];
    item->title = h->name;
    item->header = h->section;
    item->detail = m->removed[index] ? "removed" : h->agent;
    item->icon = h->icon;
    item->icon_color = m->removed[index] ? KT_COLOR_FAINT : h->tone;
}

static void head_cell(void *context, size_t row, size_t column, struct kt_table_cell *cell) {
    const struct model *m = context;
    const struct head *h = &heads[m->order[row]];
    const char *values[COLUMNS];
    values[0] = h->name; values[1] = h->status; values[2] = h->agent;
    values[3] = h->group ? h->group : "\xe2\x80\x94"; values[4] = h->report ? h->report : "\xe2\x80\x94";
    cell->text = values[column];
    if (column == 1) cell->color = h->tone;
    if (column >= 3 && !(column == 4 ? h->report : h->group)) cell->color = KT_COLOR_FAINT;
}

/* The selected head's details as styled lines. */
static size_t detail_line(void *context, size_t index, struct kt_span *spans, size_t capacity) {
    static char text[2][160];
    const struct model *m = context;
    size_t item = kt_list_index(&m->list);
    const struct head *h = item < HEADS ? &heads[item] : &heads[1];
    static const char *const labels[] = {NULL, NULL, "Changes    ", "Reported   ", "Approvals  ", "Group      ", NULL, NULL};
    if (capacity < 2 || index >= 8) return 0;
    spans[0].style = KT_INK(KT_COLOR_MUTED, 0);
    spans[1].style = KT_INK(KT_COLOR_TEXT, 0);
    switch (index) {
    case 0:
        spans[0].text = h->icon ? h->icon : "";
        spans[0].style = KT_INK(h->tone, 0);
        snprintf(text[0], sizeof(text[0]), " %s  %s \xc2\xb7 this machine", h->status ? h->status : "", h->agent ? h->agent : "");
        spans[1].text = text[0];
        return 2;
    case 2: spans[1].text = "4 files uncommitted \xc2\xb7 0 queued"; break;
    case 3: spans[1].text = h->report ? h->report : "nothing yet"; break;
    case 4: spans[1].text = "\xe2\x97\x90 1 of 2 approved \xc2\xb7 waiting on you"; spans[1].style = KT_INK(KT_COLOR_WARNING, 0); break;
    case 5: spans[1].text = h->group ? h->group : "none"; break;
    case 7:
        snprintf(text[1], sizeof(text[1]), "%s works in its own branch, worktree and terminal; this text wraps "
                 "to the width of the pane.", h->name);
        spans[0].text = text[1];
        return 1;
    default: spans[0].text = ""; return 1;
    }
    spans[0].text = labels[index];
    return 2;
}

static size_t log_line(void *context, size_t index, struct kt_span *spans, size_t capacity) {
    static char time[16];
    (void)context;
    if (capacity < 2) return 0;
    snprintf(time, sizeof(time), "12:%02u:%02u  ", (unsigned)(index / 60 % 60), (unsigned)(index % 60));
    spans[0].text = time;
    spans[0].style = KT_INK(KT_COLOR_FAINT, 0);
    spans[1].text = log_words[index % (sizeof(log_words) / sizeof(log_words[0]))];
    spans[1].style = KT_INK(index % 7 == 3 ? KT_COLOR_WARNING : KT_COLOR_TEXT, 0);
    return 2;
}

static void table_rows(struct model *m) {
    size_t i;
    m->rows = 0;
    for (i = 0; i < HEADS; i++)
        if (!heads[i].section) m->order[m->rows++] = i;
}

/* The application sorts; the table only says by what. */
static int compare_rows(const struct model *m, size_t a, size_t b) {
    const char *x, *y;
    size_t column = m->table.sort_column;
    struct kt_table_cell first = {NULL, 0, 0}, second = {NULL, 0, 0};
    head_cell((void *)(uintptr_t)m, a, column, &first);
    head_cell((void *)(uintptr_t)m, b, column, &second);
    x = first.text ? first.text : "";
    y = second.text ? second.text : "";
    return m->table.sort_descending ? strcmp(y, x) : strcmp(x, y);
}

static void sort_rows(struct model *m) {
    size_t i, j;
    table_rows(m);
    if (m->table.sort_column == KT_NONE) return;
    for (i = 1; i < m->rows; i++)
        for (j = i; j > 0 && compare_rows(m, j - 1, j) > 0; j--) {
            size_t swap = m->order[j];
            m->order[j] = m->order[j - 1];
            m->order[j - 1] = swap;
        }
}

/* ---- set up -------------------------------------------------------------- */

static void focus_page(struct model *m);

static void forms_init(struct model *m) {
    static const char rebind[] = "form.next = down\nform.previous = up\n";
    struct kt_field *f = m->task_fields;
    struct kt_keymap map;
    char error[96];
    kt_textinput_init(&m->task_name, NULL, 0);
    m->task_name.placeholder = "fix-login-redirect";
    f[0].kind = KT_FIELD_TEXT; f[0].label = "Task name"; f[0].input = &m->task_name;
    f[0].hint = "Becomes the branch and worktree name";
    f[1].kind = KT_FIELD_CHOICE; f[1].label = "Agent"; f[1].options = agents; f[1].option_count = 4;
    kt_form_init(&m->task, "New task", f, 2);
    m->task.keys[KT_FORM_SUBMIT].label = "create";
    m->task.keys[KT_FORM_NEXT].priority = -1;
    f = m->settings_fields;
    kt_textinput_init(&m->settings_name, NULL, 0);
    kt_textinput_set(&m->settings_name, "myproject");
    f[0].kind = KT_FIELD_TEXT; f[0].label = "Project name"; f[0].input = &m->settings_name;
    f[0].hint = "Shown in the status line";
    f[1].kind = KT_FIELD_CHOICE; f[1].label = "Default agent"; f[1].options = agents; f[1].option_count = 4;
    f[2].kind = KT_FIELD_TOGGLE; f[2].label = "Notifications"; f[2].on = true;
    kt_form_init(&m->settings, NULL, f, 3);
    m->settings.keys[KT_FORM_SUBMIT].label = "save";
    /* Tab belongs to the tabs here: the inline form moves with up and down. */
    map = kt_form_keymap(&m->settings);
    (void)kt_keymap_configure(&map, rebind, sizeof(rebind) - 1, error, sizeof(error));
}

static void model_init(struct model *m) {
    memset(m, 0, sizeof(*m));
    kt_tabs_init(&m->tabs, pages, PAGES);
    kt_list_init(&m->list, head_item, m, HEADS);
    kt_list_filter_storage(&m->list, m->matches, HEADS);
    m->list.status_line = true;
    kt_viewport_init(&m->detail, detail_line, m, 8);
    m->detail.wrap = true;
    m->detail.scrollbar = false;
    kt_viewport_init(&m->log, log_line, NULL, 24);
    m->log.follow = m->log.at_end = true;
    m->log_count = 24;
    table_rows(m);
    kt_table_init(&m->table, columns, COLUMNS, head_cell, m, m->rows);
    m->table.sortable = m->table.status_line = true;
    kt_help_init(&m->help);
    kt_modals_init(&m->modals);
    forms_init(m);
    kt_palette_init(&m->palette, "Commands");
    kt_spinner_init(&m->spinner, SPIN);
    m->spinner.label = "building";
    kt_spinner_show(&m->spinner, false, NULL);
    kt_progress_init(&m->progress);
    kt_toasts_init(&m->toasts, TOAST);
    memcpy(m->keys, app_keys, sizeof(app_keys));
    focus_page(m);
}

/* ---- layout -------------------------------------------------------------- */

struct layout { struct kt_rect header, tabs, body, left, left_rule, right, right_rule, status, footer, toasts; };

static struct layout layout_of(int columns, int rows) {
    struct layout l;
    struct kt_rect all = {0, 0, columns, rows}, body;
    l.header = kt_rect_cut(all, KT_SIDE_TOP, 1, &all);
    l.tabs = kt_rect_inset(l.header, 0, 1, 0, 12);
    l.footer = kt_rect_cut(all, KT_SIDE_BOTTOM, 1, &all);
    l.status = kt_rect_cut(all, KT_SIDE_BOTTOM, 1, &all);
    (void)kt_rect_cut(all, KT_SIDE_TOP, 1, &all);
    l.body = kt_rect_pad(all, 1, 0);
    l.toasts = kt_rect_inset(l.body, 0, 0, 1, 0);
    body = l.body;
    l.left = kt_rect_cut(body, KT_SIDE_LEFT, body.width * 2 / 5, &body);
    l.right = kt_rect_inset(body, 0, 0, 0, 3);
    l.left_rule = kt_rect_cut(l.left, KT_SIDE_TOP, 1, &l.left);
    l.right_rule = kt_rect_cut(l.right, KT_SIDE_TOP, 1, &l.right);
    return l;
}

static void place_all(struct model *m, int columns, int rows) {
    struct layout l = layout_of(columns, rows);
    struct kt_rect whole = kt_rect_inset(l.body, 1, 0, 0, 0);
    m->columns = columns;
    m->rows_on_screen = rows;
    kt_tabs_place(&m->tabs, l.tabs);
    kt_list_place(&m->list, l.left);
    kt_viewport_place(&m->detail, l.right);
    kt_viewport_place(&m->log, whole);
    kt_table_place(&m->table, whole);
    kt_form_place(&m->settings, kt_rect_inset(kt_rect_cut(whole, KT_SIDE_LEFT, 48, NULL), 1, 0, 0, 2));
    kt_toasts_place(&m->toasts, l.toasts);
    kt_modals_place(&m->modals, (struct kt_rect){0, 0, columns, rows});
}

/* ---- focus and the footer ------------------------------------------------ */

static struct kt_help_source source(const struct kt_binding *bindings, size_t count, uint32_t scope) {
    struct kt_help_source s;
    s.bindings = bindings;
    s.count = count;
    s.scope = scope;
    return s;
}

/* The page's component first, then the tabs, the application and help. */
static size_t page_source(const struct model *m, struct kt_help_source *out) {
    switch (m->tabs.active) {
    case PAGE_HEADS: *out = source(m->list.keys, KT_LIST_ACTIONS, kt_list_scope(&m->list)); return 1;
    case PAGE_TABLE: *out = source(m->table.keys, KT_TABLE_ACTIONS, kt_table_scope(&m->table)); return 1;
    case PAGE_LOG: *out = source(m->log.keys, KT_VIEWPORT_ACTIONS, kt_viewport_scope(&m->log)); return 1;
    default: *out = source(m->settings.keys, KT_FORM_ACTIONS, kt_form_scope(&m->settings)); return 1;
    }
}

static void refresh_help(struct model *m) {
    struct kt_help_source sources[4];
    size_t n = page_source(m, sources);
    sources[n++] = source(m->keys, APP_ACTIONS, app_scope(m));
    sources[n++] = source(m->tabs.keys, KT_TABS_ACTIONS, kt_tabs_scope(&m->tabs));
    sources[n++] = source(m->help.keys, KT_HELP_ACTIONS, typing(m) ? 0 : kt_help_scope(&m->help));
    (void)kt_help_set(&m->help, sources, n);
}

static void focus_page(struct model *m) {
    kt_form_focus(&m->settings, m->settings.focus);
    if (m->tabs.active != PAGE_SETTINGS) m->settings_name.focused = false;
}

/* ---- actions ------------------------------------------------------------- */

static void toast(struct model *m, struct kt_cmds *cmds, enum kt_toast_kind kind, const char *format, const char *what) {
    snprintf(m->toast, sizeof(m->toast), format, what);
    kt_toasts_push(&m->toasts, cmds, kind, m->toast);
}

static size_t selected_head(const struct model *m) {
    if (m->tabs.active == PAGE_TABLE) return m->table.selected < m->rows ? m->order[m->table.selected] : KT_NONE;
    return kt_list_index(&m->list);
}

static void open_modal(struct model *m, struct kt_modal modal) {
    if (!kt_modals_push(&m->modals, &modal)) kt_modals_pop(&m->modals);
}

static void ask_remove(struct model *m) {
    static char question[160];
    size_t head = selected_head(m);
    if (head == KT_NONE || heads[head].section) return;
    snprintf(question, sizeof(question), "Remove %s?\n\nCloses its terminal and deletes the worktree. The branch is "
             "kept. Heads with uncommitted changes are refused.", heads[head].name);
    kt_confirm_init(&m->confirm, "Remove head", question, "Remove", "Cancel", true);
    open_modal(m, kt_confirm_modal(&m->confirm, MODAL_REMOVE, 58));
}

static void ask_task(struct model *m) {
    kt_textinput_set(&m->task_name, "");
    m->task_fields[0].error[0] = '\0';
    kt_form_focus(&m->task, 0);
    open_modal(m, kt_form_modal(&m->task, MODAL_NEW, 58));
}

static void open_commands(struct model *m) {
    struct kt_help_source sources[2];
    sources[0] = source(m->keys, APP_ACTIONS, app_scope(m));
    sources[1] = source(m->tabs.keys, KT_TABS_ACTIONS, kt_tabs_scope(&m->tabs));
    (void)kt_palette_set(&m->palette, sources, 2);
    open_modal(m, kt_palette_modal(&m->palette, MODAL_PALETTE, 64));
}

static void start_job(struct model *m, struct kt_cmds *cmds) {
    if (m->job) return;
    kt_progress_set(&m->progress, 0);
    kt_spinner_show(&m->spinner, true, cmds);
    kt_spinner_start(&m->spinner, cmds);
    m->job = kt_cmd_timer(cmds, TICK, 120, 120);
}

static void perform(struct model *m, uint32_t action, struct kt_cmds *cmds) {
    switch (action) {
    case APP_NEW: ask_task(m); break;
    case APP_REMOVE: ask_remove(m); break;
    case APP_JOB: start_job(m, cmds); break;
    case APP_COMMANDS: open_commands(m); break;
    case APP_WRAP: m->log.wrap = !m->log.wrap; kt_viewport_bottom(&m->log); break;
    case APP_THEME: m->theme = (m->theme + 1) % THEMES; kt_cmd_theme(cmds, themes[m->theme]); break;
    case APP_QUIT: kt_cmd_quit(cmds, 0); break;
    default: break;
    }
}

/* One step of the pretend job: progress, a log line, and at the end a toast. */
static void job_tick(struct model *m, struct kt_cmds *cmds) {
    kt_progress_set(&m->progress, m->progress.value + 0.05);
    if (m->log_count < LOG_MAX) kt_viewport_set_count(&m->log, ++m->log_count);
    if (m->progress.value < 1.0) return;
    kt_cmd_cancel(cmds, m->job);
    m->job = 0;
    kt_spinner_stop(&m->spinner, cmds);
    kt_spinner_show(&m->spinner, false, cmds);
    toast(m, cmds, KT_TOAST_SUCCESS, "%s", "Job finished: 118 checks passed");
}

/* ---- update -------------------------------------------------------------- */

/* Whether a modal's outcome ends it: a form stays open while it is filled
 * in or refused, the others end with any outcome. */
static bool closes(uint32_t id, int result) {
    switch (id) {
    case MODAL_NEW: return result == KT_FORM_SUBMITTED || result == KT_FORM_CANCELLED;
    case MODAL_PALETTE: return result == KT_PALETTE_CHOSEN || result == KT_PALETTE_CANCELLED;
    default: return true;
    }
}

static void modal_result(struct model *m, struct kt_cmds *cmds) {
    size_t head = selected_head(m);
    int result = m->modals.result;
    if (!closes(m->modals.id, result)) return;
    if (m->modals.id == MODAL_REMOVE && result == KT_CONFIRM_ACCEPTED && head != KT_NONE) {
        m->removed[head] = true;
        toast(m, cmds, KT_TOAST_WARNING, "Removed %s", heads[head].name);
    }
    if (m->modals.id == MODAL_NEW && result == KT_FORM_SUBMITTED)
        toast(m, cmds, KT_TOAST_SUCCESS, "Created %s", kt_textinput_value(&m->task_name));
    if (m->modals.id == MODAL_PALETTE && result == KT_PALETTE_CHOSEN) {
        kt_modals_pop(&m->modals);
        perform(m, kt_palette_chosen(&m->palette)->action, cmds);
        return;
    }
    kt_modals_pop(&m->modals);
}

static bool page_update(struct model *m, const struct kt_msg *msg, struct kt_cmds *cmds) {
    switch (m->tabs.active) {
    case PAGE_HEADS: {
        enum kt_list_result r = kt_list_update(&m->list, msg, cmds);
        if (r == KT_LIST_MOVED) kt_viewport_top(&m->detail);
        if (r == KT_LIST_CHOSEN) toast(m, cmds, KT_TOAST_INFO, "Opened %s", heads[kt_list_index(&m->list)].name);
        return r != KT_LIST_IGNORED;
    }
    case PAGE_TABLE: {
        enum kt_table_result r = kt_table_update(&m->table, msg, cmds);
        if (r == KT_TABLE_SORTED) sort_rows(m);
        return r != KT_TABLE_IGNORED;
    }
    case PAGE_LOG: return kt_viewport_update(&m->log, msg, cmds) != KT_VIEWPORT_IGNORED;
    default: {
        enum kt_form_result r = kt_form_update(&m->settings, msg, cmds);
        if (r == KT_FORM_SUBMITTED) toast(m, cmds, KT_TOAST_SUCCESS, "Saved settings for %s",
                                          kt_textinput_value(&m->settings_name));
        return r != KT_FORM_IGNORED && r != KT_FORM_CANCELLED;
    }
    }
}

static bool app_update(struct model *m, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct kt_keymap map = {m->keys, APP_ACTIONS};
    const struct kt_binding *b;
    enum kt_key_match match;
    if (msg->type != KT_MSG_KEY) return false;
    match = kt_keymap_feed(&map, &m->chord, app_scope(m), &msg->u.event, &b);
    if (match == KT_KEY_ACTION) perform(m, b->action, cmds);
    return match != KT_KEY_UNBOUND;
}

static bool switch_tab(struct model *m, const struct kt_msg *msg, struct kt_cmds *cmds) {
    if (kt_tabs_update(&m->tabs, msg, cmds) == KT_TABS_IGNORED) return false;
    focus_page(m);
    return true;
}

/* Keys that type nothing (tab, shift+tab) go to the tabs before the page,
 * so a form or a filter never keeps them; characters go to the page first. */
static bool base_input(struct model *m, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool keys_first = msg->type == KT_MSG_KEY && !kt_event_text(&msg->u.event);
    struct kt_modal help;
    if (keys_first && switch_tab(m, msg, cmds)) return true;
    if (page_update(m, msg, cmds)) return true;
    if (!keys_first && switch_tab(m, msg, cmds)) return true;
    if (kt_help_update(&m->help, msg, cmds) == KT_HELP_OPENED) {
        help = kt_help_modal(&m->help, MODAL_HELP, 0);
        open_modal(m, help);
        return true;
    }
    return app_update(m, msg, cmds);
}

/* Input: the top modal, else toasts under the mouse, else the base view. */
static bool input(struct model *m, const struct kt_msg *msg, struct kt_cmds *cmds) {
    switch (kt_modals_update(&m->modals, msg, cmds)) {
    case KT_MODALS_RESULT: modal_result(m, cmds); return true;
    case KT_MODALS_IGNORED: break;
    default: return true;
    }
    if (kt_toasts_update(&m->toasts, msg, cmds) != KT_TOASTS_IGNORED) return true;
    return base_input(m, msg, cmds);
}

static bool update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct model *m = model;
    bool handled = true;
    if (msg->type == KT_MSG_RESIZE) place_all(m, msg->u.resize.columns, msg->u.resize.rows);
    else if (msg->type == KT_MSG_TIMER && msg->tag == TICK) job_tick(m, cmds);
    else if (kt_spinner_update(&m->spinner, msg, cmds) == KT_SPINNER_IGNORED) handled = input(m, msg, cmds);
    refresh_help(m);
    return handled;
}

static void init(void *model, struct kt_cmds *cmds) {
    (void)cmds;
    refresh_help(model);
}

/* ---- view ---------------------------------------------------------------- */

static void rule(struct kt_canvas *canvas, struct kt_rect area, const char *title) {
    kt_rule_styled(canvas, area, title, KT_ALIGN_LEFT, KT_INK(KT_COLOR_TEXT, KT_BOLD), KT_INK(KT_COLOR_FAINT, 0));
}

static void draw_heads(const struct model *m, struct kt_canvas *canvas, const struct layout *l) {
    size_t head = kt_list_index(&m->list);
    rule(canvas, l->left_rule, "Heads");
    kt_list_view(&m->list, canvas, m->list.area);
    rule(canvas, l->right_rule, head < HEADS ? heads[head].name : "");
    kt_viewport_view(&m->detail, canvas, m->detail.area);
}

static void draw_settings(const struct model *m, struct kt_canvas *canvas, const struct layout *l) {
    struct kt_rect side = kt_rect_inset(l->body, 1, 0, 0, 54);
    rule(canvas, (struct kt_rect){l->body.x, l->body.y, 50, 1}, "Settings");
    kt_form_view(&m->settings, canvas, m->settings.area);
    rule(canvas, (struct kt_rect){side.x, l->body.y, side.width, 1}, "Activity");
    (void)kt_paragraph(canvas, kt_rect_inset(side, 1, 0, 0, 0),
                       "w runs a pretend job: the spinner and the bar in the status line move only while it runs, "
                       "and a toast reports when it ends.", KT_INK(KT_COLOR_MUTED, 0));
}

static void draw_page(const struct model *m, struct kt_canvas *canvas, const struct layout *l) {
    struct kt_rect top = {l->body.x, l->body.y, l->body.width, 1};
    switch (m->tabs.active) {
    case PAGE_HEADS: draw_heads(m, canvas, l); break;
    case PAGE_TABLE: rule(canvas, top, "Heads"); kt_table_view(&m->table, canvas, m->table.area); break;
    case PAGE_LOG: rule(canvas, top, m->log.wrap ? "Log (wrapped)" : "Log"); kt_viewport_view(&m->log, canvas, m->log.area); break;
    default: draw_settings(m, canvas, l); break;
    }
}

static void draw_status(const struct model *m, struct kt_canvas *canvas, struct kt_rect status, const char *theme) {
    char where[96];
    const struct kt_span spans[] = {{" \xe2\x97\x8f live ", KT_STYLE(KT_COLOR_SUCCESS, KT_COLOR_SUNKEN, 0)},
                                    {"  ", KT_INK(KT_COLOR_MUTED, 0)}, {where, KT_INK(KT_COLOR_MUTED, 0)}};
    size_t item = kt_list_index(&m->list);
    snprintf(where, sizeof(where), "%s \xc2\xb7 %s \xc2\xb7 %s theme", kt_textinput_value(&m->settings_name),
             item < HEADS ? heads[item].name : "nothing selected", theme);
    (void)kt_spans(canvas, kt_rect_inset(status, 0, 0, 0, 1), spans, 3);
    if (!m->job) return;
    kt_spinner_view(&m->spinner, canvas, (struct kt_rect){status.x + status.width - 44, status.y, 12, 1});
    kt_progress_view(&m->progress, canvas, (struct kt_rect){status.x + status.width - 31, status.y, 30, 1});
}

static void draw(const struct model *m, struct kt_canvas *canvas, const char *theme) {
    struct layout l = layout_of(canvas->width, canvas->height);
    kt_text(canvas, kt_rect_inset(l.header, 0, 0, 0, 1), "\xe2\x97\x86 kettle", KT_INK(KT_COLOR_ACCENT, KT_BOLD));
    kt_tabs_view(&m->tabs, canvas, m->tabs.area);
    draw_page(m, canvas, &l);
    draw_status(m, canvas, l.status, theme);
    kt_help_view(&m->help, canvas, kt_rect_inset(l.footer, 0, 0, 0, 1));
    kt_toasts_view(&m->toasts, canvas, m->toasts.area);
    kt_modals_view(&m->modals, canvas, (struct kt_rect){0, 0, canvas->width, canvas->height});
}

static void view(const void *model, struct kt_frame *frame) {
    draw(model, kt_frame_canvas(frame), kt_frame_theme(frame)->name);
}

static const struct kt_app components_app = {init, update, view};

/* ---- main ---------------------------------------------------------------- */

static const char *variable(void *context, const char *name) { (void)context; return getenv(name); }

/* The first screen without the runtime, once per built-in theme. */
static int print(void) {
    static struct kt_cell cells[100 * 30];
    static struct model model;
    struct kt_canvas canvas;
    struct kt_paint paint;
    size_t i;
    int row;
    paint.profile = kt_profile_detect(variable, NULL, isatty(STDOUT_FILENO) != 0);
    for (i = 0; i < 2; i++) {
        model_init(&model);
        place_all(&model, 100, 30);
        refresh_help(&model);
        paint.theme = themes[i];
        if (!kt_init(&canvas, cells, sizeof(cells) / sizeof(cells[0]), 100, 30, true)) return 1;
        kt_clear(&canvas, KT_BASE);
        draw(&model, &canvas, themes[i]->name);
        for (row = 0; row < canvas.height; row++)
            if (!kt_write_row(&canvas, row, stdout, &paint) || putchar('\n') == EOF) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    static struct model model;
    struct kt_options options = {0};
    struct kt_program *program;
    int status;
    if (argc > 1 && !strcmp(argv[1], "--print")) return print();
    model_init(&model);
    options.alt_screen = options.bracketed_paste = true;
    options.mouse = KT_MOUSE_BUTTONS;
    options.theme = themes[0];
    program = kt_program_new(&components_app, &model, &options);
    if (!program) return 1;
    status = kt_program_run(program);
    kt_program_free(program);
    if (status < 0) fputs("components needs an interactive terminal; try --print\n", stderr);
    return status < 0 ? 1 : status;
}
