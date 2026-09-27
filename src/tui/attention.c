#define _POSIX_C_SOURCE 200809L
#include "internal.h"

#define ATTENTION_TEXT 128U
#define ATTENTION_REASON 256U
#define ATTENTION_DIGEST 65U
#define ATTENTION_LINE 8192U
#define ATTENTION_DUPLICATES 1024U
#define ATTENTION_OPS 32U
#define ATTENTION_GROUP ((size_t)-1)

struct attention_item {
    char source[ATTENTION_TEXT]; char kind[ATTENTION_TEXT]; char reason[ATTENTION_REASON];
    char project[ATTENTION_TEXT]; char host[ATTENTION_TEXT]; char task[ATTENTION_TEXT];
    char run[ATTENTION_TEXT]; char step[ATTENTION_TEXT]; char attempt[ATTENTION_TEXT];
    char head[ATTENTION_TEXT]; char instance[ATTENTION_TEXT]; char request[ATTENTION_TEXT];
    char binding[ATTENTION_TEXT]; char revision[ATTENTION_DIGEST]; char identity[ATTENTION_DIGEST];
    char freshness[ATTENTION_TEXT]; char route_kind[ATTENTION_TEXT]; char navigable[ATTENTION_TEXT];
    char label[ATTENTION_TEXT]; char detail[ATTENTION_REASON];
    bool unseen;
};
struct attention_seen { char identity[ATTENTION_DIGEST]; char revision[ATTENTION_DIGEST]; };
/* Seen markers persist per user through `hydra workflow attention-seen`. The
 * store survives snapshot replacement; local marks apply at once and the
 * queued public command records them. */
struct attention_op { char identity[ATTENTION_DIGEST]; char revision[ATTENTION_DIGEST]; bool clear, list; };
struct attention_store {
    struct native_capture job;
    struct attention_op ops[ATTENTION_OPS];
    size_t op_count;
    bool running_list, failed;
};
struct native_attention {
    struct attention_item items[NATIVE_ATTENTION_ITEMS];
    struct attention_seen seen[NATIVE_ATTENTION_SEEN];
    struct attention_store *store;
    size_t count, seen_count, selected, scroll;
    bool detail, stale, have_good, loading, partial, truncated, on_group, seen_open;
    unsigned need_count, seen_group_count, stale_count, unknown_count, expired_count, failure_count;
    char error[TEXT], feedback[TEXT];
};

static void review_identity(const struct attention_item *item, struct native_review_identity *id)
{
    memset(id, 0, sizeof(*id));
    copy_text(id->source, sizeof(id->source), item->source);
    copy_text(id->kind, sizeof(id->kind), item->kind);
    copy_text(id->project, sizeof(id->project), item->project);
    copy_text(id->host, sizeof(id->host), item->host);
    copy_text(id->task, sizeof(id->task), item->task);
    copy_text(id->run, sizeof(id->run), item->run);
    copy_text(id->step, sizeof(id->step), item->step);
    copy_text(id->attempt, sizeof(id->attempt), item->attempt);
    copy_text(id->head, sizeof(id->head), item->head);
    copy_text(id->instance, sizeof(id->instance), item->instance);
    copy_text(id->request, sizeof(id->request), item->request);
    copy_text(id->binding, sizeof(id->binding), item->binding);
    copy_text(id->revision, sizeof(id->revision), item->revision);
    copy_text(id->identity, sizeof(id->identity), item->identity);
}

static bool item_selected(const struct native_attention *view)
{ return view && !view->on_group && view->selected < view->count; }

static void sync_review(struct app *app)
{
    struct native_attention *view = app->attention;
    struct native_review_identity id;
    if (!item_selected(view)) { native_review_sync(app, NULL, false); return; }
    review_identity(&view->items[view->selected], &id);
    native_review_sync(app, &id, view->have_good && !view->stale);
}

static bool put_field(char *dst, size_t cap, const char *value, size_t maximum)
{
    size_t length;
    if (!value || !*value) return false;
    if (!strcmp(value, "-")) { dst[0] = '-'; dst[1] = '\0'; return true; }
    length = strlen(value);
    if (length > maximum || length >= cap) return false;
    memcpy(dst, value, length + 1U);
    return true;
}
static bool valid_digest(const char *value)
{
    size_t i;
    if (!value || strlen(value) != 64U) return false;
    for (i = 0U; i < 64U; i++) {
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
    return true;
}
static bool item_equal(const struct attention_item *left, const struct attention_item *right)
{ return !memcmp(left, right, sizeof(*left)); }
static bool seen_revision(const struct native_attention *view, const char *identity, const char *revision)
{
    size_t i;
    for (i = 0U; i < view->seen_count; i++) {
        if (!strcmp(view->seen[i].identity, identity) && !strcmp(view->seen[i].revision, revision)) return true;
    }
    return false;
}
/* One marker per identity: marking a new revision replaces the older one. */
static void forget_identity(struct native_attention *view, const char *identity)
{
    size_t i = 0U;
    while (i < view->seen_count) {
        if (strcmp(view->seen[i].identity, identity)) { i++; continue; }
        memmove(view->seen + i, view->seen + i + 1U, (view->seen_count - i - 1U) * sizeof(view->seen[0]));
        view->seen_count--;
    }
}
static void remember_revision(struct native_attention *view, const char *identity, const char *revision)
{
    size_t slot;
    forget_identity(view, identity);
    if (view->seen_count == NATIVE_ATTENTION_SEEN) {
        memmove(view->seen, view->seen + 1U, (NATIVE_ATTENTION_SEEN - 1U) * sizeof(view->seen[0]));
        view->seen_count--;
    }
    slot = view->seen_count++;
    /* Both inputs are complete validated digest arrays. */
    memmove(view->seen[slot].identity, identity, sizeof(view->seen[slot].identity));
    memmove(view->seen[slot].revision, revision, sizeof(view->seen[slot].revision));
}

static bool copy_item_fields(char *fields[], struct attention_item *item, size_t field_count)
{
    char *destinations[] = {item->source, item->kind, item->reason, item->project, item->host, item->task, item->run, item->step, item->attempt, item->head, item->instance, item->request, item->binding, item->revision, item->identity, item->freshness, item->route_kind, item->navigable, item->label, item->detail};
    const size_t capacities[] = {sizeof(item->source), sizeof(item->kind), sizeof(item->reason), sizeof(item->project), sizeof(item->host), sizeof(item->task), sizeof(item->run), sizeof(item->step), sizeof(item->attempt), sizeof(item->head), sizeof(item->instance), sizeof(item->request), sizeof(item->binding), sizeof(item->revision), sizeof(item->identity), sizeof(item->freshness), sizeof(item->route_kind), sizeof(item->navigable), sizeof(item->label), sizeof(item->detail)};
    size_t i;
    copy_text(item->label, sizeof(item->label), "-");
    copy_text(item->detail, sizeof(item->detail), "-");
    for (i = 0U; i + 1U < field_count; i++) {
        size_t maximum = i == 2U || i == 19U ? 255U : i == 12U ? 64U : 127U;
        if ((i == 13U || i == 14U) && !valid_digest(fields[i + 1U])) return false;
        if (!put_field(destinations[i], capacities[i], fields[i + 1U], maximum)) return false;
    }
    return true;
}
static bool one_of(const char *value, const char *const choices[], size_t count)
{
    size_t i;
    for (i = 0U; i < count; i++) if (!strcmp(value, choices[i])) return true;
    return false;
}
/* Version 1 items have 19 fields; version 2 appends a presentation label and
 * version 3 a presentation detail (the requirements a failed check decides). */
static bool parse_item(char *line, struct attention_item *item, unsigned version)
{
    static const char *const freshness[] = {"fresh", "stale", "unknown", "expired"};
    static const char *const routes[] = {"workflow-request", "workflow-evidence", "task-observe", "task-result", "agent-record", "unavailable"};
    char *fields[22];
    size_t expected = 18U + version;
    if (split_fields(line, fields, 22U) != expected || strcmp(fields[0], "ITEM")) return false;
    if (!copy_item_fields(fields, item, expected)) return false;
    if (!one_of(item->freshness, freshness, 4U) || !one_of(item->route_kind, routes, 6U)) return false;
    return !strcmp(item->navigable, "0") || !strcmp(item->navigable, "1");
}
static bool parse_end(char *line, size_t wire_count, struct native_attention *view)
{
    char *fields[4]; unsigned expected, partial, truncated;
    if (split_fields(line, fields, 4U) != 4U || !parse_unsigned(fields[1], &expected) || !parse_unsigned(fields[2], &partial) || !parse_unsigned(fields[3], &truncated)) return false;
    /* A truncated document is necessarily partial.  Reject contradictory
     * flags instead of presenting an apparently complete snapshot. */
    if (expected != wire_count || partial > 1U || truncated > 1U || (truncated && !partial)) return false;
    view->partial = partial != 0U; view->truncated = truncated != 0U;
    return true;
}
struct attention_parse { size_t wire, unique, duplicates; unsigned version; bool ended; };
static bool accept_item(struct native_attention *view, char *line, struct attention_parse *state)
{
    struct attention_item item = {0}; size_t prior;
    if (!parse_item(line, &item, state->version)) return false;
    for (prior = 0U; prior < state->unique; prior++) {
        if (strcmp(view->items[prior].identity, item.identity)) continue;
        if (strcmp(view->items[prior].revision, item.revision) || !item_equal(&view->items[prior], &item)) return false;
        state->duplicates++;
        return state->duplicates <= ATTENTION_DUPLICATES;
    }
    if (state->unique == NATIVE_ATTENTION_ITEMS) return false;
    view->items[state->unique++] = item;
    return true;
}
static bool valid_line(char *line, size_t *length)
{
    size_t i;
    *length = strlen(line);
    if (*length == 0U || line[*length - 1U] != '\n') return false;
    for (i = 0U; i < *length; i++) {
        unsigned char c = (unsigned char)line[i];
        if ((c < 0x20U && c != '\t' && c != '\n') || c == 0x7fU) return false;
    }
    line[--*length] = '\0';
    return true;
}
static bool parse_header(char *line, struct attention_parse *state)
{
    char *fields[3];
    if (split_fields(line, fields, 3U) != 2U || strcmp(fields[0], "HYDRA_ATTENTION")) return false;
    if (!strcmp(fields[1], "1")) state->version = 1U;
    else if (!strcmp(fields[1], "2")) state->version = 2U;
    else if (!strcmp(fields[1], "3")) state->version = 3U;
    return state->version != 0U;
}
static bool parse_stream_line(char *line, struct native_attention *view, struct attention_parse *state)
{
    if (!state->version) return parse_header(line, state);
    if (!strncmp(line, "END\t", 4U)) {
        if (!parse_end(line, state->wire, view)) return false;
        state->ended = true;
        return true;
    }
    if (strncmp(line, "ITEM\t", 5U) || state->wire == SIZE_MAX) return false;
    state->wire++;
    return state->wire <= ATTENTION_DUPLICATES && accept_item(view, line, state);
}
static bool parse_stream(FILE *input, struct native_attention *view)
{
    char line[ATTENTION_LINE]; size_t length;
    struct attention_parse state = {0};
    while (fgets(line, sizeof(line), input) != NULL) {
        if (state.ended || !valid_line(line, &length)) return false;
        if (!parse_stream_line(line, view, &state)) return false;
    }
    if (!state.version || !state.ended || ferror(input)) return false;
    view->count = state.unique;
    return true;
}

/* Decisions stay listed until decided: marking an approval seen never moves
 * it out of the list and never counts as approving or rejecting it. A failure
 * likewise stays until its run is resolved (the step succeeds, or a later run
 * of the same plan succeeds); marking it seen resolves nothing. */
static bool is_failure(const struct attention_item *item) { return !strcmp(item->kind, "failure"); }
static bool needs_decision(const struct attention_item *item)
{ return !strcmp(item->kind, "approval") || !strcmp(item->kind, "permission") || is_failure(item); }
static bool eligible(const struct attention_item *item)
{ return needs_decision(item) || !strcmp(item->kind, "result"); }
static bool in_seen_group(const struct attention_item *item)
{ return !item->unseen && !needs_decision(item); }
static void classify(struct native_attention *view)
{
    size_t i;
    view->need_count = 0U; view->seen_group_count = 0U; view->stale_count = 0U; view->unknown_count = 0U; view->expired_count = 0U;
    view->failure_count = 0U;
    for (i = 0U; i < view->count; i++) {
        const struct attention_item *item = &view->items[i];
        bool fresh = !strcmp(item->freshness, "fresh");
        if (in_seen_group(item)) view->seen_group_count++;
        if (fresh && is_failure(item)) view->failure_count++;
        if (fresh && eligible(item) && (item->unseen || needs_decision(item))) view->need_count++;
        else if (!strcmp(item->freshness, "stale")) view->stale_count++;
        else if (!strcmp(item->freshness, "expired")) view->expired_count++;
        else if (!fresh || !eligible(item)) view->unknown_count++;
    }
}
static void apply_seen(struct native_attention *view)
{
    size_t i;
    for (i = 0U; i < view->count; i++)
        view->items[i].unseen = !seen_revision(view, view->items[i].identity, view->items[i].revision);
    classify(view);
}

/* Row order: failures, then the other items needing the user, then one
 * collapsible Seen group. */
static size_t build_rows(const struct native_attention *view, size_t *rows)
{
    size_t i, n = 0U;
    for (i = 0U; i < view->count; i++) if (is_failure(&view->items[i])) rows[n++] = i;
    for (i = 0U; i < view->count; i++) if (!in_seen_group(&view->items[i]) && !is_failure(&view->items[i])) rows[n++] = i;
    if (view->seen_group_count) rows[n++] = ATTENTION_GROUP;
    for (i = 0U; view->seen_open && i < view->count; i++) if (in_seen_group(&view->items[i])) rows[n++] = i;
    return n;
}
static size_t current_row(const struct native_attention *view, const size_t *rows, size_t n)
{
    size_t i;
    for (i = 0U; i < n; i++) {
        if (view->on_group ? rows[i] == ATTENTION_GROUP : rows[i] == view->selected) return i;
    }
    return 0U;
}
static void select_row(struct native_attention *view, size_t row)
{
    view->on_group = row == ATTENTION_GROUP;
    if (!view->on_group) view->selected = row;
}
/* Keep a valid selection after regrouping (a seen item may have moved). */
static void settle_selection(struct native_attention *view)
{
    size_t rows[NATIVE_ATTENTION_ITEMS + 1U], n = build_rows(view, rows), i;
    /* An open detail keeps its item even after it moves into the collapsed group. */
    if (view->detail && item_selected(view)) return;
    if (!n) { view->on_group = false; view->selected = 0U; return; }
    for (i = 0U; i < n; i++) if (view->on_group ? rows[i] == ATTENTION_GROUP : rows[i] == view->selected) return;
    select_row(view, rows[0]);
}

/* ---- persistent seen store ------------------------------------------------ */

static struct attention_store *store_of(struct native_attention *view)
{
    if (!view->store) {
        view->store = calloc(1U, sizeof(*view->store));
        if (view->store) view->store->job.fd = -1;
    }
    return view->store;
}
static void enqueue(struct native_attention *view, const struct attention_op *op)
{
    struct attention_store *store = store_of(view);
    if (!store) return;
    if (op->list) {
        size_t i;
        for (i = 0U; i < store->op_count; i++) if (store->ops[i].list) return;
    }
    if (store->op_count == ATTENTION_OPS) {
        memmove(store->ops, store->ops + 1U, (ATTENTION_OPS - 1U) * sizeof(store->ops[0]));
        store->op_count--;
    }
    store->ops[store->op_count++] = *op;
}
static bool start_op(struct app *app, struct attention_store *store)
{
    struct attention_op *op = &store->ops[0];
    char *argv[] = {(char *)app->hydra, "workflow", "attention-seen", op->list ? "list" : op->clear ? "clear" : "mark",
        op->list ? NULL : op->identity, op->list || op->clear ? NULL : op->revision, NULL};
    store->running_list = op->list;
    return native_capture_start(&store->job, argv, 10000L);
}
static bool parse_seen_line(char *line, struct native_attention *next, unsigned *expected, bool *ended)
{
    char *fields[4];
    size_t n = split_fields(line, fields, 4U);
    if (n == 2U && !strcmp(fields[0], "END")) { *ended = parse_unsigned(fields[1], expected); return *ended; }
    if (n != 3U || strcmp(fields[0], "SEEN") || !valid_digest(fields[1]) || !valid_digest(fields[2]) ||
        next->seen_count == NATIVE_ATTENTION_SEEN) return false;
    remember_revision(next, fields[1], fields[2]);
    return true;
}
/* The store's list replaces local markers only when no local change is still
 * queued, so an older listing can never undo a newer mark. */
static bool parse_seen(FILE *input, struct native_attention *next)
{
    char line[256]; size_t length; unsigned expected = 0U, rows = 0U; bool header = false, ended = false;
    while (fgets(line, sizeof(line), input)) {
        if (ended || !valid_line(line, &length)) return false;
        if (!header) { header = !strcmp(line, "HYDRA_ATTENTION_SEEN\t1"); if (!header) return false; continue; }
        if (!parse_seen_line(line, next, &expected, &ended)) return false;
        if (!ended) rows++;
    }
    return header && ended && expected == rows && !ferror(input);
}
static void complete_op(struct app *app, struct attention_store *store)
{
    struct native_attention *view = app->attention;
    bool success, pending = store->op_count > 1U;
    FILE *input = native_capture_result(&store->job, &success);
    struct native_attention *next = input && success ? calloc(1U, sizeof(*next)) : NULL;
    memmove(store->ops, store->ops + 1U, (store->op_count - 1U) * sizeof(store->ops[0]));
    store->op_count--;
    store->failed = !next || !parse_seen(input, next);
    if (input) fclose(input);
    if (!store->failed && !pending) {
        memcpy(view->seen, next->seen, sizeof(view->seen)); view->seen_count = next->seen_count;
        apply_seen(view); settle_selection(view);
    }
    if (store->failed) copy_text(view->error, sizeof(view->error), "Seen markers are unavailable; marks last only for this session");
    free(next);
}
static void store_tick(struct app *app)
{
    struct native_attention *view = app->attention;
    struct attention_store *store = view ? view->store : NULL;
    if (!store) return;
    if (store->job.pid && native_capture_step(&store->job)) complete_op(app, store);
    if (!store->job.pid && store->op_count && !start_op(app, store)) {
        memmove(store->ops, store->ops + 1U, (store->op_count - 1U) * sizeof(store->ops[0]));
        store->op_count--; store->failed = true;
    }
}
/* Let queued markers reach the store before the client exits (bounded). */
static void store_flush(struct app *app)
{
    const struct timespec pause = {0, 10000000L};
    struct attention_store *store = app->attention ? app->attention->store : NULL;
    unsigned waited = 0U;
    while (store && (store->job.pid || store->op_count) && waited++ < 300U) {
        if (!store->job.pid && store->ops[0].list) { store->op_count = 0U; break; }
        store_tick(app);
        (void)nanosleep(&pause, NULL);
    }
    if (store) native_capture_destroy(&store->job);
}

void native_attention_destroy(struct app *app)
{
    if (!app->attention) return;
    store_flush(app);
    free(app->attention->store); free(app->attention); app->attention = NULL;
}
static void retain_state(struct native_attention *old, struct native_attention *next, char *selected_id, bool *detail)
{
    if (!old) return;
    if (item_selected(old)) copy_text(selected_id, ATTENTION_DIGEST, old->items[old->selected].identity);
    next->scroll = old->scroll; next->seen_count = old->seen_count; memcpy(next->seen, old->seen, sizeof(next->seen)); *detail = old->detail;
    next->store = old->store; old->store = NULL;
    next->seen_open = old->seen_open; next->on_group = old->on_group;
    copy_text(next->feedback, sizeof(next->feedback), old->feedback);
}
static void attention_error(struct app *app, const char *message)
{
    if (!app->attention) return;
    app->attention->loading = false;
    app->attention->stale = app->attention->have_good;
    copy_text(app->attention->error, sizeof(app->attention->error), message);
}
static void finalize_attention(struct native_attention *next, const char *selected_id, bool detail)
{
    size_t i;
    bool found = false;
    for (i = 0U; i < next->count && selected_id[0] && !found; i++) {
        if (!strcmp(selected_id, next->items[i].identity)) { next->selected = i; found = true; }
    }
    if (!found) next->selected = 0U;
    next->detail = found && detail;
    next->have_good = true;
    next->stale = false;
    apply_seen(next);
    if (!found && !next->on_group) {
        /* Without a retained selection the first row is selected: a failure when there is one. */
        size_t rows[NATIVE_ATTENTION_ITEMS + 1U];
        if (build_rows(next, rows)) select_row(next, rows[0]);
    }
    settle_selection(next);
}
static void accept_attention(struct app *app, FILE *input)
{
    struct native_attention *next = calloc(1U, sizeof(*next)); char selected_id[ATTENTION_DIGEST] = ""; bool detail = false;
    if (!next) { if (input) fclose(input); return; }
    retain_state(app->attention, next, selected_id, &detail);
    if (!input || !parse_stream(input, next)) {
        if (input) fclose(input);
        if (app->attention) { app->attention->store = next->store; }
        free(next);
        attention_error(app, app->attention && app->attention->have_good ? "Attention stream invalid; showing last good snapshot" : "Attention unavailable; no valid snapshot yet");
        return;
    }
    fclose(input);
    finalize_attention(next, selected_id, detail);
    next->loading = false;
    free(app->attention); app->attention = next;
}
static void capture_failure(struct app *app)
{ attention_error(app, app->attention && app->attention->have_good ? "Attention unavailable; showing last good snapshot" : "Attention unavailable; no valid snapshot yet"); }
void native_attention_tick(struct app *app, bool request)
{
    struct native_capture *job; char *argv[4];
    struct attention_op list = {"", "", false, true};
    if (!app->attention) app->attention = calloc(1U, sizeof(*app->attention));
    if (!app->attention || !app->observations) return;
    job = &app->observations->jobs[4];
    if (job->pid && native_capture_step(job)) {
        FILE *input = native_capture_take(job);
        if (input) accept_attention(app, input); else capture_failure(app);
    }
    store_tick(app);
    sync_review(app);
    if (!request || app->view != 9 || job->pid) return;
    argv[0] = (char *)app->hydra; argv[1] = (char *)(app->fleet ? "fleet" : "workflow"); argv[2] = (char *)"attention-data"; argv[3] = NULL;
    app->attention->loading = true;
    enqueue(app->attention, &list);
    if (!native_capture_start(job, argv, app->fleet ? 13000L : 10000L)) capture_failure(app);
}

/* ---- wording ---------------------------------------------------------------- */

static const char *failure_title(const struct attention_item *item)
{
    static const char *const map[][2] = {{"check_failed", "Check failed"}, {"step_failed", "Step failed"},
        {"step_recovery_required", "Step needs recovery"}, {"run_failed", "Run failed"},
        {"run_recovery_required", "Run needs recovery"}, {"task_failed", "Task failed"}};
    size_t i;
    for (i = 0U; i < sizeof(map) / sizeof(map[0]); i++) if (!strcmp(item->reason, map[i][0])) return map[i][1];
    return "Failed";
}
static const char *item_title(const struct attention_item *item)
{
    static const char *const map[][2] = {{"approval", "Approval needed"}, {"approval_expired", "Approval expired"},
        {"result", "Result ready for review"}, {"permission", "Permission requested"}};
    size_t i;
    if (is_failure(item)) return failure_title(item);
    for (i = 0U; i < sizeof(map) / sizeof(map[0]); i++) if (!strcmp(item->kind, map[i][0])) return map[i][1];
    return "Needs inspection";
}
static bool present(const char *value) { return value[0] && strcmp(value, "-"); }
/* "Check failed · plan · step — requirements; review the log and send the plan
 * back or retry": the failure's one-line account and what the user can do. */
static void failure_tail(const struct app *app, const struct attention_item *item, char *out, size_t size)
{
    if (present(item->detail)) text_append(out, size, "%s%s", app->ascii ? " - " : " \xe2\x80\x94 ", item->detail);
    text_append(out, size, "; review the log and send the plan back or retry");
}
static void item_subject(const struct app *app, const struct attention_item *item, char *out, size_t size)
{
    const char *name = present(item->label) ? item->label : present(item->task) ? item->task : present(item->run) ? item->run : "unnamed";
    out[0] = '\0';
    text_append(out, size, "%s", name);
    if (present(item->step)) text_append(out, size, "%s%s", dot(app), item->step);
    if (present(item->host) && strcmp(item->host, "local")) text_append(out, size, "%son %s", dot(app), item->host);
}
static void attempt_text(const struct attention_item *item, char *out, size_t size)
{
    const char *number = !strncmp(item->attempt, "attempt-", 8U) ? item->attempt + 8 : item->attempt;
    out[0] = '\0';
    if (present(item->attempt)) text_append(out, size, " (attempt %s)", number);
}
static const char *reason_text(const char *reason)
{
    static const char *const map[][2] = {
        {"result_ready", "finished and sealed its output. Press r to see what it produced and how it was checked."},
        {"approval_pending", "is waiting for your decision. Press r to review the request; marking it seen does not approve it."},
        {"approval_expired", "asked for a decision, but the request expired before anyone decided."},
        {"result_binding_unknown", "finished, but its recorded output does not match what the plan declared."},
        {"missing_request", "waits for approval, but no request is recorded."},
        {"approval_binding_unknown", "has an approval request that no longer matches its head."},
        {"retention_expired", "has evidence that expired under the retention policy."},
        {"missing_authoritative_attempt", "has no attempt Hydra can treat as current."},
        {"missing_attempt", "points at an attempt whose records are missing."},
        {"invalid_attempt", "records an attempt number outside the supported range."},
        {"malformed_request", "has a malformed approval request."},
        {"request_step_mismatch", "has an approval request recorded for a different step."},
        {"malformed_expiry", "has an approval request with an unreadable expiry."},
        {"missing_steps", "has no readable step records."},
        {"check_failed", "failed its check. Press r to see the failed requirement and the log summary."},
        {"step_failed", "failed. Press r to see its log and what the run recorded."},
        {"step_recovery_required", "stopped in a state Hydra cannot finish on its own. Press r to see its records."},
        {"run_failed", "failed after its steps finished, so its result was not delivered. Press r to see what was checked."},
        {"run_recovery_required", "stopped in a state Hydra cannot finish on its own. Press r to see its records."},
        {"task_failed", "failed on its host. Press r to see its retained result and logs."},
        {"path_unavailable", "has a record path too long to read safely."}};
    size_t i;
    for (i = 0U; i < sizeof(map) / sizeof(map[0]); i++) if (!strcmp(reason, map[i][0])) return map[i][1];
    return NULL;
}
/* A reason code Hydra does not know keeps its kind's meaning and is quoted
 * verbatim; an unknown kind stays an explicit unknown. */
static const char *kind_text(const struct attention_item *item)
{
    if (!strcmp(item->kind, "approval") || !strcmp(item->kind, "permission")) return "is waiting for your decision";
    if (!strcmp(item->kind, "result")) return "finished and sealed its output";
    if (!strcmp(item->kind, "approval_expired")) return "asked for a decision, but the request expired";
    if (is_failure(item)) return "failed";
    return "could not be classified by Hydra";
}
static void item_why(const struct attention_item *item, char *out, size_t size)
{
    char attempt[48];
    const char *known = reason_text(item->reason);
    attempt_text(item, attempt, sizeof(attempt));
    out[0] = '\0';
    text_append(out, size, "%s%s%s ", present(item->step) ? "Step " : is_failure(item) ? "This run" : "This item",
                present(item->step) ? item->step : "", attempt);
    if (known) text_append(out, size, "%s", known);
    else text_append(out, size, "%s (%s).%s", kind_text(item), item->reason,
        eligible(item) ? "" : " Inspect its records before acting.");
    if (is_failure(item)) text_append(out, size, " Marking it seen keeps it listed until the step succeeds or a later run of this plan succeeds.");
    if (!strcmp(item->freshness, "stale")) text_append(out, size, " The source could not be re-read; this is its last observation.");
}
static const char *seen_status(const struct attention_item *item)
{
    if (item->unseen) return "New: not marked seen yet.";
    if (is_failure(item)) return "Seen, but still needs your decision until the run is resolved.";
    return needs_decision(item) ? "Seen, but still needs your decision." : "Seen at this revision; a new revision shows it again.";
}

/* ---- rendering ---------------------------------------------------------------- */

struct cursor { size_t line, from, to; };
static size_t text_width(const struct app *app)
{ return app->content_width > 2 ? (size_t)app->content_width - 1U : 1U; }
/* Word wrap into the content width; lines outside [from, to) are measured only. */
/* Bytes of text that fit in room: break at a space in the second half of the
 * line when there is one, and never inside a UTF-8 sequence. */
static size_t wrap_take(const char *text, size_t room)
{
    size_t take = strlen(text);
    if (take <= room) return take;
    take = room;
    while (take > room / 2U && text[take] != ' ') take--;
    if (text[take] != ' ') take = room;
    while (take > 1U && ((unsigned char)text[take] & 0xC0U) == 0x80U) take--;
    return take;
}
static void wrap(struct app *app, struct cursor *at, const char *first, const char *next, const char *text, enum tone tone)
{
    size_t width = text_width(app);
    const char *prefix = first;
    do {
        size_t room = width > strlen(prefix) + 8U ? width - strlen(prefix) : 8U, take = wrap_take(text, room);
        if (at->line >= at->from && at->line < at->to) { style(app, tone); linef(app, "%s%.*s", prefix, (int)take, text); style(app, TONE_BASE); }
        at->line++;
        text += take;
        while (*text == ' ') text++;
        prefix = next;
    } while (*text);
}
static void render_item(struct app *app, struct cursor *at, const struct attention_item *item, bool selected)
{
    char title[512], subject[384], why[768];
    enum tone tone = selected ? TONE_SELECTED : item->unseen && eligible(item) ? TONE_STRONG : TONE_BASE;
    item_subject(app, item, subject, sizeof(subject));
    title[0] = '\0';
    text_append(title, sizeof(title), "%s%s%s%s", !strcmp(item->freshness, "stale") ? "(stale) " : "", item_title(item), dot(app), subject);
    if (is_failure(item)) failure_tail(app, item, title, sizeof(title));
    if (item->unseen && eligible(item)) text_append(title, sizeof(title), "  NEW");
    else if (!item->unseen && needs_decision(item)) text_append(title, sizeof(title), "  seen, still needs a decision");
    wrap(app, at, selected ? "> " : "  ", "  ", title, tone);
    item_why(item, why, sizeof(why));
    wrap(app, at, "    ", "    ", why, selected ? TONE_SELECTED : TONE_MUTED);
}
static void render_group(struct app *app, struct cursor *at, const struct native_attention *view, bool selected)
{
    char text[160];
    text[0] = '\0';
    text_append(text, sizeof(text), "[%s] Seen (%u)  %s", view->seen_open ? "-" : "+", view->seen_group_count,
        view->seen_open ? "Enter collapses" : "collapsed; Enter shows them");
    wrap(app, at, selected ? "> " : "  ", "  ", text, selected ? TONE_SELECTED : TONE_MUTED);
}
static void render_row(struct app *app, struct cursor *at, const struct native_attention *view, size_t row, bool selected)
{
    if (row == ATTENTION_GROUP) render_group(app, at, view, selected);
    else render_item(app, at, &view->items[row], selected);
}
static void render_rows(struct app *app, struct native_attention *view)
{
    size_t rows[NATIVE_ATTENTION_ITEMS + 1U], n = build_rows(view, rows), selected = current_row(view, rows, n), i;
    size_t available = app->limit > app->line ? (size_t)(app->limit - app->line) : 1U, start = 0U, end = 0U;
    struct cursor measure = {0U, 0U, 0U}, draw;
    if (!n) { linef(app, "Nothing needs your attention."); return; }
    for (i = 0U; i < n; i++) {
        if (i == selected) start = measure.line;
        render_row(app, &measure, view, rows[i], false);
        if (i == selected) end = measure.line;
    }
    if (view->scroll > start) view->scroll = start;
    if (end > view->scroll + available) view->scroll = end - available;
    draw = (struct cursor){0U, view->scroll, view->scroll + available};
    for (i = 0U; i < n && draw.line < draw.to; i++) render_row(app, &draw, view, rows[i], i == selected);
}
static void snapshot_notes(struct app *app, const struct native_attention *view)
{
    struct cursor at = {0U, 0U, SIZE_MAX};
    if (view->stale) linef(app, "STALE: last good attention snapshot. The latest refresh failed; I tries again.");
    if (view->truncated) wrap(app, &at, "", "", "Only part of the attention list could be read (a size limit was reached); some items may be missing.", TONE_WARNING);
    else if (view->partial) wrap(app, &at, "", "", view->unknown_count ? "Some records could not be read completely; they are listed below as Needs inspection."
        : "Some records could not be read completely; the list may be incomplete.", TONE_WARNING);
    if (view->error[0]) linef(app, "%s", view->error);
    if (view->feedback[0]) wrap(app, &at, "", "", view->feedback, TONE_SUCCESS);
}
static void detail_value(struct app *app, const char *label, const char *value)
{
    struct cursor at = {0U, 0U, SIZE_MAX};
    wrap(app, &at, label, "          ", value, TONE_BASE);
}
static void render_detail(struct app *app, const struct native_attention *view)
{
    const struct attention_item *item = &view->items[view->selected];
    struct cursor at = {0U, 0U, SIZE_MAX};
    char subject[384], text[768];
    item_subject(app, item, subject, sizeof(subject));
    style(app, TONE_MUTED); linef(app, "ATTENTION DETAIL"); style(app, TONE_BASE);
    text[0] = '\0';
    text_append(text, sizeof(text), "%s%s%s", item_title(item), dot(app), subject);
    if (is_failure(item)) failure_tail(app, item, text, sizeof(text));
    wrap(app, &at, "", "", text, TONE_STRONG);
    item_why(item, text, sizeof(text));
    wrap(app, &at, "", "", text, TONE_BASE);
    linef(app, "Status: %s", seen_status(item));
    snapshot_notes(app, view);
    if (is_failure(item))
        wrap(app, &at, "", "", "Next: r reviews the failed requirement and its log. To send the plan back or retry, open its planning "
             "conversation from Work: F requests changes; once the agent revises the plan, V validates it and E runs it.", TONE_BASE);
    linef(app, "r review  s %s  Enter/Esc back", item->unseen ? "mark seen" : "mark new again");
    linef(app, "");
    style(app, TONE_MUTED); linef(app, "Technical identity  %s", item->identity); style(app, TONE_BASE);
    linef(app, "%s  %s  Source: %s  Freshness: %s", item->kind, item->reason, item->source, item->freshness);
    linef(app, "Project: %s  Host: %s", item->project, item->host);
    detail_value(app, "Task: ", item->task); detail_value(app, "Run: ", item->run);
    detail_value(app, "Step: ", item->step); detail_value(app, "Attempt: ", item->attempt);
    linef(app, "Head: %s  Instance: %s", item->head, item->instance);
    linef(app, "Request: %s  Binding: %s", item->request, item->binding);
    linef(app, "Route: %s  navigable=%s", item->route_kind, item->navigable); linef(app, "Revision SHA256: %s", item->revision); linef(app, "Identity SHA256: %s", item->identity);
}
void render_attention(struct app *app)
{
    struct native_attention *view = app->attention;
    const char *sep;
    if (native_review_render(app)) return;
    if (!view) { linef(app, "ATTENTION / loading"); return; }
    if (view->detail && item_selected(view)) { render_detail(app, view); return; }
    sep = dot(app);
    linef(app, "ATTENTION  %u need you%s%u seen%s%u stale%s%u unknown", view->stale ? 0U : view->need_count, sep,
        view->seen_group_count, sep, view->stale_count, sep, view->unknown_count);
    if (view->failure_count) linef(app, "%u failed check%s or step%s need%s a decision", view->failure_count,
        view->failure_count == 1U ? "" : "s", view->failure_count == 1U ? "" : "s", view->failure_count == 1U ? "s" : "");
    if (view->expired_count) linef(app, "%u expired approval request%s", view->expired_count, view->expired_count == 1U ? "" : "s");
    snapshot_notes(app, view);
    render_rows(app, view);
}

/* ---- keys -------------------------------------------------------------------- */

static bool attention_move(struct native_attention *view, char key)
{
    size_t rows[NATIVE_ATTENTION_ITEMS + 1U], n = build_rows(view, rows), at;
    int direction = key == 'j' || key == 'B' || key == ']' ? 1 : key == 'k' || key == 'A' || key == '[' ? -1 : 0;
    if (!direction) return false;
    if (!n) return true;
    at = current_row(view, rows, n);
    if (direction > 0 && at + 1U < n) at++;
    else if (direction < 0 && at) at--;
    select_row(view, rows[at]);
    view->feedback[0] = '\0';
    return true;
}
static bool open_review(struct app *app, char key)
{
    struct native_attention *view = app->attention;
    struct native_review_identity id;
    if (key != 'r' || !item_selected(view)) return false;
    if (view->stale) { copy_text(app->notice, sizeof(app->notice), "Refresh attention before reviewing a stale selection"); return true; }
    review_identity(&view->items[view->selected], &id);
    native_review_open(app, &id);
    return true;
}
/* s toggles the marker for the exact identity and revision; it is recorded
 * through the public command and never changes the item itself. */
static void toggle_seen(const struct app *app, struct native_attention *view)
{
    struct attention_item *item = &view->items[view->selected];
    struct attention_op op = {"", "", !item->unseen, false};
    size_t rows[NATIVE_ATTENTION_ITEMS + 1U], position, n;
    char subject[384], title[512];
    copy_text(op.identity, sizeof(op.identity), item->identity);
    copy_text(op.revision, sizeof(op.revision), item->revision);
    if (item->unseen) remember_revision(view, item->identity, item->revision);
    else forget_identity(view, item->identity);
    item_subject(app, item, subject, sizeof(subject));
    title[0] = '\0'; view->feedback[0] = '\0';
    text_append(title, sizeof(title), "%s%s%s", item_title(item), dot(app), subject);
    text_append(view->feedback, sizeof(view->feedback), op.clear ? "Marked new again: %s" : needs_decision(item) ?
        "Marked seen: %s. It stays listed until someone decides." : "Marked seen: %s. Moved to Seen.", title);
    position = current_row(view, rows, build_rows(view, rows));
    apply_seen(view);
    enqueue(view, &op);
    /* In the list, the next row takes the marked row's place. */
    if (!view->detail && (n = build_rows(view, rows))) select_row(view, rows[position < n ? position : n - 1U]);
    settle_selection(view);
}
static void attention_enter(struct native_attention *view)
{
    if (view->on_group) { view->seen_open = !view->seen_open; view->scroll = 0U; return; }
    if (view->count) view->detail = !view->detail;
}
bool native_attention_key(struct app *app, char key)
{
    struct native_attention *view = app->attention; bool handled = true;
    if (!view) return false;
    if (native_review_key(app, key) || open_review(app, key)) return true;
    if (attention_move(view, key)) { sync_review(app); return true; }
    if (key == '\r' || key == '\n') attention_enter(view);
    else if (key == 's' && item_selected(view)) toggle_seen(app, view);
    else if (key == 27) { if (view->detail) { view->detail = false; settle_selection(view); } else app->view = 0; }
    else if (key == 'I') native_attention_tick(app, true);
    else handled = false;
    return handled;
}
