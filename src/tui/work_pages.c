#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "attention.h"
#include "inbox.h"
#include "plan_draft.h"
#include "plan_summary.h"
/* The side pane's pages: Details (always), Plan (a head with a plan) and Run
 * (a run, or a head a run created). Details and Plan are text that scrolls;
 * Run is the run's graph, its steps with the selected step's evidence, and
 * what comes next. Every action a page offers is a key in the footer and
 * goes through plan_actions.c or the run controls. */

#define DANGER KT_STYLE(KT_COLOR_DANGER, KT_COLOR_SURFACE, 0)

/* Draws with the line-based renderers into r, scrolled by *scroll rows; a
 * scroll past the end comes back to the last full page. Returns the rows
 * the text takes. */
static size_t page_lines(struct app *app, struct kt_rect r, size_t *scroll, void (*draw)(struct app *)) {
    struct kt_canvas saved = app->frame, view;
    int x = app->content_x, width = app->content_width, line = app->line, limit = app->limit, tone = app->tone;
    size_t total;
    if (r.width < 1 || r.height < 1 || !kt_canvas_view(&view, &saved, r)) return 0;
    app->frame = view;
    app->content_x = 0; app->content_width = r.width;
    app->line = -(int)(*scroll > 100000U ? 100000U : *scroll); app->limit = r.height + 100000;
    draw(app);
    total = (size_t)(app->line + (int)*scroll);
    if (*scroll + (size_t)r.height > total) *scroll = total > (size_t)r.height ? total - (size_t)r.height : 0U;
    app->frame = saved;
    app->content_x = x; app->content_width = width; app->line = line; app->limit = limit; app->tone = tone;
    return total;
}

/* ---- Details -------------------------------------------------------------- */

static void details_draw(struct app *app) { render_head_detail(app); }

/* ---- Run ------------------------------------------------------------------ */

static size_t run_step_count(const struct app *app, size_t run, size_t *indices) {
    return app->workflows ? workflow_nodes(app->workflows, run, indices) : 0;
}

/* "● running · step 2 of 4 · 6m10s" */
static void run_lead(struct app *app, struct kt_rect r, size_t run, const size_t *indices, size_t n) {
    const struct workflow_run *wr = &app->workflows->runs[run];
    struct kt_style tone;
    const char *mark = work_state_glyph(app, wr->state, &tone);
    char text[TEXT + 96];
    size_t i, at = 0;
    for (i = 0; i < n; i++) if (!strcmp(app->workflows->nodes[indices[i]].state, "running") && !at) at = i + 1;
    /* The state first, so a narrow pane keeps it; the run's name last. */
    snprintf(text, sizeof(text), "%s", wr->state);
    if (at) text_append(text, sizeof(text), "%sstep %zu of %zu", dot(app), at, n);
    text_append(text, sizeof(text), "%s%s %s", dot(app), run_label_kind(wr), wr->name);
    kt_text(&app->frame, (struct kt_rect){r.x, r.y, 1, 1}, mark, tone);
    kt_text(&app->frame, (struct kt_rect){r.x + 2, r.y, r.width - 2, 1}, text, KT_STRONG);
}

static void run_graph(struct app *app, struct kt_rect r, size_t run, const size_t *indices, size_t n) {
    struct kt_graph_layout layout;
    struct kt_node nodes[KT_GRAPH_MAX_NODES];
    struct kt_edge edges[KT_GRAPH_MAX_EDGES];
    struct kt_canvas c;
    size_t i, count;
    if (!n || r.height < 4 || !workflow_edges(app->workflows, indices, n, edges, &count) || !kt_graph_layout(edges, count, n, &layout)) return;
    for (i = 0; i < n; i++) {
        const struct workflow_node *node = &app->workflows->nodes[indices[i]];
        nodes[i] = (struct kt_node){node->id, node->state, !strcmp(node->state, "failed") || !strcmp(node->state, "recovery-required") ?
            KT_WARNING : !strcmp(node->state, "succeeded") ? KT_SUCCESS : KT_BASE};
    }
    if (app->graph_follow) {
        app->graph_x = layout.x[app->workflow_node] - (r.width - 22) / 2;
        app->graph_y = layout.y[app->workflow_node] - (r.height - 4) / 2;
        if (app->graph_x < 0) app->graph_x = 0;
        if (app->graph_y < 0) app->graph_y = 0;
    }
    if (kt_canvas_view(&c, &app->frame, r)) kt_graph(&c, (struct kt_rect){0, 0, r.width, r.height}, nodes, n, edges, count, &layout,
                                                       app->graph_x, app->graph_y, app->workflow_node);
    (void)run;
}

/* STEP STATE TIME, the selected step barred. */
static int run_steps(struct app *app, struct kt_rect r, const size_t *indices, size_t n, bool focused) {
    /* STATE fits "waiting-approval" where the pane allows; a longer state
     * is cut with an ellipsis, a column apart from TIME. */
    int y = r.y, name = r.width >= 34 ? 14 : 10, state = r.width >= 46 ? 20 : 14, time_x = 1 + name + state;
    size_t i;
    kt_text(&app->frame, (struct kt_rect){r.x + 1, y, name, 1}, "STEP", KT_MUTED);
    kt_text(&app->frame, (struct kt_rect){r.x + 1 + name, y, state, 1}, "STATE", KT_MUTED);
    kt_text(&app->frame, (struct kt_rect){r.x + time_x, y++, r.width - time_x, 1}, "TIME", KT_MUTED);
    for (i = 0; i < n && y < r.y + r.height; i++, y++) {
        const struct workflow_node *node = &app->workflows->nodes[indices[i]];
        char cell[96], time[48];
        struct kt_style tone;
        const char *mark = work_state_glyph(app, node->state, &tone);
        bool selected = i == app->workflow_node;
        if (selected) kt_text(&app->frame, (struct kt_rect){r.x, y, 1, 1}, app->ascii ? ">" : "\xe2\x96\x8c",
                              focused ? KT_FOCUS : KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SURFACE, 0));
        fit_text(app, node->id, (size_t)name - 1U, cell, sizeof(cell));
        kt_text(&app->frame, (struct kt_rect){r.x + 1, y, name, 1}, cell, selected ? KT_STRONG : KT_BASE);
        kt_text(&app->frame, (struct kt_rect){r.x + 1 + name, y, 2, 1}, mark, tone);
        fit_text(app, node->state, (size_t)state - 3U, cell, sizeof(cell));
        kt_text(&app->frame, (struct kt_rect){r.x + 3 + name, y, state - 2, 1}, cell, tone);
        step_duration_text(node, time, sizeof(time));
        kt_text(&app->frame, (struct kt_rect){r.x + time_x, y, r.width - time_x, 1}, time, KT_BASE);
    }
    return y;
}

static void run_heads(struct app *app, size_t run);

/* The selected step: the head it ran on, its agent and tokens, where its
 * output is, then the run's next step and the recorded evidence. */
static void step_evidence_draw(struct app *app) {
    size_t indices[KT_GRAPH_MAX_NODES], n, run = work_run(app);
    const struct workflow_node *node;
    char text[1536];
    n = run_step_count(app, run, indices);
    if (!n || app->workflow_node >= n) return;
    node = &app->workflows->nodes[indices[app->workflow_node]];
    labelled_at(app, 8, "Head", node->head[0] ? node->head : "none", KT_BASE);
    snprintf(text, sizeof(text), "%s%s%s, attempt %u", node->kind, node->role[0] ? "/" : "", node->role, node->attempts);
    labelled_at(app, 8, "Kind", text, KT_BASE);
    if (node->exec.present) {
        exec_model_text(&node->exec, !strcmp(node->state, "running"), text, sizeof(text));
        labelled_at(app, 8, "Agent", node->profile[0] ? node->profile : "command", KT_BASE);
        labelled_at(app, 8, "Model", text, KT_MUTED);
        exec_tokens_text(&node->exec, text, sizeof(text));
        labelled_at(app, 8, "Tokens", text, KT_BASE);
    }
    labelled_at(app, 8, "Needs", node->needs[0] ? node->needs : "nothing", KT_MUTED);
    run_next_text(app, run, text, sizeof(text));
    linef(app, "");
    labelled_at(app, 8, "Next", text, run_needs_attention(app, run) ? KT_WARNING : KT_BASE);
    run_heads(app, run);
}

/* The heads the run created, one a row: "kill-dry-run-check (retired)"
 * says why one that is not listed in Work is gone. */
static void run_heads(struct app *app, size_t run) {
    const struct workflow_model *m = app->workflows;
    char text[TEXT + 64];
    size_t i, shown = 0;
    for (i = 0; i < m->head_count; i++) {
        const struct workflow_head *h = &m->heads[i];
        if (h->run != run) continue;
        snprintf(text, sizeof(text), "%s (%s)", h->branch, head_for_branch(app, h->branch) ? h->role : run_head_absence(app, h));
        labelled_at(app, 8, shown++ ? "" : "Heads", text, KT_BASE);
    }
}

/* Where the recorded evidence goes under a summary of used rows: right
 * under it, or (at least five rows) at the bottom of r when the summary
 * leaves less. Returns its rows, 0 when there is no room or no record. */
static int evidence_place(const struct app *app, struct kt_rect r, size_t used, int *top) {
    int rows;
    *top = r.y + 2 + (int)(used < (size_t)r.height ? used : (size_t)r.height);
    rows = r.y + r.height - *top;
    if (rows < 5 && r.height > 12) { *top = r.y + r.height - 5; rows = 5; }
    return app->evidence && app->evidence->run[0] && rows >= 5 ? rows : 0;
}

/* The selected step's summary, then the recorded evidence in the rows left
 * (at least four). The two read as one text: PgDn first brings the rest of
 * a summary that does not fit into view (its Next row among them), then
 * pages through the evidence; PgUp goes back the same way. */
static void run_evidence(struct app *app, struct kt_rect r, const size_t *indices, size_t n) {
    size_t *offset = &app->workspace->scroll[WORK_RUN], zero = 0, used, hidden, summary, record;
    char title[96];
    struct kt_canvas c;
    int top, rows, area;
    app->workspace->evidence_rows = 0;
    if (r.height < 2) return;
    snprintf(title, sizeof(title), "%.60s", app->workflow_node < n ? app->workflows->nodes[indices[app->workflow_node]].id : "step");
    used = page_lines(app, (struct kt_rect){r.x, r.y + 1, r.width - 1, r.height - 1}, &zero, step_evidence_draw);
    rows = evidence_place(app, r, used, &top);
    area = rows ? top - r.y - 1 : r.height - 1;
    hidden = used > (size_t)area ? used - (size_t)area : 0;
    summary = *offset < hidden ? *offset : hidden;
    work_rule(app, r, title, false, summary < hidden ? "pgdn more" : "evidence");
    if (hidden) {
        kt_fill(&app->frame, (struct kt_rect){r.x, r.y + 1, r.width - 1, r.height - 1}, KT_BASE);
        (void)page_lines(app, (struct kt_rect){r.x, r.y + 1, r.width - 1, area}, &summary, step_evidence_draw);
    }
    app->workspace->evidence_rows = rows ? rows - 1 : area;
    if (!rows || !kt_canvas_view(&c, &app->frame, (struct kt_rect){r.x, top, r.width - 1, rows})) { *offset = summary; return; }
    record = *offset - summary;
    kt_fill(&app->frame, (struct kt_rect){r.x, top, r.width - 1, rows}, KT_BASE);
    native_workspace_evidence_text(app, &c, &record);
    *offset = summary + record;
}

static void run_page(struct app *app, struct kt_rect r, bool focused) {
    size_t indices[KT_GRAPH_MAX_NODES], n, run = work_run(app);
    int y, graph;
    if (run == SIZE_MAX) { statistics_wrap(&app->frame, r.x, r.y, r.width, r.height, KT_MUTED, "No run selected."); return; }
    if (app->workflow_run != run) { app->workflow_run = run; app->workflow_node = 0; }
    n = run_step_count(app, run, indices);
    if (n > KT_GRAPH_MAX_NODES) n = KT_GRAPH_MAX_NODES;
    if (app->workflow_node >= n) app->workflow_node = 0;
    run_lead(app, r, run, indices, n);
    graph = r.height >= 24 ? 9 : r.height >= 16 ? 5 : 0;
    if (graph) run_graph(app, (struct kt_rect){r.x, r.y + 2, r.width - 1, graph}, run, indices, n);
    y = run_steps(app, (struct kt_rect){r.x, r.y + 2 + graph + (graph ? 1 : 0), r.width - 1, (int)n + 1}, indices, n, focused);
    if (y + 2 < r.y + r.height) run_evidence(app, (struct kt_rect){r.x, y + 1, r.width, r.y + r.height - y - 1}, indices, n);
    if (app->workflow_stale) kt_text(&app->frame, (struct kt_rect){r.x, r.y + 1, r.width - 1, 1}, "last good snapshot (stale)", KT_WARNING);
}

/* ---- Plan ------------------------------------------------------------------ */

/* A published plan for the head that is not on screen yet (Plan ready). */
bool work_head_plan_ready(const struct app *app, const struct head *h) {
    size_t i;
    if (!h || !app->inbox || app->fleet) return false;
    if (app->plan && !strcmp(app->plan->proposal_head, h->branch) && app->plan->state != PLAN_RETURNED) return false;
    for (i = 0; i < app->inbox->plan_count; i++) if (!strcmp(app->inbox->plans[i].branch, h->branch)) return true;
    return false;
}

/* The worker of the latest finished run the selected head launched, whose
 * verified result can land; false when there is none. */
bool work_land_worker(const struct app *app, char *out, size_t size) {
    const struct head *h = selected_head((struct app *)app);
    const struct workflow_model *m = app->workflows;
    size_t i, run = SIZE_MAX, index;
    if (!h || !m || app->fleet) return false;
    index = (size_t)(h - app->model.heads);
    for (i = 0; i < m->run_count; i++)
        if (run_owner_head(app, i) == index && !strcmp(m->runs[i].state, "succeeded") &&
            (run == SIZE_MAX || strcmp(m->runs[i].created, m->runs[run].created) > 0)) run = i;
    for (i = 0; run != SIZE_MAX && i < m->head_count; i++)
        if (m->heads[i].run == run && !strcmp(m->heads[i].role, "worker") && head_for_branch(app, m->heads[i].branch)) {
            copy_text(out, size, m->heads[i].branch);
            return true;
        }
    return false;
}

/* The run the head's plan launched most recently, for review and status. */
const char *work_plan_run(const struct app *app) {
    const struct head *h = selected_head((struct app *)app);
    const struct workflow_model *m = app->workflows;
    size_t i, run = SIZE_MAX, index;
    if (!h || !m) return NULL;
    if (app->plan && app->plan->launched_run[0]) return app->plan->launched_run;
    index = (size_t)(h - app->model.heads);
    for (i = 0; i < m->run_count; i++)
        if (run_owner_head(app, i) == index && (run == SIZE_MAX || strcmp(m->runs[i].created, m->runs[run].created) > 0)) run = i;
    return run == SIZE_MAX ? NULL : m->runs[run].id;
}

/* "READY · revision 3 · awaiting you" */
static void plan_state_text(const struct app *app, const struct native_plan *p, char *out, size_t size) {
    static const char *states[][2] = {{"DRAFT", "not validated yet"}, {"VALIDATING", ""}, {"INVALID", "F sends the problems back"},
        {"READY", "awaiting you"}, {"VIEW UNAVAILABLE", ""}, {"RETURNED", "changes requested"}};
    const char *word = states[p->state][0], *why = states[p->state][1];
    if (p->state == PLAN_READY && !strcmp(p->digest, p->launched_digest)) {
        word = p->launched_run[0] ? "RUN RECORDED" : native_plan_launch_failed(p) ? "LAUNCH FAILED" : "LAUNCH REQUESTED";
        why = p->launched_run[0] ? "the Run page follows it" : native_plan_launch_failed(p) ? "nothing ran; E launches again" :
            "awaiting run receipt";
    }
    snprintf(out, size, "%s%srevision %u%s%s", word, dot(app), p->revision, why[0] ? dot(app) : "", why);
}

/* What the plan runs, under which limits and what it writes, from the
 * compiled preview; a draft says it is shown once validated. */
static void plan_scope(struct app *app, const struct native_plan *p) {
    struct plan_summary s;
    char text[512];
    if (p->state == PLAN_DRAFT || !p->text || !plan_summary_parse(p->text, p->text_length, &s)) {
        labelled_at(app, 8, "Scope", "shown once Hydra validates this revision (E approves and validates it)", KT_MUTED);
        return;
    }
    snprintf(text, sizeof(text), "%zu steps%s%u head%s%sat most %u min", p->graph.node_count, dot(app), s.heads, s.heads == 1 ? "" : "s",
             dot(app), s.minutes);
    labelled_at(app, 8, "Scope", text, KT_BASE);
    labelled_at(app, 8, "Writes", s.writes[0] ? s.writes : "inside the heads it spawns only", KT_BASE);
    labelled_at(app, 8, "Tools", s.tools[0] ? s.tools : "unknown", KT_BASE);
}

static void plan_steps(struct app *app, const struct native_plan *p) {
    size_t i;
    if (!p->graph.node_count) return;
    section(app, "Steps");
    for (i = 0; i < p->graph.node_count; i++) {
        char text[TEXT + 64];
        const struct workflow_node *n = &p->graph.nodes[i];
        snprintf(text, sizeof(text), "%zu %-12.12s %s%s%s", i + 1, n->id, n->kind, n->role[0] ? dot(app) : "", n->role);
        linef(app, "%s", text);
    }
}

/* The keys that act on the plan, in words, as the mockup's button row. */
static void plan_actions(struct app *app, const struct native_plan *p) {
    char text[256] = "", worker[TEXT];
    const char *more = app->ascii ? "..." : "\xe2\x80\xa6";
    bool ready = work_head_plan_ready(app, selected_head(app));
    if (ready || (p && (p->state == PLAN_DRAFT || p->state == PLAN_READY))) text_append(text, sizeof(text), "E approve%s  ", more);
    if (ready || (p && p->state != PLAN_RETURNED)) text_append(text, sizeof(text), "F request changes%s  ", more);
    if (work_land_worker(app, worker, sizeof(worker))) text_append(text, sizeof(text), "L land worker%s  ", more);
    if (work_plan_run(app)) text_append(text, sizeof(text), "r result review  s run status");
    if (!text[0]) return;
    linef(app, "");
    paragraph(app, text, KT_STRONG);
}

static bool plan_draft_account(struct app *app, const struct native_plan *p);

/* The plan's state, objective, scope and steps; true when a draft was told
 * in words, so the document under it is not needed. */
static bool plan_loaded(struct app *app, const struct native_plan *p) {
    char text[TEXT + 64];
    plan_state_text(app, p, text, sizeof(text));
    paragraph(app, text, p->state == PLAN_INVALID ? KT_WARNING : KT_STRONG);
    if (p->state == PLAN_READY && p->launch_failure[0] && native_plan_launch_failed(p)) paragraph(app, p->launch_failure, KT_WARNING);
    if (p->notice[0]) paragraph(app, p->notice, KT_WARNING);
    if (p->objective[0]) { section(app, "Objective"); paragraph(app, p->objective, KT_BASE); }
    linef(app, "");
    if (p->state == PLAN_DRAFT && plan_draft_account(app, p)) { plan_actions(app, p); return true; }
    plan_scope(app, p);
    plan_steps(app, p);
    plan_actions(app, p);
    return false;
}

/* A draft not validated yet, in words: its limits and what each step does,
 * read from the JSON the agent published. False when it cannot be read,
 * and the page shows the draft as published instead. */
static bool plan_draft_account(struct app *app, const struct native_plan *p) {
    struct plan_draft *d = p->source_bytes ? malloc(sizeof(*d)) : NULL;
    char text[1200];
    size_t i;
    if (!d || !plan_draft_parse(p->source_bytes, p->source_length, d)) { free(d); return false; }
    snprintf(text, sizeof(text), "%zu step%s%s%u head%s", d->steps_total, d->steps_total == 1 ? "" : "s", dot(app), d->heads,
             d->heads == 1 ? "" : "s");
    if (d->minutes) text_append(text, sizeof(text), "%sat most %u min", dot(app), d->minutes);
    labelled_at(app, 8, "Scope", text, KT_BASE);
    labelled_at(app, 8, "Writes", d->writes[0] ? d->writes : "inside the heads it spawns only", KT_BASE);
    if (d->tools[0]) labelled_at(app, 8, "Tools", d->tools, KT_BASE);
    section(app, "Steps");
    for (i = 0; i < d->step_count; i++) {
        const struct plan_draft_step *step = &d->steps[i];
        snprintf(text, sizeof(text), "%zu %-12.12s %s%s%s", i + 1, step->id, step->kind, step->role[0] ? dot(app) : "", step->role);
        linef(app, "%s", text);
        if (step->what[0]) labelled_at(app, 2, "", step->what, KT_MUTED);
    }
    if (d->steps_total > d->step_count) linef(app, "%zu more steps", d->steps_total - d->step_count);
    linef(app, "");
    paragraph(app, "Hydra has not checked this draft yet. Approving it (E) validates exactly this revision first; "
              "nothing runs if validation finds a problem, and the problems show here.", KT_MUTED);
    free(d);
    return true;
}

/* The plan document itself, below the summary: the compiled preview, or a
 * draft as published when it cannot be read as a plan. */
static void plan_document(struct app *app, const struct native_plan *p) {
    const char *text = p->state == PLAN_DRAFT && p->source_bytes ? p->source_bytes : p->text;
    size_t length = p->state == PLAN_DRAFT && p->source_bytes ? p->source_length : p->text_length;
    size_t offset = 0;
    if (!text || !length) return;
    section(app, p->state == PLAN_DRAFT ? "Draft as published" : "Technical (compiled preview)");
    while (offset < length) {
        const char *end = memchr(text + offset, '\n', length - offset);
        size_t line = end ? (size_t)(end - text - offset) : length - offset;
        size_t skip = 0;
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "%.*s", (int)(line < sizeof(buffer) ? line : sizeof(buffer) - 1U), text + offset);
        paragraph_skip(app, buffer[0] ? buffer : " ", KT_BASE, &skip);
        offset += line + 1;
    }
}

static void plan_published(struct app *app, const struct head *h) {
    char text[TEXT + 160];
    snprintf(text, sizeof(text), "Plan ready%spublished by %s's agent", dot(app), h->branch);
    paragraph(app, text, KT_WARNING);
    linef(app, "");
    paragraph(app, "Nothing runs until you approve it. E reads this revision under the guided policy and approves exactly it "
              "(one confirmation); F sends it back to the agent with your feedback.", KT_BASE);
    plan_actions(app, NULL);
}

static void plan_launched_runs(struct app *app, const struct head *h) {
    size_t run, index = (size_t)(h - app->model.heads), shown = 0;
    char text[1024];
    for (run = app->workflows ? app->workflows->run_count : 0; run-- > 0 && shown < 3;) {
        if (run_owner_head(app, run) != index) continue;
        if (!shown++) section(app, "Runs it launched");
        plan_approval_text(app, run, text, sizeof(text));
        labelled_at(app, 8, "Plan", text, run_needs_attention(app, run) ? KT_WARNING : KT_BASE);
        run_next_text(app, run, text, sizeof(text));
        labelled_at(app, 8, "Next", text, KT_BASE);
    }
}

static void plan_draw(struct app *app) {
    const struct head *h = selected_head(app);
    const struct native_plan *p = app->plan;
    bool mine = p && h && (!p->proposal_head[0] || !strcmp(p->proposal_head, h->branch)), told = false;
    /* A plan imported from files, with no head selected. */
    if (!h && p && !p->proposal_head[0]) { if (!plan_loaded(app, p)) plan_document(app, p); return; }
    if (!h) return;
    if (mine) told = plan_loaded(app, p);
    else if (work_head_plan_ready(app, h)) plan_published(app, h);
    if (!mine && !work_head_plan_ready(app, h)) { paragraph(app, "No plan on screen for this head.", KT_MUTED); plan_actions(app, NULL); }
    plan_launched_runs(app, h);
    if (mine && !told) plan_document(app, p);
}

static void plan_page(struct app *app, struct kt_rect r) {
    const struct native_plan *p = app->plan;
    const struct head *h = selected_head(app);
    int graph = 0;
    struct kt_canvas c;
    if (p && (h ? !p->proposal_head[0] || !strcmp(p->proposal_head, h->branch) : !p->proposal_head[0]) && p->graph.run_count &&
        r.height >= 20) graph = 6;
    page_lines(app, (struct kt_rect){r.x, r.y, r.width, r.height - graph}, &app->workspace->scroll[WORK_PLAN], plan_draw);
    if (graph && kt_canvas_view(&c, &app->frame, (struct kt_rect){r.x, r.y + r.height - graph, r.width, graph}))
        native_workspace_graph(app, &c, true);
}

/* ---- dispatch -------------------------------------------------------------- */

void work_page_draw(struct app *app, int page, struct kt_rect r, bool focused) {
    if (page == WORK_RUN) run_page(app, r, focused);
    else if (page == WORK_PLAN) plan_page(app, r);
    else page_lines(app, r, &app->workspace->scroll[WORK_DETAILS], details_draw);
}

/* Whether the evidence of the selected run step is wanted: the Run page is on screen. */
bool work_run_shown(const struct app *app) {
    return app->view == 7 && app->workspace && work_page_shown(app, !(app->workspace->merged || app->workspace->compact)) == WORK_RUN &&
        !(app->workspace->compact && app->workspace->layout.focus == WORK_PANE_LIST);
}
