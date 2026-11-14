#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
void statistics_toggle(struct app *app) {
    if (!statistics_init(app)) return;
    if (app->view == 8) app->view = app->statistics->back_view;
    else {
        app->statistics->back_view = app->view;
        app->statistics->graph_open = false;
        app->view = 8;
        statistics_request(app);
    }
    app->help = false; app->diagnostics = false;
    app->notice[0] = '\0';
}

#define GRAPH_LOADING "Opening the graph..."

/* Shows the selected run in the graph view when the retained workflow read
 * includes it. */
static bool statistics_graph_open(struct app *app) {
    struct statistics_view *v = app->statistics;
    size_t i;
    for (i=0;i<app->workflows->run_count;i++) if (!strcmp(app->workflows->runs[i].id,v->selected_id)) break;
    if (i == app->workflows->run_count) return false;
    app->workflow_run=i; app->workflow_node=0; app->graph_x=app->graph_y=0; app->graph_follow=true;
    app->view=5; v->graph_open=true;
    return true;
}

/* g asks for a fresh workflow read and does not wait for it. A retained read
 * that shows the run opens the graph at once; otherwise the graph opens, or
 * says why it cannot, when the read arrives. */
static void statistics_graph(struct app *app) {
    struct statistics_view *v = app->statistics;
    if (app->fleet || !v->model || !v->count) return;
    v->graph_run[0]='\0';
    native_observations_request(app,OBSERVE_WORKFLOWS);
    if (app->workflows && !app->workflow_stale && statistics_graph_open(app)) return;
    copy_text(v->graph_run,sizeof(v->graph_run),v->selected_id);
    copy_text(app->notice,sizeof(app->notice),GRAPH_LOADING);
}

/* A workflow read ended. The graph that g asked for opens only if the same
 * run is still selected in Statistics. */
void statistics_graph_arrived(struct app *app, bool ok) {
    struct statistics_view *v = app->statistics;
    bool wanted;
    if (!v || !v->graph_run[0]) return;
    wanted = app->view==8 && !strcmp(v->graph_run,v->selected_id);
    v->graph_run[0]='\0';
    if (!strcmp(app->notice,GRAPH_LOADING)) app->notice[0]='\0';
    if (!wanted) return;
    if (!ok) copy_text(app->notice,sizeof(app->notice),"Graph unavailable; statistics selection retained");
    else if (!statistics_graph_open(app))
        copy_text(app->notice,sizeof(app->notice),"Selected run outside graph sample; inspect recorded run evidence here");
}

static void statistics_workflow(struct statistics_view *v, int direction) {
    const char *names[HS_RUNS+1] = {""};
    size_t count=1, selected=0, i, j;
    if (!v->model) return;
    for(i=0;i<v->model->run_count;i++) {
        for(j=1;j<count;j++) if(!strcmp(names[j],v->model->runs[i].name)) break;
        if(j==count) names[count++]=v->model->runs[i].name;
    }
    for(i=1;i<count;i++) if(!strcmp(names[i],v->filter.workflow)) selected=i;
    selected=(selected+(direction>0 ? 1 : count-1))%count;
    copy_text(v->filter.workflow,sizeof(v->filter.workflow),names[selected]);
}

static bool statistics_filter_key(struct app *app, char key) {
    struct statistics_view *v=app->statistics;
    if(key=='M' && !app->fleet) { v->metric_page=(v->metric_page+1)%(HS_METRICS+1); v->step_scroll=0; }
    else if(key=='T' && !app->fleet) { v->filter.days=v->filter.days==0 ? 1 : v->filter.days==1 ? 7 : 0; v->detail=false; }
    else if(key=='!') { v->filter.attention=!v->filter.attention; v->detail=false; }
    else if(key=='0') { memset(&v->filter,0,sizeof(v->filter)); v->detail=false; }
    else return false;
    return true;
}

/* Keys of the workflow runs page (statistics_key routes to it). */
bool statistics_runs_key(struct app *app, char key) {
    struct statistics_view *v=app->statistics;
    if(!v) return false;
    app->notice[0]='\0';
    if(key=='j' || key=='k') statistics_move(app,key=='j' ? 1 : -1);
    else if(statistics_filter_key(app,key)) { }
    else if((key=='[' || key==']') && !app->fleet) { statistics_workflow(v,key==']' ? 1 : -1); v->detail=false; }
    else if(key=='/') {
        char query[80];
        if(prompt_text(app,app->fleet ? "Find host: " : "Find workflow, run or project: ",query,sizeof(query))==0) {
            copy_text(v->filter.query,sizeof(v->filter.query),query); v->detail=false;
        }
    }
    else if(key=='\r' || key=='\n') { v->detail=!v->detail; v->step_scroll=0; }
    else if(key=='g') statistics_graph(app);
    else if(key=='r') statistics_request(app);
    else if(strchr("pac AxGd",key)) copy_text(app->notice,sizeof(app->notice),"Statistics is read-only; Enter evidence / g graph / D back");
    else return false;
    statistics_visible(app);
    return true;
}

bool statistics_back(struct app *app) {
    struct statistics_view *v=app->statistics;
    if(!v) return false;
    if(app->view==8) {
        if(app->help) app->help=false;
        else if(v->detail) v->detail=false;
        else app->view=v->back_view;
    } else if(app->view==5 && v->graph_open) { app->view=8; v->graph_open=false; }
    else return false;
    return true;
}
