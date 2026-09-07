/* Read-only evidence follows the selected recorded run and step. */
#define NATIVE_EVIDENCE_LIMIT (1024U*1024U)
struct native_evidence {
    struct native_capture job;
    char run[80], step[65];
    char *text;
    size_t length;
    unsigned verdict;
    bool stale;
};

static void native_evidence_destroy(struct app *app) {
    if (!app->evidence) return;
    native_capture_destroy(&app->evidence->job);
    free(app->evidence->text); free(app->evidence); app->evidence=NULL;
}

static bool native_evidence_accept(struct native_evidence *e, FILE *input) {
    char header[256],expected[256],*text;
    static const char *ends[]={"\nHYDRA_WORKFLOW_EVIDENCE_END\tunavailable\n","\nHYDRA_WORKFLOW_EVIDENCE_END\tpending\n",
        "\nHYDRA_WORKFLOW_EVIDENCE_END\tverified\n","\nHYDRA_WORKFLOW_EVIDENCE_END\trefused\n"};
    size_t length,end_length=0;
    unsigned verdict;
    if (!input || !fgets(header,sizeof(header),input)) return false;
    snprintf(expected,sizeof(expected),"HYDRA_WORKFLOW_EVIDENCE\t1\t%s\t%s\n",e->run,e->step);
    if (strcmp(header,expected)) return false;
    text=malloc(NATIVE_EVIDENCE_LIMIT+1);
    if (!text) return false;
    length=fread(text,1,NATIVE_EVIDENCE_LIMIT,input);
    for (verdict=0;verdict<4;verdict++) {
        end_length=strlen(ends[verdict]);
        if (length>=end_length && !memcmp(text+length-end_length,ends[verdict],end_length)) break;
    }
    if (ferror(input) || fgetc(input)!=EOF || verdict==4) {
        free(text); return false;
    }
    length-=end_length; text[length]='\0';
    free(e->text); e->text=text; e->length=length; e->verdict=verdict; return true;
}

static void native_evidence_tick(struct app *app, bool watch) {
    struct native_evidence *e=app->evidence;
    struct workflow_model *m=app->workflows;
    size_t indices[TV_GRAPH_MAX_NODES],count=0;
    const char *run="",*step="-";
    bool visible=app->view==7 && native_workspace_monitoring(app) && !app->fleet;
    if (visible && m && app->workflow_run<m->run_count) {
        run=m->runs[app->workflow_run].id;
        count=workflow_nodes(m,app->workflow_run,indices);
        if (count<=TV_GRAPH_MAX_NODES && app->workflow_node<count) step=m->nodes[indices[app->workflow_node]].id;
    }
    if (!e && !run[0]) return;
    if (!e) {
        e=calloc(1,sizeof(*e));
        if (!e) return;
        e->job.fd=-1; app->evidence=e;
    }
    if (visible && (strcmp(e->run,run) || strcmp(e->step,step))) {
        native_capture_destroy(&e->job);
        free(e->text); e->text=NULL; e->length=0; e->stale=false; e->verdict=0;
        copy_text(e->run,sizeof(e->run),run); copy_text(e->step,sizeof(e->step),step);
        watch=true;
    }
    if (e->job.pid && native_capture_step(&e->job)) {
        FILE *input=native_capture_take(&e->job);
        e->stale=!native_evidence_accept(e,input);
        if (input) fclose(input);
    }
    if (visible && watch && e->run[0] && !e->job.pid) {
        char *argv[]={(char *)app->hydra,"workflow","--workspace-evidence",e->run,e->step,NULL};
        if (!native_capture_start(&e->job,argv,10000)) e->stale=true;
    }
}
