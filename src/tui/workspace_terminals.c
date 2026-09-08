#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Two visible attachments per layout; cached clients retain terminal state.
 * A session is painted in at most one pane, so it receives one terminal size. */
int native_workspace_agent_index(struct native_workspace *w, int pane) {
    int first=w->agents[w->mode].first;
    return first ? pane>=first && pane<=first+1 ? pane-first : -1 : pane==3 ? 0 : -1;
}

struct native_terminal *native_workspace_terminal(struct app *app, int pane) {
    struct native_workspace *w=app->workspace;
    int index;
    size_t slot;
    if (!w || !app->terminals || (index=native_workspace_agent_index(w,pane))<0) return NULL;
    if (!w->agents[w->mode].first) return native_terminal_selected(app);
    slot=w->agents[w->mode].slots[index];
    return slot<NATIVE_TERMINALS ? &app->terminals->slots[slot] : NULL;
}

void native_workspace_sync_terminal(struct app *app) {
    struct native_terminal *t=native_workspace_terminal(app,app->workspace->layout.focus);
    size_t head;
    if (!t || !t->screen) return;
    app->terminals->selected=(size_t)(t-app->terminals->slots);
    for (head=0;head<app->model.head_count;head++)
        if (!strcmp(app->model.heads[head].head_id,t->head) && !strcmp(app->model.heads[head].instance,t->instance)) {
            app->selected=head; break;
        }
}

void native_workspace_focus_next(struct app *app) {
    tv_workspace_focus_next(&app->workspace->layout,1);
    native_workspace_sync_terminal(app);
}

/* Explicit attachment selects its existing pane. Layout changes keep both the
 * active session and restored pane focus, swapping bindings when necessary. */
void native_workspace_show_terminal(struct app *app, bool focus) {
    struct native_workspace *w=app->workspace;
    int first=w->agents[w->mode].first, target=native_workspace_agent_index(w,w->layout.focus), i;
    size_t selected;
    if (!app->terminals) return;
    selected=app->terminals->selected;
    if (first) {
        if (target<0) target=0;
        for (i=0;i<2;i++) if (w->agents[w->mode].slots[i]==selected) break;
        if (focus && i<2) target=i;
        else if (i<2 && i!=target) {
            if (native_workspace_agent_index(w,w->layout.focus)<0) return;
            w->agents[w->mode].slots[i]=w->agents[w->mode].slots[target];
        }
        w->agents[w->mode].slots[target]=selected;
    }
    if (focus) w->layout.focus=first ? first+target : 3;
}

void native_workspace_split_agents(struct app *app) {
    struct native_workspace *w=app->workspace;
    int first=w->agents[w->mode].first;
    size_t selected, other;
    if (first) {
        /* This is always the last split added to this layout. */
        w->layout.panes[3]=w->layout.panes[first];
        w->layout.panes[3].parent=2;
        memset(&w->layout.panes[first],0,2*sizeof(w->layout.panes[0]));
        w->layout.count=first;
        if (w->layout.focus>=first) w->layout.focus=3;
        w->layout.drag=-1;
        w->agents[w->mode].first=0;
        native_workspace_invalidate(app);
        return;
    }
    if (!app->terminals || !native_terminal_selected(app)->screen) goto unavailable;
    selected=app->terminals->selected;
    for (other=0;other<NATIVE_TERMINALS;other++) if (other!=selected && app->terminals->slots[other].screen) break;
    if (other==NATIVE_TERMINALS) goto unavailable;
    first=tv_workspace_split(&w->layout,3,TV_COLUMNS,500);
    if (first<0) return;
    w->agents[w->mode].first=first;
    w->agents[w->mode].slots[0]=selected; w->agents[w->mode].slots[1]=other;
    w->layout.focus=first;
    native_workspace_invalidate(app);
    copy_text(app->notice,sizeof(app->notice),"Two attached agents / Ctrl-B Tab changes focus / Ctrl-B S restores one pane");
    return;
unavailable:
    copy_text(app->notice,sizeof(app->notice),"Attach two local heads with a before splitting agent panes");
}
