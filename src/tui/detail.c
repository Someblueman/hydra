#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Details of one head. A headless head is described by the steps that run
 * on it (agent, model, tokens, live output), never as a terminal that is
 * missing. Technical details are grouped by what they identify. */

#define DETAIL_LABEL 12

/* Bytes of text that fit width columns, broken at a space when one is near. */
static size_t wrap_chunk(const char *text, size_t length, size_t width) {
    size_t cut;
    if (length <= width) return length;
    for (cut = width; cut > width / 2 && cut < length; cut--) if (text[cut] == ' ') return cut;
    return width < length ? width : length;
}

/* "Label       text" with the text wrapped under itself. */
static void labelled_at(struct app *app, int label_width, const char *label, const char *text, enum tv_style tone) {
    size_t length = strlen(text), offset = 0, width;
    int saved_x = app->content_x, saved_width = app->content_width;
    if (app->line >= app->limit) return;
    column(app, 0, label_width, TV_MUTED, label);
    app->content_x += label_width; app->content_width -= label_width;
    width = app->content_width > 0 ? (size_t)app->content_width : 1;
    do {
        char piece[1024];
        size_t chunk = wrap_chunk(text + offset, length - offset, width);
        snprintf(piece, sizeof(piece), "%.*s", (int)(chunk < sizeof(piece) ? chunk : sizeof(piece) - 1), text + offset);
        column(app, 0, app->content_width, tone, piece);
        app->line++;
        offset += chunk;
        offset += strspn(text + offset, " ");
    } while (offset < length && app->line < app->limit);
    app->content_x = saved_x; app->content_width = saved_width;
}

static void labelled(struct app *app, const char *label, const char *text, enum tv_style tone) {
    labelled_at(app, DETAIL_LABEL, label, text, tone);
}

static void detail_fleet(struct app *app, const struct head *head) {
    pair(app, "Host", head->remote_host, TV_BASE, "Desired", head->desired, TV_BASE);
    linef(app, "Project     %s", head->remote_project);
    section(app, "NEXT");
    linef(app, "a  attach to the remote terminal to see what it is doing");
    linef(app, "c  interrupt the agent; the worktree and its files are kept");
}

static void detail_group_label(const struct app *app, const struct head *head, char *group, size_t size) {
    const char *sep = dot(app);
    bool has_pr = head->pr[0] && strcmp(head->pr, "-");
    snprintf(group, size, "%s%s%s", head->group[0] && strcmp(head->group, "-") ? head->group : "none",
             has_pr ? sep : "", has_pr ? "PR " : "");
    if (has_pr) text_append(group, size, "%.40s", head->pr);
}

static const char *role_text(const char *role) {
    if (!strcmp(role, "worker")) return "worker (holds the run's result)";
    if (!strcmp(role, "verifier")) return "verifier (retired when the run finishes)";
    return "created by the run";
}

/* Two labelled values per row where they fit; one per row otherwise, so a
 * long agent or run description is never cut. */
static void detail_pair(struct app *app, const char *left_label, const char *left, enum tv_style left_tone,
                        const char *right_label, const char *right, enum tv_style right_tone) {
    if (app->content_width >= 110) { pair(app, left_label, left, left_tone, right_label, right, right_tone); return; }
    labelled(app, left_label, left, left_tone);
    labelled(app, right_label, right, right_tone);
}

static void detail_run_pair(struct app *app, size_t owner, size_t record) {
    const struct workflow_run *r = &app->workflows->runs[owner];
    char run[TEXT + 96];
    snprintf(run, sizeof(run), "%s %s%s%s", run_label_kind(r), r->name, dot(app), r->state);
    detail_pair(app, "Run", run, run_needs_attention(app, owner) ? TV_WARNING : TV_BASE, "Role",
                record != SIZE_MAX ? role_text(app->workflows->heads[record].role) : "created by the run", TV_BASE);
}

/* Reported outcome and group; an empty group is left out of a narrow view. */
static void detail_reported(struct app *app, const struct head *head, const char *group) {
    const char *reported = head->declared[0] ? head->declared : "nothing yet";
    enum tv_style tone = head->declared[0] ? TV_STRONG : TV_MUTED;
    if (app->content_width < 110 && !strcmp(group, "none")) labelled(app, "Reported", reported, tone);
    else detail_pair(app, "Reported", reported, tone, "Group", group, TV_BASE);
}

static void detail_identity(struct app *app, const struct head *head) {
    char agent[TEXT + 96], group[TEXT + 64];
    size_t record, owner = head_owner_run(app, head, &record);
    head_agent_text(app, head, agent, sizeof(agent));
    if (head_headless(head)) detail_pair(app, "Agent", agent, TV_BASE, "Terminal", "headless (no terminal)", TV_MUTED);
    else detail_pair(app, "Agent", agent, TV_BASE, "Session", status_label(head), status_tone(head));
    if (owner != SIZE_MAX) detail_run_pair(app, owner, record);
    detail_group_label(app, head, group, sizeof(group));
    detail_reported(app, head, group);
    if (!head_headless(head) && !strcmp(display_status(head), "STALE"))
        paragraph(app, "The terminal is gone but the last observation said the agent was still working. Files in the worktree are kept; check them before removing the head.", TV_WARNING);
}

static void detail_step_exec(struct app *app, const struct workflow_node *step) {
    char text[512];
    const struct workflow_exec *e = &step->exec;
    bool running = !strcmp(step->state, "running");
    labelled(app, "Executable", e->version[0] ? e->version : "unknown (the probe recorded no version)", e->version[0] ? TV_BASE : TV_MUTED);
    exec_model_text(e, running, text, sizeof(text));
    labelled(app, "Model", text, !strcmp(e->model_source, "observed") ? TV_BASE : TV_MUTED);
    exec_effort_text(e, running, text, sizeof(text));
    labelled(app, "Effort", text, !strcmp(e->effort_source, "observed") ? TV_BASE : TV_MUTED);
    if (exec_configuration_text(e, text, sizeof(text))) labelled(app, "Configured", text, TV_MUTED);
    exec_tokens_text(e, text, sizeof(text));
    labelled(app, "Tokens", text, TV_BASE);
}

/* The step that runs, or last ran, on this head. */
static void detail_step(struct app *app, const struct head *head) {
    const struct workflow_node *step = head_step(app, head);
    char title[160], duration[64];
    if (!step) return;
    snprintf(title, sizeof(title), "%s  %s%s%s, attempt %u", step->profile[0] ? "AGENT STEP" : "STEP", step->id,
             step->role[0] ? " / " : "", step->role, step->attempts);
    section(app, title);
    labelled(app, "State", step->state, !strcmp(step->state, "failed") ? TV_WARNING : !strcmp(step->state, "running") ? TV_SUCCESS : TV_BASE);
    if (step->profile[0]) detail_step_exec(app, step);
    step_duration_text(step, duration, sizeof(duration));
    labelled(app, "Duration", duration, TV_BASE);
}

/* The plan a planning head launched, instead of gate counts. */
static void detail_plan(struct app *app, const struct head *head) {
    size_t run, index = (size_t)(head - app->model.heads), shown = 0;
    char text[1024];
    for (run = app->workflows ? app->workflows->run_count : 0; run-- > 0 && shown < 2;) {
        if (run_owner_head(app, run) != index) continue;
        if (!shown++) section(app, "PLAN");
        plan_approval_text(app, run, text, sizeof(text));
        labelled(app, "Plan", text, run_needs_attention(app, run) ? TV_WARNING : TV_BASE);
        run_next_text(app, run, text, sizeof(text));
        labelled(app, "Next", text, TV_BASE);
    }
}

static void detail_changes(struct app *app, const struct head *head) {
    char changes[96];
    section(app, "CHANGES");
    if (!head->head_id[0]) { labelled(app, "Changed", "unknown (no head record; see Recovery)", TV_MUTED); return; }
    snprintf(changes, sizeof(changes), "%u %s in the worktree, not yet committed", head->diff, head->diff == 1 ? "file" : "files");
    labelled(app, "Changed", changes, TV_BASE);
    labelled(app, "Full diff", "everything since the branch base: press : and choose diff", TV_MUTED);
}

/* Gate approval requests for this head; a plan's own approval is shown under
 * PLAN and is not one of them. */
static void detail_checks(struct app *app, const struct head *head) {
    char approvals[128];
    unsigned pending;
    section(app, "CHECKS");
    if (!head->head_id[0]) { labelled_at(app, 19, "Approval requests", "unknown", TV_MUTED); return; }
    pending = head->gates > head->approved ? head->gates - head->approved : 0;
    if (!head->gates) snprintf(approvals, sizeof(approvals), "none");
    else snprintf(approvals, sizeof(approvals), "%u of %u approved%s%s", head->approved, head->gates,
                  pending ? dot(app) : "", pending ? "waiting for your decision" : "");
    labelled_at(app, 19, "Approval requests", approvals, pending ? TV_WARNING : TV_BASE);
    snprintf(approvals, sizeof(approvals), "%u exchanged with other heads", head->messages);
    labelled_at(app, 19, "Messages", approvals, TV_BASE);
}

/* What the transcript is and that keys shown inside it need live input. */
static void detail_preview_heading(struct app *app, bool headless) {
    section(app, headless ? "STEP OUTPUT" : "TERMINAL OUTPUT");
    style(app, TONE_MUTED);
    if (headless) linef(app, "Read-only view of the latest step on this head; refreshes every 2 seconds.");
    else linef(app, "Read-only transcript; a opens live input, p or Esc closes it.");
    if (app->ascii) linef(app, "ASCII fallback: the locale is not UTF-8, so other characters are approximated.");
    style(app, TONE_BASE);
}

/* The newest rows that fit, so a live stream keeps its latest events visible.
 * Long lines wrap with a continuation marker; colors and diffs are kept. */
static void detail_preview(struct app *app, const struct head *head) {
    struct transcript_layout layout;
    bool headless = head_headless(head);
    const char *text = app->preview_text[0] ? app->preview_text :
        headless ? "Reading the step output..." : "No terminal output available.";
    int top, limit = app->limit;
    detail_preview_heading(app, headless);
    top = app->line;
    memset(&layout, 0, sizeof(layout)); layout.follow = true;
    app->limit = limit - 1;
    transcript_view(app, text, strlen(text), &layout);
    app->limit = limit;
    if (!layout.hints) {
        /* Without provider key hints the note row goes back to the output. */
        app->line = top;
        memset(&layout, 0, sizeof(layout)); layout.follow = true;
        transcript_view(app, text, strlen(text), &layout);
        return;
    }
    app->line = limit - 1;
    style(app, TONE_MUTED);
    linef(app, "Key hints above belong to the agent's live input, not this view; a opens it.");
    style(app, TONE_BASE);
}

static void detail_next(struct app *app, const struct head *head) {
    const char *role = head_run_role(app, head);
    section(app, "NEXT");
    if (head_headless(head)) {
        linef(app, "p  output of the latest step (read-only)    : more actions (diff, approvals)");
        linef(app, "%s", role && !strcmp(role, "worker") ? "x  dismiss: removes the worker head, keeps its branch    d  technical details" :
              "x  remove this head (the branch is kept)    d  technical details");
        return;
    }
    linef(app, "a  talk to the agent in the workspace     p  show recent terminal output");
    linef(app, "c  coordination with other heads          x  remove this head");
    linef(app, ":  more actions (diff, switch, approvals)  d  technical details");
}

/* Technical details: one group per kind of identity, each row labelled. */
static void technical_row(struct app *app, const char *label, const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    labelled(app, label, text, TV_BASE);
}

static void technical_heading(struct app *app, const char *label, bool first) {
    if (first) { style(app, TONE_BORDER); linef(app, "%s", label); style(app, TONE_BASE); }
    else section(app, label);
}

static void technical_fleet(struct app *app, const struct head *head) {
    technical_heading(app, "IDENTITY", true);
    technical_row(app, "Host", "%s", head->remote_host);
    technical_row(app, "Branch", "%s", head->remote_branch);
    technical_row(app, "Project", "%s", head->remote_project);
    technical_row(app, "Head", "%s   instance: %s", head->head_id, head->instance);
    technical_heading(app, "LIFECYCLE", false);
    technical_row(app, "Desired", "%s", head->desired);
}

/* Four groups sized to fit an 80x24 terminal without scrolling. */
static void technical_local(struct app *app, const struct head *head) {
    technical_heading(app, "IDENTITY", true);
    technical_row(app, "Head", "%s   %s", head->head_id[0] ? head->head_id : "no head record", head->instance[0] ? head->instance : "no instance");
    technical_row(app, "Session", "%s (%s)   profile: %s", head->session, head_headless(head) ? "headless" : "interactive", head->profile);
    technical_row(app, "Group", "%s   PR: %s", head->group, head->pr);
    technical_heading(app, "LIFECYCLE", false);
    technical_row(app, "Outcome", "declared: %s   desired: %s", head->declared[0] ? head->declared : "none", head->desired);
    technical_row(app, "Observed", "%s (%s)   liveness: %s   display: %s", head->observed, head->confidence, head->liveness, display_status(head));
    technical_row(app, "Activity", "%u events  %u signals  %u messages  %u gates (%u approved)",
                  head->events, head->signals, head->messages, head->gates, head->approved);
    technical_row(app, "Coordinate", "%u claims  %u scopes  %u queued  %u resources  %u changed files",
                  head->claims, head->scopes, head->queue, head->resources, head->diff);
    technical_heading(app, "ADAPTER", false);
    technical_row(app, "Adapter", "adapter: %s   confidence: %s", head->adapter, head->adapter_confidence);
    technical_heading(app, "SOURCES", false);
    technical_row(app, "Lifecycle", "lifecycle source: %s", head->source);
    technical_row(app, "Adapter", "adapter source: %s", head->adapter_source);
    technical_row(app, "Notify", "notification source: %s (%u configured; delivery delegated)",
                  head->notification_source[0] ? head->notification_source : "unavailable", head->notifications);
}

static void render_technical(struct app *app) {
    const struct head *head = selected_head(app);
    if (head == NULL) { linef(app, "No selected head matching the current search."); return; }
    if (app->fleet) technical_fleet(app, head);
    else technical_local(app, head);
}

void render_head_detail(struct app *app) {
    const struct head *head = selected_head(app);
    if (head == NULL) { render_empty_work(app); return; }
    if (app->diagnostics) { render_technical(app); return; }
    style(app, TONE_STRONG); linef(app, "%s", head->branch); style(app, TONE_BASE);
    if (app->fleet) { detail_fleet(app, head); return; }
    detail_identity(app, head);
    /* The output view keeps the identity and gives the rest to the output. */
    if (app->preview) { detail_preview(app, head); return; }
    detail_step(app, head);
    detail_plan(app, head);
    detail_changes(app, head);
    detail_checks(app, head);
    detail_next(app, head);
}
