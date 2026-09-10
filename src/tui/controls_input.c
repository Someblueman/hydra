#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool fleet_control_key(struct app *app, char key, const char *action) {
    struct task_observation *task, *current = NULL;
    char host[128], task_id[128], observed_spec[65], observed_request[128], observed_state[64], request[128] = "-", trust[80] = "", answer[128], prompt[256];
    if (!app->model.task_count) { copy_text(app->notice,sizeof(app->notice),"No remote task observation is selected; refresh before mutating."); return true; }
    if (app->task_selected >= app->model.task_count) app->task_selected = 0;
    task = &app->model.tasks[app->task_selected];
    if (strcmp(task->freshness, "fresh") || !strcmp(task->state, "outcome_unknown")) { copy_text(app->notice,sizeof(app->notice),"Remote task evidence is stale or uncertain; inspect and reconcile before mutating."); return true; }
    if ((key == 'R' && strcmp(task->state, "waiting_approval")) || ((key == 'Y' || key == 'N') && strcmp(task->state, "waiting_approval")) || (key == 'X' && (!strcmp(task->state, "succeeded") || !strcmp(task->state, "failed") || !strcmp(task->state, "cancelled")))) { copy_text(app->notice,sizeof(app->notice),"That remote task state does not permit this mutation."); return true; }
    copy_text(host, sizeof(host), task->host); copy_text(task_id, sizeof(task_id), task->task_id); copy_text(observed_spec, sizeof(observed_spec), task->spec_sha256); copy_text(observed_request, sizeof(observed_request), task->request_id); copy_text(observed_state, sizeof(observed_state), task->state);
    if ((key == 'Y' || key == 'N') && (!strcmp(observed_request, "-") || prompt_text(app,"Repeat the displayed request ID: ",request,sizeof(request))!=0 || strcmp(request, observed_request))) { copy_text(app->notice,sizeof(app->notice),"Request ID changed or does not match receiver evidence."); return true; }
    if (key == 'R' || key == 'Y' || key == 'N') {
        if (strcmp(observed_spec, "-") && (prompt_text(app,"Repeat the displayed specification digest: ",trust,sizeof(trust))!=0 || strcmp(trust, observed_spec))) { copy_text(app->notice,sizeof(app->notice),"Specification digest changed or does not match receiver evidence."); return true; }
        if (!trust[0]) { copy_text(app->notice,sizeof(app->notice),"Remote task has no bound specification digest; mutation disabled."); return true; }
    }
    snprintf(prompt,sizeof(prompt),"Task %s on %s / request %s. Type %s to dispatch: ",task_id,host,request,action);
    if (prompt_text(app,prompt,answer,sizeof(answer))!=0 || strcmp(answer,action)) return true;
    for (size_t i = 0; i < app->model.task_count; i++) if (!strcmp(app->model.tasks[i].host, host) && !strcmp(app->model.tasks[i].task_id, task_id)) { current = &app->model.tasks[i]; break; }
    if (!current || strcmp(current->spec_sha256, observed_spec) || strcmp(current->request_id, observed_request) || strcmp(current->state, observed_state) || strcmp(current->freshness, "fresh") || !strcmp(current->state, "outcome_unknown")) { copy_text(app->notice,sizeof(app->notice),"Task evidence changed during confirmation; no mutation dispatched."); return true; }
    if (!native_fleet_control_submit(app,current,action,request,trust)) copy_text(app->notice,sizeof(app->notice),"Remote control not dispatched; inspect task evidence and retry explicitly.");
    return true;
}
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool native_control_key(struct app *app, char key) {
    const char *action=key=='R' ? "resume" : key=='X' ? "cancel" : key=='Y' ? "approve" : key=='N' ? "reject" : NULL;
    char run[80],request[80]="-",answer[128],prompt[256];
    if (!action || !native_workspace_monitoring(app)) return false;
    if (app->fleet) return fleet_control_key(app, key, action);
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
    if (key=='Y' || key=='N') {
        if (prompt_text(app,"Request ID from the evidence pane: ",request,sizeof(request))!=0 || !request[0]) return true;
    }
    snprintf(prompt,sizeof(prompt),"Run %s / request %s. Type %s to confirm: ",run,request,action);
    if (prompt_text(app,prompt,answer,sizeof(answer))!=0) return true;
    if (strcmp(answer,action) || !app->workflows || app->workflow_run>=app->workflows->run_count ||
        strcmp(run,app->workflows->runs[app->workflow_run].id) || !native_control_submit(app,run,action,request))
        copy_text(app->notice,sizeof(app->notice),"Control not submitted: confirmation, selected run or owner availability changed.");
    return true;
}
