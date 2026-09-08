#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
static bool native_plan_accept(struct native_plan *p, FILE *input) {
    struct workflow_model *graph=calloc(1,sizeof(*graph));
    char *preview=calloc(1,NATIVE_PLAN_TEXT+1), *line=NULL;
    char digest[65]="", objective[4096]="";
    size_t capacity=0, bytes=0, used=0, lines=0;
    bool header=false, metadata=false, trailer=false, valid=false;
    ssize_t length;
    if (!graph || !preview) goto done;
    while ((length=getline(&line,&capacity,input))>=0) {
        char *fields[6];
        size_t count,i;
        if (!length || line[length-1]!='\n' || memchr(line,'\0',(size_t)length) ||
            (bytes+=(size_t)length)>MAX_DATA_BYTES || trailer) goto done;
        line[length-1]='\0'; count=split_fields(line,fields,6);
        if (!header) {
            if (count!=2 || strcmp(fields[0],"HYDRA_PLAN_TUI") || strcmp(fields[1],"1")) goto done;
            header=true; continue;
        }
        if (!metadata && count==4 && !strcmp(fields[0],"P")) {
            if (strlen(fields[1])!=64 || !workflow_id(fields[2])) goto done;
            for (i=0;i<64;i++) if (!strchr("0123456789abcdef",fields[1][i])) goto done;
            copy_text(digest,sizeof(digest),fields[1]); copy_text(objective,sizeof(objective),fields[3]);
            copy_text(graph->runs[0].id,sizeof(graph->runs[0].id),fields[2]);
            copy_text(graph->runs[0].name,sizeof(graph->runs[0].name),fields[2]);
            copy_text(graph->runs[0].state,sizeof(graph->runs[0].state),"awaiting-approval");
            graph->run_count=1; metadata=true;
        } else if (metadata && !lines && count==5 && !strcmp(fields[0],"N")) {
            struct workflow_node *node;
            if (graph->node_count>=64 || !workflow_id(fields[1]) || strlen(fields[1])>=65 ||
                strlen(fields[2])>=32 || strlen(fields[3])>=40 || !fields[4][0] || strlen(fields[4])>=1024) goto done;
            for (i=0;i<graph->node_count;i++) if (!strcmp(graph->nodes[i].id,fields[1])) goto done;
            node=&graph->nodes[graph->node_count++];
            copy_text(node->id,sizeof(node->id),fields[1]); copy_text(node->kind,sizeof(node->kind),fields[2]);
            copy_text(node->state,sizeof(node->state),fields[3]); copy_text(node->needs,sizeof(node->needs),fields[4]);
        } else if (metadata && count==2 && !strcmp(fields[0],"T")) {
            size_t n=strlen(fields[1]);
            if (n+1>NATIVE_PLAN_TEXT-used) goto done;
            memcpy(preview+used,fields[1],n); used+=n; preview[used++]='\n'; lines++;
        } else if (metadata && count==3 && !strcmp(fields[0],"Z")) {
            unsigned nodes, text_lines;
            if (!parse_unsigned(fields[1],&nodes) || !parse_unsigned(fields[2],&text_lines) ||
                nodes!=graph->node_count || text_lines!=lines || !nodes || !lines) goto done;
            trailer=true;
        } else goto done;
    }
    if (!trailer || ferror(input)) goto done;
    {
        size_t indices[TV_GRAPH_MAX_NODES], count, n=workflow_nodes(graph,0,indices);
        struct tv_edge edges[TV_GRAPH_MAX_EDGES]; struct tv_graph_layout layout;
        if (!workflow_edges(graph,indices,n,edges,&count) || !tv_graph_layout(edges,count,n,&layout)) goto done;
    }
    p->graph=*graph;
    free(p->text); p->text=preview; preview=NULL; p->text_length=used; p->text_scroll=0;
    copy_text(p->digest,sizeof(p->digest),digest); copy_text(p->objective,sizeof(p->objective),objective);
    if (p->selected>=p->graph.node_count) p->selected=0;
    valid=true;
done:
    free(graph); free(preview); free(line); return valid;
}
void native_plan_tick(struct app *app, bool watch) {
    struct native_plan *p=app->plan;
    if (!p) return;
    if (p->job.pid && native_capture_step(&p->job)) {
        bool success;
        FILE *input=native_capture_result(&p->job,&success);
        if (!input) {
            p->state=PLAN_UNAVAILABLE; native_plan_message(p,"No complete compiler response. The read failed or timed out; no execution occurred.");
        } else if (native_plan_changed(p)) {
            char path[4096],policy[4096];
            copy_text(path,sizeof(path),p->path); copy_text(policy,sizeof(policy),p->policy);
            (void)native_plan_load(app,path,policy);
            native_plan_message(p,"Draft or policy changed during validation. This is a new revision; press V to validate it.");
        } else if (!success) {
            char *text=calloc(1,NATIVE_PLAN_TEXT+1);
            size_t n=text ? fread(text,1,NATIVE_PLAN_TEXT,input) : 0;
            p->state=PLAN_INVALID;
            if (text && n && !ferror(input)) native_plan_message(p,text);
            else native_plan_message(p,"Validation failed without a complete diagnostic. Inspect workflow plan validate through the CLI.");
            free(text);
        } else if (!p->projecting) {
            char *argv[]={(char *)app->hydra,"workflow","plan","tui-data",p->compiled,NULL};
            p->projecting=true;
            if (!native_capture_start(&p->job,argv,10000)) {
                p->state=PLAN_UNAVAILABLE; native_plan_message(p,"Unable to load the compiled preview.");
            }
        } else if (native_plan_accept(p,input)) p->state=PLAN_READY;
        else { p->state=PLAN_UNAVAILABLE; native_plan_message(p,"The compiled preview is unavailable, incomplete or exceeds UI bounds. Inspect it with workflow plan show."); }
        if (input) fclose(input);
    }
    if (watch && !p->job.pid && p->path[0] && native_plan_changed(p)) {
        char path[4096],policy[4096];
        copy_text(path,sizeof(path),p->path); copy_text(policy,sizeof(policy),p->policy);
        (void)native_plan_load(app,path,policy);
        if (p->state==PLAN_DRAFT) native_plan_message(p,"Draft or policy changed. Previous approval is invalid; press V to validate this revision.");
    }
}
