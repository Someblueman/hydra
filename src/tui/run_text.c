#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Plain-language text for runs, their steps and the heads they created.
 * Unknown stays unknown: a missing number is never shown as zero, and a model
 * named by configuration is labelled as configuration, not observation. */

void format_duration(long long seconds, char *out, size_t size) {
    if (seconds < 0) snprintf(out, size, "unknown");
    else if (seconds < 60) snprintf(out, size, "%llds", seconds);
    else if (seconds < 3600) snprintf(out, size, "%lldm%02llds", seconds / 60, seconds % 60);
    else snprintf(out, size, "%lldh%02lldm", seconds / 3600, (seconds % 3600) / 60);
}

void format_tokens(long long value, char *out, size_t size) {
    char digits[32];
    size_t length, i, used = 0;
    if (value < 0) { snprintf(out, size, "unknown"); return; }
    snprintf(digits, sizeof(digits), "%lld", value);
    length = strlen(digits);
    for (i = 0; i < length && used + 2 < size; i++) {
        if (i && (length - i) % 3 == 0) out[used++] = ',';
        out[used++] = digits[i];
    }
    out[used] = '\0';
}

const char *run_label_kind(const struct workflow_run *run) {
    return !strcmp(run->kind, "plan") ? "plan run" : "run";
}

static size_t run_head_total(const struct workflow_model *m, size_t run) {
    size_t i, count = 0;
    for (i = 0; i < m->head_count; i++) if (m->heads[i].run == run) count++;
    return count;
}

/* "plan run kill-dry-run · succeeded · 2 heads" */
void run_summary_text(const struct app *app, size_t run, char *out, size_t size) {
    const struct workflow_model *m = app->workflows;
    const struct workflow_run *r;
    size_t heads;
    if (!m || run >= m->run_count) { snprintf(out, size, "run unavailable"); return; }
    r = &m->runs[run]; heads = run_head_total(m, run);
    snprintf(out, size, "%s %s%s%s", run_label_kind(r), r->name, dot(app), r->state);
    if (heads) text_append(out, size, "%s%zu head%s", dot(app), heads, heads == 1 ? "" : "s");
}

static const struct workflow_node *run_step_in(const struct workflow_model *m, size_t run, const char *state) {
    size_t i;
    for (i = 0; i < m->node_count; i++) if (m->nodes[i].run == run && !strcmp(m->nodes[i].state, state)) return &m->nodes[i];
    return NULL;
}

static const struct workflow_head *run_head_of(const struct app *app, size_t run, const char *role, bool present) {
    const struct workflow_model *m = app->workflows;
    size_t i;
    for (i = 0; i < m->head_count; i++) {
        const struct workflow_head *h = &m->heads[i];
        if (h->run == run && !strcmp(h->role, role) && (head_for_branch(app, h->branch) != NULL) == present) return h;
    }
    return NULL;
}

static void run_result_next(const struct app *app, size_t run, char *out, size_t size) {
    const struct workflow_head *worker = run_head_of(app, run, "worker", true);
    if (!worker) worker = run_head_of(app, run, "run", true);
    if (!worker) {
        snprintf(out, size, "%s; the heads it created are no longer active.",
                 !strcmp(app->workflows->runs[run].state, "succeeded") ? "Result delivered" : "Run finished");
        return;
    }
    snprintf(out, size, "Worker branch %.120s holds the result: review (: diff)%sland (hydra land %.120s)%sdismiss (x removes the worker head, keeps the branch)",
             worker->branch, dot(app), worker->branch, dot(app));
}

static void run_retirement_note(const struct app *app, size_t run, char *out, size_t size) {
    const struct workflow_model *m = app->workflows;
    size_t i;
    for (i = 0; i < m->head_count; i++) {
        const struct workflow_head *h = &m->heads[i];
        if (h->run != run || !h->detail[0] || !strcmp(h->retirement, "retired")) continue;
        text_append(out, size, "%s%s", dot(app), h->detail);
        return;
    }
}

static void run_active_next(const struct app *app, size_t run, char *out, size_t size) {
    const struct workflow_model *m = app->workflows;
    const struct workflow_node *n = run_step_in(m, run, "waiting-approval");
    if (n) { snprintf(out, size, "Needs your decision: step %s waits for approval (C monitor, then Y approve or N reject)", n->id); return; }
    n = run_step_in(m, run, "running");
    if (!n) { snprintf(out, size, "Waiting for the next step to become ready"); return; }
    snprintf(out, size, "Running step %s", n->id);
    if (n->head[0]) text_append(out, size, " on %s", n->head);
    if (n->profile[0]) text_append(out, size, " (%s)", n->profile);
    text_append(out, size, "; p on that head shows its output");
}

static const struct workflow_node *run_failed_step(const struct workflow_model *m, size_t run) {
    const struct workflow_node *failed = run_step_in(m, run, "failed");
    return failed ? failed : run_step_in(m, run, "recovery-required");
}

static bool run_state_active(const char *state) {
    return !strcmp(state, "running") || !strcmp(state, "queued") || !strcmp(state, "waiting-approval");
}

static void run_finished_next(const struct app *app, size_t run, char *out, size_t size) {
    const struct workflow_run *r = &app->workflows->runs[run];
    const struct workflow_node *failed = run_failed_step(app->workflows, run);
    if (!strcmp(r->state, "stale")) { snprintf(out, size, "The run's owner stopped reporting; C monitor, then R resume (hydra workflow resume %s)", r->id); return; }
    if (!strcmp(r->state, "recovery-required")) {
        snprintf(out, size, "Needs recovery%s%s: inspect it, then hydra workflow resume %s", failed ? " at step " : "", failed ? failed->id : "", r->id);
        return;
    }
    out[0] = '\0';
    if (failed) snprintf(out, size, "Step %s failed%s%s; p on that head shows its output. ", failed->id, failed->head[0] ? " on " : "", failed->head);
    run_result_next(app, run, out + strlen(out), size - strlen(out));
}

/* What happens next, or what the run needs from the user. */
void run_next_text(const struct app *app, size_t run, char *out, size_t size) {
    const struct workflow_model *m = app->workflows;
    if (!m || run >= m->run_count) { snprintf(out, size, "No run selected"); return; }
    if (run_state_active(m->runs[run].state)) run_active_next(app, run, out, size);
    else run_finished_next(app, run, out, size);
    run_retirement_note(app, run, out, size);
}

static size_t latest_run_where(const struct app *app, size_t owner, bool active) {
    const struct workflow_model *m = app->workflows;
    size_t i, best = SIZE_MAX;
    for (i = 0; m && i < m->run_count; i++) {
        const char *state = m->runs[i].state;
        if (owner != SIZE_MAX && run_owner_head(app, i) != owner) continue;
        if (active && strcmp(state, "running") && strcmp(state, "waiting-approval")) continue;
        if (best == SIZE_MAX || strcmp(m->runs[i].created, m->runs[best].created) > 0) best = i;
    }
    return best;
}

/* The run Overview centres on: the selected run row, the run of the selected
 * head, the latest run the selected head launched, else the active run. */
size_t overview_run(struct app *app) {
    const struct head *h = selected_head(app);
    size_t run;
    if (!app->workflows || app->fleet || !app->workflows->run_count) return SIZE_MAX;
    if (app->run_row && app->workflow_run < app->workflows->run_count) return app->workflow_run;
    if (h && (run = head_owner_run(app, h, NULL)) != SIZE_MAX) return run;
    if (h && (run = latest_run_where(app, (size_t)(h - app->model.heads), false)) != SIZE_MAX) return run;
    run = latest_run_where(app, SIZE_MAX, true);
    return run != SIZE_MAX ? run : latest_run_where(app, SIZE_MAX, false);
}

static const char *step_state_word(const char *state) {
    if (!strcmp(state, "succeeded")) return "done";
    if (!strcmp(state, "failed")) return "step failed";
    if (!strcmp(state, "waiting-approval")) return "awaiting you";
    if (!strcmp(state, "recovery-required")) return "needs recovery";
    return state;
}

/* "codex · step implement · running" for a headless head; the profile name
 * for an interactive agent. */
void head_agent_text(const struct app *app, const struct head *h, char *out, size_t size) {
    const struct workflow_node *step;
    if (app->fleet || !head_headless(h)) { snprintf(out, size, "%s", native_agent_name(h)); return; }
    step = head_step(app, h);
    if (!step) { snprintf(out, size, "no agent step yet"); return; }
    snprintf(out, size, "%s%sstep %s%s%s", step->profile[0] ? step->profile : "command", dot(app), step->id, dot(app), step->state);
}

const char *head_list_status(const struct app *app, const struct head *h) {
    const struct workflow_node *step;
    if (app->fleet || !head_headless(h)) return app->fleet ? h->desired : status_label(h);
    step = head_step(app, h);
    if (!step) return "headless";
    if (!strcmp(step->state, "running")) return "running";
    return step_state_word(step->state);
}

enum tv_style head_list_tone(const struct app *app, const struct head *h) {
    const struct workflow_node *step;
    if (app->fleet) return TV_BASE;
    if (!head_headless(h)) return status_tone(h);
    step = head_step(app, h);
    if (step && !strcmp(step->state, "running")) return TV_SUCCESS;
    return head_needs_attention(app, h) ? TV_WARNING : TV_BASE;
}

const char *head_run_role(const struct app *app, const struct head *h) {
    size_t record;
    if (head_owner_run(app, h, &record) == SIZE_MAX || record == SIZE_MAX) return NULL;
    return app->workflows->heads[record].role;
}

static void short_path(const char *path, char *out, size_t size) {
    const char *home = getenv("HOME");
    size_t length = home ? strlen(home) : 0;
    if (length > 1 && !strncmp(path, home, length) && path[length] == '/') snprintf(out, size, "~%.150s", path + length);
    else snprintf(out, size, "%.150s", path);
}

static void exec_setting(const struct workflow_exec *e, const char *value, const char *source, bool running, char *out, size_t size) {
    if (!e->present || !value[0]) { snprintf(out, size, "%s", running ? "not reported yet" : "unknown (not reported by the provider)"); return; }
    if (!strcmp(source, "observed")) { snprintf(out, size, "%s (reported by the provider)", value); return; }
    snprintf(out, size, "configured default: %s (not observed)", value);
}

/* Where configured defaults came from, when any value is configuration. */
bool exec_configuration_text(const struct workflow_exec *e, char *out, size_t size) {
    char where[160];
    if (!e->present || (strcmp(e->model_source, "configured") && strcmp(e->effort_source, "configured"))) return false;
    short_path(e->config_source[0] ? e->config_source : "the provider configuration", where, sizeof(where));
    snprintf(out, size, "%s, read when the step started; the provider did not report what it used", where);
    return true;
}

void exec_model_text(const struct workflow_exec *e, bool running, char *out, size_t size) {
    exec_setting(e, e->model, e->model_source, running, out, size);
}

void exec_effort_text(const struct workflow_exec *e, bool running, char *out, size_t size) {
    exec_setting(e, e->effort, e->effort_source, running, out, size);
}

void exec_tokens_text(const struct workflow_exec *e, char *out, size_t size) {
    char in[32], cached[32], used[32];
    if (!e->present || (e->tokens_in < 0 && e->tokens_cached < 0 && e->tokens_out < 0)) {
        snprintf(out, size, "%s", e->present && !strcmp(e->state, "running") ? "reported when the step finishes" : "unknown (not reported)");
        return;
    }
    format_tokens(e->tokens_in, in, sizeof(in)); format_tokens(e->tokens_cached, cached, sizeof(cached));
    format_tokens(e->tokens_out, used, sizeof(used));
    snprintf(out, size, "in %s  cached %s  out %s  cost %s", in, cached, used, e->cost[0] ? e->cost : "unknown");
}

void step_duration_text(const struct workflow_node *n, char *out, size_t size) {
    long long now = (long long)time(NULL);
    if (n->started < 0) { snprintf(out, size, "-"); return; }
    if (n->completed >= n->started) { format_duration(n->completed - n->started, out, size); return; }
    format_duration(now - n->started, out, size);
    if (!strcmp(n->state, "running")) text_append(out, size, " so far");
}

/* "revision 3 approved 2026-09-27 02:21 UTC · run run_… · succeeded". The
 * revision number is known only to the session that launched the run. */
void plan_approval_text(const struct app *app, size_t run, char *out, size_t size) {
    const struct workflow_run *r = &app->workflows->runs[run];
    const struct native_plan *p = app->plan;
    char when[40] = "at an unrecorded time";
    if (strlen(r->created) >= 16) snprintf(when, sizeof(when), "%.10s %.5s UTC", r->created, r->created + 11);
    if (p && p->launched_revision && !strcmp(p->launched_run, r->id))
        snprintf(out, size, "revision %u approved %s", p->launched_revision, when);
    else snprintf(out, size, "plan %s approved %s", r->digest[0] ? r->digest : "(digest unrecorded)", when);
    text_append(out, size, "%srun %s%s%s", dot(app), r->id, dot(app), r->state);
}

/* The agent column: the profile of an interactive head, or the agent of the
 * step a headless head last ran ("command" for a plain command step). */
const char *head_agent_short(const struct app *app, const struct head *head) {
    const struct workflow_node *step;
    if (app->fleet) return head->remote_host;
    if (!head_headless(head)) return native_agent_name(head);
    step = head_step(app, head);
    return !step ? "none" : step->profile[0] ? step->profile : "command";
}

static const char *outline_glyph(const struct app *app, bool open) {
    if (app->ascii) return open ? "-" : "+";
    return open ? "\xe2\x96\xbe" : "\xe2\x96\xb8";
}

static void outline_run_text(const struct app *app, const struct outline_row *row, struct outline_text *t) {
    const struct workflow_run *r = &app->workflows->runs[row->run];
    size_t heads = run_head_total(app->workflows, row->run);
    snprintf(t->name, sizeof(t->name), "%*s%s %s %s", row->depth * 2, "", outline_glyph(app, row->open), run_label_kind(r), r->name);
    if (heads) snprintf(t->count, sizeof(t->count), "%zu head%s", heads, heads == 1 ? "" : "s");
    t->status = r->state; t->agent = t->count;
    t->tone = run_needs_attention(app, row->run) ? TV_WARNING : !strcmp(r->state, "running") ? TV_SUCCESS : TV_BASE;
}

/* A run head that is not active: retired or kept by the run, removed by the
 * user, or not created yet because its spawn step has not run. */
const char *run_head_absence(const struct app *app, const struct workflow_head *r) {
    const struct workflow_model *m = app->workflows;
    size_t i;
    if (r->retirement[0]) return r->retirement;
    for (i = 0; i < m->node_count; i++)
        if (m->nodes[i].run == r->run && !strcmp(m->nodes[i].id, r->step) && !m->nodes[i].attempts) return "pending";
    return "removed";
}

static void outline_retired_text(const struct app *app, const struct outline_row *row, struct outline_text *t) {
    const struct workflow_head *r = &app->workflows->heads[row->record];
    snprintf(t->name, sizeof(t->name), "%*s%-8s %s", row->depth * 2, "", r->role, r->branch);
    t->status = run_head_absence(app, r);
    t->tone = TV_MUTED;
}

static void outline_head_text(const struct app *app, const struct outline_row *row, struct outline_text *t) {
    const struct head *head = &app->model.heads[row->head];
    const char *role = head_run_role(app, head);
    if (role) snprintf(t->name, sizeof(t->name), "%*s%-8s %s", row->depth * 2, "", role, head->branch);
    else snprintf(t->name, sizeof(t->name), "%*s%s", row->depth * 2, "", app->fleet && head->remote_branch[0] ? head->remote_branch : head->branch);
    t->status = head_list_status(app, head); t->agent = head_agent_short(app, head);
    t->reported = head->declared[0] ? head->declared : "-";
    t->tone = head_list_tone(app, head);
}

void outline_row_text(const struct app *app, const struct outline_row *row, struct outline_text *t) {
    t->name[0] = t->count[0] = '\0';
    t->status = t->agent = t->reported = "";
    t->tone = TV_BASE;
    if (row->kind == OUTLINE_RUN) outline_run_text(app, row, t);
    else if (row->kind == OUTLINE_RETIRED) outline_retired_text(app, row, t);
    else outline_head_text(app, row, t);
}
