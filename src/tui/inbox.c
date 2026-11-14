#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "attention.h"
#include "inbox.h"
#include <dirent.h>
/* Inbox: one list of what waits for a human, built from every source that
 * knows about such a thing, in sections ordered by what blocks work first:
 * decisions (approval requests, head approval gates, plans an agent
 * published), failures, results to review, agents whose terminal stopped or
 * whose state is unknown, repairs (recovery findings and attention items
 * Hydra cannot classify), the user's own actions that failed this session,
 * and a collapsed Seen group. Every item is acted on where it is listed; each
 * action asks first when it changes anything, and sends nothing when what it
 * was asked about changed while the question was open. */

static struct native_inbox *inbox_of(struct app *app) {
    if (!app->inbox) app->inbox = calloc(1U, sizeof(*app->inbox));
    return app->inbox;
}

void inbox_destroy(struct app *app) { free(app->inbox); app->inbox = NULL; }

/* ---- what the state store says: gates and published plans --------------- */

static bool is_directory_path(const char *path) {
    struct stat st;
    return path[0] == '/' && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool child_file(const char *directory, const char *name, struct stat *st) {
    char path[SOURCE_TEXT + 160];
    struct stat ignored;
    if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path)) return false;
    return stat(path, st ? st : &ignored) == 0;
}

static bool gate_name(const char *name) {
    return name[0] && name[0] != '.' && strlen(name) < 64U && strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == strlen(name);
}

/* The gates under a head that are not approved yet, by name. */
static void add_gate(struct native_inbox *in, const struct head *h, const char *name) {
    if (in->gate_count >= INBOX_GATES) return;
    copy_text(in->gates[in->gate_count].branch, sizeof(in->gates[0].branch), h->branch);
    copy_text(in->gates[in->gate_count++].name, sizeof(in->gates[0].name), name);
}

/* The gates under a head that are not approved yet, by name; one unnamed
 * item when the snapshot counts them but their records cannot be read here. */
static void head_gates(struct native_inbox *in, const struct head *h) {
    char directory[SOURCE_TEXT + 16], gate[SOURCE_TEXT + 96];
    DIR *stream = NULL;
    struct dirent *entry;
    size_t before = in->gate_count;
    if (snprintf(directory, sizeof(directory), "%s/gates", h->source) < (int)sizeof(directory) && is_directory_path(directory))
        stream = opendir(directory);
    if (!stream) { add_gate(in, h, ""); return; }
    while ((entry = readdir(stream)) != NULL && in->gate_count < INBOX_GATES) {
        if (!gate_name(entry->d_name)) continue;
        if (snprintf(gate, sizeof(gate), "%s/%s", directory, entry->d_name) >= (int)sizeof(gate)) continue;
        if (!is_directory_path(gate) || child_file(gate, "approved-at", NULL)) continue;
        add_gate(in, h, entry->d_name);
    }
    closedir(stream);
    if (in->gate_count == before) add_gate(in, h, "");
}

/* Seconds since the epoch of an ISO-8601 UTC time such as
 * 2026-11-14T08:00:00Z; -1 when it is not one. */
static long long utc_seconds(const char *text) {
    int y, mo, d, h, mi, s;
    long long days;
    if (sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6 || mo < 1 || mo > 12) return -1;
    y -= mo <= 2;
    days = 365LL * y + y / 4 - y / 100 + y / 400 + (153LL * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1 - 719468LL;
    return days * 86400LL + h * 3600LL + mi * 60LL + s;
}

/* A run this head's plan launched at or after the draft was published. */
static bool plan_launched(const struct app *app, const struct head *h, time_t published) {
    size_t i;
    for (i = 0; app->workflows && i < app->workflows->run_count; i++) {
        const struct workflow_run *r = &app->workflows->runs[i];
        if (strcmp(r->planning, h->branch)) continue;
        if (utc_seconds(r->created) >= (long long)published) return true;
    }
    return false;
}

/* A draft the head's agent published that was neither sent back nor run. */
static void head_plan(struct app *app, struct native_inbox *in, const struct head *h) {
    char planning[SOURCE_TEXT + 16];
    struct stat st;
    if (in->plan_count >= INBOX_PLANS) return;
    if (snprintf(planning, sizeof(planning), "%s/planning", h->source) >= (int)sizeof(planning) || !is_directory_path(planning)) return;
    if (!plan_action_published(planning, &st) || plan_launched(app, h, st.st_mtime)) return;
    copy_text(in->plans[in->plan_count].branch, sizeof(in->plans[0].branch), h->branch);
    snprintf(in->plans[in->plan_count++].draft, sizeof(in->plans[0].draft), "%.500s/draft.json", planning);
}

void inbox_refresh(struct app *app) {
    struct native_inbox *in = inbox_of(app);
    size_t i;
    if (!in) return;
    in->gate_count = in->plan_count = 0;
    for (i = 0; !app->fleet && i < app->model.head_count; i++) {
        const struct head *h = &app->model.heads[i];
        if (h->gates > h->approved) head_gates(in, h);
        if (h->head_id[0] && is_directory_path(h->source)) head_plan(app, in, h);
    }
}

/* ---- the list ------------------------------------------------------------- */

static void add(struct native_inbox *in, enum inbox_kind kind, enum inbox_section section, size_t index) {
    if (in->count >= INBOX_ENTRIES) return;
    in->entries[in->count++] = (struct inbox_entry){kind, section, index};
    in->section_count[section]++;
}

static enum inbox_section attention_section(const struct attention_item *item) {
    if (attention_in_seen_group(item)) return INBOX_SEEN;
    if (attention_is_approval(item)) return INBOX_DECISIONS;
    if (attention_is_failure(item)) return INBOX_FAILED;
    if (!strcmp(item->kind, "result")) return INBOX_RESULTS;
    return INBOX_REPAIRS;
}

static void add_attention(struct native_inbox *in, const struct app *app, enum inbox_section section) {
    size_t i;
    for (i = 0; app->attention && i < app->attention->count; i++)
        if (attention_section(&app->attention->items[i]) == section) add(in, INBOX_ATTENTION, section, i);
}

/* A head that needs the user because of its terminal: stopped, or a state
 * Hydra cannot read. Heads of a run report through the run. */
static bool agent_listed(const struct app *app, const struct head *h) {
    if (app->fleet || head_remote(h) || head_headless(h)) return false;
    if (head_terminal_gone(app, h)) return true;
    return !strcmp(display_status(h), "UNAVAILABLE");
}

/* A finding that says the same as an Agents item ("terminal stopped") is
 * folded into that item rather than listed twice. */
static bool recovery_folded(const struct app *app, const struct recovery *r) {
    const struct head *h;
    if (strcmp(r->kind, "dead-session")) return false;
    h = head_for_branch(app, r->label);
    return h && agent_listed(app, h);
}

/* A plan run that finished with its result on a worker head that is still
 * listed: hydra land merges it (L). The workflow head index, or SIZE_MAX. */
static size_t land_worker(const struct app *app, size_t run) {
    const struct workflow_model *m = app->workflows;
    size_t i;
    if (app->fleet || strcmp(m->runs[run].state, "succeeded") || strcmp(m->runs[run].kind, "plan")) return SIZE_MAX;
    for (i = 0; i < m->head_count; i++) {
        const struct workflow_head *h = &m->heads[i];
        const struct head *listed;
        if (h->run != run || (strcmp(h->role, "worker") && strcmp(h->role, "run"))) continue;
        listed = head_for_branch(app, h->branch);
        if (listed && !head_remote(listed)) return i;
    }
    return SIZE_MAX;
}

/* The fleet helper is missing: only the user can build or install it. */
static bool user_finding(const struct recovery *r) { return !strcmp(r->kind, "fleet-unavailable"); }

static void add_repairs(struct native_inbox *in, struct app *app, enum inbox_section section) {
    size_t i;
    for (i = 0; i < app->model.recovery_count; i++) {
        const struct recovery *r = &app->model.recovery[i];
        if (recovery_folded(app, r) || user_finding(r) != (section == INBOX_ACTIONS)) continue;
        add(in, INBOX_RECOVERY, section, i);
    }
}

static void add_sources(struct native_inbox *in, struct app *app) {
    size_t i, worker;
    add_attention(in, app, INBOX_DECISIONS);
    for (i = 0; i < in->plan_count; i++) add(in, INBOX_PLAN, INBOX_DECISIONS, i);
    for (i = 0; i < in->gate_count; i++) add(in, INBOX_GATE, INBOX_DECISIONS, i);
    add_attention(in, app, INBOX_FAILED);
    for (i = 0; app->workflows && i < app->workflows->run_count; i++)
        if ((worker = land_worker(app, i)) != SIZE_MAX) add(in, INBOX_LAND, INBOX_RESULTS, worker);
    add_attention(in, app, INBOX_RESULTS);
    for (i = 0; i < app->model.head_count; i++)
        if (agent_listed(app, &app->model.heads[i])) add(in, INBOX_HEAD, INBOX_AGENTS, i);
    add_repairs(in, app, INBOX_REPAIRS);
    add_attention(in, app, INBOX_REPAIRS);
    add_repairs(in, app, INBOX_ACTIONS);
    for (i = app->failed_count; i-- > 0;) if (!app->failed[i].dismissed) add(in, INBOX_FAILED_ACTION, INBOX_ACTIONS, i);
    add_attention(in, app, INBOX_SEEN);
    for (i = app->failed_count; i-- > 0;) if (app->failed[i].dismissed) add(in, INBOX_FAILED_ACTION, INBOX_SEEN, i);
}

/* The rows: each non-empty section under its heading; Seen is one row that
 * opens to show its items. */
static void build_rows(struct native_inbox *in) {
    size_t i;
    int section = -1;
    in->row_count = 0;
    for (i = 0; i < in->count; i++) {
        const struct inbox_entry *e = &in->entries[i];
        if ((int)e->section != section) {
            section = (int)e->section;
            in->rows[in->row_count++] = e->section == INBOX_SEEN ? INBOX_ROW_GROUP : INBOX_ROW_HEADING(e->section);
        }
        if (e->section != INBOX_SEEN || in->seen_open) in->rows[in->row_count++] = i;
    }
}

static bool row_selectable(size_t row) { return row == INBOX_ROW_GROUP || row < INBOX_ENTRIES; }

/* A stable name for an entry, so the selection follows its item across
 * rebuilds: identities for attention, branch and gate, finding, action. */
static void entry_key(const struct app *app, const struct native_inbox *in, size_t row, char *out, size_t size) {
    const struct inbox_entry *e;
    if (row == INBOX_ROW_GROUP) { snprintf(out, size, "seen-group"); return; }
    e = &in->entries[row];
    switch (e->kind) {
    case INBOX_ATTENTION: snprintf(out, size, "a:%s", app->attention->items[e->index].identity); break;
    case INBOX_GATE: snprintf(out, size, "g:%.120s/%s", in->gates[e->index].branch, in->gates[e->index].name); break;
    case INBOX_PLAN: snprintf(out, size, "p:%.200s", in->plans[e->index].branch); break;
    case INBOX_HEAD: snprintf(out, size, "h:%.200s", app->model.heads[e->index].branch); break;
    case INBOX_RECOVERY: snprintf(out, size, "r:%.60s:%.200s", app->model.recovery[e->index].kind, app->model.recovery[e->index].label); break;
    case INBOX_LAND: snprintf(out, size, "l:%.200s", app->workflows->heads[e->index].branch); break;
    default: snprintf(out, size, "f:%lld:%.200s", (long long)app->failed[e->index].at, app->failed[e->index].title); break;
    }
}

/* The selected row: the item named by the selection, else the row at its
 * old place (the next item takes a vanished one's place), else the first. */
/* An item nobody marked seen: attention items carry their marker. */
static bool row_unseen(const struct app *app, const struct native_inbox *in, size_t row) {
    const struct inbox_entry *e;
    if (row >= in->count) return false;
    e = &in->entries[row];
    return e->section != INBOX_SEEN && (e->kind != INBOX_ATTENTION || app->attention->items[e->index].unseen);
}

/* The default selection: the first unseen item, else the first row. */
static void settle_default(struct app *app, struct native_inbox *in) {
    size_t i, first = SIZE_MAX;
    for (i = 0; i < in->row_count; i++) {
        if (!row_selectable(in->rows[i])) continue;
        if (first == SIZE_MAX) first = i;
        if (row_unseen(app, in, in->rows[i])) { first = i; break; }
    }
    in->selected_row = first == SIZE_MAX ? 0 : first;
    if (first == SIZE_MAX) in->selected[0] = '\0';
    else entry_key(app, in, in->rows[first], in->selected, sizeof(in->selected));
}

static void settle(struct app *app, struct native_inbox *in) {
    char key[sizeof(in->selected)];
    size_t i, fallback = SIZE_MAX;
    if (!in->chosen) { settle_default(app, in); return; }
    for (i = 0; i < in->row_count; i++) {
        if (!row_selectable(in->rows[i])) continue;
        if (fallback == SIZE_MAX || i <= in->selected_row) fallback = i;
        entry_key(app, in, in->rows[i], key, sizeof(key));
        if (in->selected[0] && !strcmp(key, in->selected)) { in->selected_row = i; return; }
    }
    if (fallback == SIZE_MAX) { in->selected_row = 0; in->selected[0] = '\0'; return; }
    for (i = in->selected_row; in->selected[0] && i < in->row_count; i++)
        if (row_selectable(in->rows[i])) { fallback = i; break; }
    in->selected_row = fallback;
    entry_key(app, in, in->rows[fallback], in->selected, sizeof(in->selected));
}

static void sync_review(struct app *app, struct native_inbox *in) {
    const struct inbox_entry *e = inbox_selected(app);
    struct native_review_identity id;
    if (!e || e->kind != INBOX_ATTENTION || app->view != 9) { native_review_sync(app, NULL, false); return; }
    attention_review_identity(&app->attention->items[e->index], &id);
    native_review_sync(app, &id, app->attention->have_good && !app->attention->stale);
    (void)in;
}

void inbox_build(struct app *app) {
    struct native_inbox *in = inbox_of(app);
    if (!in) return;
    in->count = 0;
    memset(in->section_count, 0, sizeof(in->section_count));
    add_sources(in, app);
    build_rows(in);
    settle(app, in);
    sync_review(app, in);
}

/* Headless fixtures: the exact review of the selected item, read from a
 * framed document file, on the Review page. */
bool native_attention_review_load(struct app *app, const char *path) {
    const struct inbox_entry *e;
    struct native_review_identity id;
    bool loaded;
    inbox_build(app);
    e = inbox_selected(app);
    if (!e || e->kind != INBOX_ATTENTION) return false;
    attention_review_identity(&app->attention->items[e->index], &id);
    loaded = native_review_load(app, &id, path);
    /* A document that is not this item's shows as unavailable on the page. */
    if (native_review_active(app)) {
        app->inbox->page = INBOX_REVIEW;
        if (app->cols < 100) app->inbox->detail_open = true; else app->inbox->focus_detail = true;
    }
    return loaded;
}

const struct inbox_entry *inbox_selected(struct app *app) {
    struct native_inbox *in = app->inbox;
    size_t row;
    if (!in || in->selected_row >= in->row_count) return NULL;
    row = in->rows[in->selected_row];
    return row < in->count ? &in->entries[row] : NULL;
}

void inbox_badge(struct app *app, size_t *count, bool *failure, bool *known) {
    struct native_inbox *in;
    inbox_build(app);
    in = app->inbox;
    *count = in ? in->count - in->section_count[INBOX_SEEN] : 0;
    *failure = in && (in->section_count[INBOX_FAILED] || in->section_count[INBOX_ACTIONS]);
    *known = app->fleet || (app->attention && app->attention->have_good) || attention_no_runs(app);
}

/* ---- moving -------------------------------------------------------------- */

static void select_row(struct app *app, struct native_inbox *in, size_t row) {
    in->chosen = true;
    in->selected_row = row;
    entry_key(app, in, in->rows[row], in->selected, sizeof(in->selected));
    in->detail_scroll = 0;
    if (app->attention) app->attention->feedback[0] = '\0';
}

static void move(struct app *app, int direction) {
    struct native_inbox *in = app->inbox;
    size_t row = in->selected_row;
    while (direction > 0 ? row + 1 < in->row_count : row > 0) {
        row = direction > 0 ? row + 1 : row - 1;
        if (row_selectable(in->rows[row])) { select_row(app, in, row); return; }
    }
}

void inbox_click(struct app *app, size_t row, unsigned button) {
    struct native_inbox *in = app->inbox;
    inbox_build(app);
    if (!in) return;
    if (button == 64U || button == 65U) { move(app, button == 64U ? -1 : 1); return; }
    if (button == 0U && row < in->row_count && row_selectable(in->rows[row])) { select_row(app, in, row); in->focus_detail = false; }
}

/* ---- what each item is about --------------------------------------------- */

static const struct attention_item *selected_item(struct app *app) {
    const struct inbox_entry *e = inbox_selected(app);
    return e && e->kind == INBOX_ATTENTION ? &app->attention->items[e->index] : NULL;
}

static size_t run_index(const struct app *app, const char *run) {
    size_t i;
    for (i = 0; app->workflows && run && i < app->workflows->run_count; i++)
        if (!strcmp(app->workflows->runs[i].id, run)) return i;
    return SIZE_MAX;
}

static bool run_open(const struct app *app, const char *run) {
    size_t i = run_index(app, run);
    const char *state;
    if (i == SIZE_MAX) return false;
    state = app->workflows->runs[i].state;
    return strcmp(state, "succeeded") && strcmp(state, "failed") && strcmp(state, "cancelled");
}

/* The head that planned a run, by branch; NULL when it is not listed. */
static struct head *planning_head(struct app *app, const char *run) {
    size_t i = run_index(app, run);
    if (i == SIZE_MAX || !app->workflows->runs[i].planning[0]) return NULL;
    return native_plan_head(app, app->workflows->runs[i].planning);
}

static const struct head *entry_head(struct app *app, const struct inbox_entry *e) {
    const char *branch = NULL;
    if (e->kind == INBOX_HEAD) return &app->model.heads[e->index];
    if (e->kind == INBOX_GATE) branch = app->inbox->gates[e->index].branch;
    else if (e->kind == INBOX_PLAN) branch = app->inbox->plans[e->index].branch;
    else if (e->kind == INBOX_RECOVERY) branch = app->model.recovery[e->index].label;
    else if (e->kind == INBOX_LAND) branch = app->workflows->heads[e->index].branch;
    return branch ? head_for_branch(app, branch) : NULL;
}

/* The finding behind an item: a recovery row, or the one folded into an
 * Agents item. */
static const struct recovery *entry_recovery(struct app *app, const struct inbox_entry *e) {
    size_t i;
    if (!e) return NULL;
    if (e->kind == INBOX_RECOVERY) return &app->model.recovery[e->index];
    if (e->kind != INBOX_HEAD) return NULL;
    for (i = 0; i < app->model.recovery_count; i++)
        if (!strcmp(app->model.recovery[i].kind, "dead-session") && !strcmp(app->model.recovery[i].label, app->model.heads[e->index].branch))
            return &app->model.recovery[i];
    return NULL;
}

/* ---- what applies --------------------------------------------------------- */

static bool attention_applies(const struct app *app, const struct attention_item *item, enum key_action action) {
    switch (action) {
    case KEY_INBOX_REVIEW: return true;
    case KEY_INBOX_SEEN: return true;
    case KEY_INBOX_APPROVE: case KEY_INBOX_REJECT: return attention_is_approval(item) && attention_present(item->request);
    case KEY_INBOX_RESUME: return attention_is_failure(item) && !app->fleet && run_open(app, item->run);
    case KEY_INBOX_RETURN: return attention_is_failure(item) && !app->fleet && planning_head((struct app *)app, item->run);
    case KEY_INBOX_GOTO: return run_index(app, item->run) != SIZE_MAX;
    default: return false;
    }
}

static bool entry_applies(struct app *app, const struct inbox_entry *e, enum key_action action) {
    const struct recovery *r = entry_recovery(app, e);
    switch (e->kind) {
    case INBOX_ATTENTION: return attention_applies(app, &app->attention->items[e->index], action);
    case INBOX_GATE: return (action == KEY_INBOX_APPROVE && app->inbox->gates[e->index].name[0]) || action == KEY_INBOX_GOTO;
    case INBOX_PLAN: return action == KEY_INBOX_APPROVE || action == KEY_INBOX_RETURN || action == KEY_INBOX_GOTO;
    case INBOX_HEAD:
        if (action == KEY_INBOX_CHECK) return r != NULL;
        return action == KEY_INBOX_GOTO || (action == KEY_INBOX_RESUME && head_terminal_gone(app, &app->model.heads[e->index]));
    case INBOX_RECOVERY:
        if (action == KEY_INBOX_REMOVE) return !strcmp(r->kind, "orphan-worktree") && !app->fleet;
        return action == KEY_INBOX_CHECK && r->action[0] && !user_finding(r);
    case INBOX_LAND: return action == KEY_INBOX_LAND || action == KEY_INBOX_GOTO;
    default: return action == KEY_INBOX_SEEN;
    }
}

static bool page_available(const struct app *app, enum inbox_page page) {
    const struct inbox_entry *e = inbox_selected((struct app *)app);
    if (page == INBOX_SUMMARY || page == INBOX_TECHNICAL) return e != NULL;
    return e && e->kind == INBOX_ATTENTION;
}

static bool review_applies(const struct app *app, enum key_action action) {
    bool active = native_review_active(app);
    if (action == KEY_REVIEW_RELOAD || action == KEY_REVIEW_REFERENCE) return selected_item((struct app *)app) != NULL;
    if (action == KEY_REVIEW_OPEN) return active && app->inbox->page == INBOX_REFERENCES;
    if (action == KEY_REVIEW_LOG) return active && native_review_has_log(app);
    if (action == KEY_REVIEW_NEXT_SECTION || action == KEY_REVIEW_PREVIOUS_SECTION) return active && app->inbox->page == INBOX_REVIEW;
    return active;
}

bool inbox_applies(const struct app *app, enum key_action action) {
    const struct inbox_entry *e;
    if (!app->inbox) return action == KEY_INBOX_UP || action == KEY_INBOX_DOWN;
    if (action >= KEY_REVIEW_UP && action <= KEY_REVIEW_RELOAD) return review_applies(app, action);
    switch (action) {
    case KEY_INBOX_UP: case KEY_INBOX_DOWN: return app->inbox->row_count > 0;
    case KEY_INBOX_PAGE_PREVIOUS: case KEY_INBOX_PAGE_NEXT: return inbox_selected((struct app *)app) != NULL;
    case KEY_INBOX_PANE: case KEY_INBOX_PANE_BACK: return inbox_selected((struct app *)app) != NULL;
    case KEY_INBOX_OPEN: return app->inbox->row_count > 0;
    default: break;
    }
    e = inbox_selected((struct app *)app);
    return e && entry_applies((struct app *)app, e, action);
}

enum key_chain_id inbox_chain(const struct app *app) {
    const struct native_inbox *in = app->inbox;
    if (in && (in->focus_detail || in->detail_open) && (in->page == INBOX_REVIEW || in->page == INBOX_REFERENCES))
        return KEY_CHAIN_REVIEW;
    return KEY_CHAIN_INBOX;
}

/* ---- actions --------------------------------------------------------------- */

/* Below 100 columns the detail replaces the list; at 100 and more it is
 * beside it and takes the keys. */
static void show_detail(struct app *app, struct native_inbox *in) {
    if (app->cols < 100) in->detail_open = true;
    else in->focus_detail = true;
}

static void open_entry(struct app *app, struct native_inbox *in) {
    const struct inbox_entry *e = inbox_selected(app);
    size_t row = in->selected_row < in->row_count ? in->rows[in->selected_row] : 0;
    if (row == INBOX_ROW_GROUP) { in->seen_open = !in->seen_open; inbox_build(app); return; }
    if (!e) return;
    if (e->kind == INBOX_FAILED_ACTION) { show_result(app, app->failed[e->index].title, app->failed[e->index].output); return; }
    show_detail(app, in);
}

static bool refuse_stale(struct app *app) {
    char text[TEXT + 64];
    if (!app->attention || !app->attention->stale) return false;
    snprintf(text, sizeof(text), "This item is from an old list; reading it again failed: %.180s", app->attention->error);
    toast(app, KT_TOAST_ERROR, text);
    return true;
}

static void review_entry(struct app *app, struct native_inbox *in) {
    const struct attention_item *item = selected_item(app);
    struct native_review_identity id;
    if (!item || refuse_stale(app)) return;
    attention_review_identity(item, &id);
    native_review_open(app, &id);
    in->page = INBOX_REVIEW;
    show_detail(app, in);
}

static void seen_entry(struct app *app) {
    const struct inbox_entry *e = inbox_selected(app);
    if (!e) return;
    if (e->kind == INBOX_ATTENTION) {
        attention_toggle_seen(app, e->index);
        if (app->attention->feedback[0]) toast(app, KT_TOAST_SUCCESS, app->attention->feedback);
    } else if (e->kind == INBOX_FAILED_ACTION) app->failed[e->index].dismissed = !app->failed[e->index].dismissed;
    inbox_build(app);
}

/* An exact copy of what the decision is about, held across the question. */
struct decision { char identity[ATTENTION_DIGEST], revision[ATTENTION_DIGEST], run[ATTENTION_TEXT], step[ATTENTION_TEXT];
                  char request[ATTENTION_TEXT], task[ATTENTION_TEXT], host[ATTENTION_TEXT]; };

static void decision_copy(const struct attention_item *item, struct decision *d) {
    copy_text(d->identity, sizeof(d->identity), item->identity); copy_text(d->revision, sizeof(d->revision), item->revision);
    copy_text(d->run, sizeof(d->run), item->run); copy_text(d->step, sizeof(d->step), item->step);
    copy_text(d->request, sizeof(d->request), item->request); copy_text(d->task, sizeof(d->task), item->task);
    copy_text(d->host, sizeof(d->host), item->host);
}

/* A request on another machine: decided through its host's receiver. */
static bool remote_item(const struct app *app, const struct attention_item *item) {
    return app->fleet || (attention_present(item->host) && strcmp(item->host, "local") && attention_present(item->task));
}

static const struct task_observation *fleet_task(const struct app *app, const struct decision *d) {
    size_t i;
    for (i = 0; i < app->model.task_count; i++)
        if (!strcmp(app->model.tasks[i].host, d->host) && !strcmp(app->model.tasks[i].task_id, d->task)) return &app->model.tasks[i];
    return NULL;
}

/* Why a remote request cannot be decided now: its task is not observed, or
 * not freshly, or no longer waits on this request. NULL when it can. */
static const char *remote_refusal(const struct app *app, const struct decision *d, bool approve) {
    const struct task_observation *task = fleet_task(app, d);
    const char *reason;
    if (!task) return "its task is not in the latest observation of that host";
    if ((reason = native_fleet_task_refusal(app, task, approve ? 'Y' : 'N')) != NULL) return reason;
    if (strcmp(task->request_id, d->request)) return "the host reports a different request now";
    return NULL;
}

static bool decision_send(struct app *app, const struct decision *d, bool remote, const char *action) {
    struct task_observation task;
    const struct task_observation *found;
    if (!remote) return native_control_submit(app, d->run, action, d->request);
    if (!(found = fleet_task(app, d)) || !(found = native_fleet_task_current(app, found))) return false;
    task = *found;
    return native_fleet_control_submit(app, &task, action, d->request, task.spec_sha256);
}

static void decision_body(const struct decision *d, bool remote, bool approve, char *body, size_t size) {
    const char *effect = approve ? "continues: the waiting step may run" : "fails at this step and stops";
    if (remote) snprintf(body, size, "%s %s on %s?\n\nTask %s %s.\n\nRequest %s. The host's receiver acts on exactly "
                         "this task, request and specification; if any of them changes before you confirm, nothing is sent.",
                         approve ? "Approve" : "Reject", d->step, d->host, d->task, effect, d->request);
    else snprintf(body, size, "%s %s?\n\nRun %s %s.\n\nRequest %s. If the run or request changes before you "
                  "confirm, nothing is sent.", approve ? "Approve" : "Reject", d->step, d->run, effect, d->request);
}

/* Y and N on a request: the confirmation names the exact run, step and
 * request (and host, for remote work); the request is read again after it,
 * and nothing is sent when it changed or the list went stale meanwhile. A
 * remote request is decided only on a fresh observation of its task. */
static void decide(struct app *app, const struct attention_item *item, bool approve) {
    struct decision d;
    char body[1024];
    const char *refusal;
    bool remote = remote_item(app, item);
    if (refuse_stale(app)) return;
    decision_copy(item, &d);
    if (remote && (refusal = remote_refusal(app, &d, approve)) != NULL) {
        snprintf(body, sizeof(body), "Not decided on %.60s: %.300s", d.host, refusal);
        toast(app, KT_TOAST_ERROR, body);
        return;
    }
    decision_body(&d, remote, approve, body, sizeof(body));
    if (!dialog_confirm(app, approve ? "Approve request" : "Reject request", body, approve ? "Approve" : "Reject", !approve)) {
        copy_text(app->notice, sizeof(app->notice), "Nothing was decided; the request still waits.");
        return;
    }
    if (attention_find(app, d.identity, d.revision) == SIZE_MAX || (remote && remote_refusal(app, &d, approve)) ||
        !decision_send(app, &d, remote, approve ? "approve" : "reject"))
        toast(app, KT_TOAST_ERROR, "Not sent: the request changed, went stale or its owner is unavailable.");
}

static void actor(char *out, size_t size) {
    const char *user = getenv("USER");
    copy_text(out, size, user && user[0] && strlen(user) < 64U && !strpbrk(user, "\t\n") ? user : "hydra-tui");
}

/* Y on a head's approval gate: hydra gate approve, as the user. */
static void approve_gate(struct app *app, size_t index) {
    struct inbox_gate gate = app->inbox->gates[index];
    char body[640], by[64], output[4096];
    char *argv[] = {(char *)app->hydra, "gate", "approve", gate.branch, "--name", gate.name, "--by", by, NULL};
    actor(by, sizeof(by));
    snprintf(body, sizeof(body), "Approve gate %s of %.120s?\n\nRecorded as approved by %s. Hydra refuses the approval if "
             "the head's commit or worktree changed since the gate's check passed.", gate.name, gate.branch, by);
    if (!dialog_confirm(app, "Approve gate", body, "Approve", false)) {
        copy_text(app->notice, sizeof(app->notice), "Gate not approved; nothing changed.");
        return;
    }
    if (run_captured(app, argv, output, sizeof(output), 30000L) == 0) {
        toast(app, KT_TOAST_SUCCESS, "Gate approved");
        native_observations_tick(app, true);
        return;
    }
    action_failed(app, "Approving a gate did not finish", output);
    show_result(app, "Gate not approved", output[0] ? output : "The command produced no output.");
}

static void approve_entry(struct app *app, const struct inbox_entry *e, bool approve) {
    char branch[TEXT];
    if (e->kind == INBOX_ATTENTION) { decide(app, &app->attention->items[e->index], approve); return; }
    if (!approve) return;
    if (e->kind == INBOX_GATE) { approve_gate(app, e->index); return; }
    copy_text(branch, sizeof(branch), app->inbox->plans[e->index].branch);
    plan_action_approve_published(app, branch);
}

/* R on a stopped terminal: hydra resume --terminal starts it again in the
 * worktree it had; the worktree is never created, reset or cleaned. */
static void restart_terminal(struct app *app, const struct head *h) {
    char branch[TEXT], body[512], output[8192];
    char *argv[] = {(char *)app->hydra, (char *)"resume", (char *)"--terminal", branch, NULL};
    copy_text(branch, sizeof(branch), h->branch);
    snprintf(body, sizeof(body), "Restart the terminal of %.120s?\n\nIt starts again in the same worktree with its agent; "
             "files are not changed.", branch);
    if (!dialog_confirm(app, "Restart terminal", body, "Restart", false)) {
        copy_text(app->notice, sizeof(app->notice), "Terminal not restarted; nothing changed.");
        return;
    }
    if (run_captured(app, argv, output, sizeof(output), 60000L) == 0) {
        snprintf(body, sizeof(body), "Terminal of %.120s restarted; o opens it in Work", branch);
        toast(app, KT_TOAST_SUCCESS, body);
        native_observations_tick(app, true);
        return;
    }
    action_failed(app, "Restarting a terminal did not finish", output);
    show_result(app, "Terminal not restarted; its worktree and files are unchanged", output);
}

static void resume_run(struct app *app, const struct attention_item *item) {
    char run[ATTENTION_TEXT], body[512];
    copy_text(run, sizeof(run), item->run);
    snprintf(body, sizeof(body), "Resume run %s?\n\nIts owner continues the steps that are ready. A decision alone never "
             "resumes a run; this does.", run);
    if (!dialog_confirm(app, "Resume run", body, "Resume", false)) {
        copy_text(app->notice, sizeof(app->notice), "Control not submitted; nothing changed.");
        return;
    }
    if (!run_open(app, run) || !native_control_submit(app, run, "resume", "-"))
        toast(app, KT_TOAST_ERROR, "Not submitted: the run ended or its owner is unavailable.");
}

static void resume_entry(struct app *app, const struct inbox_entry *e) {
    if (e->kind == INBOX_HEAD) restart_terminal(app, &app->model.heads[e->index]);
    else if (e->kind == INBOX_ATTENTION && !refuse_stale(app)) resume_run(app, &app->attention->items[e->index]);
}

static void return_entry(struct app *app, const struct inbox_entry *e) {
    char branch[TEXT], run[ATTENTION_TEXT], what[ATTENTION_TEXT * 2];
    const struct attention_item *item;
    struct head *h;
    if (e->kind == INBOX_PLAN) {
        copy_text(branch, sizeof(branch), app->inbox->plans[e->index].branch);
        plan_action_return_published(app, branch);
        return;
    }
    item = &app->attention->items[e->index];
    if (!(h = planning_head(app, item->run))) return;
    copy_text(branch, sizeof(branch), h->branch);
    copy_text(run, sizeof(run), item->run);
    snprintf(what, sizeof(what), "%s of step %s", attention_item_title(item), item->step);
    plan_action_return_failure(app, branch, run, what);
}

/* o: the item's head or run in Work; Esc there comes back here. */
static void goto_entry(struct app *app, struct native_inbox *in, const struct inbox_entry *e) {
    const struct head *h = e->kind == INBOX_ATTENTION ? NULL : entry_head(app, e);
    size_t run = e->kind == INBOX_ATTENTION ? run_index(app, app->attention->items[e->index].run) : SIZE_MAX;
    if (!h && run == SIZE_MAX) { copy_text(app->notice, sizeof(app->notice), "It is not listed in Work"); return; }
    app->task_row = false;
    if (h) { app->run_row = false; app->selected = (size_t)(h - app->model.heads); }
    else { app->run_row = true; app->workflow_run = run; run_set_expanded(app, run, true); }
    /* The page that answers the item: the Run page for a run, the Plan page
     * for a plan, the Details page for a head. */
    work_open(app, h ? (e->kind == INBOX_PLAN || e->kind == INBOX_LAND ? WORK_PLAN : WORK_DETAILS) : WORK_RUN);
    in->jumped = true;
}

/* L on a result ready to land: hydra land <worker>, confirmed first. */
static void land_entry(struct app *app, const struct inbox_entry *e) {
    char worker[TEXT];
    copy_text(worker, sizeof(worker), app->workflows->heads[e->index].branch);
    plan_action_land(app, worker);
    native_observations_tick(app, true);
}

static void page_step(struct app *app, struct native_inbox *in, int direction) {
    int page = (int)in->page, tries;
    for (tries = 0; tries < INBOX_PAGES; tries++) {
        page = (page + direction + INBOX_PAGES) % INBOX_PAGES;
        if (page_available(app, (enum inbox_page)page)) break;
    }
    in->page = (enum inbox_page)page;
    in->detail_scroll = 0;
}

static void pane(struct app *app, struct native_inbox *in) {
    if (app->cols < 100) in->detail_open = !in->detail_open;
    else in->focus_detail = !in->focus_detail;
}

/* The keys of the review pages, as review.c takes them. */
static char review_char(enum key_action action) {
    static const char keys[] = {
        [KEY_REVIEW_UP - KEY_REVIEW_UP] = 'k', [KEY_REVIEW_DOWN - KEY_REVIEW_UP] = 'j',
        [KEY_REVIEW_PAGE_DOWN - KEY_REVIEW_UP] = ' ', [KEY_REVIEW_PAGE_UP - KEY_REVIEW_UP] = 'b',
        [KEY_REVIEW_TOP - KEY_REVIEW_UP] = 'g', [KEY_REVIEW_END - KEY_REVIEW_UP] = 'G',
        [KEY_REVIEW_NEXT_SECTION - KEY_REVIEW_UP] = 'n', [KEY_REVIEW_PREVIOUS_SECTION - KEY_REVIEW_UP] = 'N',
        [KEY_REVIEW_OPEN - KEY_REVIEW_UP] = '\r', [KEY_REVIEW_REFERENCE - KEY_REVIEW_UP] = '+',
        [KEY_REVIEW_LOG - KEY_REVIEW_UP] = 'L'};
    return keys[action - KEY_REVIEW_UP];
}

static void review_perform(struct app *app, struct native_inbox *in, enum key_action action) {
    if (action == KEY_REVIEW_RELOAD) { review_entry(app, in); return; }
    if (action == KEY_REVIEW_REFERENCE && !native_review_active(app)) review_entry(app, in);
    (void)native_review_key(app, review_char(action), in->page == INBOX_REFERENCES);
}

static void entry_perform(struct app *app, struct native_inbox *in, const struct inbox_entry *e, enum key_action action) {
    const struct recovery *r = entry_recovery(app, e);
    switch (action) {
    case KEY_INBOX_APPROVE: case KEY_INBOX_REJECT: approve_entry(app, e, action == KEY_INBOX_APPROVE); break;
    case KEY_INBOX_RESUME: resume_entry(app, e); break;
    case KEY_INBOX_RETURN: return_entry(app, e); break;
    case KEY_INBOX_GOTO: goto_entry(app, in, e); break;
    case KEY_INBOX_LAND: land_entry(app, e); break;
    case KEY_INBOX_CHECK: if (r) recovery_check_action(app, (size_t)(r - app->model.recovery)); break;
    case KEY_INBOX_REMOVE: if (r) recovery_remove_action(app, (size_t)(r - app->model.recovery)); break;
    default: break;
    }
}

static bool navigation_perform(struct app *app, struct native_inbox *in, enum key_action action) {
    switch (action) {
    case KEY_INBOX_UP: case KEY_INBOX_DOWN: move(app, action == KEY_INBOX_DOWN ? 1 : -1); return true;
    case KEY_INBOX_OPEN: open_entry(app, in); return true;
    case KEY_INBOX_REVIEW: review_entry(app, in); return true;
    case KEY_INBOX_SEEN: seen_entry(app); return true;
    case KEY_INBOX_PANE: case KEY_INBOX_PANE_BACK: pane(app, in); return true;
    case KEY_INBOX_PAGE_PREVIOUS: case KEY_INBOX_PAGE_NEXT: page_step(app, in, action == KEY_INBOX_PAGE_NEXT ? 1 : -1); return true;
    default: return false;
    }
}

bool inbox_perform(struct app *app, enum key_action action) {
    struct native_inbox *in;
    const struct inbox_entry *e;
    if (action < KEY_REVIEW_UP || action > KEY_INBOX_LAST) return false;
    if (!(in = inbox_of(app))) return true;
    inbox_build(app);
    if (!inbox_applies(app, action)) return true;
    /* Whatever the user did, the selection is now theirs to keep. */
    in->chosen = true;
    if (action <= KEY_REVIEW_RELOAD) { review_perform(app, in, action); return true; }
    if (navigation_perform(app, in, action)) return true;
    if ((e = inbox_selected(app)) != NULL) entry_perform(app, in, e, action);
    inbox_build(app);
    return true;
}

bool inbox_back(struct app *app) {
    struct native_inbox *in = app->inbox;
    if (!in) return false;
    if ((in->page == INBOX_REVIEW || in->page == INBOX_REFERENCES) && native_review_back(app)) return true;
    if (in->focus_detail || in->detail_open) { in->focus_detail = in->detail_open = false; return true; }
    return false;
}

/* ---- repairs ------------------------------------------------------------- */

/* c: runs the finding's recorded check, with its output in the viewer. */
void recovery_check_action(struct app *app, size_t index) {
    const struct recovery *item;
    char command[TEXT], *argv[16], *save = NULL, *word, output[8192], title[TEXT + 64], detail[1024];
    size_t count = 0U;
    int status;
    if (index >= app->model.recovery_count) return;
    item = &app->model.recovery[index];
    copy_text(command, sizeof(command), item->action);
    word = strtok_r(command, " ", &save);
    if (!word) { copy_text(app->notice, sizeof(app->notice), "No check is recorded for this finding"); return; }
    argv[count++] = (char *)app->hydra;
    if (strcmp(word, "hydra") != 0) argv[count++] = word;
    while ((word = strtok_r(NULL, " ", &save)) != NULL && count < 15U) argv[count++] = word;
    argv[count] = NULL;
    recovery_explain(item, title, sizeof(title), detail, sizeof(detail));
    snprintf(app->notice, sizeof(app->notice), "Running %s...", item->action);
    status = run_captured(app, argv, output, sizeof(output), 30000L);
    if (status == 0) snprintf(app->notice, sizeof(app->notice), "Check finished: %s", item->action);
    else snprintf(app->notice, sizeof(app->notice), "Check failed (exit %d): %s", status, item->action);
    show_result(app, title, output[0] ? output : "The check produced no output.");
}

static bool orphan_removal_confirmed(struct app *app, const struct recovery *item) {
    char body[1024];
    snprintf(body, sizeof(body), "Remove leftover worktree %.300s?\n\nDeletes only that directory; the branch is kept.", item->label);
    if (dialog_confirm(app, "Remove leftover worktree", body, "Remove", true)) return true;
    copy_text(app->notice, sizeof(app->notice), "Removal cancelled; nothing changed");
    return false;
}

/* Remove the selected leftover worktree through the shell gc authority, scoped
 * to its path. Worktrees with uncommitted changes are never offered; gc keeps
 * its own dirty refusal and the UI never adds --include-dirty. */
void recovery_remove_action(struct app *app, size_t index) {
    const struct recovery *item;
    char path[SOURCE_TEXT], output[8192];
    char *argv[] = {(char *)app->hydra, (char *)"gc", (char *)"--policy", (char *)"orphaned", (char *)"--apply", (char *)"--path", path, NULL};
    if (index >= app->model.recovery_count) return;
    item = &app->model.recovery[index];
    if (!strcmp(item->kind, "orphan-worktree-dirty")) {
        copy_text(app->notice, sizeof(app->notice), "This worktree has uncommitted changes, so Hydra keeps it; review the work there first");
        return;
    }
    if (strcmp(item->kind, "orphan-worktree") != 0 || !item->source[0]) {
        copy_text(app->notice, sizeof(app->notice), "x removes leftover worktrees only; c runs this finding's check");
        return;
    }
    if (!orphan_removal_confirmed(app, item)) return;
    copy_text(path, sizeof(path), item->source);
    snprintf(app->notice, sizeof(app->notice), "Removing leftover worktree %.200s...", path);
    if (run_captured(app, argv, output, sizeof(output), 60000L) == 0 && strstr(output, "removed-orphan\t") != NULL) {
        snprintf(app->notice, sizeof(app->notice), "Removed leftover worktree %.200s; the branch is kept", path);
    } else {
        copy_text(app->notice, sizeof(app->notice), "The worktree was not removed; see output");
        action_failed(app, "Removing a leftover worktree did not finish", output);
        show_result(app, "REMOVAL OUTPUT", output[0] ? output : "The removal produced no output.");
    }
    native_observations_tick(app, true);
}
