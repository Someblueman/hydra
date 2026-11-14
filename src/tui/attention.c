#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "attention.h"

#define ATTENTION_LINE 8192U
#define ATTENTION_DUPLICATES 1024U
#define ATTENTION_OPS 32U

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

void attention_review_identity(const struct attention_item *item, struct native_review_identity *id)
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
    copy_text(id->label, sizeof(id->label), item->label);
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

bool attention_is_failure(const struct attention_item *item) { return !strcmp(item->kind, "failure"); }
bool attention_is_approval(const struct attention_item *item)
{ return !strcmp(item->kind, "approval") || !strcmp(item->kind, "permission"); }
bool attention_needs_decision(const struct attention_item *item)
{ return attention_is_approval(item) || attention_is_failure(item); }
bool attention_eligible(const struct attention_item *item)
{ return attention_needs_decision(item) || !strcmp(item->kind, "result"); }
bool attention_in_seen_group(const struct attention_item *item)
{ return !item->unseen && !attention_needs_decision(item); }
#define is_failure attention_is_failure
#define needs_decision attention_needs_decision
#define eligible attention_eligible
#define in_seen_group attention_in_seen_group
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
        /* One listing may wait behind a running one: the running listing may
         * predate another client's mark. */
        size_t i;
        for (i = store->job.pid ? 1U : 0U; i < store->op_count; i++) if (store->ops[i].list) return;
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
        apply_seen(view);
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
/* Let queued markers reach the store before the client exits (bounded); a
 * listing in flight answers nobody now and is stopped. */
static void store_flush(struct app *app)
{
    const struct timespec pause = {0, 10000000L};
    struct attention_store *store = app->attention ? app->attention->store : NULL;
    unsigned waited = 0U;
    while (store && (store->job.pid || store->op_count) && waited++ < 300U) {
        if (store->job.pid && store->running_list && store->op_count) {
            native_capture_destroy(&store->job);
            memmove(store->ops, store->ops + 1U, (store->op_count - 1U) * sizeof(store->ops[0]));
            store->op_count--;
            continue;
        }
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
/* What a new snapshot keeps of the old: the seen markers, their store and
 * the last feedback. Inbox keeps its own selection by identity. */
static void retain_state(struct native_attention *old, struct native_attention *next)
{
    if (!old) return;
    next->seen_count = old->seen_count; memcpy(next->seen, old->seen, sizeof(next->seen));
    next->store = old->store; old->store = NULL;
    next->read_at = old->read_at;
    copy_text(next->feedback, sizeof(next->feedback), old->feedback);
}
bool attention_present(const char *value) { return value[0] && strcmp(value, "-"); }
#define present attention_present
static void attention_error(struct app *app, const char *message)
{
    if (!app->attention) return;
    app->attention->loading = false;
    app->attention->stale = app->attention->have_good;
    copy_text(app->attention->error, sizeof(app->attention->error), message);
}
static void accept_attention(struct app *app, FILE *input)
{
    struct native_attention *next = calloc(1U, sizeof(*next));
    if (!next) { if (input) fclose(input); return; }
    retain_state(app->attention, next);
    if (!input || !parse_stream(input, next)) {
        if (input) fclose(input);
        if (app->attention) { app->attention->store = next->store; }
        free(next);
        attention_error(app, app->attention && app->attention->have_good ? "Attention stream invalid; showing last good snapshot" : "Attention unavailable; no valid snapshot yet");
        return;
    }
    fclose(input);
    next->have_good = true;
    next->good_at = time(NULL);
    apply_seen(next);
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
    job = &app->observations->jobs[OBSERVE_ATTENTION];
    if (job->pid && native_capture_step(job)) {
        FILE *input = native_capture_take(job);
        if (input) accept_attention(app, input); else capture_failure(app);
    }
    store_tick(app);
    if (!request || !native_observations_attention_due(app, app->attention->read_at)) return;
    /* A refresh always rereads the seen store, even while the previous
     * attention snapshot is still loading. */
    enqueue(app->attention, &list);
    if (job->pid) return;
    /* One stream for local and remote items; the compatibility entry keeps
     * the fleet helper's own stream. */
    argv[0] = (char *)app->hydra; argv[1] = (char *)(app->fleet ? "fleet" : "tui");
    argv[2] = (char *)(app->fleet ? "attention-data" : "--attention-data"); argv[3] = NULL;
    app->attention->loading = true;
    app->attention->read_at = time(NULL);
    if (!native_capture_start(job, argv, app->fleet ? 13000L : 25000L)) capture_failure(app);
}

/* Starts an attention read wherever the user is (the run monitor needs the
 * requests waiting on its runs); the result arrives with the next tick. */
void native_attention_read(struct app *app)
{
    struct native_capture *job;
    char *argv[4];
    if (!app->attention) app->attention = calloc(1U, sizeof(*app->attention));
    if (!app->attention || !app->observations) return;
    job = &app->observations->jobs[OBSERVE_ATTENTION];
    if (job->pid) return;
    /* One stream for local and remote items; the compatibility entry keeps
     * the fleet helper's own stream. */
    argv[0] = (char *)app->hydra; argv[1] = (char *)(app->fleet ? "fleet" : "tui");
    argv[2] = (char *)(app->fleet ? "attention-data" : "--attention-data"); argv[3] = NULL;
    app->attention->loading = true;
    app->attention->read_at = time(NULL);
    if (!native_capture_start(job, argv, app->fleet ? 13000L : 25000L)) capture_failure(app);
}

/* The approval request waiting on a run in the last good attention
 * snapshot, and its step; false when none is known (or the list is stale). */
bool native_attention_run_request(const struct app *app, const char *run, char *request, size_t size,
                                  char *step, size_t step_size)
{
    const struct native_attention *view = app->attention;
    size_t i;
    if (!view || !view->have_good || view->stale) return false;
    for (i = 0U; i < view->count; i++) {
        const struct attention_item *item = &view->items[i];
        if (strcmp(item->run, run) || !present(item->request) || strcmp(item->kind, "approval")) continue;
        copy_text(request, size, item->request);
        copy_text(step, step_size, item->step);
        return true;
    }
    return false;
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
const char *attention_item_title(const struct attention_item *item)
{
    static const char *const map[][2] = {{"approval", "Approval needed"}, {"approval_expired", "Approval expired"},
        {"result", "Result ready for review"}, {"permission", "Permission requested"}};
    size_t i;
    if (is_failure(item)) return failure_title(item);
    for (i = 0U; i < sizeof(map) / sizeof(map[0]); i++) if (!strcmp(item->kind, map[i][0])) return map[i][1];
    return "Needs inspection";
}
/* "kill-dry-run · repo-check · on builder": the task, its step and, away
 * from this machine, its host. */
void attention_item_subject(const struct app *app, const struct attention_item *item, char *out, size_t size)
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
/* What happened and what the review will show. Keys are not repeated here:
 * the detail's Next paragraph and its buttons carry them. */
static const char *reason_text(const char *reason)
{
    static const char *const map[][2] = {
        {"result_ready", "finished and sealed its output; its result, checks and changes are ready to review."},
        {"approval_pending", "is waiting for your decision; marking it seen does not approve it."},
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
        {"check_failed", "failed its check; review the log, then send the plan back or retry."},
        {"step_failed", "failed; review its log, then send the plan back or retry."},
        {"step_recovery_required", "stopped in a state Hydra cannot finish on its own; the review shows its records."},
        {"run_failed", "failed after its steps finished, so its result was not delivered; the review shows what was checked."},
        {"run_recovery_required", "stopped in a state Hydra cannot finish on its own; the review shows its records."},
        {"task_failed", "failed on its host; the review shows its retained result and logs."},
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
void attention_item_why(const struct attention_item *item, char *out, size_t size)
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
    if (is_failure(item) && present(item->detail)) text_append(out, size, " It decides %s.", item->detail);
    if (!strcmp(item->freshness, "stale")) text_append(out, size, " The source could not be re-read; this is its last observation.");
}
const char *attention_seen_status(const struct attention_item *item)
{
    if (item->unseen) return is_failure(item) ? "New: not marked seen yet; listed until the run is resolved." : "New: not marked seen yet.";
    if (is_failure(item)) return "Seen, but still needs your decision until the run is resolved.";
    return needs_decision(item) ? "Seen, but still needs your decision." : "Seen at this revision; a new revision shows it again.";
}

/* What the user can do about the item, by the keys that act on it here. */
const char *attention_next_text(const struct app *app, const struct attention_item *item)
{
    bool remote = present(item->host) && strcmp(item->host, "local");
    if (is_failure(item)) return app->fleet || remote ? "r reviews the failed requirement and its log, as its host retained them."
        : "r reviews the failed requirement and its log. F sends it back to the agent that planned the run, with your note; R resumes the run once it can continue.";
    if (!strcmp(item->kind, "approval") || !strcmp(item->kind, "permission")) return remote
        ? "r reviews the request and its evidence. Y approves and N rejects it on its host, after a confirmation, while the host's observation is fresh."
        : "r reviews the request and its evidence. Y approves and N rejects it, after a confirmation; a decision alone never resumes a run.";
    if (!strcmp(item->kind, "result")) return "r reviews what was produced, how it was checked and the changes on the worker branch; s marks the item seen once you have.";
    if (!strcmp(item->kind, "approval_expired")) return "r reviews the expired request; a new request appears here when the step asks again.";
    return "r shows the records Hydra could read; inspect them before acting.";
}

/* ---- what Inbox asks of the snapshot -------------------------------------- */

size_t attention_find(const struct app *app, const char *identity, const char *revision)
{
    const struct native_attention *view = app->attention;
    size_t i;
    if (!view || !view->have_good || view->stale) return SIZE_MAX;
    for (i = 0U; i < view->count; i++)
        if (!strcmp(view->items[i].identity, identity) && !strcmp(view->items[i].revision, revision)) return i;
    return SIZE_MAX;
}

/* s toggles the marker for the exact identity and revision; it is recorded
 * through the public command and never changes the item itself. */
void attention_toggle_seen(struct app *app, size_t index)
{
    struct native_attention *view = app->attention;
    struct attention_item *item;
    struct attention_op op = {"", "", false, false};
    char subject[384], title[512];
    if (!view || index >= view->count) return;
    item = &view->items[index];
    op.clear = !item->unseen;
    copy_text(op.identity, sizeof(op.identity), item->identity);
    copy_text(op.revision, sizeof(op.revision), item->revision);
    if (item->unseen) remember_revision(view, item->identity, item->revision);
    else forget_identity(view, item->identity);
    attention_item_subject(app, item, subject, sizeof(subject));
    title[0] = '\0'; view->feedback[0] = '\0';
    text_append(title, sizeof(title), "%s%s%s", attention_item_title(item), dot(app), subject);
    text_append(view->feedback, sizeof(view->feedback), op.clear ? "Marked new again: %s" : needs_decision(item) ?
        "Marked seen: %s. It stays listed until someone decides." : "Marked seen: %s. Moved to Seen.", title);
    apply_seen(view);
    enqueue(view, &op);
}

/* Headless fixtures: one attention stream read from a file, with no refresh
 * and no seen store; a second unreadable file leaves the first as the stale
 * last good snapshot, as a failed refresh does. */
bool native_attention_load(struct app *app, const char *path)
{
    FILE *input = fopen(path, "r");
    if (!app->attention) app->attention = calloc(1U, sizeof(*app->attention));
    if (!app->attention) { if (input) fclose(input); return false; }
    accept_attention(app, input);
    return app->attention->have_good;
}

/* A project whose workflow read succeeded with no runs has nothing for the
 * attention read to find (that read then fails for want of run storage):
 * the list is empty for a known reason, not unavailable. Registered hosts
 * may still have remote items, so they are read as usual. */
bool attention_no_runs(const struct app *app)
{
    if (app->fleet || app->model.host_count || !app->workflows || app->workflow_stale || app->workflows->run_count) return false;
    return !app->attention || !app->attention->have_good;
}
