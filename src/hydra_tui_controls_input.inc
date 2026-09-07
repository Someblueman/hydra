static bool native_control_key(struct app *app, char key) {
    const char *action=key=='R' ? "resume" : key=='X' ? "cancel" : key=='Y' ? "approve" : key=='N' ? "reject" : NULL;
    char run[80],request[80]="-",answer[128],prompt[256];
    if (!action || !native_workspace_monitoring(app)) return false;
    if (app->fleet || !app->workflows || app->workflow_run>=app->workflows->run_count) {
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
