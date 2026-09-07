/* A bounded set of detached CLI owners; no lifecycle engine in the UI. */
static void native_controls_tick(struct app *app) {
    size_t i;
    for (i=0;i<4;i++) if (app->control_pids[i]>0) {
        int status=0;
        pid_t result=waitpid(app->control_pids[i],&status,WNOHANG);
        if (result==app->control_pids[i]) {
            const char *state=!WIFEXITED(status) ? "interrupted; outcome unknown" :
                WEXITSTATUS(status)==0 ? "completed" : WEXITSTATUS(status)==3 ? "paused for input" : "failed";
            snprintf(app->notice,sizeof(app->notice),"Control %s / %s / inspect evidence",state,app->control_labels[i]);
            app->control_pids[i]=0;
        } else if (result<0 && errno==ECHILD) {
            app->control_pids[i]=0;
            copy_text(app->notice,sizeof(app->notice),"Control owner unavailable; inspect recorded state. No automatic retry.");
        }
    }
}

static bool native_control_submit(struct app *app, const char *run, const char *action, const char *request) {
    size_t i;
    char *argv[]={"nohup",(char *)app->hydra,"workflow","--workspace-control",(char *)run,(char *)action,(char *)request,NULL};
    native_controls_tick(app);
    for (i=0;i<4;i++) if (!app->control_pids[i]) break;
    if (i==4 || !native_detached_start(&app->control_pids[i],argv,-1)) return false;
    snprintf(app->control_labels[i],sizeof(app->control_labels[i]),"%s run %s",action,run);
    snprintf(app->notice,sizeof(app->notice),"%s requested. Decisions do not resume work automatically.",app->control_labels[i]);
    return true;
}
