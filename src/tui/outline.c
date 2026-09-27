#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Work is organised by what the user started. A head a workflow run created
 * with a spawn step belongs to that run, and a run launched from a planning
 * conversation belongs to that planning head. Every association comes from
 * recorded run state (planning-branch, spawn-step heads); nothing is guessed
 * from names. Run rows start collapsed; expansion is per run and shared by
 * Work, Overview and the workspace navigation. */

bool head_headless(const struct head *h) {
    if (h->remote_host[0]) return false;
    if (h->terminal[0]) return !strcmp(h->terminal, "headless");
    return !strcmp(h->session, "-") || !strcmp(h->desired, "headless");
}

static bool run_newer(const struct workflow_run *a, const struct workflow_run *b) {
    return strcmp(a->created, b->created) > 0;
}

/* The run whose spawn step created this head, or SIZE_MAX. */
size_t head_owner_run(const struct app *app, const struct head *h, size_t *record) {
    const struct workflow_model *m = app->workflows;
    size_t i, run = SIZE_MAX;
    if (record) *record = SIZE_MAX;
    if (!m || app->fleet || !h) return SIZE_MAX;
    for (i = 0; i < m->head_count; i++) {
        if (strcmp(m->heads[i].branch, h->branch)) continue;
        if (run != SIZE_MAX && !run_newer(&m->runs[m->heads[i].run], &m->runs[run])) continue;
        run = m->heads[i].run;
        if (record) *record = i;
    }
    return run;
}

/* The planning head that launched a run, when that head is itself a task the
 * user started (not a head another run created), or SIZE_MAX. The recorded
 * head ID must match: a later head reusing the branch name is not the owner. */
size_t run_owner_head(const struct app *app, size_t run) {
    const struct workflow_run *r;
    const struct head *h;
    if (!app->workflows || run >= app->workflows->run_count || !app->workflows->runs[run].planning[0]) return SIZE_MAX;
    r = &app->workflows->runs[run];
    h = head_for_branch(app, r->planning);
    if (!h || (r->planning_head[0] && strcmp(r->planning_head, h->head_id))) return SIZE_MAX;
    if (head_owner_run(app, h, NULL) != SIZE_MAX) return SIZE_MAX;
    return (size_t)(h - app->model.heads);
}

static bool step_active(const char *state) {
    return !strcmp(state, "running") || !strcmp(state, "waiting-approval") || !strcmp(state, "retrying");
}

/* The step that is running on the head, or the one that started last. */
const struct workflow_node *head_step(const struct app *app, const struct head *h) {
    const struct workflow_model *m = app->workflows;
    const struct workflow_node *best = NULL;
    size_t i;
    if (!m || !h || app->fleet) return NULL;
    for (i = 0; i < m->node_count; i++) {
        const struct workflow_node *n = &m->nodes[i];
        if (!strcmp(n->kind, "spawn") || strcmp(n->head, h->branch) || (!n->attempts && !step_active(n->state))) continue;
        if (best && step_active(best->state) && !step_active(n->state)) continue;
        if (!best || (step_active(n->state) && !step_active(best->state)) || n->started > best->started) best = n;
    }
    return best;
}

static bool step_needs_user(const char *state) {
    return !strcmp(state, "failed") || !strcmp(state, "recovery-required") || !strcmp(state, "waiting-approval");
}

/* Attention comes from the work, not from how it is run: a headless head
 * never needs attention merely because it has no terminal session. */
bool head_needs_attention(const struct app *app, const struct head *h) {
    const struct workflow_node *step;
    if (app->fleet) return false;
    if (h->gates > h->approved) return true;
    if (!head_headless(h)) return strcmp(display_status(h), "LIVE") != 0;
    step = head_step(app, h);
    return step && step_needs_user(step->state);
}

static bool run_has_live_head(const struct app *app, size_t run) {
    const struct workflow_model *m = app->workflows;
    size_t i;
    for (i = 0; i < m->head_count; i++)
        if (m->heads[i].run == run && head_for_branch(app, m->heads[i].branch)) return true;
    return false;
}

static bool run_retirement_needs_user(const struct app *app, size_t run) {
    const struct workflow_model *m = app->workflows;
    size_t i;
    for (i = 0; i < m->head_count; i++) {
        const struct workflow_head *r = &m->heads[i];
        if (r->run == run && (!strcmp(r->retirement, "kept") || !strcmp(r->retirement, "failed")) &&
            head_for_branch(app, r->branch)) return true;
    }
    return false;
}

bool run_needs_attention(const struct app *app, size_t run) {
    const struct workflow_model *m = app->workflows;
    const char *state;
    size_t i;
    if (!m || run >= m->run_count) return false;
    state = m->runs[run].state;
    if (!strcmp(state, "stale") || !strcmp(state, "recovery-required") || !strcmp(state, "waiting-approval")) return true;
    for (i = 0; i < m->node_count; i++) if (m->nodes[i].run == run && !strcmp(m->nodes[i].state, "waiting-approval")) return true;
    if (!strcmp(state, "failed") && run_has_live_head(app, run)) return true;
    return run_retirement_needs_user(app, run);
}

/* Header and Overview counts: tasks the user started; run heads count once,
 * inside their run. */
static void work_count_head(const struct app *app, const struct head *h, struct work_counts *n) {
    const struct workflow_node *step;
    if (head_owner_run(app, h, NULL) != SIZE_MAX) { n->run_heads++; return; }
    n->heads++;
    step = head_headless(h) ? head_step(app, h) : NULL;
    if (step ? !strcmp(step->state, "running") : !strcmp(display_status(h), "LIVE")) n->running++;
    if (head_needs_attention(app, h)) n->attention++;
}

void work_counts(const struct app *app, struct work_counts *n) {
    size_t i;
    memset(n, 0, sizeof(*n));
    for (i = 0; i < app->model.head_count; i++) work_count_head(app, &app->model.heads[i], n);
    if (!app->workflows || app->fleet) return;
    for (i = 0; i < app->workflows->run_count; i++) {
        const char *state = app->workflows->runs[i].state;
        if (!strcmp(state, "running") || !strcmp(state, "waiting-approval")) n->runs_active++;
        if (run_needs_attention(app, i)) n->attention++;
        n->runs++;
    }
}

size_t attention_count(const struct app *app) {
    struct work_counts n;
    if (app->fleet) return 0;
    work_counts(app, &n);
    return n.attention;
}

bool run_expanded(const struct app *app, size_t run) {
    size_t i;
    if (!app->workflows || run >= app->workflows->run_count) return false;
    for (i = 0; i < app->expanded_count; i++) if (!strcmp(app->expanded_runs[i], app->workflows->runs[run].id)) return true;
    return false;
}

void run_set_expanded(struct app *app, size_t run, bool expand) {
    size_t i;
    if (!app->workflows || run >= app->workflows->run_count) return;
    for (i = 0; i < app->expanded_count; i++) if (!strcmp(app->expanded_runs[i], app->workflows->runs[run].id)) break;
    if (expand && i == app->expanded_count && i < WF_RUNS)
        copy_text(app->expanded_runs[app->expanded_count++], sizeof(app->expanded_runs[0]), app->workflows->runs[run].id);
    else if (!expand && i < app->expanded_count) {
        app->expanded_count--;
        if (i < app->expanded_count) memcpy(app->expanded_runs[i], app->expanded_runs[app->expanded_count], sizeof(app->expanded_runs[0]));
    }
}

/* Outline construction. Rows are preorder: user heads, their runs, then the
 * heads each expanded run created; runs without a planning head come last. */
struct outline_build {
    struct app *app;
    struct outline_row *rows;
    size_t capacity, count;
    size_t owner[MAX_HEADS], run_owner[WF_RUNS];
};

static void outline_add(struct outline_build *b, enum outline_kind kind, size_t head, size_t run, size_t record, int depth) {
    if (b->count < b->capacity) b->rows[b->count++] = (struct outline_row){kind, head, run, record, depth, false};
}

static bool run_member_matches(const struct outline_build *b, size_t run) {
    const struct app *app = b->app;
    size_t i;
    for (i = 0; i < app->model.head_count; i++)
        if (b->owner[i] == run && head_matches(&app->model.heads[i], app->search)) return true;
    return false;
}

/* A run shows its heads when the user expanded it, when a search matches one
 * of them, or when the selected head is one of them. */
static bool outline_open(const struct outline_build *b, size_t run) {
    const struct app *app = b->app;
    if (run_expanded(app, run)) return true;
    if (app->search[0] && run_member_matches(b, run)) return true;
    return !app->run_row && app->selected < app->model.head_count && b->owner[app->selected] == run;
}

static void outline_members(struct outline_build *b, size_t run, int depth) {
    const struct app *app = b->app;
    const struct workflow_model *m = app->workflows;
    size_t i;
    for (i = 0; i < app->model.head_count; i++)
        if (b->owner[i] == run && head_matches(&app->model.heads[i], app->search))
            outline_add(b, OUTLINE_HEAD, i, run, SIZE_MAX, depth);
    if (app->search[0]) return;
    for (i = 0; i < m->head_count; i++)
        if (m->heads[i].run == run && !head_for_branch(app, m->heads[i].branch))
            outline_add(b, OUTLINE_RETIRED, SIZE_MAX, run, i, depth);
}

static void outline_run(struct outline_build *b, size_t run, int depth, bool owner_matches) {
    const struct app *app = b->app;
    bool name = app->search[0] && text_matches(app->workflows->runs[run].name, app->search);
    if (app->search[0] && !owner_matches && !name && !run_member_matches(b, run)) return;
    outline_add(b, OUTLINE_RUN, SIZE_MAX, run, SIZE_MAX, depth);
    if (!outline_open(b, run)) return;
    if (b->count) b->rows[b->count - 1].open = true;
    outline_members(b, run, depth + 1);
}

static bool run_has_heads(const struct outline_build *b, size_t run) {
    const struct workflow_model *m = b->app->workflows;
    size_t i;
    for (i = 0; i < m->head_count; i++) if (m->heads[i].run == run) return true;
    return false;
}

static bool head_runs_match(const struct outline_build *b, size_t head) {
    const struct app *app = b->app;
    size_t r;
    for (r = 0; app->workflows && r < app->workflows->run_count; r++)
        if (b->run_owner[r] == head && run_member_matches(b, r)) return true;
    return false;
}

static void outline_user_head(struct outline_build *b, size_t head) {
    struct app *app = b->app;
    bool matches = head_matches(&app->model.heads[head], app->search);
    size_t r;
    if (!matches && !head_runs_match(b, head)) return;
    outline_add(b, OUTLINE_HEAD, head, SIZE_MAX, SIZE_MAX, 0);
    for (r = 0; app->workflows && r < app->workflows->run_count; r++)
        if (b->run_owner[r] == head) outline_run(b, r, 1, matches);
}

size_t outline_build(struct app *app, struct outline_row *rows, size_t capacity) {
    struct outline_build b;
    size_t i;
    b.app = app; b.rows = rows; b.capacity = capacity; b.count = 0;
    for (i = 0; i < app->model.head_count && i < MAX_HEADS; i++) b.owner[i] = head_owner_run(app, &app->model.heads[i], NULL);
    for (i = 0; i < WF_RUNS; i++) b.run_owner[i] = run_owner_head(app, i);
    for (i = 0; i < app->model.head_count; i++) if (b.owner[i] == SIZE_MAX) outline_user_head(&b, i);
    for (i = 0; app->workflows && !app->fleet && i < app->workflows->run_count; i++)
        if (b.run_owner[i] == SIZE_MAX && run_has_heads(&b, i)) outline_run(&b, i, 0, false);
    return b.count;
}

/* Index of the current selection among the rows, or count when absent. */
size_t outline_position(const struct app *app, const struct outline_row *rows, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (app->run_row ? rows[i].kind == OUTLINE_RUN && rows[i].run == app->workflow_run :
            rows[i].kind == OUTLINE_HEAD && rows[i].head == app->selected) return i;
    }
    return count;
}

void outline_select(struct app *app, const struct outline_row *row) {
    if (row->kind == OUTLINE_RUN) {
        if (app->workflow_run != row->run) app->workflow_node = 0;
        app->run_row = true; app->workflow_run = row->run;
    } else if (row->kind == OUTLINE_HEAD) {
        app->run_row = false; app->selected = row->head;
    }
}

static bool outline_skip(const struct outline_row *row, bool heads_only) {
    return row->kind == OUTLINE_RETIRED || (heads_only && row->kind != OUTLINE_HEAD);
}

/* The next selectable row from position in direction, or count. */
static size_t outline_next(const struct outline_row *rows, size_t count, size_t position, int direction, bool heads_only) {
    size_t next = position;
    if (position == count) {
        for (next = 0; next < count && outline_skip(&rows[next], heads_only); next++) { }
        return next;
    }
    do {
        if (direction < 0 ? next == 0 : next + 1 >= count) return count;
        next = direction < 0 ? next - 1 : next + 1;
    } while (outline_skip(&rows[next], heads_only));
    return next;
}

/* Moves between selectable rows; retired heads are shown but not selected,
 * and views about one head (details) move between heads only. */
void outline_move(struct app *app, int direction, bool heads_only) {
    static struct outline_row rows[OUTLINE_ROWS];
    size_t count = outline_build(app, rows, OUTLINE_ROWS);
    size_t next = outline_next(rows, count, outline_position(app, rows, count), direction, heads_only);
    if (next < count) outline_select(app, &rows[next]);
}

/* Enter or l/h on a run row expands or collapses it; h on a run head
 * collapses its run and selects the run row. */
bool outline_toggle(struct app *app, int direction) {
    size_t run;
    if (app->run_row) {
        bool open = run_expanded(app, app->workflow_run);
        run_set_expanded(app, app->workflow_run, direction ? direction > 0 : !open);
        return true;
    }
    if (direction >= 0 || app->selected >= app->model.head_count) return false;
    run = head_owner_run(app, &app->model.heads[app->selected], NULL);
    if (run == SIZE_MAX) return false;
    run_set_expanded(app, run, false);
    app->run_row = true; app->workflow_run = run;
    return true;
}

/* Leaving Work or Overview on a run row: views about one head show the run's
 * first head, or the planning head that launched it. */
void outline_release_run(struct app *app) {
    size_t i, owner;
    if (!app->run_row) return;
    app->run_row = false;
    if (!app->workflows || app->workflow_run >= app->workflows->run_count) return;
    for (i = 0; i < app->model.head_count; i++)
        if (head_owner_run(app, &app->model.heads[i], NULL) == app->workflow_run) { app->selected = i; return; }
    owner = run_owner_head(app, app->workflow_run);
    if (owner != SIZE_MAX) app->selected = owner;
}
