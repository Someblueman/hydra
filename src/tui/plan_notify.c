#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Hydra-authored messages typed into the planning agent's conversation. The
 * agent reads its terminal, not Hydra's inbox, so interactive heads receive
 * text as input. Typing text is not evidence that the agent acted on it. */

struct head *native_plan_head(struct app *app, const char *branch) {
    size_t i;
    if (app->fleet || !branch[0]) return NULL;
    for (i=0;i<app->model.head_count;i++) if (!strcmp(app->model.heads[i].branch,branch)) return &app->model.heads[i];
    return NULL;
}

/* Attaches the head's conversation when needed and queues the text. Focus
 * moves the user into that pane in the conversation layout. */
bool native_plan_converse(struct app *app, const struct head *h, const char *text, bool submit, bool focus) {
    struct native_terminal *t=native_terminal_for_head(app,h);
    if (!t) {
        if (!native_workspace_init(app) || !native_terminal_attach_head(app,h)) return false;
        t=native_terminal_for_head(app,h);
    }
    if (!t || !native_terminal_deliver(app,t,text,submit)) return false;
    if (focus) {
        app->terminals->selected=(size_t)(t-app->terminals->slots);
        if (app->workspace->mode!=0) native_workspace_mode(app,0);
        app->view=7;
        native_workspace_show_terminal(app,true);
    }
    return true;
}

/* The exact planning instance bound at launch; NULL once it is not current. */
static struct head *planning_instance(struct app *app) {
    struct native_plan *p=app->plan;
    size_t i;
    if (!p || !p->planning_head[0]) return NULL;
    for (i=0;i<app->model.head_count;i++) {
        struct head *h=&app->model.heads[i];
        if (!strcmp(h->head_id,p->planning_head) && !strcmp(h->instance,p->planning_instance)) return h;
    }
    return NULL;
}

/* Each notice key is typed at most once per launch. */
static bool notice_once(struct native_plan *p, const char *key) {
    size_t i;
    for (i=0;i<p->notified_count;i++) if (!strcmp(p->notified[i],key)) return false;
    if (p->notified_count>=NATIVE_PLAN_NOTICES) return false;
    copy_text(p->notified[p->notified_count++],sizeof(p->notified[0]),key);
    return true;
}

/* Hydra events are submitted: they are not user-drafted text, and the agent
 * must see them to follow the run. */
static void notify(struct app *app, const char *key, const char *text) {
    struct head *h=planning_instance(app);
    if (!h || !notice_once(app->plan,key)) return;
    if (!native_plan_converse(app,h,text,true,false))
        snprintf(app->notice,sizeof(app->notice),"Run notice not typed into %.100s; its inbox holds the recorded notice",h->branch);
}

void native_plan_notify_started(struct app *app) {
    struct native_plan *p=app->plan;
    char text[1024];
    const char *run=p->launched_run;
    snprintf(text,sizeof(text),"Hydra: the user approved revision %u (digest %.12s) and run %s started. "
        "Follow it with hydra workflow status %s --json; relay progress, any approval requests (hydra workflow requests %s), "
        "failures and the final result to the user. Do not execute or modify the run yourself.",
        p->launched_revision,p->launched_digest,run,run,run);
    notify(app,"started",text);
}

static void notify_step(struct app *app, const struct workflow_node *node, const char *run) {
    char key[112], text[1024];
    if (!strcmp(node->state,"waiting-approval")) {
        snprintf(key,sizeof(key),"approval.%s",node->id);
        snprintf(text,sizeof(text),"Hydra: run %s is waiting for an approval decision on step %s. Tell the user; the user decides in Hydra (hydra workflow requests %s).",run,node->id,run);
    } else if (!strcmp(node->state,"failed") || !strcmp(node->state,"recovery-required")) {
        snprintf(key,sizeof(key),"%s.%s",node->state,node->id);
        snprintf(text,sizeof(text),"Hydra: step %s %s in run %s. Tell the user and show hydra workflow status %s; do not retry or modify the run yourself.",
            node->id,!strcmp(node->state,"failed") ? "failed" : "needs recovery",run,run);
    } else return;
    notify(app,key,text);
}

/* Run state changes that need the user, observed in the workflow snapshot. */
void native_plan_notices_tick(struct app *app) {
    struct native_plan *p=app->plan;
    size_t r,i;
    if (!p || !p->launched_run[0] || !app->workflows || app->workflow_stale) return;
    for (r=0;r<app->workflows->run_count && strcmp(app->workflows->runs[r].id,p->launched_run);r++) {}
    if (r==app->workflows->run_count) return;
    for (i=0;i<app->workflows->node_count;i++)
        if (app->workflows->nodes[i].run==r) notify_step(app,&app->workflows->nodes[i],p->launched_run);
    {
        const char *state=app->workflows->runs[r].state;
        char key[112], text[512];
        if (strcmp(state,"succeeded") && strcmp(state,"failed") && strcmp(state,"cancelled") && strcmp(state,"recovery-required")) return;
        snprintf(key,sizeof(key),"run.%s",state);
        snprintf(text,sizeof(text),"Hydra: run %s finished: %s. Report the outcome to the user (hydra workflow plan result %s).",
            p->launched_run,!strcmp(state,"recovery-required") ? "needs recovery" : state,p->launched_run);
        notify(app,key,text);
    }
}
