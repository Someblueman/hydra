#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
static void control_body(const char *action, const char *run, const char *request, const char *step, char *body, size_t size) {
    if (!strcmp(action, "approve") || !strcmp(action, "reject"))
        snprintf(body, size, "%s %s?\n\nRun %s %s.\n\nRequest %s. If the run or request changes before you "
                 "confirm, nothing is sent.", !strcmp(action, "approve") ? "Approve" : "Reject", step, run,
                 !strcmp(action, "approve") ? "continues: the waiting step may run" : "fails at this step and stops",
                 request);
    else if (!strcmp(action, "resume"))
        snprintf(body, size, "Resume run %s?\n\nIts owner continues the steps that are ready. A decision alone never "
                 "resumes a run; this does.", run);
    else
        snprintf(body, size, "Cancel run %s?\n\nSteps that are running are stopped and the run ends as cancelled. "
                 "Heads it created and their work are kept.", run);
}

static const char *control_title(const char *action) {
    return !strcmp(action, "approve") ? "Approve request" : !strcmp(action, "reject") ? "Reject request" :
        !strcmp(action, "resume") ? "Resume run" : "Cancel run";
}

/* The undecided input request recorded in the run's evidence, when there is
 * exactly one: the evidence pane lists each as "Request: ID" with its
 * "State: pending" and "Decision: none". Several, none or stale evidence
 * leave nothing to decide on; the confirmation names the binding. */
static bool block_has(const char *from, const char *until, const char *line) {
    const char *at = strstr(from, line);
    return at && (!until || at < until);
}

static bool evidence_single_request(const struct app *app, char *out, size_t size) {
    const char *text = app->evidence && app->evidence->text && !app->evidence->stale &&
        !strcmp(app->evidence->run, app->workflows->runs[app->workflow_run].id) ? app->evidence->text : NULL;
    const char *at;
    size_t found = 0;
    out[0] = '\0';
    while (text && (at = strstr(text, "\nRequest: ")) != NULL) {
        const char *id = at + 10, *end = strchr(id, '\n'), *next;
        if (!end) break;
        next = strstr(end, "\nRequest: ");
        if (block_has(end, next, "\nState: pending\n") && block_has(end, next, "\nDecision: none\n") &&
            (size_t)(end - id) < size) {
            snprintf(out, size, "%.*s", (int)(end - id), id);
            found++;
        }
        text = end;
    }
    if (found != 1) out[0] = '\0';
    return found == 1;
}

/* The run and request are read again after the confirmation: when either
 * changed while the dialog was open, nothing is sent. */
static bool control_current(const struct app *app, const char *run, const char *request) {
    char now[80] = "-", step[80];
    if (!app->workflows || app->workflow_run >= app->workflows->run_count ||
        strcmp(run, app->workflows->runs[app->workflow_run].id)) return false;
    if (!strcmp(request, "-")) return true;
    if (native_attention_run_request(app, run, now, sizeof(now), step, sizeof(step))) return !strcmp(now, request);
    return evidence_single_request(app, now, sizeof(now)) && !strcmp(now, request);
}

static bool control_request(struct app *app, const char *run, char *request, size_t size, char *step, size_t step_size) {
    if (native_attention_run_request(app, run, request, size, step, step_size)) return true;
    /* Attention has not listed it yet: the run's loaded evidence may name
     * exactly one undecided request. */
    if (evidence_single_request(app, request, size)) { copy_text(step, step_size, "the waiting step"); return true; }
    native_attention_read(app);
    copy_text(app->notice, sizeof(app->notice), "No approval request is known to wait on this run; reading requests again");
    return false;
}
bool native_control_key(struct app *app, char key) {
    const char *action=key=='R' ? "resume" : key=='X' ? "cancel" : key=='Y' ? "approve" : key=='N' ? "reject" : NULL;
    char run[80],request[80]="-",step[80]="-",body[1024],accept[16];
    if (!action) return false;
    /* A selected remote task takes the receiver controls; a local run the
     * workflow controls of the monitor layout. */
    if (selected_task(app) || (app->task_row && app->view != 7)) return native_fleet_control_key(app, key, action);
    if (!native_workspace_monitoring(app)) return false;
    if (!app->workflows || app->workflow_run>=app->workflows->run_count) {
        copy_text(app->notice,sizeof(app->notice),"Select a local recorded run before using workflow controls."); return true;
    }
    copy_text(run,sizeof(run),app->workflows->runs[app->workflow_run].id);
    if (key=='R' || key=='X') {
        const char *state=app->workflows->runs[app->workflow_run].state;
        if (!strcmp(state,"succeeded") || !strcmp(state,"failed") || !strcmp(state,"cancelled")) {
            copy_text(app->notice,sizeof(app->notice),"Terminal run: no resume or cancel. Inspect evidence before starting new work."); return true;
        }
    }
    if ((key=='Y' || key=='N') && !control_request(app,run,request,sizeof(request),step,sizeof(step))) return true;
    control_body(action,run,request,step,body,sizeof(body));
    snprintf(accept,sizeof(accept),"%c%s",action[0]-'a'+'A',action+1);
    if (!dialog_confirm(app,control_title(action),body,accept,key=='X' || key=='N')) {
        copy_text(app->notice,sizeof(app->notice),"Control not submitted; nothing changed."); return true;
    }
    if (!control_current(app,run,request) || !native_control_submit(app,run,action,request))
        copy_text(app->notice,sizeof(app->notice),"Control not submitted: confirmation, selected run or owner availability changed.");
    return true;
}
