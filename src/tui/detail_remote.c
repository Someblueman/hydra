#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Details of remote work: a head on another host, or a receiver-owned task.
 * Every value names its observation and age; nothing here is a liveness
 * probe, and a stale or retained observation is said to be one. */

#define REMOTE_LABEL 13

static void remote_row(struct app *app, const char *label, const char *text, enum tv_style tone) {
    if (app->line >= app->limit) return;
    column(app, 0, REMOTE_LABEL, TV_MUTED, label);
    column(app, REMOTE_LABEL, app->content_width - REMOTE_LABEL, tone, text);
    app->line++;
}

static void remote_host_rows(struct app *app, const char *name) {
    const struct host_observation *host = host_for_name(app, name);
    char text[2048];
    if (!host) { remote_row(app, "Host", "not in the latest remote observation", TV_WARNING); return; }
    host_state_text(app, host, text, sizeof(text));
    if (strcmp(host->state, "responded")) { memmove(text + 14, text, strlen(text) + 1); memcpy(text, "disconnected; ", 14); }
    remote_row(app, "Connection", text, !strcmp(host->state, "responded") && !app->remote_stale ? TV_BASE : TV_WARNING);
    if (host->last_confirmed[0] && strcmp(host->last_confirmed, "-")) remote_row(app, "Confirmed", host->last_confirmed, TV_BASE);
}

void remote_detail(struct app *app, const struct head *head) {
    char text[2048];
    const char *status = remote_head_status(app, head);
    enum tv_style tone = remote_head_tone(app, head);
    remote_row(app, "Where", head->remote_host, TV_BASE);
    remote_row(app, "Project", head->remote_project, TV_BASE);
    snprintf(text, sizeof(text), "%s%sdesired %s", status, dot(app), head->desired);
    remote_row(app, "Status", text, tone);
    remote_observation_text(app, head, text, sizeof(text));
    remote_row(app, "Observation", text, tone);
    remote_host_rows(app, head->remote_host);
    remote_row(app, "Agent", remote_agent_name(head), TV_BASE);
    remote_row(app, "Liveness", "unobserved: a remote head reports desired state, not a live process", TV_MUTED);
    if (head->retained || !strcmp(status, "disconnected") || !strcmp(status, "stale"))
        paragraph(app, "This is the host's last observation, not its current state. Its agent may still be working; nothing here completes or stops it. r retries the host.", TV_WARNING);
    section(app, "NEXT");
    linef(app, "a  talk to the agent in its terminal on %s", head->remote_host);
    linef(app, "x  interrupt the agent (confirmed); the worktree and files stay on the host");
    linef(app, "r  retry the host    d  technical details    c  coordination");
}

void remote_technical(struct app *app, const struct head *head) {
    char text[2048];
    style(app, TONE_BORDER); linef(app, "IDENTITY"); style(app, TONE_BASE);
    remote_row(app, "Host", head->remote_host, TV_BASE);
    remote_row(app, "Branch", head->remote_branch, TV_BASE);
    remote_row(app, "Project", head->remote_project, TV_BASE);
    snprintf(text, sizeof(text), "%s   instance: %s", head->head_id, head->instance);
    remote_row(app, "Head", text, TV_BASE);
    section(app, "LIFECYCLE");
    remote_row(app, "Desired", head->desired, TV_BASE);
    snprintf(text, sizeof(text), "%s%sage %s%s%s", head->freshness, dot(app), head->age, head->retained ? dot(app) : "", head->retained ? "retained" : "");
    remote_row(app, "Observation", text, TV_BASE);
    snprintf(text, sizeof(text), "remote observation %lds old%s", remote_age(app), app->remote_stale ? ", stale" : "");
    remote_row(app, "Source", text, TV_BASE);
}

void task_detail(struct app *app, const struct task_observation *task) {
    char text[2048];
    const char *status = task_status(app, task);
    enum tv_style tone = task_tone(app, task);
    remote_row(app, "Where", task->host, TV_BASE);
    remote_row(app, "Workspace", task->workspace, TV_BASE);
    snprintf(text, sizeof(text), "%s%sowner %s", status, dot(app), task->owner);
    remote_row(app, "State", text, tone);
    snprintf(text, sizeof(text), "%s%sreceiver observed %s%sconfirmed %s", task->retained ? "retained; host disconnected" : task->freshness,
             dot(app), task->receiver_observed, dot(app), task->last_confirmed);
    remote_row(app, "Observation", text, task->retained || strcmp(task->freshness, "fresh") ? TV_WARNING : TV_BASE);
    remote_host_rows(app, task->host);
    snprintf(text, sizeof(text), "%s%s%s", task->profile, task->step_id[0] && strcmp(task->step_id, "-") ? "  step " : "", task->step_id[0] && strcmp(task->step_id, "-") ? task->step_id : "");
    remote_row(app, "Agent", text, TV_BASE);
    snprintf(text, sizeof(text), "%s%s%s", task->waiting_reason, task->waiting_detail[0] && strcmp(task->waiting_detail, "-") ? ": " : "",
             task->waiting_detail[0] && strcmp(task->waiting_detail, "-") ? task->waiting_detail : "");
    remote_row(app, "Waiting", text, strcmp(task->waiting_reason, "none") ? TV_WARNING : TV_BASE);
    remote_row(app, "Next", task->next_action, TV_BASE);
    snprintf(text, sizeof(text), "%s%sverification %s", task->result_state, dot(app), task->verification_state);
    remote_row(app, "Result", text, TV_BASE);
    snprintf(text, sizeof(text), "%s%sscope %s%srequested %s", task->cancellation, dot(app), task->cancellation_scope, dot(app), task->cancel_requested_at);
    remote_row(app, "Cancel", text, strcmp(task->cancellation, "-") ? TV_WARNING : TV_MUTED);
    remote_row(app, "Request", task->request_id, TV_MUTED);
    remote_row(app, "Spec", task->spec_sha256, TV_MUTED);
    if (strcmp(status, task->state))
        paragraph(app, "This is the receiver's last observation, not its current state; controls stay disabled until a fresh one arrives. r retries the host.", TV_WARNING);
    section(app, "NEXT");
    linef(app, "Y approve  N reject  R resume  X cancel   (each confirmed; fresh evidence only)");
    linef(app, "r  retry the host    I  attention items for this task");
}
