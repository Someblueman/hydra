#define _POSIX_C_SOURCE 200809L
#include "internal.h"

/* Attention is deliberately a display cache.  It owns no workflow state and
 * never issues an acknowledgement or action.  Producer strings are bounded
 * here so a hostile stream cannot grow either the item cache or a line. */
#define ATTENTION_TEXT 128
#define ATTENTION_REVISION 256

struct attention_item {
    char source[ATTENTION_TEXT], kind[ATTENTION_TEXT], reason[ATTENTION_TEXT];
    char project[ATTENTION_TEXT], host[ATTENTION_TEXT], task[ATTENTION_TEXT];
    char run[ATTENTION_TEXT], step[ATTENTION_TEXT], attempt[ATTENTION_TEXT];
    char head[ATTENTION_TEXT], instance[ATTENTION_TEXT], request[ATTENTION_TEXT];
    char spec[ATTENTION_TEXT], revision[ATTENTION_TEXT], freshness[ATTENTION_TEXT];
    char route_kind[ATTENTION_TEXT], route_id[ATTENTION_TEXT], route_arg[ATTENTION_TEXT];
    char identity[ATTENTION_REVISION], semantic_revision[ATTENTION_REVISION];
    bool unseen;
};
struct attention_seen {
    char identity[ATTENTION_REVISION], semantic_revision[ATTENTION_REVISION];
};
struct native_attention {
    struct attention_item items[NATIVE_ATTENTION_ITEMS];
    struct attention_seen seen[NATIVE_ATTENTION_SEEN];
    size_t count, seen_count, selected;
    bool detail, stale, have_good, loading;
    unsigned current_count, stale_count, unknown_count;
    char error[TEXT];
};

static bool field(char *dst, size_t cap, const char *value) {
    size_t length = strlen(value);
    if (length == 0U || length >= cap) return false;
    memcpy(dst, value, length + 1U);
    return true;
}
static bool seen_revision(const struct native_attention *view, const char *id, const char *rev) {
    size_t i;
    for (i = 0U; i < view->seen_count; i++)
        if (!strcmp(view->seen[i].identity, id) && !strcmp(view->seen[i].semantic_revision, rev)) return true;
    return false;
}
static void remember_revision(struct native_attention *view, const char *id, const char *rev) {
    size_t slot;
    if (seen_revision(view, id, rev)) return;
    if (view->seen_count == NATIVE_ATTENTION_SEEN) {
        memmove(view->seen, view->seen + 1U, (NATIVE_ATTENTION_SEEN - 1U) * sizeof(view->seen[0]));
        view->seen_count--;
    }
    slot = view->seen_count++;
    (void)field(view->seen[slot].identity, sizeof(view->seen[slot].identity), id);
    (void)field(view->seen[slot].semantic_revision, sizeof(view->seen[slot].semantic_revision), rev);
}
static bool parse_item(char *line, struct attention_item *item) {
    char *fields[20];
    size_t n = split_fields(line, fields, 21U), i;
    char *dest[] = {item->source,item->kind,item->reason,item->project,item->host,item->task,
        item->run,item->step,item->attempt,item->head,item->instance,item->request,item->spec,
        item->revision,item->freshness,item->route_kind,item->route_id,item->route_arg,
        item->identity,item->semantic_revision};
    size_t caps[] = {sizeof(item->source),sizeof(item->kind),sizeof(item->reason),sizeof(item->project),sizeof(item->host),sizeof(item->task),
        sizeof(item->run),sizeof(item->step),sizeof(item->attempt),sizeof(item->head),sizeof(item->instance),sizeof(item->request),sizeof(item->spec),
        sizeof(item->revision),sizeof(item->freshness),sizeof(item->route_kind),sizeof(item->route_id),sizeof(item->route_arg),
        sizeof(item->identity),sizeof(item->semantic_revision)};
    if (n != 20U || strcmp(fields[0], "ITEM") != 0) return false;
    for (i = 0U; i < 19U; i++) if (!field(dest[i], caps[i], fields[i + 1U])) return false;
    return true;
}
static bool parse_stream(FILE *input, struct native_attention *view) {
    char line[4096], *fields[4];
    size_t count = 0U;
    bool header = false, ended = false;
    while (fgets(line, sizeof(line), input) != NULL) {
        size_t length = strlen(line);
        if (length == sizeof(line) - 1U && line[length - 1U] != '\n') return false;
        if (length && line[length - 1U] == '\n') line[length - 1U] = '\0';
        if (!header) {
            char *h[3];
            if (split_fields(line, h, 3U) != 2U || strcmp(h[0], "HYDRA_ATTENTION") || strcmp(h[1], "1")) return false;
            header = true; continue;
        }
        if (!strcmp(line, "END\t0")) { ended = true; break; }
        if (!strncmp(line, "END\t", 4U)) {
            unsigned expected;
            if (split_fields(line, fields, 3U) != 2U || !parse_unsigned(fields[1], &expected) || expected != count) return false;
            ended = true; break;
        }
        if (count >= NATIVE_ATTENTION_ITEMS) return false;
        if (!parse_item(line, &view->items[count])) return false;
        if (strcmp(view->items[count].freshness, "current") &&
            strcmp(view->items[count].freshness, "stale") &&
            strcmp(view->items[count].freshness, "unknown")) return false;
        for (size_t prior = 0U; prior < count; prior++) {
            if (!strcmp(view->items[prior].identity, view->items[count].identity)) {
                if (!strcmp(view->items[prior].semantic_revision, view->items[count].semantic_revision)) break;
                view->items[prior] = view->items[count];
                goto next_line;
            }
        }
        for (size_t prior = 0U; prior < count; prior++)
            if (!strcmp(view->items[prior].identity, view->items[count].identity) &&
                !strcmp(view->items[prior].semantic_revision, view->items[count].semantic_revision)) goto next_line;
        count++;
next_line:;
    }
    if (!header || !ended || ferror(input)) return false;
    view->count = count;
    return true;
}
static void classify(struct native_attention *view) {
    size_t i;
    view->current_count = view->stale_count = view->unknown_count = 0U;
    for (i = 0U; i < view->count; i++) {
        const char *freshness = view->items[i].freshness;
        if (!strcmp(freshness, "current")) view->current_count++;
        else if (!strcmp(freshness, "stale")) view->stale_count++;
        else view->unknown_count++;
    }
}
void native_attention_destroy(struct app *app) { free(app->attention); app->attention = NULL; }

static void accept_attention(struct app *app, FILE *input) {
    struct native_attention *next;
    size_t i, old_selected = 0U;
    char selected_id[ATTENTION_REVISION] = "";
    if (!input) return;
    next = calloc(1U, sizeof(*next));
    if (next == NULL) { fclose(input); return; }
    if (app->attention && app->attention->selected < app->attention->count)
        copy_text(selected_id, sizeof(selected_id), app->attention->items[app->attention->selected].identity);
    if (!parse_stream(input, next)) {
        fclose(input); free(next);
        if (app->attention) { app->attention->stale = true; copy_text(app->attention->error, sizeof(app->attention->error), "Attention stream invalid; showing last good"); }
        return;
    }
    fclose(input);
    if (app->attention) {
        next->seen_count = app->attention->seen_count;
        memcpy(next->seen, app->attention->seen, sizeof(next->seen));
    }
    for (i = 0U; i < next->count; i++) {
        bool known = seen_revision(next, next->items[i].identity, next->items[i].semantic_revision);
        next->items[i].unseen = !known;
        remember_revision(next, next->items[i].identity, next->items[i].semantic_revision);
        if (selected_id[0] && !strcmp(selected_id, next->items[i].identity)) old_selected = i;
    }
    next->selected = selected_id[0] ? old_selected : 0U;
    next->have_good = true; next->stale = false; next->error[0] = '\0'; classify(next);
    free(app->attention); app->attention = next;
}

void native_attention_tick(struct app *app, bool request) {
    struct native_capture *job;
    char *argv[4];
    if (!app->attention) { app->attention = calloc(1U, sizeof(*app->attention)); if (!app->attention) return; }
    if (!app->observations) return;
    job = &app->observations->jobs[4];
    if (job->pid && native_capture_step(job)) {
        FILE *input = native_capture_take(job);
        if (input) accept_attention(app, input);
        else if (app->attention->have_good) app->attention->stale = true;
    }
    if (!request || app->view != 9 || job->pid) return;
    argv[0] = (char *)app->hydra; argv[1] = (char *)(app->fleet ? "fleet" : "workflow");
    argv[2] = (char *)"attention-data"; argv[3] = NULL;
    if (!native_capture_start(job, argv, app->fleet ? 13000L : 10000L)) app->attention->stale = true;
}

void render_attention(struct app *app) {
    struct native_attention *view = app->attention;
    size_t i;
    if (!view) { linef(app, "Loading attention..."); return; }
    if (view->detail && view->selected < view->count) {
        const struct attention_item *item = &view->items[view->selected];
        linef(app, "ATTENTION DETAIL");
        linef(app, "%s  %s", item->kind, item->reason);
        linef(app, "Source: %s  Freshness: %s", item->source, item->freshness);
        linef(app, "Project: %s  Host: %s", item->project, item->host);
        linef(app, "Task: %s  Run: %s  Step: %s  Attempt: %s", item->task, item->run, item->step, item->attempt);
        linef(app, "Head: %s  Instance: %s", item->head, item->instance);
        linef(app, "Request: %s  Spec: %s", item->request, item->spec);
        linef(app, "Route: %s  %s  %s", item->route_kind, item->route_id, item->route_arg);
        linef(app, "Revision: %s  Semantic: %s", item->revision, item->semantic_revision);
        linef(app, "Enter/Esc back   (read-only; no acknowledgement sent)");
        return;
    }
    linef(app, "ATTENTION  %u current  %u stale  %u unknown", view->current_count, view->stale_count, view->unknown_count);
    if (view->stale) linef(app, "STALE: last good attention snapshot");
    if (!view->count) { linef(app, view->error[0] ? view->error : "No attention items."); return; }
    for (i = 0U; i < view->count && app->line < app->limit - 1; i++) {
        const struct attention_item *item = &view->items[i];
        if (i == view->selected) style(app, TONE_SELECTED);
        linef(app, "%c %-10.10s %-12.12s %-12.12s %-16.16s %s%s", i == view->selected ? '>' : ' ',
              item->freshness, item->kind, item->source, item->reason, item->unseen ? " NEW" : "", item->identity);
        style(app, TONE_BASE);
    }
    linef(app, "j/k select  Enter details  I refresh  Esc heads");
}

bool native_attention_key(struct app *app, char key) {
    struct native_attention *view = app->attention;
    if (!view) return false;
    if (key == 'j' && view->selected + 1U < view->count) view->selected++;
    else if (key == 'k' && view->selected) view->selected--;
    else if (key == '\r' || key == '\n') view->detail = !view->detail;
    else if (key == 27) { view->detail = false; app->view = 0; }
    else if (key == 'I') { native_attention_tick(app, true); }
    else return false;
    return true;
}
