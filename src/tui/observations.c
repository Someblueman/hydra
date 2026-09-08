#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* One in-flight read per source. Completed snapshots replace whole models;
 * terminal input and output continue while an adapter runs or times out. */


void native_observations_cancel(struct app *app, size_t source) {
    if (app->observations && source<4) native_capture_destroy(&app->observations->jobs[source]);
}

void native_observations_destroy(struct app *app) {
    size_t i;
    native_evidence_destroy(app);
    free(app->links); app->links=NULL;
    native_controls_tick(app); /* Reap finished owners; active owners survive UI exit. */
    if (!app->observations) return;
    for (i=0;i<4;i++) native_capture_destroy(&app->observations->jobs[i]);
    free(app->observations); app->observations=NULL;
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
        for (i=0;i<4;i++) app->observations->jobs[i].fd=-1;
    }
    for (i=0;i<4;i++) {
        struct native_capture *p=&app->observations->jobs[i];
        if (p->pid && native_capture_step(p)) {
            FILE *input;
            bool timed_out=p->timed_out;
            input=native_capture_take(p);
            if (i==0) {
                bool complete=input!=NULL;
                int result=accept_model_data(app,input);
                record_snapshot(app,result==0);
                if (result && (timed_out || !complete)) copy_text(app->snapshot_error,sizeof(app->snapshot_error),timed_out ?
                    "shell data adapter timed out; showing last good snapshot" : "shell data adapter failed; showing last good snapshot");
                if (app->fleet && app->statistics) statistics_visible(app);
            } else if (i==1) (void)accept_workflows(app,input);
            else if (i==2) (void)accept_statistics(app,input);
            else native_links_accept(app,input);
        }
        if (request && !p->pid && (i==0 || (!app->fleet && ((i==1 && (app->view==5 || app->view==7)) || (i==2 && app->view==8) || (i==3 && app->view==7))))) {
            char *argv[]={(char *)app->hydra,i==0 ? app->fleet ? "fleet" : "tui" : "workflow",
                i==0 ? app->fleet ? "tui-visual-data" : "--data" : i==1 ? "tui-data" : i==2 ? "statistics-data" : "--workspace-links",NULL};
            if (!native_capture_start(p,argv,app->fleet ? 3500 : 2000)) {
                if (i==0) { record_snapshot(app,false); copy_text(app->snapshot_error,sizeof(app->snapshot_error),"Unable to start snapshot observation"); }
                else if (i==1) (void)accept_workflows(app,NULL);
                else if (i==2) (void)accept_statistics(app,NULL);
                else native_links_accept(app,NULL);
            }
        }
    }
    native_evidence_tick(app,request);
}
