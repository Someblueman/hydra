#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
static bool confirm_local_control(struct app *app, const char *run, const char *request,
                                   const char *action, const char *answer) {
    return !strcmp(answer, action) && app->workflows && app->workflow_run < app->workflows->run_count &&
        !strcmp(run, app->workflows->runs[app->workflow_run].id) && native_control_submit(app, run, action, request);
}
/* The undecided input request recorded in the run's evidence, when there is
 * exactly one: the evidence pane lists each as "Request: ID" with its
 * "State: pending" and "Decision: none". Several, none or stale evidence
 * leave the choice to the user; the typed confirmation names the binding. */
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

bool native_control_key(struct app *app, char key) {
    const char *action=key=='R' ? "resume" : key=='X' ? "cancel" : key=='Y' ? "approve" : key=='N' ? "reject" : NULL;
    char run[80],request[80]="-",answer[128],prompt[256];
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
    if ((key=='Y' || key=='N') && !evidence_single_request(app, request, sizeof(request))) {
        if (prompt_text(app,"Request ID from the evidence pane: ",request,sizeof(request))!=0 || !request[0]) return true;
    }
    snprintf(prompt,sizeof(prompt),"Run %s / request %s. Type %s to confirm: ",run,request,action);
    if (prompt_text(app,prompt,answer,sizeof(answer))!=0) return true;
    if (!confirm_local_control(app, run, request, action, answer))
        copy_text(app->notice,sizeof(app->notice),"Control not submitted: confirmation, selected run or owner availability changed.");
    return true;
}
