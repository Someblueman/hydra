/* Wrapped documents share painting and clamp scroll when their length changes. */
static void native_workspace_text(struct tv_canvas *c, const char *text, size_t length, size_t *scroll) {
    int pass;
    if (!text || c->height<2 || c->width<1) return;
    for (pass=0;pass<2;pass++) {
        size_t offset=0,line=0;
        int x=0,y=1;
        while (offset<length && (!pass || y<c->height)) {
            uint32_t cp;
            size_t used=tv_utf8_decode(text+offset,length-offset,&cp);
            int width;
            if (!used) break;
            offset+=used;
            if (cp=='\n') { x=0; if (line++>=*scroll && pass) y++; continue; }
            if (cp<32 || cp==127) cp=' ';
            width=tv_codepoint_width(cp);
            if (width<0 || (!c->unicode && cp>126)) { cp='?'; width=1; }
            if (x+width>c->width) { x=0; if (line++>=*scroll && pass) y++; }
            if (pass && y>=c->height) break;
            if (pass && line>=*scroll) tv_put(c,x,y,cp,TV_BASE);
            x+=width;
        }
        if (!pass) {
            size_t visible=(size_t)(c->height-1),maximum=line+1>visible ? line+1-visible : 0;
            if (*scroll>maximum) *scroll=maximum;
        }
    }
}

static void native_workspace_evidence_text(struct app *app, struct tv_canvas *c, size_t *scroll) {
    struct native_evidence *e=app->evidence;
    const char *state="unavailable";
    size_t i;
    if (!e || !e->run[0]) { tv_text(c,(struct tv_rect){0,0,c->width,1},"No selected recorded run",TV_WARNING); return; }
    if (app->workflows) for (i=0;i<app->workflows->run_count;i++)
        if (!strcmp(app->workflows->runs[i].id,e->run)) { state=app->workflows->runs[i].state; break; }
    dashboard_text(c,0,0,c->width,e->stale || e->verdict==3 ? TV_WARNING : TV_STRONG,"%s / %s%s / %s",
        e->stale ? "STALE EVIDENCE" : !e->text ? "LOADING EVIDENCE" : e->verdict==2 ? "VERIFIED ARTIFACTS" :
        e->verdict==3 ? "ARTIFACTS REFUSED" : c->width<60 ? "OBSERVED" : "OBSERVED EVIDENCE",
        c->width<60 ? "" : "run ",state,e->step);
    if (e->text) native_workspace_text(c,e->text,e->length,scroll);
    else tv_text(c,(struct tv_rect){0,1,c->width,1},e->stale ? "Complete evidence unavailable" : "Reading run, output and artifact records",TV_WARNING);
}

static void native_workspace_plan_text(struct app *app, struct tv_canvas *c, size_t *scroll) {
    struct native_plan *p=app->plan;
    const char *text;
    size_t length;
    static const char *states[]={"DRAFT / unvalidated","VALIDATING","INVALID","READY / awaiting approval","VIEW UNAVAILABLE"};
    if (!p) { tv_text(c,(struct tv_rect){0,0,c->width,c->height},"P load draft + policy / V validate",TV_WARNING); return; }
    dashboard_text(c,0,0,c->width,TV_STRONG,"Revision %u / %s",p->revision,
        p->state==PLAN_READY && !strcmp(p->digest,p->launched_digest) ?
        (p->launched_run[0] ? "RUN RECORDED / C monitor" : "LAUNCH REQUESTED / awaiting run receipt") : states[p->state]);
    text=p->state==PLAN_DRAFT && p->source_bytes ? p->source_bytes : p->text;
    length=p->state==PLAN_DRAFT && p->source_bytes ? p->source_length : p->text_length;
    native_workspace_text(c,text,length,scroll);
}
static void native_workspace_graph(struct app *app, struct tv_canvas *c, bool planning) {
    struct workflow_model *m=planning ? app->plan ? &app->plan->graph : NULL : app->workflows;
    size_t run=planning ? 0 : app->workflow_run, indices[TV_GRAPH_MAX_NODES], n,count,i;
    size_t *selected=planning && app->plan ? &app->plan->selected : &app->workflow_node;
    struct tv_edge edges[TV_GRAPH_MAX_EDGES];
    struct tv_node nodes[TV_GRAPH_MAX_NODES];
    struct tv_graph_layout layout;
    int sx,sy;
    if (!m || !m->run_count) { tv_text(c,(struct tv_rect){0,0,c->width,1},planning ? "Validate a draft to inspect dependencies" : "No recorded run",TV_WARNING); return; }
    if (run>=m->run_count) run=0;
    n=workflow_nodes(m,run,indices);
    if (!n || !workflow_edges(m,indices,n,edges,&count) || !tv_graph_layout(edges,count,n,&layout)) return;
    if (*selected>=n) *selected=0;
    dashboard_text(c,0,0,c->width,TV_STRONG,"%s / %s",m->runs[run].name,m->runs[run].state);
    if (c->width<26 || c->height<7) {
        const struct workflow_node *node=&m->nodes[indices[*selected]];
        dashboard_text(c,0,1,c->width,TV_BASE,"%zu/%zu %s [%s]",*selected+1,n,node->id,node->state);
        dashboard_text(c,0,2,c->width,TV_BASE,"Needs: %s",node->needs); return;
    }
    for (i=0;i<n;i++) {
        struct workflow_node *node=&m->nodes[indices[i]];
        nodes[i]=(struct tv_node){node->id,node->state,!strcmp(node->state,"failed") ? TV_WARNING : TV_BASE};
    }
    sx=layout.x[*selected]-(c->width-22)/2; sy=layout.y[*selected]-(c->height-5)/2;
    tv_graph(c,(struct tv_rect){0,1,c->width,c->height-1},nodes,n,edges,count,&layout,sx<0 ? 0 : sx,sy<0 ? 0 : sy,*selected);
}
