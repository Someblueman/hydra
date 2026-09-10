#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* A bounded set of detached CLI owners; no lifecycle engine in the UI. */
void native_controls_tick(struct app *app) {
    size_t i;
    for (i=0;i<4;i++) if (app->control_pids[i]>0) {
        int status=0;
        pid_t result=waitpid(app->control_pids[i],&status,WNOHANG);
        if (result==app->control_pids[i]) {
            const char *state=!WIFEXITED(status) ? "interrupted; outcome unknown" :
                WEXITSTATUS(status)==0 ? "dispatch finished; outcome unconfirmed" : WEXITSTATUS(status)==3 ? "paused for input" : "failed";
            snprintf(app->notice,sizeof(app->notice),"Control %s / refresh task evidence: %s",state,app->control_labels[i]);
            app->control_pids[i]=0;
        } else if (result<0 && errno==ECHILD) {
            app->control_pids[i]=0;
            copy_text(app->notice,sizeof(app->notice),"Control owner unavailable; inspect recorded state. No automatic retry.");
        }
    }
}

bool native_fleet_control_submit(struct app *app, const struct task_observation *task,
                                 const char *action, const char *request, const char *trust) {
    size_t i, n = 0;
    char *argv[16];
    if (!task || !task->host[0] || !task->task_id[0] || !action) return false;
    argv[n++] = "nohup"; argv[n++] = (char *)app->hydra; argv[n++] = "fleet"; argv[n++] = "task";
    if (!strcmp(action, "approve") || !strcmp(action, "reject")) {
        argv[n++] = "decide"; argv[n++] = (char *)task->host; argv[n++] = "--id"; argv[n++] = (char *)task->task_id;
        argv[n++] = "--request"; argv[n++] = (char *)request; argv[n++] = "--decision"; argv[n++] = (char *)action;
    } else {
        argv[n++] = (char *)action; argv[n++] = (char *)task->host; argv[n++] = "--id"; argv[n++] = (char *)task->task_id;
    }
    if (trust && trust[0]) { argv[n++] = "--trust-spec"; argv[n++] = (char *)trust; }
    argv[n] = NULL;
    native_controls_tick(app);
    for (i = 0; i < 4 && app->control_pids[i]; i++) { }
    if (i == 4 || !native_detached_start(&app->control_pids[i], argv, -1)) return false;
    snprintf(app->control_labels[i], sizeof(app->control_labels[i]), "%s task %s", action, task->task_id);
    snprintf(app->notice, sizeof(app->notice), "%s dispatched for %s; refresh receiver evidence", action, task->task_id);
    return true;
}

bool native_control_submit(struct app *app, const char *run, const char *action, const char *request) {
    size_t i;
    char *argv[]={"nohup",(char *)app->hydra,"workflow","--workspace-control",(char *)run,(char *)action,(char *)request,NULL};
    native_controls_tick(app);
    for (i=0;i<4;i++) if (!app->control_pids[i]) break;
    if (i==4 || !native_detached_start(&app->control_pids[i],argv,-1)) return false;
    snprintf(app->control_labels[i],sizeof(app->control_labels[i]),"%s run %s",action,run);
    snprintf(app->notice,sizeof(app->notice),"%s requested. Decisions do not resume work automatically.",app->control_labels[i]);
    return true;
}
