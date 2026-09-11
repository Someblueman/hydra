#define _POSIX_C_SOURCE 200809L
#include "internal.h"

#define ATTENTION_TEXT 128U
#define ATTENTION_REASON 256U
#define ATTENTION_DIGEST 65U
#define ATTENTION_LINE 8192U
#define ATTENTION_DUPLICATES 1024U

struct attention_item {
    char source[ATTENTION_TEXT]; char kind[ATTENTION_TEXT]; char reason[ATTENTION_REASON];
    char project[ATTENTION_TEXT]; char host[ATTENTION_TEXT]; char task[ATTENTION_TEXT];
    char run[ATTENTION_TEXT]; char step[ATTENTION_TEXT]; char attempt[ATTENTION_TEXT];
    char head[ATTENTION_TEXT]; char instance[ATTENTION_TEXT]; char request[ATTENTION_TEXT];
    char binding[ATTENTION_TEXT]; char revision[ATTENTION_DIGEST]; char identity[ATTENTION_DIGEST];
    char freshness[ATTENTION_TEXT]; char route_kind[ATTENTION_TEXT]; char navigable[ATTENTION_TEXT];
    bool unseen;
};
struct attention_seen { char identity[ATTENTION_DIGEST]; char revision[ATTENTION_DIGEST]; };
struct native_attention {
    struct attention_item items[NATIVE_ATTENTION_ITEMS];
    struct attention_seen seen[NATIVE_ATTENTION_SEEN];
    size_t count, seen_count, selected, scroll;
    bool detail, stale, have_good, loading, partial, truncated;
    unsigned current_count, stale_count, unknown_count;
    char error[TEXT];
};

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
static void remember_revision(struct native_attention *view, const char *identity, const char *revision)
{
    size_t slot;
    if (seen_revision(view, identity, revision)) return;
    if (view->seen_count == NATIVE_ATTENTION_SEEN) {
        memmove(view->seen, view->seen + 1U, (NATIVE_ATTENTION_SEEN - 1U) * sizeof(view->seen[0]));
        view->seen_count--;
    }
    slot = view->seen_count++;
    (void)snprintf(view->seen[slot].identity, sizeof(view->seen[slot].identity), "%s", identity);
    (void)snprintf(view->seen[slot].revision, sizeof(view->seen[slot].revision), "%s", revision);
}

static bool copy_item_fields(char *fields[], struct attention_item *item)
{
    char *destinations[] = {item->source, item->kind, item->reason, item->project, item->host, item->task, item->run, item->step, item->attempt, item->head, item->instance, item->request, item->binding, item->revision, item->identity, item->freshness, item->route_kind, item->navigable};
    const size_t capacities[] = {sizeof(item->source), sizeof(item->kind), sizeof(item->reason), sizeof(item->project), sizeof(item->host), sizeof(item->task), sizeof(item->run), sizeof(item->step), sizeof(item->attempt), sizeof(item->head), sizeof(item->instance), sizeof(item->request), sizeof(item->binding), sizeof(item->revision), sizeof(item->identity), sizeof(item->freshness), sizeof(item->route_kind), sizeof(item->navigable)};
    size_t i;
    for (i = 0U; i < 18U; i++) {
        size_t maximum = i == 2U ? 255U : i == 12U ? 64U : 127U;
        if ((i == 13U || i == 14U) && !valid_digest(fields[i + 1U])) return false;
        if (!put_field(destinations[i], capacities[i], fields[i + 1U], maximum)) return false;
    }
    return true;
}
static bool parse_item(char *line, struct attention_item *item)
{
    char *fields[20];
    size_t i;
    if (split_fields(line, fields, 20U) != 19U || strcmp(fields[0], "ITEM")) return false;
    if (!copy_item_fields(fields, item)) return false;
    i = 0U;
    while (i < 4U && strcmp(item->freshness, (const char *[]) {"fresh", "stale", "unknown", "expired"}[i])) i++;
    if (i == 4U) return false;
    i = 0U;
    while (i < 6U && strcmp(item->route_kind, (const char *[]) {"workflow-request", "workflow-evidence", "task-observe", "task-result", "agent-record", "unavailable"}[i])) i++;
    if (i == 6U) return false;
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
static bool accept_item(struct native_attention *view, char *line, size_t *unique, size_t *duplicates)
{
    struct attention_item item = {0}; size_t prior;
    if (!parse_item(line, &item)) return false;
    for (prior = 0U; prior < *unique; prior++) {
        if (strcmp(view->items[prior].identity, item.identity)) continue;
        if (strcmp(view->items[prior].revision, item.revision) || !item_equal(&view->items[prior], &item)) return false;
        (*duplicates)++;
        return *duplicates <= ATTENTION_DUPLICATES;
    }
    if (*unique == NATIVE_ATTENTION_ITEMS) return false;
    view->items[*unique] = item; (*unique)++;
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
static bool parse_stream_line(char *line, struct native_attention *view, size_t *wire,
                              size_t *unique, size_t *duplicates, bool *header, bool *ended)
{
    if (!*header) {
        char *fields[3];
        if (split_fields(line, fields, 3U) != 2U || strcmp(fields[0], "HYDRA_ATTENTION") || strcmp(fields[1], "1")) return false;
        *header = true;
        return true;
    }
    if (!strncmp(line, "END\t", 4U)) {
        if (!parse_end(line, *wire, view)) return false;
        *ended = true;
        return true;
    }
    if (strncmp(line, "ITEM\t", 5U) || *wire == SIZE_MAX) return false;
    (*wire)++;
    return *wire <= ATTENTION_DUPLICATES && accept_item(view, line, unique, duplicates);
}
static bool parse_stream(FILE *input, struct native_attention *view)
{
    char line[ATTENTION_LINE]; size_t wire_count = 0U, unique = 0U, duplicates = 0U, length; bool header = false, ended = false;
    while (fgets(line, sizeof(line), input) != NULL) {
        if (ended || !valid_line(line, &length)) return false;
        if (!parse_stream_line(line, view, &wire_count, &unique, &duplicates, &header, &ended)) return false;
    }
    if (!header || !ended || ferror(input)) return false;
    view->count = unique;
    return true;
}
static bool eligible(const struct attention_item *item)
{ return !strcmp(item->kind, "approval") || !strcmp(item->kind, "result") || !strcmp(item->kind, "permission"); }
static void classify(struct native_attention *view)
{
    size_t i;
    view->current_count = 0U; view->stale_count = 0U; view->unknown_count = 0U;
    for (i = 0U; i < view->count; i++) {
        const char *freshness = view->items[i].freshness;
        if (!strcmp(freshness, "fresh") && eligible(&view->items[i]) && view->items[i].unseen) view->current_count++;
        else if (!strcmp(freshness, "stale")) view->stale_count++;
        else if (strcmp(freshness, "fresh") || !eligible(&view->items[i])) view->unknown_count++;
    }
}
void native_attention_destroy(struct app *app) { free(app->attention); app->attention = NULL; }
static void retain_state(const struct native_attention *old, struct native_attention *next, char *selected_id, bool *detail)
{
    if (!old) return;
    if (old->selected < old->count) copy_text(selected_id, ATTENTION_DIGEST, old->items[old->selected].identity);
    next->scroll = old->scroll; next->seen_count = old->seen_count; memcpy(next->seen, old->seen, sizeof(next->seen)); *detail = old->detail;
}
static void attention_error(struct app *app, const char *message)
{
    if (!app->attention) return;
    app->attention->loading = false;
    app->attention->stale = app->attention->have_good;
    copy_text(app->attention->error, sizeof(app->attention->error), message);
}
static void finalize_attention(struct native_attention *next, const char *selected_id,
                               bool detail)
{
    size_t i, selected = 0U;
    bool found = false;
    for (i = 0U; i < next->count; i++) {
        next->items[i].unseen = !seen_revision(next, next->items[i].identity, next->items[i].revision);
        if (selected_id[0] && !strcmp(selected_id, next->items[i].identity)) { selected = i; found = true; }
    }
    next->selected = found ? selected : 0U;
    if (next->scroll > next->selected) next->scroll = next->selected;
    next->detail = found && detail;
    next->have_good = true;
    next->stale = false;
    classify(next);
}
static void keep_visible(struct app *app);
static void accept_attention(struct app *app, FILE *input)
{
    struct native_attention *next = calloc(1U, sizeof(*next)); char selected_id[ATTENTION_DIGEST] = ""; bool detail = false;
    if (!next) { if (input) fclose(input); return; }
    retain_state(app->attention, next, selected_id, &detail);
    if (!input || !parse_stream(input, next)) {
        if (input) fclose(input); free(next);
        attention_error(app, app->attention && app->attention->have_good ? "Attention stream invalid; showing last good snapshot" : "Attention unavailable; no valid snapshot yet");
        return;
    }
    fclose(input);
    finalize_attention(next, selected_id, detail);
    next->loading = false;
    free(app->attention); app->attention = next;
    keep_visible(app);
}
static void capture_failure(struct app *app)
{ attention_error(app, app->attention && app->attention->have_good ? "Attention unavailable; showing last good snapshot" : "Attention unavailable; no valid snapshot yet"); }
void native_attention_tick(struct app *app, bool request)
{
    struct native_capture *job; char *argv[4];
    if (!app->attention) app->attention = calloc(1U, sizeof(*app->attention));
    if (!app->attention || !app->observations) return;
    job = &app->observations->jobs[4];
    if (job->pid && native_capture_step(job)) {
        FILE *input = native_capture_take(job);
        if (input) accept_attention(app, input); else capture_failure(app);
    }
    if (!request || app->view != 9 || job->pid) return;
    argv[0] = (char *)app->hydra; argv[1] = (char *)(app->fleet ? "fleet" : "workflow"); argv[2] = (char *)"attention-data"; argv[3] = NULL;
    app->attention->loading = true;
    if (!native_capture_start(job, argv, app->fleet ? 13000L : 10000L)) capture_failure(app);
}
static size_t visible_rows(const struct app *app) { return app->rows > 8 ? (size_t)(app->rows - 8) : 1U; }
static void keep_visible(struct app *app)
{
    struct native_attention *view = app->attention; size_t rows = visible_rows(app);
    if (view->selected < view->scroll) view->scroll = view->selected;
    if (view->selected >= view->scroll + rows) view->scroll = view->selected - rows + 1U;
}
static void detail_value(struct app *app, const char *label, const char *value)
{
    size_t offset = 0U, width = app->boxed && app->cols > 5 ? (size_t)app->cols - 5U : app->cols > 1 ? (size_t)app->cols - 1U : 1U;
    size_t length = strlen(value);
    if (length == 0U) { linef(app, "%s", label); return; }
    while (offset < length || offset == 0U) {
        const char *prefix = offset == 0U ? label : "          ";
        size_t prefix_length = strlen(prefix);
        size_t chunk = length - offset;
        if (prefix_length >= width) prefix_length = width - 1U;
        if (chunk > width - prefix_length) chunk = width - prefix_length;
        linef(app, "%s%.*s", prefix, (int)chunk, value + offset);
        offset += chunk;
    }
}
static void render_detail(struct app *app, const struct native_attention *view)
{
    const struct attention_item *item = &view->items[view->selected];
    linef(app, "ATTENTION DETAIL  %s", item->identity);
    if (view->stale) linef(app, "STALE: last good attention snapshot (current count unavailable)");
    if (view->partial || view->truncated) linef(app, "PARTIAL SNAPSHOT: partial=%d truncated=%d", view->partial, view->truncated);
    linef(app, "%s  %s", item->kind, item->reason); linef(app, "Source: %s  Freshness: %s", item->source, item->freshness);
    linef(app, "Project: %s  Host: %s", item->project, item->host);
    detail_value(app, "Task: ", item->task); detail_value(app, "Run: ", item->run);
    detail_value(app, "Step: ", item->step); detail_value(app, "Attempt: ", item->attempt);
    linef(app, "Head: %s  Instance: %s", item->head, item->instance);
    linef(app, "Request: %s  Binding: %s", item->request, item->binding);
    linef(app, "Route: %s  navigable=%s", item->route_kind, item->navigable); linef(app, "Revision SHA256: %s", item->revision); linef(app, "Identity SHA256: %s", item->identity);
    linef(app, "Enter/Esc back   s acknowledge client-only");
}
static void render_rows(struct app *app, const struct native_attention *view)
{
    size_t i, rows = visible_rows(app);
    for (i = view->scroll; i < view->count && i < view->scroll + rows && app->line < app->limit - 1; i++) {
        const struct attention_item *item = &view->items[i];
        if (i == view->selected) style(app, TONE_SELECTED);
        linef(app, "%c %-8.8s %-10.10s %-14.14s %-20.20s%s", i == view->selected ? '>' : ' ', item->freshness, item->kind, item->source, item->reason, item->unseen ? " NEW" : "");
        style(app, TONE_BASE);
    }
}
void render_attention(struct app *app)
{
    struct native_attention *view = app->attention;
    if (!view) { linef(app, "ATTENTION / loading"); return; }
    if (view->detail && view->selected < view->count) { render_detail(app, view); return; }
    linef(app, "ATTENTION  %u current  %u stale  %u unknown", view->stale ? 0U : view->current_count, view->stale_count, view->unknown_count);
    if (view->stale) linef(app, "STALE: last good attention snapshot (current count unavailable)");
    if (view->partial || view->truncated) linef(app, "PARTIAL SNAPSHOT: partial=%d truncated=%d", view->partial, view->truncated);
    if (view->error[0]) linef(app, "%s", view->error);
    render_rows(app, view); linef(app, "j/k or arrows select  Enter details  s acknowledge client-only  I refresh  Esc heads");
}
static bool attention_move(struct native_attention *view, char key)
{
    if ((key == 'j' || key == 'B') && view->selected + 1U < view->count) { view->selected++; return true; }
    if ((key == 'k' || key == 'A') && view->selected) { view->selected--; return true; }
    return false;
}
bool native_attention_key(struct app *app, char key)
{
    struct native_attention *view = app->attention; bool handled = true;
    if (!view) return false;
    if (attention_move(view, key)) { keep_visible(app); return true; }
    if (key == '\r' || key == '\n') { if (view->count) view->detail = !view->detail; }
    else if (key == 's' && view->selected < view->count && view->items[view->selected].unseen) {
        view->items[view->selected].unseen = false; remember_revision(view, view->items[view->selected].identity, view->items[view->selected].revision); classify(view);
    } else if (key == 27) { view->detail = false; app->view = 0; }
    else if (key == 'I') native_attention_tick(app, true);
    else handled = false;
    if (handled) keep_visible(app);
    return handled;
}
