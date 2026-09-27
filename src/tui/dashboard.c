#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Hydra maps observations to reusable widgets. These functions do not collect
 * data or infer remote liveness from desired state. */
void dashboard_style(void *context, enum tv_style tone) {
    emit_style(context, (enum tone)tone);
}

void dashboard_text(struct tv_canvas *c, int x, int y, int width,
                           enum tv_style tone, const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format); vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    tv_text(c, (struct tv_rect){x, y, width, 1}, buffer, tone);
}

void dashboard_card(struct tv_canvas *c, int x, int width, const char *title,
                           size_t count, const char *caption, enum tv_style tone) {
    tv_panel(c, (struct tv_rect){x, 0, width, 5}, title);
    if (count == SIZE_MAX) dashboard_text(c, x + 2, 1, width - 4, tone, "--");
    else dashboard_text(c, x + 2, 1, width - 4, tone, "%zu", count);
    dashboard_text(c, x + 2, 3, width - 4, TV_BASE, "%s", caption);
}

static void dashboard_head_hit(struct app *app, struct tv_rect r, int row, size_t item) {
    if (app->hit_count >= MAX_HEADS) return;
    app->hit_rows[app->hit_count] = app->line + row + 1;
    app->hit_bottom[app->hit_count] = app->line + row + 1;
    app->hit_left[app->hit_count] = app->content_x + r.x + 3;
    app->hit_right[app->hit_count] = app->content_x + r.x + r.width - 2;
    app->hit_items[app->hit_count++] = item;
}

static void dashboard_outline_row(struct app *app, struct tv_canvas *c, struct tv_rect r, int row, const struct outline_row *item, bool selected) {
    struct outline_text t;
    outline_row_text(app, item, &t);
    dashboard_text(c, r.x + 2, row, r.width - 4, selected ? TV_SELECTED : item->kind == OUTLINE_RETIRED ? TV_MUTED : TV_BASE,
                   "%c %-*.*s", selected ? '>' : ' ', r.width - 21, r.width - 21, t.name);
    dashboard_text(c, r.x + r.width - 18, row, 15, selected ? TV_SELECTED : t.tone, "%s", t.status);
    if (item->kind == OUTLINE_HEAD) dashboard_head_hit(app, r, row, item->head);
    else if (item->kind == OUTLINE_RUN) dashboard_head_hit(app, r, row, MAX_HEADS + 1U + item->run);
}

static void dashboard_heads(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    static struct outline_row rows[OUTLINE_ROWS];
    size_t i, count, position, start;
    int row = r.y + 2, available = r.height - 4;
    struct work_counts n;
    tv_panel(c, r, app->fleet ? "REMOTE HEADS / desired state" : "WORK / what you started, with its runs");
    retarget_selection(app);
    count = outline_build(app, rows, OUTLINE_ROWS);
    position = outline_position(app, rows, count);
    if (available < 1) return;
    start = position < count && position >= (size_t)available ? position - (size_t)available + 1 : 0;
    for (i = start; i < count && row < r.y + r.height - 2; i++, row++)
        dashboard_outline_row(app, c, r, row, &rows[i], i == position);
    work_counts(app, &n);
    if (!count) dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER, "No matching heads");
    else if (app->fleet) dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER, "%zu heads / Enter opens details", count);
    else dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER, "%zu head%s you started / Enter opens or expands",
                        n.heads, n.heads == 1 ? "" : "s");
}

static void dashboard_detail_fleet(struct tv_canvas *c, struct tv_rect r, const struct head *h) {
    dashboard_text(c, r.x + 2, r.y + 6, r.width - 4, TV_BASE, "Desired: %s", h->desired);
    dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, TV_MUTED, "CPU / memory: not measured");
    if (r.height > 10) dashboard_text(c, r.x + 2, r.y + 9, r.width - 4, TV_BASE, "%s", h->remote_project);
}

static void dashboard_detail_local(struct app *app, struct tv_canvas *c, struct tv_rect r, const struct head *h) {
    unsigned pending;
    if (!h->head_id[0]) {
        dashboard_text(c, r.x + 2, r.y + 6, r.width - 4, TV_WARNING, "No head record; counters unknown");
        dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, TV_MUTED, "Recovery explains what is missing");
        return;
    }
    pending = h->gates > h->approved ? h->gates - h->approved : 0;
    dashboard_text(c, r.x + 2, r.y + 6, r.width - 4, TV_BASE, "Changed files %u   Reported: %s", h->diff, h->declared[0] ? h->declared : "nothing yet");
    if (h->gates) dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, pending ? TV_WARNING : TV_BASE,
                                 pending ? "Approval requests %u of %u approved, %u waiting for you" : "Approval requests %u of %u approved", h->approved, h->gates, pending);
    else dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, TV_MUTED, "No approval requests");
    (void)app;
}

static void dashboard_detail_run(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    char text[1024];
    int row = r.y + 4;
    size_t offset = 0, length, width = (size_t)(r.width - 4);
    run_summary_text(app, app->workflow_run, text, sizeof(text));
    dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_STRONG, "%s", text);
    run_next_text(app, app->workflow_run, text, sizeof(text));
    length = strlen(text);
    while (offset < length && row < r.y + r.height - 1) {
        dashboard_text(c, r.x + 2, row++, (int)width, TV_BASE, "%.*s", (int)width, text + offset);
        offset += width;
    }
}

static void dashboard_detail_head(struct app *app, struct tv_canvas *c, struct tv_rect r, const struct head *h) {
    char agent[TEXT + 96];
    if (app->fleet) snprintf(agent, sizeof(agent), "%s", h->remote_host);
    else head_agent_text(app, h, agent, sizeof(agent));
    dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_STRONG, "%s", h->branch);
    dashboard_text(c, r.x + 2, r.y + 4, r.width - 4, TV_BASE, "%s: %s", app->fleet ? "Host" : "Agent", agent);
    if (!app->fleet && head_headless(h)) dashboard_text(c, r.x + 2, r.y + 5, r.width - 4, TV_MUTED, "Terminal: headless (no terminal)");
    else dashboard_text(c, r.x + 2, r.y + 5, r.width - 4, app->fleet ? TV_BASE : status_tone(h), "Session: %s", app->fleet ? h->desired : status_label(h));
}

static void dashboard_detail(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    const struct head *h = selected_head(app);
    tv_panel(c, r, "SELECTED / what it needs");
    if (!app->fleet && app->run_row && app->workflows && app->workflow_run < app->workflows->run_count) { dashboard_detail_run(app, c, r); return; }
    if (!h) { dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_BASE, "No head selected"); return; }
    dashboard_detail_head(app, c, r, h);
    if (r.height < 9) return;
    if (app->fleet) dashboard_detail_fleet(c, r, h);
    else dashboard_detail_local(app, c, r, h);
}

static void fleet_binding_line(struct tv_canvas *c, struct tv_rect r, int *row,
                               const char *label, const char *value, enum tv_style tone) {
    char text[512];
    snprintf(text, sizeof(text), "%s%s", label, value);
    size_t used = 0, length = strlen(text), width = (size_t)(r.width - 4);
    while (used < length && *row < r.y + r.height - 2) {
        dashboard_text(c, r.x + 2, (*row)++, (int)width, tone, "%.*s", (int)width, text + used);
        used += width;
    }
}
static void selected_task_details(struct tv_canvas *c, struct tv_rect r, int *row,
                                 const struct task_observation *task) {
    char text[512];
    snprintf(text, sizeof(text), "%s / owner %s / %s / host %s", task->state, task->owner, task->freshness, task->host);
    fleet_binding_line(c, r, row, "State: ", text, TV_BASE);
    snprintf(text, sizeof(text), "%s / scope %s / requested %s", task->cancellation, task->cancellation_scope, task->cancel_requested_at);
    fleet_binding_line(c, r, row, "Cancellation: ", text, TV_WARNING);
    fleet_binding_line(c, r, row, "Wait: ", task->waiting_reason, TV_WARNING);
    fleet_binding_line(c, r, row, "Next: ", task->next_action, TV_BASE);
    fleet_binding_line(c, r, row, "Spec: ", task->spec_sha256, TV_BORDER);
    fleet_binding_line(c, r, row, "Request: ", task->request_id, TV_BORDER);
    snprintf(text, sizeof(text), "%s / verification %s", task->result_state, task->verification_state);
    fleet_binding_line(c, r, row, "Result: ", text, TV_BASE);
}
static void dashboard_fleet_tasks(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    int row = r.y + 2, available = r.height - 14;
    size_t selected = app->task_selected, start;
    tv_panel(c, r, "REMOTE TASKS / receiver-owned observations");
    if (available < 1) available = 1;
    if (available > 5) available = 5;
    start = selected < app->model.task_count && selected >= (size_t)available ? selected - (size_t)available + 1 : 0;
    for (size_t i = start; i < app->model.task_count && i < start + (size_t)available; i++)
        dashboard_text(c, r.x + 2, row++, r.width - 4, i == selected ? TV_SELECTED : TV_BASE,
                       "%c %s", i == selected ? '>' : ' ', app->model.tasks[i].task_id);
    row++;
    if (selected < app->model.task_count) selected_task_details(c, r, &row, &app->model.tasks[selected]);
    else fleet_binding_line(c, r, &row, "Selection changed: ", "use j/k to select a current task", TV_WARNING);
    dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER,
                   "%zu tasks / j k select / Y approve N reject R resume X cancel", app->model.task_count);
}

static void dashboard_fleet_hosts(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    size_t i; int row = r.y + 2;
    for (i = 0; i < app->model.host_count && row < r.y + r.height - 2; i++) {
        const struct host_observation *host = &app->model.hosts[i];
        if (!strcmp(host->state, "failed")) dashboard_text(c, r.x + 2, row++, r.width - 4, TV_WARNING,
                       "%s / failed / -- heads / %s / %s", host->name, host->error, host->freshness);
        else dashboard_text(c, r.x + 2, row++, r.width - 4, TV_BASE,
                       "%s / responded / %u heads / %s", host->name, host->heads, host->freshness);
    }
    dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER, "H hosts / %zu total / CPU and memory unavailable", app->model.host_count);
    if (row == r.y + 2) dashboard_text(c, r.x + 2, row, r.width - 4, TV_WARNING, "No host observations; inspect Recovery");
}

static void dashboard_fleet_charts(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    if (app->model.task_count) dashboard_fleet_tasks(app, c, r);
    else dashboard_fleet_hosts(app, c, r);
}

static bool queue_has_data(const struct app *app) {
    size_t i;
    for (i = 0; i < app->history_count; i++) if (app->history_valid[i] && app->queue_history[i] > 0) return true;
    return false;
}

static void dashboard_queue_chart(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    size_t i; double maximum = 1;
    tv_panel(c, r, "QUEUE DEPTH / waiting work");
    for (i = 0; i < app->history_count; i++) if (app->history_valid[i] && app->queue_history[i] > maximum) maximum = app->queue_history[i];
    dashboard_text(c, r.x + 2, r.y + 1, r.width - 4, TV_MUTED, "0..%.0f queued entries over %zu samples", maximum, app->history_count);
    dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_MUTED, "A gap means the queue was unknown at that sample");
    tv_plot(c, (struct tv_rect){r.x + 2, r.y + 3, r.width - 4, r.height - 5},
            app->queue_history, app->history_valid, app->history_count, maximum, TV_BORDER);
}

static void dashboard_charts(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    tv_panel(c, r, "HOST OBSERVATIONS");
    dashboard_fleet_charts(app, c, r);
}

/* The run panel: the run is the primary object of planned work. */
/* Column origins; TRIES shows only when the head column keeps 30 cells. */
struct run_columns { int step, kind, state, tries, time, head; bool wide, attempts; };

static void run_columns_layout(int width, struct run_columns *k) {
    k->wide = width >= 84;
    k->attempts = width >= 106;
    k->step = 0;
    k->kind = 17;
    k->state = k->wide ? 32 : 16;
    k->tries = 50;
    k->time = k->attempts ? 56 : k->wide ? 50 : 30;
    k->head = k->time + 14;
}

static const char *run_step_head(const struct app *app, const struct workflow_node *n, char *out, size_t size) {
    const struct workflow_model *m = app->workflows;
    size_t i;
    if (!n->head[0]) return "-";
    for (i = 0; i < m->head_count; i++) {
        const struct workflow_head *h = &m->heads[i];
        if (h->run != n->run || strcmp(h->branch, n->head) || head_for_branch(app, h->branch)) continue;
        snprintf(out, size, "%s (%s)", n->head, run_head_absence(app, h));
        return out;
    }
    return n->head;
}

static enum tv_style run_step_tone(const char *state) {
    if (!strcmp(state, "failed") || !strcmp(state, "recovery-required") || !strcmp(state, "waiting-approval")) return TV_WARNING;
    if (!strcmp(state, "running")) return TV_SUCCESS;
    if (!strcmp(state, "succeeded")) return TV_BASE;
    return TV_MUTED;
}

static void run_step_line(struct app *app, struct tv_canvas *c, struct tv_rect r, int row, const struct workflow_node *n, const struct run_columns *k) {
    char kind[48], time[48], tries[16], head[TEXT + 32];
    int x = r.x + 2, width = r.width - 4;
    snprintf(kind, sizeof(kind), "%s%s%s", n->kind, n->role[0] ? "/" : "", n->role);
    step_duration_text(n, time, sizeof(time));
    snprintf(tries, sizeof(tries), "%u", n->attempts);
    dashboard_text(c, x + k->step, row, (k->wide ? k->kind : k->state) - 1, TV_BASE, "%s", n->id);
    if (k->wide) dashboard_text(c, x + k->kind, row, k->state - k->kind - 1, TV_MUTED, "%s", kind);
    dashboard_text(c, x + k->state, row, 17, run_step_tone(n->state), "%s", n->state);
    if (k->attempts) dashboard_text(c, x + k->tries, row, 5, TV_BASE, "%s", tries);
    dashboard_text(c, x + k->time, row, 13, TV_BASE, "%s", time);
    dashboard_text(c, x + k->head, row, width - k->head, TV_BASE, "%s", run_step_head(app, n, head, sizeof(head)));
}

static const char *setting_mark(const char *source) {
    return !strcmp(source, "configured") ? " (configured)" : !strcmp(source, "observed") ? "" : "";
}

/* One line under an agent step: executable, model, effort and tokens. */
static void run_agent_line(struct app *app, struct tv_canvas *c, struct tv_rect r, int row, const struct workflow_node *n) {
    const struct workflow_exec *e = &n->exec;
    const char *sep = dot(app);
    char in[32], cached[32], out[32];
    if (!strcmp(e->state, "pending")) { dashboard_text(c, r.x + 4, row, r.width - 6, TV_MUTED, "%s is starting; its receipt is not recorded yet", n->profile); return; }
    format_tokens(e->tokens_in, in, sizeof(in)); format_tokens(e->tokens_cached, cached, sizeof(cached));
    format_tokens(e->tokens_out, out, sizeof(out));
    dashboard_text(c, r.x + 4, row, r.width - 6, TV_MUTED, "%s%s%s%s%s, effort %s%s%stokens in %s, cached %s, out %s",
                   e->version[0] ? e->version : "version unknown", sep,
                   e->model[0] ? e->model : "model unknown", setting_mark(e->model_source),
                   "", e->effort[0] ? e->effort : "unknown", setting_mark(e->effort_source), sep, in, cached, out);
}

static void run_panel_empty(struct tv_canvas *c, struct tv_rect r) {
    dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_STRONG, "No workflow run yet.");
    dashboard_text(c, r.x + 2, r.y + 3, r.width - 4, TV_BASE, "A run appears here when you execute an approved plan (E in the plan review)");
    dashboard_text(c, r.x + 2, r.y + 4, r.width - 4, TV_BASE, "or start one with hydra workflow run. It shows each step, the head it ran on,");
    dashboard_text(c, r.x + 2, r.y + 5, r.width - 4, TV_BASE, "and what needs you next.");
}

static int run_panel_steps(struct app *app, struct tv_canvas *c, struct tv_rect r, size_t run, int bottom) {
    const struct workflow_model *m = app->workflows;
    struct run_columns k;
    int row = r.y + 2;
    size_t i;
    run_columns_layout(r.width - 4, &k);
    dashboard_text(c, r.x + 2 + k.step, r.y + 1, 16, TV_MUTED, "STEP");
    if (k.wide) dashboard_text(c, r.x + 2 + k.kind, r.y + 1, 14, TV_MUTED, "KIND/ROLE");
    if (k.attempts) dashboard_text(c, r.x + 2 + k.tries, r.y + 1, 5, TV_MUTED, "TRIES");
    dashboard_text(c, r.x + 2 + k.state, r.y + 1, 16, TV_MUTED, "STATE");
    dashboard_text(c, r.x + 2 + k.time, r.y + 1, 13, TV_MUTED, "TIME");
    dashboard_text(c, r.x + 2 + k.head, r.y + 1, r.width - 4 - k.head, TV_MUTED, "HEAD");
    for (i = 0; i < m->node_count && row < bottom; i++) {
        const struct workflow_node *n = &m->nodes[i];
        if (n->run != run) continue;
        run_step_line(app, c, r, row++, n, &k);
        if (n->exec.present && k.wide && row < bottom - 1) run_agent_line(app, c, r, row++, n);
    }
    return row;
}

/* Bytes of text that fit width columns, broken at a space when one is near. */
static size_t wrap_width(const char *text, size_t length, size_t width) {
    size_t cut;
    if (length <= width) return length;
    for (cut = width; cut > width / 2; cut--) if (text[cut] == ' ') return cut;
    return width;
}

static void run_panel_next(struct app *app, struct tv_canvas *c, struct tv_rect r, size_t run, int row) {
    char text[1536];
    size_t offset = 0, length, width = (size_t)(r.width - 10);
    enum tv_style tone = run_needs_attention(app, run) ? TV_WARNING : TV_BASE;
    run_next_text(app, run, text, sizeof(text));
    length = strlen(text);
    dashboard_text(c, r.x + 2, row, 6, TV_STRONG, "Next");
    while (offset < length && row < r.y + r.height - 1) {
        size_t chunk = wrap_width(text + offset, length - offset, width);
        dashboard_text(c, r.x + 8, row++, (int)width, tone, "%.*s", (int)chunk, text + offset);
        offset += chunk;
        offset += strspn(text + offset, " ");
    }
}

static void dashboard_run_panel(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    size_t run = overview_run(app);
    char title[TEXT + 160];
    int row;
    if (run == SIZE_MAX) { tv_panel(c, r, "RUN / no run yet"); run_panel_empty(c, r); return; }
    run_summary_text(app, run, title, sizeof(title));
    if (app->workflows->runs[run].planning[0]) text_append(title, sizeof(title), "%sfrom %s", dot(app), app->workflows->runs[run].planning);
    {
        char panel[TEXT + 200];
        snprintf(panel, sizeof(panel), "RUN / %s", title);
        tv_panel(c, r, panel);
    }
    row = run_panel_steps(app, c, r, run, r.y + r.height - 3);
    run_panel_next(app, c, r, run, r.y + r.height - 1 - (row + 1) >= 3 ? row + 1 : row);
}

static int dashboard_middle_height(const struct app *app, int height) {
    static struct outline_row rows[OUTLINE_ROWS];
    int middle = height / 2, wanted;
    if (app->fleet) {
        if (middle > 12 && app->model.head_count < 8) middle = 12;
        if (app->model.task_count && height - middle - 7 < 6) middle = 10;
        return middle < 10 ? 10 : middle;
    }
    wanted = (int)outline_build((struct app *)app, rows, OUTLINE_ROWS) + 5;
    if (wanted < 10) wanted = 10;
    return middle < wanted ? middle : wanted;
}

static void dashboard_empty(struct app *app, struct tv_canvas *c, int width) {
    dashboard_text(c, 1, 1, width - 2, TV_STRONG, "%s", app->fleet ? "No remote heads observed." : "No agent work in this project yet.");
    dashboard_text(c, 1, 3, width - 2, TV_BASE, "%s", app->fleet ? "Hosts shows connectivity for each machine; Recovery lists host problems."
                                                    : "Overview summarises what is running, what needs a decision, what changed and each run's progress.");
    if (!app->fleet) dashboard_text(c, 1, 4, width - 2, TV_BASE, "Press n to start a task; it appears here as soon as its terminal opens.");
}

static size_t changed_files(const struct app *app) {
    size_t i, total = 0;
    for (i = 0; i < app->model.head_count; i++) total += app->model.heads[i].diff;
    return total;
}

static void dashboard_cards(struct app *app, struct tv_canvas *c, int width) {
    int card = width / 4;
    struct work_counts n;
    char runs[64];
    if (app->fleet) {
        dashboard_card(c, 0, card - 1, "RUNNING", app->model.head_count, "remote, liveness unobserved", TV_SUCCESS);
        dashboard_card(c, card, card - 1, "NEED ATTENTION", app->model.recovery_count, "recovery findings", app->model.recovery_count ? TV_WARNING : TV_BASE);
        dashboard_card(c, card * 2, card - 1, "CHANGED FILES", SIZE_MAX, "not observed remotely", TV_STRONG);
        dashboard_card(c, card * 3, width - card * 3, "FINISHED", SIZE_MAX, "not observed remotely", TV_STRONG);
        return;
    }
    work_counts(app, &n);
    snprintf(runs, sizeof(runs), "%zu recorded in this project", n.runs);
    dashboard_card(c, 0, card - 1, "RUNNING", n.running + n.runs_active, "heads and runs working now", TV_SUCCESS);
    dashboard_card(c, card, card - 1, "NEED ATTENTION", n.attention, app->model.recovery_count ? "failures, decisions, stopped; see Recovery too" : "failures, decisions, stopped terminals", n.attention ? TV_WARNING : TV_BASE);
    dashboard_card(c, card * 2, card - 1, "RUNS ACTIVE", n.runs_active, runs, TV_STRONG);
    dashboard_card(c, card * 3, width - card * 3, "CHANGED FILES", changed_files(app), "uncommitted, all heads", TV_STRONG);
}

/* Rows the run panel needs to show every step of the run Overview centres
 * on, its agent lines and what comes next. */
static int run_panel_rows(struct app *app) {
    size_t run = overview_run(app), i;
    int rows = 6;
    if (run == SIZE_MAX) return 7;
    for (i = 0; i < app->workflows->node_count; i++)
        if (app->workflows->nodes[i].run == run) rows += app->workflows->nodes[i].exec.present ? 2 : 1;
    return rows;
}

static void dashboard_bottom(struct app *app, struct tv_canvas *c, int width, int bottom, int remaining) {
    if (remaining < 6) return;
    if (app->fleet) { dashboard_charts(app, c, (struct tv_rect){0, bottom, width, remaining}); return; }
    if (queue_has_data(app) && width >= 120 && app->history_count >= 2) {
        int third = width / 3;
        dashboard_run_panel(app, c, (struct tv_rect){0, bottom, width - third - 1, remaining});
        dashboard_queue_chart(app, c, (struct tv_rect){width - third, bottom, third, remaining});
    } else dashboard_run_panel(app, c, (struct tv_rect){0, bottom, width, remaining});
}

/* Cards need six rows; a shorter view keeps the run panel and replaces the
 * cards with one summary line. */
static void dashboard_wide(struct app *app, struct tv_canvas *c, int width, int height) {
    int half = width * 3 / 5, middle = dashboard_middle_height(app, height), top = 6, run_rows;
    struct work_counts n;
    if (!app->fleet && height < 32) {
        work_counts(app, &n);
        dashboard_text(c, 1, 0, width - 2, TV_STRONG, "%zu running / %zu need attention / %zu active run%s / %zu to repair",
                       n.running + n.runs_active, n.attention, n.runs_active, n.runs_active == 1 ? "" : "s", app->model.recovery_count);
        top = 2;
    } else dashboard_cards(app, c, width);
    run_rows = app->fleet ? 6 : run_panel_rows(app);
    if (!app->fleet && height - top - middle - 1 < run_rows) middle = height - top - 1 - run_rows;
    if (middle < 7) middle = 7;
    dashboard_heads(app, c, (struct tv_rect){0, top, half - 1, middle});
    dashboard_detail(app, c, (struct tv_rect){half, top, width - half, middle});
    dashboard_bottom(app, c, width, top + middle + 1, height - top - middle - 1);
}

static void dashboard_medium(struct app *app, struct tv_canvas *c, int width, int height) {
    static struct outline_row rows[OUTLINE_ROWS];
    int list_height = height / 2, rows_needed = app->fleet ? (int)app->model.head_count + 4 : (int)outline_build(app, rows, OUTLINE_ROWS) + 4;
    struct work_counts n;
    if (rows_needed < list_height) list_height = rows_needed;
    if (!app->fleet && height - 2 - list_height < run_panel_rows(app)) list_height = height - 2 - run_panel_rows(app);
    if (list_height < 7) list_height = 7;
    work_counts(app, &n);
    if (app->fleet) dashboard_text(c, 1, 0, width - 2, TV_STRONG, "%zu remote heads / liveness unobserved / %zu recovery findings",
                                  app->model.head_count, app->model.recovery_count);
    else dashboard_text(c, 1, 0, width - 2, TV_STRONG, "%zu running / %zu need attention / %zu active run%s / %zu to repair",
                        n.running + n.runs_active, n.attention, n.runs_active, n.runs_active == 1 ? "" : "s", app->model.recovery_count);
    dashboard_heads(app, c, (struct tv_rect){0, 2, width, list_height});
    if (app->fleet) dashboard_charts(app, c, (struct tv_rect){0, list_height + 3, width, height - list_height - 3});
    else dashboard_run_panel(app, c, (struct tv_rect){0, list_height + 2, width, height - list_height - 2});
}

static void dashboard_narrow(struct app *app, struct tv_canvas *c, int width, int height) {
    struct work_counts n;
    work_counts(app, &n);
    if (app->fleet) dashboard_text(c, 1, 0, width - 2, TV_STRONG, "%zu remote heads / liveness unobserved", app->model.head_count);
    else dashboard_text(c, 1, 0, width - 2, TV_STRONG, "%zu running / %zu need attention / %zu to repair", n.running, n.attention, app->model.recovery_count);
    dashboard_heads(app, c, (struct tv_rect){0, 2, width, height - 2});
}

void render_dashboard(struct app *app) {
    struct tv_canvas c;
    int width, height;
    if (!frame_content(app, &c)) return;
    width = c.width; height = c.height;
    if (width > 300) width = 300;
    if (height > 120) height = 120;
    if (width < 38 || height < 7) { linef(app, "Overview needs more space"); return; }
    if (!app->model.head_count && !(app->fleet && app->model.task_count)) { dashboard_empty(app, &c, width); app->line = app->limit; return; }
    if (app->fleet && app->model.task_count) dashboard_fleet_tasks(app, &c, (struct tv_rect){0, 0, width, height});
    else if (width >= 90 && height >= 20) dashboard_wide(app, &c, width, height);
    else if (width >= 70 && height >= 16) dashboard_medium(app, &c, width, height);
    else dashboard_narrow(app, &c, width, height);
    app->line = app->limit;
}
