#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
#include "snapshot.h"
#include "store.h"
/* One in-flight read per source. Completed snapshots replace whole models;
 * terminal input and output continue while an adapter runs or times out. */


void native_observations_cancel(struct app *app, size_t source) {
    if (!app->observations || source>=OBSERVE_SOURCES) return;
    native_capture_destroy(&app->observations->jobs[source]);
    if (source==OBSERVE_HEADS) snapshot_cancel(app->snapshot);
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
    snapshot_free(app->snapshot); app->snapshot=NULL;
    free(app->observations); app->observations=NULL;
}

/* The local head snapshot comes from the state store when the CLI that
 * started the control centre said so; a fleet snapshot never does. */
static bool heads_from_store(const struct app *app) {
    return app->store && !app->fleet;
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
    if (result && (timed_out || !complete)) snprintf(app->snapshot_error,sizeof(app->snapshot_error),"%s %s; showing last good snapshot",
        heads_from_store(app) ? "state read" : "shell data adapter",timed_out ? "timed out" : "failed");
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

/* Takes a finished read; input is NULL when it failed. A complete read is
 * remembered with the stamp of the runs it was started on. */
static void observation_accept(struct app *app, size_t source, FILE *input, bool timed_out) {
    bool ok=true;
    if (source==OBSERVE_HEADS) complete_snapshot(app,input,timed_out);
    else if (source==OBSERVE_WORKFLOWS) { ok=accept_workflows(app,input)==0; statistics_graph_arrived(app,ok); }
    else if (source==OBSERVE_STATISTICS) (void)accept_statistics(app,input);
    else ok=native_links_accept(app,input);
    app->observations->kept[source].valid=ok;
    app->observations->kept[source].stamp=app->observations->kept[source].asked;
    app->observations->kept[source].reused=0;
}

/* Do not refresh what has not changed. With the store readable here, a look
 * at the runs directory says what the CLI's workflow readers would print: a
 * project without runs gets the document they print for it, and runs whose
 * records are unchanged keep the read they have. A run that has not ended is
 * always asked for (its state depends on the clock and on a live owner), and
 * an unchanged read of ended runs is asked for again every
 * WORKFLOW_REUSE_LIMIT refreshes, in case a record changed in place. */
#define WORKFLOW_REUSE_LIMIT 15U

static FILE *document(const char *text) {
    FILE *file=tmpfile();
    if (file && (fputs(text,file)==EOF || fseek(file,0,SEEK_SET)!=0)) { fclose(file); file=NULL; }
    return file;
}

/* What the CLI prints for a project that records no run. Outside a project
 * the links reader fails, and so does this. */
static void no_runs_accept(struct app *app, size_t source, const struct store_location *where, const struct store_runs *runs) {
    char text[STORE_PATH+STORE_TEXT+64], root[STORE_PATH];
    size_t i;
    if (source==OBSERVE_WORKFLOWS) { observation_accept(app,source,document("HYDRA_WORKFLOW_TUI\t2\n"),false); return; }
    if (!runs->project[0] || !where->toplevel[0]) { observation_accept(app,source,NULL,false); return; }
    copy_text(root,sizeof(root),where->toplevel);
    for (i=0;root[i];i++) if (root[i]=='\t' || root[i]=='\r' || root[i]=='\n') root[i]=' ';
    snprintf(text,sizeof(text),"HYDRA_WORKSPACE_LINKS\t1\nP\t%s\t%s\nZ\n",runs->project,root);
    observation_accept(app,source,document(text),false);
}

/* True when a workflow source needs no CLI read now. asked: the user asked
 * for a fresh read, which unchanged records do not answer. */
static bool workflow_shortcut(struct app *app, size_t source, bool asked) {
    const struct store_location *where=heads_from_store(app) ? snapshot_where(app->snapshot) : NULL;
    struct store_runs runs;
    bool links=source==OBSERVE_LINKS;
    if (!where || (source!=OBSERVE_WORKFLOWS && !links)) return false;
    store_runs(where,&runs);
    if (!runs.count) {
        no_runs_accept(app,source,where,&runs);
        app->observations->kept[source].valid=false;
        return true;
    }
    app->observations->kept[source].asked=links ? runs.graphs : runs.records;
    if (asked || !app->observations->kept[source].valid || app->observations->kept[source].stamp!=app->observations->kept[source].asked) return false;
    if (links) return true;
    if (!runs.settled || ++app->observations->kept[source].reused>WORKFLOW_REUSE_LIMIT) return false;
    app->workflow_at=time(NULL);
    return true;
}

/* The store answers the workflow sources only once the first refresh has
 * found the repository; until then their reads wait for it. */
static bool location_pending(const struct app *app, size_t source) {
    return heads_from_store(app) && (source==OBSERVE_WORKFLOWS || source==OBSERVE_LINKS) &&
        !snapshot_where(app->snapshot) && snapshot_active(app->snapshot);
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

/* Starts the read of the state store; it advances on later ticks. */
static void store_start(struct app *app) {
    const struct head *selected=app->selected<app->model.head_count ? &app->model.heads[app->selected] : NULL;
    app->observations->again[OBSERVE_HEADS]=false;
    if (!app->snapshot) app->snapshot=snapshot_new(app->hydra);
    if (app->snapshot) { snapshot_begin(app->snapshot,selected ? selected->branch : "",observation_budget(app,OBSERVE_HEADS)); return; }
    record_snapshot(app,false); app->notice[0]='\0';
    copy_text(app->snapshot_error,sizeof(app->snapshot_error),"Unable to start snapshot observation");
}

static bool observation_running(const struct app *app, size_t source) {
    if (source==OBSERVE_HEADS && snapshot_active(app->snapshot)) return true;
    return app->observations->jobs[source].pid!=0;
}

void native_observations_request(struct app *app, size_t source) {
    if (source>=OBSERVE_ATTENTION || (app->fleet && source!=OBSERVE_HEADS) || !observations_ready(app)) return;
    if (observation_running(app,source) || location_pending(app,source)) app->observations->again[source]=true;
    else if (source==OBSERVE_HEADS && heads_from_store(app)) store_start(app);
    else if (!workflow_shortcut(app,source,true)) observation_start(app,source);
}

bool native_observations_loading(const struct app *app, size_t source) {
    return app->observations && source<OBSERVE_SOURCES && observation_running(app,source);
}

/* The head snapshot read from the store: take a finished refresh, then start
 * the next one when it is due or was asked for meanwhile. */
static void store_tick(struct app *app, bool request) {
    FILE *rows;
    bool timed_out;
    if (snapshot_active(app->snapshot) && snapshot_step(app->snapshot,&rows,&timed_out)) complete_snapshot(app,rows,timed_out);
    if (snapshot_active(app->snapshot)) return;
    if (request || app->observations->again[OBSERVE_HEADS]) store_start(app);
    /* A refresh with nothing to wait for has already ended. */
    if (snapshot_active(app->snapshot) && snapshot_step(app->snapshot,&rows,&timed_out)) complete_snapshot(app,rows,timed_out);
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
    if (!request && !app->observations->again[source]) return;
    if (location_pending(app,source)) { app->observations->again[source]=true; return; }
    if (observation_shown(app,source) && !workflow_shortcut(app,source,app->observations->again[source])) observation_start(app,source);
    app->observations->again[source]=false;
}

void native_observations_tick(struct app *app, bool request) {
    size_t source;
    native_plan_tick(app,request);
    native_plan_launch_tick(app,request);
    native_controls_tick(app);
    if (!app->observations && (!request || !observations_ready(app))) return;
    if (heads_from_store(app)) store_tick(app,request);
    for (source=heads_from_store(app) ? OBSERVE_WORKFLOWS : OBSERVE_HEADS;source<OBSERVE_ATTENTION;source++) observation_tick(app,source,request);
    preview_tick(app);
    native_evidence_tick(app,request);
    native_attention_tick(app,request);
    native_review_tick(app);
}
