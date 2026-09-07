static void statistics_toggle(struct app *app) {
    if (!statistics_init(app)) return;
    if (app->view == 8) app->view = app->statistics->back_view;
    else {
        app->statistics->back_view = app->view;
        app->statistics->graph_open = false;
        app->view = 8;
        (void)refresh_statistics(app, NULL);
    }
    app->help = false; app->diagnostics = false;
    app->notice[0] = '\0';
}

static void statistics_graph(struct app *app) {
    struct statistics_view *v = app->statistics;
    size_t i;
    if (app->fleet || !v->model || !v->count) return;
    if (refresh_workflows(app,NULL)) {
        copy_text(app->notice,sizeof(app->notice),"Graph unavailable; statistics selection retained"); return;
    }
    for (i=0;i<app->workflows->run_count;i++) if (!strcmp(app->workflows->runs[i].id,v->selected_id)) break;
    if (i == app->workflows->run_count) {
        copy_text(app->notice,sizeof(app->notice),"Selected run outside graph sample; inspect recorded run evidence here"); return;
    }
    app->workflow_run=i; app->workflow_node=0; app->graph_x=app->graph_y=0; app->graph_follow=true;
    app->view=5; v->graph_open=true;
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

static bool statistics_key(struct app *app, char key) {
    struct statistics_view *v=app->statistics;
    if(!v) return false;
    app->notice[0]='\0';
    if(key=='j' || key=='k') statistics_move(app,key=='j' ? 1 : -1);
    else if(key=='T' && !app->fleet) { v->filter.days=v->filter.days==0 ? 1 : v->filter.days==1 ? 7 : 0; v->detail=false; }
    else if(key=='!') { v->filter.attention=!v->filter.attention; v->detail=false; }
    else if(key=='0') { memset(&v->filter,0,sizeof(v->filter)); v->detail=false; }
    else if((key=='[' || key==']') && !app->fleet) { statistics_workflow(v,key==']' ? 1 : -1); v->detail=false; }
    else if(key=='/') {
        char query[80];
        if(prompt_text(app,app->fleet ? "Find host: " : "Find workflow, run or project: ",query,sizeof(query))==0) {
            copy_text(v->filter.query,sizeof(v->filter.query),query); v->detail=false;
        }
    }
    else if(key=='\r' || key=='\n') { v->detail=!v->detail; v->step_scroll=0; }
    else if(key=='g') statistics_graph(app);
    else if(key=='r') (void)refresh_statistics(app,NULL);
    else if(strchr(":pac AxGd",key)) copy_text(app->notice,sizeof(app->notice),"Statistics is read-only; Enter evidence / g graph / D back");
    else return false;
    statistics_visible(app);
    return true;
}

static bool statistics_back(struct app *app) {
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
