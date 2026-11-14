#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Which attached client each conversation pane shows. One pane shows the
 * selected head's client; S splits it into two agents side by side, each
 * bound to a client slot. Cached clients keep their terminal state, and a
 * session is painted in at most one pane, so it receives one terminal size. */
int native_workspace_agent_index(struct native_workspace *w, int pane) {
    int first = w->agents.first;
    if (first) return pane >= first && pane <= first + 1 ? pane - first : -1;
    return pane == WORK_PANE_CONVERSATION ? 0 : -1;
}

/* The client a pane shows: the selected head's, or the split pane's slot. */
struct native_terminal *native_workspace_terminal(struct app *app, int pane) {
    struct native_workspace *w = app->workspace;
    int index;
    size_t slot;
    if (!w || !app->terminals || (index = native_workspace_agent_index(w, pane)) < 0) return NULL;
    if (!w->agents.first) {
        if ((w->merged || w->compact) && work_page_shown(app, false) != WORK_CONVERSATION) return NULL;
        return work_terminal(app);
    }
    slot = w->agents.slots[index];
    return slot < NATIVE_TERMINALS ? &app->terminals->slots[slot] : NULL;
}

/* A focused agent pane selects its head, so the list follows the agent. */
void native_workspace_sync_terminal(struct app *app) {
    struct native_terminal *t;
    size_t head;
    if (!app->workspace || !app->workspace->agents.first) return;
    t = native_workspace_terminal(app, app->workspace->layout.focus);
    if (!t || !t->screen) return;
    app->terminals->selected = (size_t)(t - app->terminals->slots);
    for (head = 0; head < app->model.head_count; head++)
        if (!strcmp(app->model.heads[head].head_id, t->head) && !strcmp(app->model.heads[head].instance, t->instance) &&
            !strcmp(app->model.heads[head].remote_host, t->remote_host) &&
            !strcmp(app->model.heads[head].remote_project, t->remote_project)) {
            app->run_row = app->task_row = false;
            app->selected = head; break;
        }
}

/* An attachment shows in the conversation; with focus, keys go to it. A
 * split pane takes the attached client into the pane that has focus. */
void native_workspace_show_terminal(struct app *app, bool focus) {
    struct native_workspace *w = app->workspace;
    int first, target = 0, i;
    size_t selected;
    if (!w || !app->terminals) return;
    first = w->agents.first;
    selected = app->terminals->selected;
    if (first) {
        target = native_workspace_agent_index(w, w->layout.focus);
        if (target < 0) target = 0;
        for (i = 0; i < 2; i++) if (w->agents.slots[i] == selected) break;
        if (i < 2) target = i;
        w->agents.slots[target] = selected;
    }
    if (!focus) return;
    w->layout.focus = first ? first + target : WORK_PANE_CONVERSATION;
    if (w->merged || w->compact) work_set_page(app, WORK_CONVERSATION);
}

void native_workspace_split_agents(struct app *app) {
    struct native_workspace *w = app->workspace;
    int first = w->agents.first;
    size_t selected, other;
    if (first) {
        /* This is always the last split added to the layout. */
        w->layout.panes[WORK_PANE_CONVERSATION] = w->layout.panes[first];
        w->layout.panes[WORK_PANE_CONVERSATION].parent = 2; w->layout.panes[WORK_PANE_CONVERSATION].min_width = 24;
        memset(&w->layout.panes[first], 0, 2 * sizeof(w->layout.panes[0]));
        w->layout.count = first;
        if (w->layout.focus >= first) w->layout.focus = WORK_PANE_CONVERSATION;
        w->layout.drag = -1;
        w->agents.first = 0;
        return;
    }
    if (!app->terminals || !native_terminal_selected(app)->screen) goto unavailable;
    selected = app->terminals->selected;
    for (other = 0; other < NATIVE_TERMINALS; other++) if (other != selected && app->terminals->slots[other].screen) break;
    if (other == NATIVE_TERMINALS) goto unavailable;
    first = kt_workspace_split(&w->layout, WORK_PANE_CONVERSATION, KT_COLUMNS, 500);
    if (first < 0) return;
    /* Two conversations share the centre side by side. */
    w->layout.panes[first].min_width = w->layout.panes[first + 1].min_width = 30;
    w->agents.first = first;
    w->agents.slots[0] = selected; w->agents.slots[1] = other;
    w->layout.focus = first;
    key_hint(app, app->notice, sizeof(app->notice), " / ", "Two attached agents|Tab changes focus|{workspace.split} restores one pane");
    return;
unavailable:
    copy_text(app->notice, sizeof(app->notice), "Attach two heads with a before splitting the conversation");
}
