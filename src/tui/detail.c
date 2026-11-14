#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Details of one head. A headless head is described by the steps that run
 * on it (agent, model, tokens, live output), never as a terminal that is
 * missing. Technical details are grouped by what they identify. */

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

static void detail_run_pair(struct app *app, size_t owner, size_t record) {
    const struct workflow_run *r = &app->workflows->runs[owner];
    char run[TEXT + 96];
    snprintf(run, sizeof(run), "%s %s%s%s", run_label_kind(r), r->name, dot(app), r->state);
    labelled_pair(app, "Run", run, run_needs_attention(app, owner) ? KT_WARNING : KT_BASE, "Role",
                record != SIZE_MAX ? role_text(app->workflows->heads[record].role) : "created by the run", KT_BASE);
}

/* Reported outcome and group; an empty group is left out of a narrow view. */
static void detail_reported(struct app *app, const struct head *head, const char *group) {
    const char *reported = head->declared[0] ? head->declared : "nothing yet; declared when done: done or blocked";
    struct kt_style tone = head->declared[0] ? KT_STRONG : KT_MUTED;
    if (app->content_width < 110 && !strcmp(group, "none")) labelled(app, "Reported", reported, tone);
    else labelled_pair(app, "Reported", reported, tone, "Group", group, KT_BASE);
}

/* The terminal's state: running, unless Hydra's launcher said in it that
 * the agent left, which turns the pane into a plain shell. */
static void detail_session(struct app *app, const struct head *head, const char *agent) {
    char session[96];
    int exited = !strcmp(status_label(head), "running") ? head_agent_exit(app, head) : -1;
    if (exited >= 0) {
        snprintf(session, sizeof(session), "agent exited (status %d); the terminal is a shell", exited);
        labelled_pair(app, "Agent", agent, KT_BASE, "Session", session, KT_WARNING);
        return;
    }
    snprintf(session, sizeof(session), "%s%s", status_label(head), !strcmp(status_label(head), "running") ? " (the live terminal)" : "");
    labelled_pair(app, "Agent", agent, KT_BASE, "Session", session, status_tone(head));
}

/* A run that works on this head without having created it: its step here
 * and the unfinished steps that step waits for (the workflow link that
 * coordination showed), high on the page so a short screen keeps it. */
static void detail_workflow(struct app *app, const struct head *head) {
    char link[1024];
    if (head_step_link(app, head, link, sizeof(link)))
        labelled(app, "Workflow", link, strstr(link, "; waits for ") ? KT_WARNING : KT_BASE);
}

static void detail_identity(struct app *app, const struct head *head) {
    char agent[TEXT + 96], group[TEXT + 64];
    const char *objective = head_objective(app, head);
    size_t record, owner = head_owner_run(app, head, &record);
    head_agent_text(app, head, agent, sizeof(agent));
    if (head_headless(head)) labelled_pair(app, "Agent", agent, KT_BASE, "Terminal", "headless (no terminal)", KT_MUTED);
    else detail_session(app, head, agent);
    if (objective[0]) labelled(app, "Objective", objective, KT_BASE);
    if (owner != SIZE_MAX) detail_run_pair(app, owner, record);
    else detail_workflow(app, head);
    detail_group_label(app, head, group, sizeof(group));
    detail_reported(app, head, group);
}

/* Why the head needs the user, and the key that acts on it, right under the
 * name. Two rows at most, so the account and every action fit 80x24;
 * nothing is inferred from a missing terminal beyond what was observed. */
static void detail_attention(struct app *app, const struct head *head) {
    const struct workflow_node *step = head_step(app, head);
    char text[256];
    if (head_headless(head)) {
        if (step && (!strcmp(step->state, "failed") || !strcmp(step->state, "recovery-required"))) {
            snprintf(text, sizeof(text), "Step %s %s; its output is in the conversation and Inbox (2) lists what it decides.", step->id,
                     !strcmp(step->state, "failed") ? "failed" : "needs recovery");
            paragraph(app, text, KT_WARNING);
        }
        return;
    }
    if (!strcmp(display_status(head), "STALE"))
        paragraph(app, "The terminal is gone; the agent was last seen working. The worktree's files are kept: a restarts the terminal. Check them before removing it.", KT_WARNING);
    else if (!strcmp(display_status(head), "STOPPED") && !strcmp(head->observed, "exited"))
        paragraph(app, "The agent reported that it exited and its terminal is closed. Files in the worktree are kept: a restarts the terminal there.", KT_BASE);
    else if (!strcmp(display_status(head), "STOPPED") && !strcmp(head->observed, "failed"))
        paragraph(app, "The agent reported a failure and its terminal is closed. Files in the worktree are kept: a restarts the terminal there.", KT_WARNING);
    else if (head->head_id[0] && head->gates > head->approved) {
        unsigned pending = head->gates - head->approved;
        snprintf(text, sizeof(text), "%u approval request%s waiting for your decision; Inbox (2) approves it.", pending, pending == 1 ? " is" : "s are");
        paragraph(app, text, KT_WARNING);
    }
}

static void detail_step_exec(struct app *app, const struct workflow_node *step) {
    char text[512];
    const struct workflow_exec *e = &step->exec;
    bool running = !strcmp(step->state, "running");
    labelled(app, "Executable", e->version[0] ? e->version : "unknown (the probe recorded no version)", e->version[0] ? KT_BASE : KT_MUTED);
    exec_model_text(e, running, text, sizeof(text));
    labelled(app, "Model", text, !strcmp(e->model_source, "observed") ? KT_BASE : KT_MUTED);
    exec_effort_text(e, running, text, sizeof(text));
    labelled(app, "Effort", text, !strcmp(e->effort_source, "observed") ? KT_BASE : KT_MUTED);
    if (exec_configuration_text(e, text, sizeof(text))) labelled(app, "Configured", text, KT_MUTED);
    exec_tokens_text(e, text, sizeof(text));
    labelled(app, "Tokens", text, KT_BASE);
}

/* The step that runs, or last ran, on this head. */
static void detail_step(struct app *app, const struct head *head) {
    const struct workflow_node *step = head_step(app, head);
    char title[160], duration[64];
    if (!step) return;
    snprintf(title, sizeof(title), "%s  %s%s%s, attempt %u", step->profile[0] ? "AGENT STEP" : "STEP", step->id,
             step->role[0] ? " / " : "", step->role, step->attempts);
    section(app, title);
    labelled(app, "State", step->state, !strcmp(step->state, "failed") ? KT_WARNING : !strcmp(step->state, "running") ? KT_SUCCESS : KT_BASE);
    if (step->profile[0]) detail_step_exec(app, step);
    step_duration_text(step, duration, sizeof(duration));
    labelled(app, "Duration", duration, KT_BASE);
}

/* The plan a planning head launched, instead of gate counts. */
static void detail_plan(struct app *app, const struct head *head) {
    size_t run, index = (size_t)(head - app->model.heads), shown = 0;
    char text[1024];
    for (run = app->workflows ? app->workflows->run_count : 0; run-- > 0 && shown < 2;) {
        if (run_owner_head(app, run) != index) continue;
        if (!shown++) section(app, "PLAN");
        plan_approval_text(app, run, text, sizeof(text));
        labelled(app, "Plan", text, run_needs_attention(app, run) ? KT_WARNING : KT_BASE);
        run_next_text(app, run, text, sizeof(text));
        labelled(app, "Next", text, KT_BASE);
    }
}

static void detail_changes(struct app *app, const struct head *head) {
    char changes[96];
    section(app, "CHANGES");
    if (!head->head_id[0]) { labelled(app, "Changed", "unknown (no head record; see Inbox)", KT_MUTED); return; }
    snprintf(changes, sizeof(changes), "%u %s in the worktree, not yet committed", head->diff, head->diff == 1 ? "file" : "files");
    labelled(app, "Changed", changes, KT_BASE);
    labelled(app, "Full diff", ": diff (everything since the branch base)", KT_MUTED);
}

/* Gate approval requests for this head; a plan's own approval is shown under
 * PLAN and is not one of them. */
/* Checks: approvals, messages and coordination with other heads. A narrow
 * pane keeps the label column of the other rows. */
static void detail_checks(struct app *app, const struct head *head) {
    char text[160];
    unsigned pending;
    bool narrow = app->content_width < 60;
    int label = narrow ? DETAIL_LABEL : 19;
    const char *approvals = narrow ? "Approvals" : "Approval requests";
    section(app, "CHECKS");
    if (!head->head_id[0]) { labelled_at(app, label, approvals, "unknown", KT_MUTED); return; }
    pending = head->gates > head->approved ? head->gates - head->approved : 0;
    if (!head->gates) snprintf(text, sizeof(text), "none");
    else snprintf(text, sizeof(text), "%u of %u approved%s%s", head->approved, head->gates,
                  pending ? dot(app) : "", pending ? "waiting for your decision" : "");
    labelled_at(app, label, approvals, text, pending ? KT_WARNING : KT_BASE);
    snprintf(text, sizeof(text), "%u exchanged with other heads", head->messages);
    labelled_at(app, label, "Messages", text, KT_BASE);
    /* Coordination between heads in one row; : claims, scopes, queue and
     * resources list each. */
    snprintf(text, sizeof(text), "%u claims%s%u scopes%s%u queued%s%u resources", head->claims, dot(app), head->scopes,
             dot(app), head->queue, dot(app), head->resources);
    labelled_at(app, label, "Coordinate", text, head->claims || head->scopes || head->queue || head->resources ? KT_BASE : KT_MUTED);
    paragraph(app, "Claims are files a head means to change and scopes the areas it stays within; Hydra flags overlap "
              "between heads before they conflict.", KT_MUTED);
}

/* Where the head works: this machine and its worktree. */
static void detail_where(struct app *app, const struct head *head) {
    char where[SOURCE_TEXT + 64], path[SOURCE_TEXT];
    (void)head;
    if (!app->links || !app->links->root[0]) { labelled(app, "Where", "this machine", KT_BASE); return; }
    fit_path(app, app->links->root, app->content_width > 30 ? (size_t)app->content_width - 27U : 8U, path, sizeof(path));
    snprintf(where, sizeof(where), "this machine%s%s", dot(app), path);
    labelled(app, "Where", where, KT_BASE);
}

/* Technical details: one group per kind of identity, each row labelled. */
static void technical_row(struct app *app, const char *label, const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    labelled(app, label, text, KT_BASE);
}

static void technical_heading(struct app *app, const char *label, bool first) {
    if (first) { style(app, TONE_BORDER); linef(app, "%s", label); style(app, TONE_BASE); }
    else section(app, label);
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
    if (head_remote(head)) remote_technical(app, head);
    else technical_local(app, head);
}

void render_head_detail(struct app *app) {
    const struct head *head = selected_head(app);
    const struct task_observation *task = selected_task(app);
    if (task) {
        char title[TEXT + 160];
        snprintf(title, sizeof(title), "task %s on %s", task->task_id, task->host);
        paragraph(app, title, KT_STRONG);
        task_detail(app, task);
        return;
    }
    if (head == NULL) { paragraph(app, app->run_row ? "A run is selected; its steps are on the Run page." : "No head selected.", KT_MUTED); return; }
    if (app->diagnostics) { render_technical(app); return; }
    /* The list's selection and the conversation name the head; the page
     * starts with what needs the user, then the facts. */
    if (head_remote(head)) { remote_detail(app, head); return; }
    detail_attention(app, head);
    detail_identity(app, head);
    detail_where(app, head);
    detail_step(app, head);
    detail_plan(app, head);
    detail_changes(app, head);
    detail_checks(app, head);
}
