#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Work's list: one outline at every width. Heads the user started, the runs
 * they launched indented under them with the heads each run created, remote
 * heads and tasks beside local ones. A row is a status glyph, its name and,
 * where there is room, the agent, the machine (WHERE) or the status in
 * words; every glyph has its word in the Details page and the header of the
 * conversation, so no meaning rests on the glyph or its colour alone. */

#define DANGER KT_STYLE(KT_COLOR_DANGER, KT_COLOR_SURFACE, 0)
#define FAINT KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SURFACE, 0)

struct glyph { const char *utf8, *ascii; };
static const struct glyph glyphs[] = {
    {"\xe2\x97\x8f", "*"}, {"\xe2\x97\x90", "~"}, {"\xe2\x96\xb2", "!"}, {"\xe2\x9c\x97", "x"},
    {"\xe2\x9c\x93", "+"}, {"\xe2\x97\x8b", "-"}, {"?", "?"}, {"\xe2\x86\xaf", "!"}, {"\xe2\x97\x8c", "."}};
enum { G_RUNNING, G_WAITING, G_PROBLEM, G_FAILED, G_DONE, G_STOPPED, G_UNKNOWN, G_RETAINED, G_LOADING };

static const char *glyph(const struct app *app, int which, struct kt_style *tone, struct kt_style style) {
    *tone = style;
    return app->ascii ? glyphs[which].ascii : glyphs[which].utf8;
}

/* A workflow or step state as a glyph and its tone. */
const char *work_state_glyph(const struct app *app, const char *state, struct kt_style *tone) {
    if (!strcmp(state, "running")) return glyph(app, G_RUNNING, tone, KT_SUCCESS);
    if (!strcmp(state, "succeeded")) return glyph(app, G_DONE, tone, KT_SUCCESS);
    if (!strcmp(state, "failed")) return glyph(app, G_FAILED, tone, DANGER);
    if (!strcmp(state, "waiting-approval")) return glyph(app, G_WAITING, tone, KT_WARNING);
    if (!strcmp(state, "recovery-required") || !strcmp(state, "stale")) return glyph(app, G_PROBLEM, tone, KT_WARNING);
    return glyph(app, G_STOPPED, tone, FAINT);
}

static const char *remote_glyph(const struct app *app, const struct head *h, struct kt_style *tone) {
    const char *status = remote_head_status(app, h);
    if (h->retained || !strcmp(status, "disconnected")) return glyph(app, G_RETAINED, tone, KT_MUTED);
    if (!strcmp(status, "stale")) return glyph(app, G_PROBLEM, tone, KT_WARNING);
    if (!strcmp(status, "unknown")) return glyph(app, G_UNKNOWN, tone, KT_MUTED);
    return glyph(app, !strcmp(status, "running") ? G_RUNNING : G_STOPPED, tone, !strcmp(status, "running") ? KT_SUCCESS : FAINT);
}

/* A head waits on the user when an approval or a published plan is pending. */
static bool head_waiting(const struct app *app, const struct head *h) {
    return (h->head_id[0] && h->gates > h->approved) || work_head_plan_ready(app, h);
}

const char *work_head_glyph(const struct app *app, const struct head *h, struct kt_style *tone) {
    const char *status;
    if (head_remote(h)) return remote_glyph(app, h, tone);
    if (head_headless(h)) {
        const struct workflow_node *step = head_step(app, h);
        return step ? work_state_glyph(app, step->state, tone) : glyph(app, G_STOPPED, tone, FAINT);
    }
    status = display_status(h);
    if (!strcmp(status, "STALE")) return glyph(app, G_PROBLEM, tone, KT_WARNING);
    if (!strcmp(status, "UNAVAILABLE")) return glyph(app, G_UNKNOWN, tone, KT_MUTED);
    if (head_waiting(app, h)) return glyph(app, G_WAITING, tone, KT_WARNING);
    if (!strcmp(status, "LIVE")) return glyph(app, G_RUNNING, tone, KT_SUCCESS);
    if (!strcmp(h->observed, "failed")) return glyph(app, G_FAILED, tone, DANGER);
    if (h->declared[0] && !strcmp(h->declared, "done")) return glyph(app, G_DONE, tone, KT_SUCCESS);
    return glyph(app, G_STOPPED, tone, FAINT);
}

static const char *task_glyph(const struct app *app, const struct task_observation *t, struct kt_style *tone) {
    const char *status = task_status(app, t);
    if (t->retained) return glyph(app, G_RETAINED, tone, KT_MUTED);
    if (!strcmp(status, "stale")) return glyph(app, G_PROBLEM, tone, KT_WARNING);
    if (!strcmp(status, "needs approval")) return glyph(app, G_WAITING, tone, KT_WARNING);
    if (!strcmp(status, "unknown")) return glyph(app, G_UNKNOWN, tone, KT_MUTED);
    if (!strcmp(t->state, "failed")) return glyph(app, G_FAILED, tone, DANGER);
    return glyph(app, !strcmp(t->state, "running") ? G_RUNNING : G_STOPPED, tone, !strcmp(t->state, "running") ? KT_SUCCESS : FAINT);
}

const char *work_loading_glyph(const struct app *app) { return app->ascii ? glyphs[G_LOADING].ascii : glyphs[G_LOADING].utf8; }

/* "3/4": steps done of the run's steps. */
static void run_steps_text(const struct app *app, size_t run, char *out, size_t size) {
    const struct workflow_model *m = app->workflows;
    size_t i, done = 0, total = 0;
    for (i = 0; i < m->node_count; i++) {
        if (m->nodes[i].run != run) continue;
        total++;
        if (!strcmp(m->nodes[i].state, "succeeded")) done++;
    }
    snprintf(out, size, "%zu/%zu", done, total);
}

/* ---- one row -------------------------------------------------------------- */

struct row_columns { int name, agent, where, status; };

/* Columns from the right: the status in words on a wide list (one pane), or
 * the agent from 30 columns, then WHERE when remote work is listed. */
static void row_columns(const struct app *app, int width, bool remote, struct row_columns *k) {
    int right = width;
    k->status = k->agent = k->where = -1;
    bool where = remote && app->cols >= 95 && width >= 36;
    if (width >= 50 && app->workspace && app->workspace->compact) { k->status = right - 16; right = k->status; }
    else if (width >= 30 && (!where || width >= 44)) { k->agent = right - 10; right = k->agent; }
    if (where) { k->where = right - 12; right = k->where; }
    k->name = right;
}

struct row_text { char name[TEXT + 64], count[32]; const char *glyph, *agent, *where, *status; struct kt_style tone; bool muted; };

static void head_row(const struct app *app, const struct outline_row *row, struct row_text *t) {
    const struct head *h = &app->model.heads[row->head];
    const char *role = head_run_role(app, h);
    t->glyph = work_head_glyph(app, h, &t->tone);
    snprintf(t->name, sizeof(t->name), "%s%s%s", role ? role : "", role ? " " : "", h->branch);
    t->agent = head_remote(h) && !strcmp(remote_agent_name(h), "not observed") ? "-" : head_agent_short(app, h);
    t->where = head_where(h);
    t->status = !head_remote(h) && head_waiting(app, h) && strcmp(display_status(h), "STALE") ? "needs you" : head_list_status(app, h);
    t->muted = h->retained;
}

static void run_row(const struct app *app, const struct outline_row *row, struct row_text *t) {
    const struct workflow_run *r = &app->workflows->runs[row->run];
    const char *expander = app->ascii ? (row->open ? "v" : ">") : (row->open ? "\xe2\x96\xbe" : "\xe2\x96\xb8");
    struct kt_style tone;
    const char *state = work_state_glyph(app, r->state, &tone);
    snprintf(t->name, sizeof(t->name), "%s %s %s %s", expander, state, !strcmp(r->kind, "plan") ? "plan run" : "run", r->name);
    t->glyph = NULL; t->tone = tone;
    run_steps_text(app, row->run, t->count, sizeof(t->count));
    t->agent = t->count; t->where = ""; t->status = r->state;
}

static void retired_row(const struct app *app, const struct outline_row *row, struct row_text *t) {
    const struct workflow_head *r = &app->workflows->heads[row->record];
    t->glyph = glyph(app, G_STOPPED, &t->tone, FAINT);
    snprintf(t->name, sizeof(t->name), "%s %s", r->role, r->branch);
    /* Where the agent would be, why the head is gone ("retired"). */
    t->status = run_head_absence(app, r); t->agent = t->status; t->where = ""; t->muted = true;
}

static void task_row(const struct app *app, const struct outline_row *row, struct row_text *t) {
    const struct task_observation *task = &app->model.tasks[row->record];
    t->glyph = task_glyph(app, task, &t->tone);
    snprintf(t->name, sizeof(t->name), "%s", task->task_id);
    t->agent = task->profile[0] && strcmp(task->profile, "-") ? task->profile : "task";
    t->where = task->host; t->status = task_status(app, task); t->muted = task->retained;
}

static void row_text(const struct app *app, const struct outline_row *row, struct row_text *t) {
    memset(t, 0, sizeof(*t));
    t->agent = t->where = t->status = "";
    if (row->kind == OUTLINE_RUN) run_row(app, row, t);
    else if (row->kind == OUTLINE_RETIRED) retired_row(app, row, t);
    else if (row->kind == OUTLINE_TASK) task_row(app, row, t);
    else head_row(app, row, t);
}

static bool row_marked(const struct app *app, const struct outline_row *row) {
    return row->kind == OUTLINE_HEAD && marked_index(app, app->model.heads[row->head].branch) < app->marked_count;
}

/* The right-hand columns of a row: agent (or step count), WHERE, status. */
static void row_cells(struct app *app, struct kt_rect r, int y, const struct outline_row *row, const struct row_text *t,
                      const struct row_columns *k, bool lit) {
    struct kt_canvas *c = &app->frame;
    struct kt_style fill = KT_SELECTED;
    if (k->agent >= 0) kt_text(c, (struct kt_rect){r.x + k->agent, y, r.width - k->agent, 1}, t->agent, lit ? fill : KT_MUTED);
    if (k->where >= 0 && row->kind != OUTLINE_RUN) kt_text(c, (struct kt_rect){r.x + k->where, y, 11, 1}, t->where, lit ? fill : KT_MUTED);
    if (k->status >= 0) kt_text_aligned(c, (struct kt_rect){r.x + k->status, y, r.width - k->status, 1}, t->status, KT_ALIGN_RIGHT,
                                        lit ? fill : t->tone);
}

/* The mark of a marked head and the state glyph; the column after them. */
static int row_marks(struct app *app, int x, int y, const struct outline_row *row, const struct row_text *t, bool lit) {
    struct kt_canvas *c = &app->frame;
    if (row_marked(app, row)) { kt_text(c, (struct kt_rect){x, y, 2, 1}, "* ", lit ? KT_SELECTED : KT_BASE); x += 2; }
    if (t->glyph) { kt_text(c, (struct kt_rect){x, y, 1, 1}, t->glyph, lit ? KT_SELECTED : t->tone); x += 2; }
    return x;
}

/* The selected row is filled and barred while the list has focus (lit);
 * without focus only a faint bar marks it. */
static void draw_row(struct app *app, struct kt_rect r, int y, const struct outline_row *row, bool selected, bool focused,
                     const struct row_columns *k) {
    struct kt_canvas *c = &app->frame;
    struct row_text t;
    bool lit = focused && selected;
    struct kt_style base;
    char cell[TEXT + 96];
    int x = r.x + 1 + row->depth * 2, room;
    row_text(app, row, &t);
    base = lit ? KT_SELECTED : t.muted || row->kind == OUTLINE_RETIRED ? KT_MUTED : KT_BASE;
    if (lit) kt_fill(c, (struct kt_rect){r.x, y, r.width, 1}, KT_SELECTED);
    if (selected) kt_text(c, (struct kt_rect){r.x, y, 1, 1}, app->ascii ? ">" : "\xe2\x96\x8c", lit ? KT_SELECTED : FAINT);
    x = row_marks(app, x, y, row, &t, lit);
    room = k->name - (x - r.x);
    fit_text(app, t.name, room > 1 ? (size_t)(room - 1) : 1U, cell, sizeof(cell));
    kt_text(c, (struct kt_rect){x, y, room, 1}, cell, lit ? KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, KT_BOLD) : base);
    row_cells(app, r, y, row, &t, k, lit);
}

static void record_row(struct app *app, struct kt_rect r, int y, const struct outline_row *row) {
    size_t item;
    if (app->hit_count >= MAX_HEADS || row->kind == OUTLINE_RETIRED) return;
    item = row->kind == OUTLINE_RUN ? MAX_HEADS + 1U + row->run :
        row->kind == OUTLINE_TASK ? MAX_HEADS + 1U + WF_RUNS + row->record : row->head;
    app->hit_rows[app->hit_count] = app->hit_bottom[app->hit_count] = y + 1;
    app->hit_left[app->hit_count] = r.x + 1; app->hit_right[app->hit_count] = r.x + r.width;
    app->hit_items[app->hit_count++] = item;
}

/* ---- the pane ------------------------------------------------------------- */

static size_t visible_heads(const struct app *app) {
    size_t i, n = 0;
    for (i = 0; i < app->model.head_count; i++) if (head_visible(app, &app->model.heads[i])) n++;
    return n + app->model.task_count;
}

/* "7 heads · 2 need you", and the filter when one is active. */
static void list_footer(struct app *app, struct kt_rect r, int y) {
    struct work_counts n;
    char text[TEXT + 96];
    work_counts(app, &n);
    snprintf(text, sizeof(text), "%zu head%s", n.heads + n.remote, n.heads + n.remote == 1 ? "" : "s");
    if (app->model.task_count) text_append(text, sizeof(text), "%s%zu task%s", dot(app), app->model.task_count, app->model.task_count == 1 ? "" : "s");
    if (n.attention) text_append(text, sizeof(text), "%s%zu need%s you", dot(app), n.attention, n.attention == 1 ? "s" : "");
    if (app->search[0]) text_append(text, sizeof(text), "%sfilter \"%s\"", dot(app), app->search);
    kt_text(&app->frame, (struct kt_rect){r.x + 1, y, r.width - 1, 1}, text, KT_MUTED);
}

static void list_title(struct app *app, char *out, size_t size) {
    char label[160];
    if (app->location[0]) snprintf(out, size, "Heads%s%s", dot(app), location_label(app, label, sizeof(label)));
    else snprintf(out, size, "Heads");
}

/* A filter that matches nothing says so; Esc clears it. */
static void list_empty(struct app *app, struct kt_rect r) {
    char text[TEXT + 64], label[160];
    if (app->search[0]) snprintf(text, sizeof(text), "No heads match \"%s\"%sesc clears", app->search, dot(app));
    else if (app->location[0]) snprintf(text, sizeof(text), "No %s to show%sesc or L shows all", location_label(app, label, sizeof(label)), dot(app));
    else if (!app->snapshot_at && !app->snapshot_stale) snprintf(text, sizeof(text), "%s Reading heads%s", work_loading_glyph(app), app->ascii ? "..." : "\xe2\x80\xa6");
    else snprintf(text, sizeof(text), "Nothing here yet; n starts a task");
    statistics_wrap(&app->frame, r.x + 1, r.y + 1, r.width - 2, r.height - 2, KT_MUTED, text);
}

/* The first row shown: the selection stays in view. */
static void list_scroll(struct native_workspace *w, size_t position, size_t n, size_t visible) {
    if (position < n && position < w->list_top) w->list_top = position;
    if (position < n && position >= w->list_top + visible) w->list_top = position + 1 - visible;
    if (w->list_top >= n) w->list_top = 0;
}

void work_list_draw(struct app *app, struct kt_rect r, bool focused) {
    static struct outline_row rows[OUTLINE_ROWS];
    struct native_workspace *w = app->workspace;
    struct row_columns k;
    char title[192], count[48];
    size_t n, position, i, visible;
    int y;
    list_title(app, title, sizeof(title));
    if (w->compact) snprintf(count, sizeof(count), "1/2 tab");
    else snprintf(count, sizeof(count), "%zu", visible_heads(app));
    work_rule(app, r, title, focused, count);
    n = outline_build(app, rows, OUTLINE_ROWS);
    position = outline_position(app, rows, n);
    if (!n) { list_empty(app, r); return; }
    visible = r.height > 3 ? (size_t)(r.height - 3) : 1U;
    list_scroll(w, position, n, visible);
    row_columns(app, r.width, app->cols >= 95 && remote_listed(app), &k);
    for (i = w->list_top, y = r.y + 1; i < n && y < r.y + r.height - 2; i++, y++) {
        record_row(app, r, y, &rows[i]);
        draw_row(app, r, y, &rows[i], i == position, focused, &k);
    }
    /* The runs are read apart from the heads; until they arrive, say so. */
    if (!app->workflows && native_observations_loading(app, OBSERVE_WORKFLOWS) && y < r.y + r.height - 2)
        canvas_text(&app->frame, r.x + 1, y, r.width - 1, KT_MUTED, "%s Reading runs%s", work_loading_glyph(app), app->ascii ? "..." : "\xe2\x80\xa6");
    if (r.height >= 4) list_footer(app, r, r.y + r.height - 1);
}

/* Whether remote work is listed, so the list makes room for WHERE. */
bool remote_listed(const struct app *app) {
    size_t i;
    if (app->model.task_count) return true;
    for (i = 0; i < app->model.head_count; i++) if (head_remote(&app->model.heads[i])) return true;
    return false;
}
