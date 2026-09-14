#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Hydra's view owns data interpretation; termviz owns layout, focus and paint.
 * The workspace shares the frame, header, tabs and footer with every other view. */

bool native_workspace_monitoring(struct app *app) {
    return app->workspace && app->workspace->mode==2;
}

void native_workspace_destroy(struct app *app) {
    if (!app->workspace) return;
    free(app->workspace); app->workspace = NULL;
}

void native_workspace_invalidate(struct app *app) {
    frame_invalidate(app);
}

bool native_workspace_init(struct app *app) {
    struct native_workspace *w;
    if (app->workspace) return true;
    w = calloc(1, sizeof(*w));
    if (!w) return false;
    app->workspace = w;
    if (!frame_alloc(app)) { native_workspace_destroy(app); return false; }
    tv_workspace_init(&w->layout, 18, 5);
    (void)tv_workspace_split(&w->layout, 0, TV_COLUMNS, 300);
    (void)tv_workspace_split(&w->layout, 2, TV_ROWS, 560);
    w->layout.panes[3].min_width = 40;
    w->root_open = true; w->theme = app->theme;
    return true;
}


void native_workspace_mode(struct app *app, int mode) {
    struct native_workspace *w;
    if (!native_workspace_init(app)) return;
    w=app->workspace;
    if (mode==w->mode && app->view==7) {
        copy_text(app->notice,sizeof(app->notice),mode==0 ? "Already in the conversation layout" : mode==1 ? "Already in the plan overview; A returns to the conversation" : "Already monitoring; A returns to the conversation");
        return;
    }
    w->saved[w->mode]=w->layout; w->saved_zoom[w->mode]=w->zoom; w->initialized[w->mode]=true;
    if (w->initialized[mode]) { w->layout=w->saved[mode]; w->zoom=w->saved_zoom[mode]; }
    else {
        tv_workspace_init(&w->layout,18,5);
        (void)tv_workspace_split(&w->layout,0,TV_COLUMNS,250);
        (void)tv_workspace_split(&w->layout,2,TV_ROWS,450);
        (void)tv_workspace_split(&w->layout,4,TV_ROWS,500);
        w->layout.panes[3].min_width=40; w->layout.focus=3; w->zoom=false;
    }
    w->mode=mode; app->view=7;
    native_workspace_show_terminal(app,false);
    native_workspace_invalidate(app);
}

static void native_workspace_tree(struct app *app) {
    struct native_workspace *w = app->workspace;
    size_t i, count = 1, chosen = 0;
    bool root_selected = w->selection_initialized && w->tree.count && !w->tree.selected;
    if (w->tree.count) w->root_open = w->nodes[0].expanded;
    bool matched[WF_RUNS]={false};
    size_t r;
    if (app->links) {
        const char *name=strrchr(app->links->root,'/');
        snprintf(w->project_label,sizeof(w->project_label),"%.180s%s",name && name[1] ? name+1 : app->links->root,
            app->links->stale ? " (links stale)" : "");
    }
    w->nodes[0] = (struct tv_tree_node){app->fleet ? "Remote heads" : app->links ? w->project_label : "This project", SIZE_MAX, 0, w->root_open, TV_STRONG};
    for (i=0;i<w->collapsed_count;) {
        size_t h;
        for (h=0;h<app->model.head_count;h++) if (!strcmp(w->collapsed[i],app->model.heads[h].branch)) break;
        if (h<app->model.head_count) i++;
        else { w->collapsed_count--; memcpy(w->collapsed[i],w->collapsed[w->collapsed_count],TEXT); }
    }
    if (!app->fleet && app->workflows) for (r=0;r<app->workflows->run_count;r++)
        snprintf(w->run_labels[r],sizeof(w->run_labels[r]),"%s%s%s",app->workflows->runs[r].name,dot(app),
            app->workflows->runs[r].state);
    retarget_selection(app);
    for (i = 0; i < app->model.head_count; i++) if (head_matches(&app->model.heads[i], app->search)) {
        const struct head *h = &app->model.heads[i];
        size_t k;
        bool expanded=true;
        if (count>=sizeof(w->nodes)/sizeof(w->nodes[0])) {
            copy_text(app->notice,sizeof(app->notice),"Navigation limit reached; use / to filter heads"); break;
        }
        for (k=0;k<w->collapsed_count;k++) if (!strcmp(w->collapsed[k],h->branch)) expanded=false;
        if (i == app->selected) { chosen = count; w->selection_initialized = true; }
        w->nodes[count++] = (struct tv_tree_node){h->branch, i, 1, expanded, app->fleet ? TV_BASE : status_tone(h)==TV_SUCCESS ? TV_BASE : status_tone(h)};
        if (!app->fleet && app->workflows) for (r=0;r<app->workflows->run_count;r++) if (native_links_match(app,r,i)) {
            if (count>=sizeof(w->nodes)/sizeof(w->nodes[0])) break;
            matched[r]=true;
            if (w->run_selected && r==app->workflow_run && i==app->selected && expanded) chosen=count;
            w->nodes[count++] = (struct tv_tree_node){w->run_labels[r],(r+1)*(MAX_HEADS+1)+i,2,false,TV_MUTED};
        }
    }
    if (!app->fleet && app->workflows) for (r=0;r<app->workflows->run_count;r++) if (!matched[r]) {
        if (count>=sizeof(w->nodes)/sizeof(w->nodes[0])) break;
        if (w->run_selected && r==app->workflow_run) chosen=count;
        w->nodes[count++]=(struct tv_tree_node){w->run_labels[r],(r+1)*(MAX_HEADS+1)+MAX_HEADS,1,false,TV_MUTED};
    }
    (void)tv_tree_init(&w->tree, w->nodes, count);
    w->tree.selected = root_selected ? 0 : chosen;
}

static void native_workspace_select(struct app *app) {
    struct native_workspace *w = app->workspace;
    size_t value,head;
    if (w->tree.selected>=w->tree.count) return;
    w->selection_initialized = true;
    value=w->nodes[w->tree.selected].value;
    w->run_selected=value!=SIZE_MAX && value>=MAX_HEADS+1;
    if (value==SIZE_MAX) return;
    head=value%(MAX_HEADS+1);
    if (head<app->model.head_count) app->selected=head;
    if (w->run_selected) {
        size_t run=value/(MAX_HEADS+1)-1;
        if (app->workflows && run<app->workflows->run_count) {
            if (app->workflow_run!=run) app->workflow_node=0;
            app->workflow_run=run;
            copy_text(app->notice,sizeof(app->notice),"Recorded run; Enter opens its evidence");
        }
    }
}

void native_workspace_move(struct app *app, int direction) {
    struct native_workspace *w = app->workspace;
    if (!w) return;
    if (native_workspace_terminal(app,w->layout.focus) && native_workspace_terminal(app,w->layout.focus)->screen) {
        struct native_terminal *t=native_workspace_terminal(app,w->layout.focus);
        t->scrolling=true;
        if (direction<0 && t->scroll<t->screen->history_count) t->scroll++;
        if (direction>0 && t->scroll) t->scroll--;
    }
    else if (native_workspace_agent_index(w,w->layout.focus)>=0) tv_workspace_scroll(&w->layout,direction,13);
    else if (w->layout.focus==5) {
        size_t *selected=w->mode==1 && app->plan ? &app->plan->selected : &app->workflow_node;
        size_t indices[TV_GRAPH_MAX_NODES];
        size_t count=w->mode==1 && app->plan ? app->plan->graph.node_count :
            app->workflows ? workflow_nodes(app->workflows,app->workflow_run,indices) : 0;
        if (direction<0 && *selected) (*selected)--;
        if (direction>0 && *selected+1<count) (*selected)++;
    }
    else if (w->layout.focus == 1) { tv_tree_move(&w->tree, direction); native_workspace_select(app); }
    else tv_workspace_scroll(&w->layout, direction, w->layout.focus==6 ? NATIVE_PLAN_TEXT :
        app->fleet ? app->model.host_count+2 : app->model.recovery_count+8);
}

/* Pane focus is the workspace's Tab model; Shift-Tab reverses it. */
void native_workspace_focus_previous(struct app *app) {
    if (!app->workspace) return;
    tv_workspace_focus_next(&app->workspace->layout,-1);
    native_workspace_sync_terminal(app);
}

bool native_workspace_key(struct app *app, char key) {
    struct native_workspace *w = app->workspace;
    if (!w) return false;
    if (key=='A' || key=='B' || key=='C') { native_workspace_mode(app,key-'A'); return true; }
    if (key=='H' && app->fleet) {
        const struct head *h=selected_head(app);
        size_t i;
        for (i=0;h && i<app->model.host_count;i++) if (!strcmp(h->remote_host,app->model.hosts[i].name)) {
            app->host_selected=i; break;
        }
        app->view=6; return true;
    }
    if (w->mode==2 && (key=='[' || key==']')) return workflow_key(app,key);
    if (key == 'z') {
        w->zoom = !w->zoom;
        copy_text(app->notice,sizeof(app->notice),w->zoom ? "Focused pane expanded; z restores the splits" : "Pane splits restored");
        return true;
    }
    if (key == 'S') { native_workspace_split_agents(app); return true; }
    if (key == '\t') { native_workspace_focus_next(app); return true; }
    if (w->layout.focus == 1 && (key == 'h' || key == 'l')) {
        size_t index=w->tree.selected, k;
        tv_tree_expand(&w->tree, key == 'l');
        if (index<w->tree.count && w->nodes[index].value<app->model.head_count) {
            const char *branch=app->model.heads[w->nodes[index].value].branch;
            for (k=0;k<w->collapsed_count;k++) if (!strcmp(w->collapsed[k],branch)) break;
            if (w->nodes[index].expanded && k<w->collapsed_count) {
                w->collapsed_count--; memcpy(w->collapsed[k],w->collapsed[w->collapsed_count],TEXT);
            } else if (!w->nodes[index].expanded && k==w->collapsed_count && k<MAX_HEADS)
                copy_text(w->collapsed[w->collapsed_count++],TEXT,branch);
        }
        native_workspace_select(app); return true;
    }
    if (w->layout.focus==1 && w->run_selected && (key=='\r' || key=='\n')) {
        native_workspace_mode(app,2); w->layout.focus=6; return true;
    }
    if (w->layout.focus==1 && w->run_selected &&
        w->nodes[w->tree.selected].value%(MAX_HEADS+1)==MAX_HEADS && strchr(":ac",key)) {
        copy_text(app->notice,sizeof(app->notice),"This run has no matching visible head; Enter opens its evidence");
        return true;
    }
    if ((key == '\r' || key == '\n' || key == ':' || key == 'a' || key == 'c') &&
        w->layout.focus == 1 && !w->tree.selected) {
        if (key == '\r' || key == '\n') {
            w->selection_initialized = true;
            w->nodes[0].expanded = !w->nodes[0].expanded;
        }
        else copy_text(app->notice, sizeof(app->notice), app->model.head_count ? "Select a head on the left first" : "Nothing selected yet; press n to start a task");
        return true;
    }
    /* Enter on a head keeps the user inside the workspace: focus its pane. */
    if ((key == '\r' || key == '\n') && w->layout.focus == 1 && selected_head(app)) {
        int first=w->agents[w->mode].first;
        w->layout.focus = first ? first : 3;
        native_workspace_sync_terminal(app);
        return true;
    }
    return false;
}

void native_workspace_mouse(struct app *app, unsigned button, int x, int y, bool release) {
    struct native_workspace *w = app->workspace;
    if (!w) return;
    if (release) { tv_workspace_release(&w->layout); return; }
    if (button == 32) { (void)tv_workspace_motion(&w->layout, x, y); return; }
    if (button == 0 || button == 64 || button == 65) {
        if (!tv_workspace_press(&w->layout, x, y)) return;
        if (w->layout.drag >= 0) return;
        native_workspace_sync_terminal(app);
        if (button == 64 || button == 65) native_workspace_move(app, button == 64 ? -1 : 1);
        else if (w->layout.focus == 1) {
            (void)tv_tree_click(&w->tree, w->layout.panes[1].scroll, y - w->layout.panes[1].bounds.y - 1);
            native_workspace_select(app);
        }
    }
}

static const char *agent_name(const struct head *h) {
    return h->profile[0] == '\0' || !strcmp(h->profile, "none") || !strcmp(h->profile, "-") ? "shell (no agent)" : h->profile;
}

static void workspace_empty(struct app *app, struct tv_canvas *c) {
    int y = 0;
    dashboard_text(c, 0, y++, c->width, TV_STRONG, "%s", app->fleet ? "No remote heads observed" : "No agent work in this project yet");
    y++;
    if (app->fleet) {
        dashboard_text(c, 0, y++, c->width, TV_BASE, "Hosts shows connectivity for each machine.");
        dashboard_text(c, 0, y++, c->width, TV_BASE, "Recovery lists host problems.");
        return;
    }
    dashboard_text(c, 0, y++, c->width, TV_BASE, "Press n to start a task. Hydra creates a branch and");
    dashboard_text(c, 0, y++, c->width, TV_BASE, "worktree, opens a terminal and starts your agent in it.");
    y++;
    dashboard_text(c, 0, y++, c->width, TV_BASE, "The conversation with that agent appears in this pane;");
    dashboard_text(c, 0, y++, c->width, TV_BASE, "its plan and progress appear next to it.");
    y++;
    dashboard_text(c, 0, y++, c->width, TV_MUTED, "n new task   : more actions   ? help");
}

static void native_workspace_details(struct app *app, struct tv_canvas *c, size_t scroll) {
    const struct head *h = selected_head(app);
    const char *sep = dot(app);
    int row;
    if (!h) { workspace_empty(app, c); return; }
    for (row = 0; row < c->height; row++) {
        char text[1024];
        enum tv_style tone = TV_BASE;
        switch (scroll + (size_t)row) {
        case 0: snprintf(text, sizeof(text), "%s", h->branch); tone = TV_STRONG; break;
        case 1: snprintf(text, sizeof(text), "Session   %s", app->fleet ? h->desired : status_label(h)); tone = app->fleet ? TV_BASE : status_tone(h); break;
        case 2: snprintf(text, sizeof(text), "Agent     %s", app->fleet ? h->profile : agent_name(h)); break;
        case 3: snprintf(text, sizeof(text), "Where     %s%s%s", app->fleet ? h->remote_host : "this machine", sep, app->fleet ? h->remote_project : app->links ? app->links->root : "this project"); break;
        case 4:
            if (app->fleet || !h->head_id[0]) { snprintf(text,sizeof(text),"Changes   unknown"); tone = TV_MUTED; }
            else snprintf(text,sizeof(text),"Changes   %u file%s not yet committed%s%u queued",h->diff,h->diff==1 ? "" : "s",sep,h->queue);
            break;
        case 5:
            if (app->fleet || !h->head_id[0]) { snprintf(text,sizeof(text),"Reported  %s", h->declared[0] ? h->declared : "nothing yet"); break; }
            snprintf(text, sizeof(text), "Reported  %s%s%u of %u approvals", h->declared[0] ? h->declared : "nothing yet", sep, h->approved, h->gates);
            if (h->gates > h->approved) tone = TV_WARNING;
            break;
        case 6: snprintf(text, sizeof(text), "%s%s%s", h->group[0] && strcmp(h->group,"-") ? "Group     " : "", h->group[0] && strcmp(h->group,"-") ? h->group : "",
                         h->pr[0] && strcmp(h->pr,"-") ? "" : ""); if (h->pr[0] && strcmp(h->pr,"-")) snprintf(text + strlen(text), sizeof(text) - strlen(text), "%sPR %s", text[0] ? sep : "", h->pr); break;
        case 8: snprintf(text, sizeof(text), "%s", !strcmp(display_status(h), "STALE") ? "The terminal is gone; files in the worktree are kept." : ""); tone = TV_WARNING; break;
        case 10: snprintf(text, sizeof(text), "a  talk to the agent here      Enter  full details"); tone = TV_MUTED; break;
        case 11: snprintf(text, sizeof(text), ":  more actions                d  technical details"); tone = TV_MUTED; break;
        default: text[0]='\0'; break;
        }
        tv_text(c, (struct tv_rect){0,row,c->width,1}, text, tone);
    }
}

static void native_workspace_activity(struct app *app, struct tv_canvas *c, size_t scroll) {
    const struct head *h = selected_head(app);
    size_t attention = attention_count(app);
    int row;
    for (row = 0; row < c->height; row++) {
        char text[1024]; size_t index = scroll + (size_t)row;
        enum tv_style tone = TV_BASE;
        text[0] = '\0';
        if (app->fleet) {
            if (!index) { snprintf(text,sizeof(text),"Hosts: %zu observed", app->model.host_count); tone = TV_STRONG; }
            else if (index<=app->model.host_count) {
                const struct host_observation *host=&app->model.hosts[index-1];
                char count[24]="unknown";
                if (strcmp(host->state,"failed")) snprintf(count,sizeof(count),"%u",host->heads);
                snprintf(text,sizeof(text),"%s%s%s%s%s heads",host->name,dot(app),host->state,dot(app),count);
                if (!strcmp(host->state,"failed")) tone = TV_WARNING;
            } else if (index==app->model.host_count+1) { snprintf(text,sizeof(text),"CPU and memory are not measured"); tone = TV_MUTED; }
        }
        else if (index == 0) {
            snprintf(text, sizeof(text), "%zu head%s%s%zu need attention%s%zu recovery finding%s", app->model.head_count, app->model.head_count==1 ? "" : "s",
                     dot(app), attention, dot(app), app->model.recovery_count, app->model.recovery_count==1 ? "" : "s");
            tone = TV_STRONG;
        }
        else if (!app->model.head_count && index == 2) { snprintf(text, sizeof(text), "Nothing has happened yet."); tone = TV_MUTED; }
        else if (index < 6 && (!h || !h->head_id[0])) {
            if (index==2 && h) { snprintf(text, sizeof(text), "No readable head record for %s; counters unknown", h->branch); tone = TV_WARNING; }
        }
        else if (index == 2) snprintf(text, sizeof(text), "Selected %s%s%u events%s%u messages%s%u signals", h->branch, dot(app), h->events, dot(app), h->messages, dot(app), h->signals);
        else if (index == 3) { snprintf(text, sizeof(text), "Approvals %u of %u%s%u queued entries", h->approved, h->gates, dot(app), h->queue); if (h->gates > h->approved) tone = TV_WARNING; }
        else if (index == 4) { snprintf(text, sizeof(text), "Claims %u%sscopes %u  (coordination with other heads)", h->claims, dot(app), h->scopes); tone = TV_MUTED; }
        else if (index == 6 && app->model.recovery_count) { snprintf(text, sizeof(text), "NEEDS REPAIR"); tone = TV_BORDER; }
        else if (index >= 7 && index - 7 < app->model.recovery_count) {
            const struct recovery *r = &app->model.recovery[index - 7];
            char detail[1024];
            recovery_explain(r, text, sizeof(text), detail, sizeof(detail));
            tone = TV_WARNING;
        }
        else if (index == 7 + app->model.recovery_count && app->model.recovery_count) { snprintf(text, sizeof(text), "Recovery explains each finding and runs its check"); tone = TV_MUTED; }
        tv_text(c, (struct tv_rect){0,row,c->width,1}, text, tone);
    }
}

static const char *pane_title(struct native_workspace *w, int i, bool agent, bool attached, const char *agent_title) {
    if (i == 1) return "PROJECT";
    if (agent) return attached ? agent_title : "SELECTED WORK";
    if (i == 5) return "DEPENDENCIES";
    if (i == 6) return w->mode == 2 ? "EVIDENCE" : "PLAN";
    return "ACTIVITY";
}

static const char *workspace_hints(struct app *app, struct native_workspace *w, int width) {
    struct native_terminal *t = native_workspace_terminal(app, w->layout.focus);
    if (t && t->screen) {
        if (t->client.finished || t->client.eof) return "Agent client disconnected  Ctrl-B r reconnect  Ctrl-B x close  Ctrl-B Tab Hydra";
        return width < 100 ? "Typing goes to the agent  Ctrl-B Tab back to Hydra  Ctrl-B x close" :
            "Typing goes to the agent  Ctrl-B Tab back to Hydra  Ctrl-B x close pane  Ctrl-B n next agent  Ctrl-B [ scroll  Ctrl-B q quit";
    }
    if (w->layout.focus == 1 && w->run_selected) return "Enter evidence  h parent  Tab next pane  ? help  q quit";
    if (w->compact) return w->mode == 2 ? "Y approve  N reject  R resume  X cancel  Tab pane  ? help  q quit" : "Tab pane  j/k move  a agent  n new  ? help  q quit";
    if (w->mode == 2) return width < 100 ? "Y approve  N reject  R resume  X cancel  A conversation  ? help  q quit" :
        "[/] run  Y approve  N reject  R resume  X cancel  A conversation  B plan  Tab pane  ? help  q quit";
    if (w->mode == 1) return width < 100 ? "P load  V validate  E approve  A conversation  ? help  q quit" :
        "P load draft  V validate  E approve exact revision  A conversation  C monitor  Tab pane  ? help  q quit";
    if (width < 100) return app->fleet ? "Tab pane  a attach  B plan  C monitor  ? help  q quit" : "Tab pane  a agent  n new task  B plan  C monitor  ? help  q quit";
    return app->fleet ? "Tab pane  Enter select  a attach  H host  B plan  C monitor  z zoom  ? help  q quit" :
        "Tab pane  Enter select  a talk to agent  n new task  x remove  B plan  C monitor  z zoom  ? help  q quit";
}

bool render_native_workspace(struct app *app, unsigned frame, bool headless) {
    struct native_workspace *w;
    struct tv_canvas *c;
    int width, height, i, x, y;
    bool compact;
    char status[512];
    const char *sep = dot(app);
    if (!native_workspace_init(app) || !frame_begin(app, headless)) return false;
    c = &app->frame; width = c->width; height = c->height;
    compact = width < 65 || height < 16;
    if (width < 19 || height < 6) return false;
    w = app->workspace; w->compact=compact;
    {
        int root=w->layout.root;
        if (w->zoom || compact) w->layout.root=w->layout.focus;
        (void)tv_workspace_layout(&w->layout, (struct tv_rect){0,compact ? 1 : 2,width,height-(compact ? 2 : 4)});
        w->layout.root=root;
    }
    native_workspace_tree(app);
    if (compact) {
        char title[96];
        snprintf(title, sizeof(title), "HYDRA / %s", w->mode==0 ? "PLAN TOGETHER" : w->mode==1 ? "PLAN OVERVIEW" : "MONITOR");
        tv_text(c, (struct tv_rect){1,0,width-2,1}, title, TV_TITLE);
    } else chrome_header(app, c, w->mode==0 ? "PLAN TOGETHER" : w->mode==1 ? "PLAN OVERVIEW" : "MONITOR");
    for (i = 0; i < w->layout.count; i++) {
        struct tv_pane *p = &w->layout.panes[i];
        struct tv_canvas view, content;
        struct native_terminal *t=native_workspace_terminal(app,i);
        bool agent=native_workspace_agent_index(w,i)>=0, attached=t && t->screen, focused=i==w->layout.focus;
        char agent_title[TEXT+64];
        if (p->split != TV_LEAF) {
            for (y = 0; y < p->divider.height; y++) for (x = 0; x < p->divider.width; x++)
                tv_put(c, p->divider.x+x, p->divider.y+y, p->split == TV_COLUMNS ? (app->ascii ? '|' : 0x2502) : (app->ascii ? '-' : 0x2500), TV_BORDER);
            continue;
        }
        if (!tv_canvas_view(&view, c, p->bounds)) continue;
        if (attached) snprintf(agent_title,sizeof(agent_title),"AGENT %s%s%s",t->label,sep,native_terminal_attention(app,t));
        if (compact) {
            if (attached) dashboard_text(&view,0,0,view.width,TV_SELECTED,"%s / %.6s / %s",
                t->client.finished || t->client.eof ? "DISCONNECTED" : "INPUT TO AGENT",t->label,native_terminal_attention(app,t));
            else dashboard_text(&view,0,0,view.width,TV_SELECTED,"FOCUS / %s",i==1 ? "NAVIGATION" : agent ? "SELECTED WORK" : i==5 ? "DEPENDENCIES" : i==6 ? "PLAN / EVIDENCE" : "ACTIVITY");
        } else {
            tv_panel_styled(&view, (struct tv_rect){0,0,view.width,view.height}, pane_title(w,i,agent,attached,agent_title),
                            focused ? TV_FOCUS : TV_BORDER, focused ? TV_SELECTED : TV_STRONG);
            if (attached) dashboard_text(&view,1,1,view.width-2,focused ? TV_SELECTED : TV_MUTED,
                "%s", t->client.finished || t->client.eof ? "Client disconnected: no input. Ctrl-B r reconnects" :
                focused ? "Typing goes to the agent. Ctrl-B Tab returns to Hydra" : "Tab here to talk to the agent");
        }
        if (!tv_canvas_view(&content, &view, compact ? (struct tv_rect){0,1,view.width,view.height-1} :
            (struct tv_rect){1,attached ? 2 : 1,view.width-2,view.height-(attached ? 3 : 2)})) continue;
        if (i == 1) tv_tree_draw(&w->tree, &content, &w->layout.panes[1].scroll, w->layout.focus == 1);
        else if (attached) native_terminal_draw(app,t,&content,w->layout.focus==i);
        else if (agent) native_workspace_details(app, &content, p->scroll);
        else if (i==5) native_workspace_graph(app,&content,w->mode==1);
        else if (i==6 && w->mode==2) native_workspace_evidence_text(app,&content,&p->scroll);
        else if (i==6 || (i==4 && app->plan)) native_workspace_plan_text(app,&content,&p->scroll);
        else native_workspace_activity(app, &content, p->scroll);
    }
    {
        long age = headless || !app->snapshot_at ? 0 : (long)(time(NULL) - app->snapshot_at);
        const char *notice=app->snapshot_stale ? app->snapshot_error : app->notice;
        enum tv_style tone = app->snapshot_stale ? TV_WARNING : app->notice[0] ? TV_BASE : TV_MUTED;
        if (age < 0) age = 0;
        if (app->snapshot_stale) snprintf(status, sizeof(status), "STALE: last good snapshot%s%s", notice[0] ? sep : "", notice);
        else if (notice[0]) snprintf(status, sizeof(status), "%s", notice);
        else snprintf(status, sizeof(status), "%s%sage %lds%s%s", w->mode==0 ? "Conversation layout" : w->mode==1 ? "Plan overview" : "Monitoring", sep, age, sep,
                      w->zoom ? "zoomed" : "A conversation  B plan  C monitor");
        if (compact) {
            if (app->notice[0] && !(native_workspace_terminal(app,w->layout.focus) && native_workspace_terminal(app,w->layout.focus)->screen))
                tv_text(c,(struct tv_rect){0,height-1,width,1},app->notice,TV_WARNING);
            else tv_text(c,(struct tv_rect){0,height-1,width,1},workspace_hints(app,w,width),TV_MUTED);
        } else chrome_footer(app, c, status, tone, workspace_hints(app, w, width));
    }
    frame_end(app, frame, headless);
    return true;
}
