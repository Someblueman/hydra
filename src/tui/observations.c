#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
/* One in-flight read per source. Completed snapshots replace whole models;
 * terminal input and output continue while an adapter runs or times out. */


void native_observations_cancel(struct app *app, size_t source) {
    if (app->observations && source<OBSERVATION_JOBS) native_capture_destroy(&app->observations->jobs[source]);
}

void native_observations_destroy(struct app *app) {
    size_t i;
    native_evidence_destroy(app);
    native_review_destroy(app);
    free(app->links); app->links=NULL;
    native_controls_tick(app); /* Reap finished owners; active owners survive UI exit. */
    if (!app->observations) return;
    for (i=0;i<OBSERVATION_JOBS;i++) native_capture_destroy(&app->observations->jobs[i]);
    native_capture_destroy(&app->observations->preview);
    free(app->observations); app->observations=NULL;
}

static void complete_snapshot(struct app *app, FILE *input, bool timed_out) {
    bool complete=input!=NULL;
    int result=accept_model_data(app,input);
    record_snapshot(app,result==0);
    if (!result) new_task_attach(app);
    if (!strcmp(app->notice,"Loading snapshot...")) app->notice[0]='\0';
    if (result && (timed_out || !complete)) copy_text(app->snapshot_error,sizeof(app->snapshot_error),timed_out ?
        "shell data adapter timed out; showing last good snapshot" : "shell data adapter failed; showing last good snapshot");
}

/* The remote observation: the fleet stream, read beside the local one. A
 * failure keeps the rows already shown as the last observation and says so. */
static void complete_remote(struct app *app, FILE *input, bool timed_out) {
    bool complete=input!=NULL;
    char error[TEXT], saved[TEXT];
    copy_text(saved,sizeof(saved),app->snapshot_error);
    if (!accept_model_data(app,input)) {
        copy_text(app->snapshot_error,sizeof(app->snapshot_error),saved);
        if (app->fleet && app->statistics) statistics_visible(app);
        if (app->view==6) native_setup_hosts_refresh(app);
        new_task_attach(app);
        /* Nothing to follow remotely: look again later rather than every cycle. */
        app->remote_next=time(NULL)+(app->model.host_count ? 0 : HYDRA_TUI_REMOTE_IDLE_SECONDS);
        return;
    }
    copy_text(error,sizeof(error),app->snapshot_error);
    copy_text(app->snapshot_error,sizeof(app->snapshot_error),saved);
    remote_failed(app,timed_out ? "remote observation timed out" : !complete ? "remote observation failed" : error);
    app->remote_next=time(NULL)+HYDRA_TUI_REMOTE_RETRY_SECONDS;
}

static long observation_budget(const struct app *app, size_t source) {
    (void)app;
    if (source==OBSERVATION_REMOTE) return HYDRA_FLEET_TUI_CAPTURE_BUDGET_MS;
    return source==0 ? HYDRA_TUI_LOCAL_CAPTURE_BUDGET_MS : 3500L;
}

/* `hydra fleet tui` (the compatibility entry) reads the fleet helper's own
 * stream; `hydra tui` reads it through the shell, which answers with an empty
 * stream when no host is registered or the helper is not built. */
static void remote_source_tick(struct app *app, bool request) {
    struct native_capture *p=&app->observations->jobs[OBSERVATION_REMOTE];
    char *argv[]={(char *)app->hydra,app->fleet ? "fleet" : "tui",app->fleet ? "tui-visual-data" : "--remote-data",NULL};
    if (p->pid && native_capture_step(p)) {
        bool timed_out=p->timed_out;
        FILE *input=native_capture_take(p);
        complete_remote(app,input,timed_out);
    }
    if (!request || p->pid || time(NULL)<app->remote_next) return;
    if (!native_capture_start(p,argv,observation_budget(app,OBSERVATION_REMOTE))) {
        remote_failed(app,"unable to start the remote observation");
        app->remote_next=time(NULL)+HYDRA_TUI_REMOTE_RETRY_SECONDS;
    }
}

void native_observations_tick(struct app *app, bool request) {
    size_t i;
    native_plan_tick(app,request);
    native_plan_launch_tick(app,request);
    native_controls_tick(app);
    if (!app->observations) {
        if (!request) return;
        app->observations=calloc(1,sizeof(*app->observations));
        if (!app->observations) return;
        for (i=0;i<OBSERVATION_JOBS;i++) app->observations->jobs[i].fd=-1;
        app->observations->preview.fd=-1;
    }
    for (i=0;i<4;i++) {
        struct native_capture *p=&app->observations->jobs[i];
        if (p->pid && native_capture_step(p)) {
            FILE *input;
            bool timed_out=p->timed_out;
            input=native_capture_take(p);
            if (i==0) {
                complete_snapshot(app,input,timed_out);
            } else if (i==1) (void)accept_workflows(app,input);
            else if (i==2) (void)accept_statistics(app,input);
            else native_links_accept(app,input);
        }
        /* Runs group heads in every local view; outside the graph views the
         * workflow read waits for a first good head snapshot. */
        if (request && !p->pid && (i==0 || (i==1 && (app->view==5 || app->view==7 || app->snapshot_at)) ||
            (i==2 && app->view==8 && !app->fleet) || (i==3 && app->view==7))) {
            char *argv[]={(char *)app->hydra,i==0 ? "tui" : "workflow",
                i==0 ? "--data" : i==1 ? "tui-data" : i==2 ? "statistics-data" : "--workspace-links",NULL};
            if (!native_capture_start(p,argv,observation_budget(app,i))) {
                if (i==0) { record_snapshot(app,false); app->notice[0]='\0'; copy_text(app->snapshot_error,sizeof(app->snapshot_error),"Unable to start snapshot observation"); }
                else if (i==1) (void)accept_workflows(app,NULL);
                else if (i==2) (void)accept_statistics(app,NULL);
                else native_links_accept(app,NULL);
            }
        }
    }
    remote_source_tick(app,request);
    headless_preview_tick(app);
    native_evidence_tick(app,request);
    native_attention_tick(app,request);
    native_review_tick(app);
}
