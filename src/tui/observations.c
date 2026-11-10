#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
/* One in-flight read per source. Completed snapshots replace whole models;
 * terminal input and output continue while an adapter runs or times out. */


void native_observations_cancel(struct app *app, size_t source) {
    if (!app->observations || source>=OBSERVE_SOURCES) return;
    native_capture_destroy(&app->observations->jobs[source]);
    app->observations->again[source]=false;
}

void native_observations_destroy(struct app *app) {
    size_t i;
    native_evidence_destroy(app);
    native_review_destroy(app);
    free(app->links); app->links=NULL;
    native_controls_tick(app); /* Reap finished owners; active owners survive UI exit. */
    if (!app->observations) return;
    for (i=0;i<OBSERVE_SOURCES;i++) native_capture_destroy(&app->observations->jobs[i]);
    native_capture_destroy(&app->observations->preview);
    free(app->observations); app->observations=NULL;
}

static bool observations_ready(struct app *app) {
    size_t i;
    if (app->observations) return true;
    app->observations=calloc(1,sizeof(*app->observations));
    if (!app->observations) return false;
    for (i=0;i<OBSERVE_SOURCES;i++) app->observations->jobs[i].fd=-1;
    app->observations->preview.fd=-1;
    return true;
}

static void complete_snapshot(struct app *app, FILE *input, bool timed_out) {
    bool complete=input!=NULL;
    int result=accept_model_data(app,input);
    record_snapshot(app,result==0);
    if (!result) new_task_attach(app);
    if (!strcmp(app->notice,"Loading snapshot...")) app->notice[0]='\0';
    if (result && (timed_out || !complete)) copy_text(app->snapshot_error,sizeof(app->snapshot_error),timed_out ?
        "shell data adapter timed out; showing last good snapshot" : "shell data adapter failed; showing last good snapshot");
    if (app->fleet && app->statistics) statistics_visible(app);
}

static long observation_budget(const struct app *app, size_t source) {
    if (app->fleet) return HYDRA_FLEET_TUI_CAPTURE_BUDGET_MS;
    return source==OBSERVE_HEADS ? HYDRA_TUI_LOCAL_CAPTURE_BUDGET_MS : 3500L;
}

/* Whether the periodic refresh reads a source. Runs group heads in every
 * local view; outside the graph views the workflow read waits for a first
 * good head snapshot. */
static bool observation_shown(const struct app *app, size_t source) {
    if (source==OBSERVE_HEADS) return true;
    if (app->fleet) return false;
    if (source==OBSERVE_WORKFLOWS) return app->view==5 || app->view==7 || app->snapshot_at;
    if (source==OBSERVE_STATISTICS) return app->view==8;
    return source==OBSERVE_LINKS && app->view==7;
}

/* Takes a finished read; input is NULL when it failed. */
static void observation_accept(struct app *app, size_t source, FILE *input, bool timed_out) {
    if (source==OBSERVE_HEADS) complete_snapshot(app,input,timed_out);
    else if (source==OBSERVE_WORKFLOWS) statistics_graph_arrived(app,accept_workflows(app,input)==0);
    else if (source==OBSERVE_STATISTICS) (void)accept_statistics(app,input);
    else native_links_accept(app,input);
}

static void observation_start(struct app *app, size_t source) {
    static const char *const commands[][2]={{"tui","--data"},{"workflow","tui-data"},{"workflow","statistics-data"},{"workflow","--workspace-links"}};
    char *argv[]={(char *)app->hydra,(char *)commands[source][0],(char *)commands[source][1],NULL};
    if (source==OBSERVE_HEADS && app->fleet) { argv[1]=(char *)"fleet"; argv[2]=(char *)"tui-visual-data"; }
    app->observations->again[source]=false;
    if (native_capture_start(&app->observations->jobs[source],argv,observation_budget(app,source))) return;
    if (source!=OBSERVE_HEADS) { observation_accept(app,source,NULL,false); return; }
    record_snapshot(app,false); app->notice[0]='\0';
    copy_text(app->snapshot_error,sizeof(app->snapshot_error),"Unable to start snapshot observation");
}

void native_observations_request(struct app *app, size_t source) {
    if (source>=OBSERVE_ATTENTION || (app->fleet && source!=OBSERVE_HEADS) || !observations_ready(app)) return;
    if (app->observations->jobs[source].pid) app->observations->again[source]=true;
    else observation_start(app,source);
}

bool native_observations_loading(const struct app *app, size_t source) {
    return app->observations && source<OBSERVE_SOURCES && app->observations->jobs[source].pid!=0;
}

/* One source: take its finished read, then start the next one when the
 * refresh is due or one was asked for meanwhile. */
static void observation_tick(struct app *app, size_t source, bool request) {
    struct native_capture *job=&app->observations->jobs[source];
    if (job->pid && native_capture_step(job)) {
        bool timed_out=job->timed_out;
        observation_accept(app,source,native_capture_take(job),timed_out);
    }
    if (job->pid) return;
    if ((request || app->observations->again[source]) && observation_shown(app,source)) observation_start(app,source);
    app->observations->again[source]=false;
}

void native_observations_tick(struct app *app, bool request) {
    size_t source;
    native_plan_tick(app,request);
    native_plan_launch_tick(app,request);
    native_controls_tick(app);
    if (!app->observations && (!request || !observations_ready(app))) return;
    for (source=0;source<OBSERVE_ATTENTION;source++) observation_tick(app,source,request);
    preview_tick(app);
    native_evidence_tick(app,request);
    native_attention_tick(app,request);
    native_review_tick(app);
}
