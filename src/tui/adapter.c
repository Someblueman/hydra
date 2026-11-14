#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
#include "attention.h"
#include "inbox.h"
void record_snapshot(struct app *app, bool valid) {
    size_t i;
    double queue = 0;
    bool known = app->model.head_count == 0;
    if (app->history_count == 120) {
        memmove(app->queue_history, app->queue_history + 1, 119 * sizeof(double));
        memmove(app->history_valid, app->history_valid + 1, 119 * sizeof(bool));
        app->history_count--;
    }
    for (i = 0; i < app->model.head_count; i++) if (app->model.heads[i].head_id[0]) {
        queue += app->model.heads[i].queue; known = true;
    }
    app->queue_history[app->history_count] = queue;
    app->history_valid[app->history_count++] = valid && known;
    app->snapshot_stale = !valid;
    if (valid) app->snapshot_at = time(NULL);
    /* Inbox's gates and published plans belong to these heads. */
    if (valid) inbox_refresh(app);
}
int accept_model_data(struct app *app, FILE *input) {
    struct model *next;
    char error[TEXT] = "";
    if (!input) return -1;
    next = malloc(sizeof(*next));
    if (next == NULL) {
        fclose(input);
        copy_text(app->snapshot_error, sizeof(app->snapshot_error), "native model allocation failed");
        return -1;
    }
    if (load_model_stream(input, next, error, sizeof(error)) != 0) {
        fclose(input);
        free(next);
        copy_text(app->snapshot_error, sizeof(app->snapshot_error), error);
        return -1;
    }
    fclose(input);
    model_accept(app, next);
    app->snapshot_error[0] = '\0';
    return 0;
}
/* Whether the observation a head comes from is stale. */
bool head_stale(const struct app *app, const struct head *h) {
    return head_remote(h) ? app->remote_stale : app->snapshot_stale;
}
/* The tmux session this control centre runs in, so a removal can leave the
 * terminal in use alone. The loop keeps running while tmux answers. */
void refresh_current_session(struct app *app) {
    char output[TEXT] = "";
    char *argv[] = {(char *)"tmux", (char *)"display-message", (char *)"-p",
                    (char *)"#{session_name}", NULL};
    app->current_session[0] = '\0';
    if (getenv("TMUX") == NULL || run_captured(app, argv, output, sizeof(output), 1000L) != 0) return;
    output[strcspn(output, "\r\n")] = '\0';
    copy_text(app->current_session, sizeof(app->current_session), output);
}

/* The preview is read in the background: a pane's recent output from tmux,
 * or, for a headless head, the bounded view of the step that runs (or last
 * ran) on it. One read is in flight at a time; a request made meanwhile
 * starts when it ends. */
static bool preview_start(struct app *app, const struct head *h) {
    struct native_observations *o = app->observations;
    char target[TEXT + 8U];
    /* Joined lines (-J) with their colors (-e): Hydra wraps them to the view
     * and renders SGR itself; the pane's history before the screen gives context. */
    char *pane[] = {(char *)"tmux", (char *)"capture-pane", (char *)"-p", (char *)"-e", (char *)"-J",
                    (char *)"-S", (char *)"-40", (char *)"-t", target, NULL};
    char *step[] = {(char *)app->hydra, (char *)"tui", (char *)"--head-output", (char *)h->branch, NULL};
    snprintf(target, sizeof(target), "%s:0.0", h->session);
    o->preview_again = false;
    o->preview_headless = head_headless(h);
    copy_text(o->preview_branch, sizeof(o->preview_branch), h->branch);
    return native_capture_start(&o->preview, o->preview_headless ? step : pane, o->preview_headless ? 4000L : 1000L);
}

/* A full buffer keeps its newest half from a line start, so a large capture
 * shows its latest lines rather than failing. */
static size_t preview_keep_tail(char *text, size_t used) {
    const char *line = memchr(text + used / 2U, '\n', used - used / 2U);
    size_t start = line ? (size_t)(line - text) + 1U : used / 2U;
    memmove(text, text + start, used - start);
    return used - start;
}

static void preview_read(struct app *app, FILE *input) {
    size_t used = 0U, length;
    for (;;) {
        if (used + 1U >= sizeof(app->preview_text)) used = preview_keep_tail(app->preview_text, used);
        length = fread(app->preview_text + used, 1, sizeof(app->preview_text) - used - 1U, input);
        if (!length) break;
        used += length;
    }
    app->preview_text[used] = '\0';
}

/* Shows a finished read. A headless head keeps the output it showed when a
 * later read fails; a pane says that its preview is unavailable. */
static void preview_accept(struct app *app, FILE *input, bool headless, bool timed_out) {
    if (input) { preview_read(app, input); fclose(input); return; }
    if (!headless) copy_text(app->preview_text, sizeof(app->preview_text), timed_out ? "preview timed out" : "preview unavailable");
    else if (!app->preview_text[0]) copy_text(app->preview_text, sizeof(app->preview_text), "Step output unavailable");
}

bool preview_loading(const struct app *app) {
    return app->observations && app->observations->preview.pid != 0;
}

void capture_preview(struct app *app) {
    struct native_observations *o = app->observations;
    const struct head *h;
    if (!app->preview || app->model.head_count == 0U || !o) return;
    retarget_selection(app);
    h = selected_head(app);
    if (h == NULL || head_remote(h)) return;
    /* Output read for another head is not this head's. */
    if (strcmp(app->preview_head, h->branch)) {
        copy_text(app->preview_head, sizeof(app->preview_head), h->branch);
        app->preview_text[0] = '\0';
    }
    if (o->preview.pid) { o->preview_again = true; return; }
    if (!preview_start(app, h) && head_headless(h))
        copy_text(app->preview_text, sizeof(app->preview_text), "Step output unavailable: the reader could not start");
}

void preview_tick(struct app *app) {
    struct native_observations *o = app->observations;
    const struct head *h;
    bool timed_out, current;
    if (!o || !o->preview.pid || !native_capture_step(&o->preview)) return;
    timed_out = o->preview.timed_out;
    h = app->preview ? selected_head(app) : NULL;
    current = h && !strcmp(o->preview_branch, h->branch) && !strcmp(app->preview_head, h->branch) &&
              o->preview_headless == head_headless(h);
    if (current) preview_accept(app, native_capture_take(&o->preview), o->preview_headless, timed_out);
    else native_capture_destroy(&o->preview);
    /* Another head was selected, or p was pressed again, during the read. */
    if (!current || o->preview_again) capture_preview(app);
}
