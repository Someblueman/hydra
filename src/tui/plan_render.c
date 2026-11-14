#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Wrapped documents share painting and clamp scroll when their length changes. */
static void native_workspace_text(struct kt_canvas *c, const char *text, size_t length, size_t *scroll) {
    int pass;
    if (!text || c->height<2 || c->width<1) return;
    for (pass=0;pass<2;pass++) {
        size_t offset=0,line=0;
        int x=0,y=1;
        while (offset<length && (!pass || y<c->height)) {
            uint32_t cp;
            size_t used=kt_utf8_decode(text+offset,length-offset,&cp);
            int width;
            if (!used) break;
            offset+=used;
            if (cp=='\n') { x=0; if (line++>=*scroll && pass) y++; continue; }
            if (cp<32 || cp==127) cp=' ';
            width=kt_codepoint_width(cp);
            if (width<0 || (!c->unicode && cp>126)) { cp='?'; width=1; }
            if (x+width>c->width) { x=0; if (line++>=*scroll && pass) y++; }
            if (pass && y>=c->height) break;
            if (pass && line>=*scroll) kt_put(c,x,y,cp,KT_BASE);
            x+=width;
        }
        if (!pass) {
            size_t visible=(size_t)(c->height-1),maximum=line+1>visible ? line+1-visible : 0;
            if (*scroll>maximum) *scroll=maximum;
        }
    }
}

void native_workspace_evidence_text(struct app *app, struct kt_canvas *c, size_t *scroll) {
    struct native_evidence *e=app->evidence;
    const char *state="unavailable";
    size_t i;
    if (!e || !e->run[0]) { kt_text(c,(struct kt_rect){0,0,c->width,1},"No selected recorded run",KT_WARNING); return; }
    if (app->workflows) for (i=0;i<app->workflows->run_count;i++)
        if (!strcmp(app->workflows->runs[i].id,e->run)) { state=app->workflows->runs[i].state; break; }
    canvas_text(c,0,0,c->width,e->stale || e->verdict==3 ? KT_WARNING : KT_STRONG,"%s / %s%s / %s",
        e->stale ? "STALE EVIDENCE" : !e->text ? "LOADING EVIDENCE" : e->verdict==2 ? "VERIFIED ARTIFACTS" :
        e->verdict==3 ? "ARTIFACTS REFUSED" : c->width<60 ? "OBSERVED" : "OBSERVED EVIDENCE",
        c->width<60 ? "" : "run ",state,e->step);
    if (e->text) native_workspace_text(c,e->text,e->length,scroll);
    else kt_text(c,(struct kt_rect){0,1,c->width,1},e->stale ? "Complete evidence unavailable" : "Reading run, output and artifact records",KT_WARNING);
}

/* The planning graph is the compiled preview ("awaiting-approval"); once the
 * revision on screen has a recorded run in the workflow snapshot, the run and
 * its steps show their live states instead. */
static const struct workflow_model *launched_run(const struct app *app, size_t *run) {
    const struct native_plan *p=app->plan;
    size_t i;
    if (!p || !p->launched_run[0] || strcmp(p->digest,p->launched_digest) || !app->workflows) return NULL;
    for (i=0;i<app->workflows->run_count;i++)
        if (!strcmp(app->workflows->runs[i].id,p->launched_run)) { *run=i; return app->workflows; }
    return NULL;
}
static const char *live_state(const struct workflow_model *live, size_t run, const struct workflow_node *node) {
    size_t i;
    if (live) for (i=0;i<live->node_count;i++)
        if (live->nodes[i].run==run && !strcmp(live->nodes[i].id,node->id)) return live->nodes[i].state;
    return node->state;
}
static void graph_nodes(const struct workflow_model *m, const size_t *indices, size_t n,
                        const struct workflow_model *live, size_t live_run, struct kt_node *nodes) {
    size_t i;
    for (i=0;i<n;i++) {
        const struct workflow_node *node=&m->nodes[indices[i]];
        const char *state=live_state(live,live_run,node);
        nodes[i]=(struct kt_node){node->id,state,!strcmp(state,"failed") ? KT_WARNING : KT_BASE};
    }
}
/* The graph a pane draws: the plan's compiled preview, or the selected workflow run. */
static struct workflow_model *graph_source(struct app *app, bool planning, size_t *run, size_t **selected) {
    *run=planning ? 0 : app->workflow_run;
    *selected=&app->workflow_node;
    if (!planning) return app->workflows;
    if (!app->plan) return NULL;
    *selected=&app->plan->selected;
    return &app->plan->graph;
}
void native_workspace_graph(struct app *app, struct kt_canvas *c, bool planning) {
    size_t run, indices[KT_GRAPH_MAX_NODES], n,count,live_run=0, *selected;
    struct workflow_model *m=graph_source(app,planning,&run,&selected);
    const struct workflow_model *live=planning ? launched_run(app,&live_run) : NULL;
    struct kt_edge edges[KT_GRAPH_MAX_EDGES];
    struct kt_node nodes[KT_GRAPH_MAX_NODES];
    struct kt_graph_layout layout;
    int sx,sy;
    if (!m || !m->run_count) { kt_text(c,(struct kt_rect){0,0,c->width,1},planning ? "Validate a draft to inspect dependencies" : "No recorded run",KT_WARNING); return; }
    if (run>=m->run_count) run=0;
    n=workflow_nodes(m,run,indices);
    if (!n || !workflow_edges(m,indices,n,edges,&count) || !kt_graph_layout(edges,count,n,&layout)) return;
    if (*selected>=n) *selected=0;
    canvas_text(c,0,0,c->width,KT_STRONG,"%s / %s",m->runs[run].name,live ? live->runs[live_run].state : m->runs[run].state);
    if (c->width<26 || c->height<7) {
        const struct workflow_node *node=&m->nodes[indices[*selected]];
        canvas_text(c,0,1,c->width,KT_BASE,"%zu/%zu %s [%s]",*selected+1,n,node->id,live_state(live,live_run,node));
        canvas_text(c,0,2,c->width,KT_BASE,"Needs: %s",node->needs); return;
    }
    graph_nodes(m,indices,n,live,live_run,nodes);
    sx=layout.x[*selected]-(c->width-22)/2; sy=layout.y[*selected]-(c->height-5)/2;
    kt_graph(c,(struct kt_rect){0,1,c->width,c->height-1},nodes,n,edges,count,&layout,sx<0 ? 0 : sx,sy<0 ? 0 : sy,*selected);
}
