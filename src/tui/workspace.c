#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Hydra's view owns data interpretation; kettle owns layout, focus and paint.
 * The workspace shares the frame, header, tabs and footer with every other view. */

bool native_workspace_monitoring(struct app *app) {
    return app->workspace && app->workspace->mode==2;
}

void native_workspace_destroy(struct app *app) {
    if (!app->workspace) return;
    free(app->workspace); app->workspace = NULL;
}

/* Narrow navigation, the agent conversation at full height, and the plan
 * review beside it (dependencies above the plan text). The conversation takes
 * most of the width; the plan overview and monitor favour the review. Where
 * both sides cannot fit their minimum widths (80 columns), kettle shows the
 * focused side: Tab or B reveals the review and A returns to the agent. */
static void workspace_layout(struct kt_workspace *layout, int mode) {
    kt_workspace_init(layout, 18, 5);
    (void)kt_workspace_split(layout, 0, KT_COLUMNS, 150);
    (void)kt_workspace_split(layout, 2, KT_COLUMNS, mode == 0 ? 620 : 380);
    (void)kt_workspace_split(layout, 4, KT_ROWS, 350);
    layout->panes[3].min_width = 50;
    layout->panes[5].min_width = layout->panes[6].min_width = 36;
}

bool native_workspace_init(struct app *app) {
    struct native_workspace *w;
    if (app->workspace) return true;
    w = calloc(1, sizeof(*w));
    if (!w) return false;
    app->workspace = w;
    workspace_layout(&w->layout, 0);
    w->layout.focus = 1;
    w->root_open = true; w->theme = app->theme;
    return true;
}

static int subtree_width(const struct kt_workspace *l, int node) {
    const struct kt_pane *p=&l->panes[node];
    int a, b;
    if (p->split==KT_LEAF) return p->min_width;
    a=subtree_width(l,p->first); b=subtree_width(l,p->second);
    return p->split==KT_COLUMNS ? a+b+1 : a>b ? a : b;
}

static bool subtree_has(const struct kt_workspace *l, int node, int leaf) {
    for (; leaf>=0; leaf=l->panes[leaf].parent) if (leaf==node) return true;
    return false;
}

/* kettle shows only the focused subtree when minima cannot fit, which would
 * drop navigation together with the side that is not focused. Where the agent
 * and the review cannot sit side by side, keep navigation and give the rest to
 * the side holding focus: the agent, unless the review is focused. */
static void workspace_arrange(struct kt_workspace *l, struct kt_rect r) {
    struct kt_pane right;
    struct kt_rect nav, divider, area;
    (void)kt_workspace_layout(l,r);
    if (l->root || l->count<7 || l->panes[2].split==KT_LEAF || (l->panes[1].bounds.width && l->panes[2].bounds.width)) return;
    right=l->panes[2];
    l->panes[2].split=KT_LEAF;
    l->panes[2].min_width=subtree_width(l,subtree_has(l,right.second,l->focus) ? right.second : right.first);
    (void)kt_workspace_layout(l,r);
    nav=l->panes[1].bounds; divider=l->panes[0].divider; area=l->panes[2].bounds;
    l->panes[2]=right;
    if (!nav.width || !area.width) { (void)kt_workspace_layout(l,r); return; }
    l->root=2; (void)kt_workspace_layout(l,area); l->root=0;
    l->panes[0].bounds=r; l->panes[0].divider=divider; l->panes[1].bounds=nav; l->bounds=r;
}

/* The review is hidden when the layout gave it no cells this frame. */
static bool workspace_review_hidden(const struct native_workspace *w) {
    return !w->compact && !w->zoom && w->layout.panes[6].bounds.width == 0;
}


void native_workspace_mode(struct app *app, int mode) {
    struct native_workspace *w;
    if (!native_workspace_init(app)) return;
    w=app->workspace;
    /* B toggles the plan overview back to the conversation; A and C are direct. */
    if (mode==1 && w->mode==1 && app->view==7) mode=0;
    w->saved[w->mode]=w->layout; w->saved_zoom[w->mode]=w->zoom; w->initialized[w->mode]=true;
    if (w->initialized[mode]) { w->layout=w->saved[mode]; w->zoom=w->saved_zoom[mode]; }
    else { workspace_layout(&w->layout,mode); w->layout.focus=3; w->zoom=false; }
    w->mode=mode; app->view=7;
    /* The monitor offers the requests waiting on its runs. */
    if (mode==2) native_attention_read(app);
    /* Explicit plan/monitor navigation keeps keys in Hydra, not the agent. */
    if (mode!=0) w->layout.focus=6;
    native_workspace_show_terminal(app,false);
}

static void workspace_project_label(struct app *app, struct native_workspace *w) {
    const char *name;
    if (!app->links) return;
    name=strrchr(app->links->root,'/');
    snprintf(w->project_label,sizeof(w->project_label),"Project: %.180s%s",name && name[1] ? name+1 : app->links->root,
        app->links->stale ? " (links stale)" : "");
}

/* Drop collapse records for heads that no longer exist. */
static void prune_collapsed(struct app *app, struct native_workspace *w) {
    size_t i;
    for (i=0;i<w->collapsed_count;) {
        size_t h;
        for (h=0;h<app->model.head_count;h++) if (!strcmp(w->collapsed[i],app->model.heads[h].branch)) break;
        if (h<app->model.head_count) i++;
        else { w->collapsed_count--; memcpy(w->collapsed[i],w->collapsed[w->collapsed_count],TEXT); }
    }
}

static void workspace_run_labels(struct app *app, struct native_workspace *w) {
    size_t r;
    if (app->fleet || !app->workflows) return;
    for (r=0;r<app->workflows->run_count;r++)
        snprintf(w->run_labels[r],sizeof(w->run_labels[r]),"%s %s%s%s",run_label_kind(&app->workflows->runs[r]),
            app->workflows->runs[r].name,dot(app),app->workflows->runs[r].state);
}

static bool head_collapsed(const struct native_workspace *w, const char *branch) {
    size_t k;
    for (k=0;k<w->collapsed_count;k++) if (!strcmp(w->collapsed[k],branch)) return true;
    return false;
}

/* The navigation tree follows the work outline: heads the user started,
 * the runs they launched, then the heads each run created. */
struct workspace_tree_build {
    struct app *app;
    struct native_workspace *w;
    size_t count, chosen;
    size_t owner[MAX_HEADS];
};

static bool tree_add(struct workspace_tree_build *b, struct kt_tree_node node) {
    if (b->count>=sizeof(b->w->nodes)/sizeof(b->w->nodes[0])) {
        copy_text(b->app->notice,sizeof(b->app->notice),"Navigation limit reached; use / to filter heads");
        return false;
    }
    b->w->nodes[b->count++]=node;
    return true;
}

static void tree_member(struct workspace_tree_build *b, size_t head, unsigned depth) {
    struct app *app=b->app;
    const struct head *h=&app->model.heads[head];
    const char *role=head_run_role(app,h);
    snprintf(b->w->member_labels[head],sizeof(b->w->member_labels[head]),"%s%s%s",role ? role : "",role ? " " : "",h->branch);
    if (head==app->selected && !app->run_row && !b->w->run_selected) b->chosen=b->count;
    (void)tree_add(b,(struct kt_tree_node){b->w->member_labels[head],head,depth,false,kt_style_equal(head_list_tone(app,h),KT_SUCCESS) ? KT_BASE : head_list_tone(app,h)});
}

static void tree_run(struct workspace_tree_build *b, size_t run, size_t owner, unsigned depth) {
    struct app *app=b->app;
    size_t i;
    bool open=run_expanded(app,run) || (!b->w->run_selected && app->selected<app->model.head_count && b->owner[app->selected]==run);
    if (b->w->run_selected && run==app->workflow_run && (owner==MAX_HEADS || owner==app->selected)) b->chosen=b->count;
    if (!tree_add(b,(struct kt_tree_node){b->w->run_labels[run],(run+1)*(MAX_HEADS+1)+owner,depth,open,
        run_needs_attention(app,run) ? KT_WARNING : KT_MUTED})) return;
    for (i=0;i<app->model.head_count;i++)
        if (b->owner[i]==run && head_matches(&app->model.heads[i],app->search)) tree_member(b,i,depth+1);
}

static void tree_user_head(struct workspace_tree_build *b, size_t head) {
    struct app *app=b->app;
    const struct head *h=&app->model.heads[head];
    size_t r;
    if (head==app->selected && !b->w->run_selected) { b->chosen=b->count; b->w->selection_initialized=true; }
    if (!tree_add(b,(struct kt_tree_node){h->branch,head,1,!head_collapsed(b->w,h->branch),
        app->fleet ? KT_BASE : kt_style_equal(head_list_tone(app,h),KT_SUCCESS) ? KT_BASE : head_list_tone(app,h)})) return;
    for (r=0;!app->fleet && app->workflows && r<app->workflows->run_count;r++)
        if (run_owner_head(app,r)==head) tree_run(b,r,head,2);
}

static void native_workspace_tree(struct app *app) {
    struct native_workspace *w = app->workspace;
    struct workspace_tree_build b = {app, w, 1, 0, {0}};
    size_t i;
    bool root_selected = w->selection_initialized && w->tree.count && !w->tree.selected;
    if (w->tree.count) w->root_open = w->nodes[0].expanded;
    workspace_project_label(app, w);
    w->nodes[0] = (struct kt_tree_node){app->fleet ? "Remote heads" : app->links ? w->project_label : "This project", SIZE_MAX, 0, w->root_open, KT_STRONG};
    prune_collapsed(app, w);
    workspace_run_labels(app, w);
    retarget_selection(app);
    for (i = 0; i < app->model.head_count && i < MAX_HEADS; i++) b.owner[i] = head_owner_run(app, &app->model.heads[i], NULL);
    for (i = 0; i < app->model.head_count; i++)
        if (b.owner[i] == SIZE_MAX && head_matches(&app->model.heads[i], app->search)) tree_user_head(&b, i);
    for (i = 0; !app->fleet && app->workflows && i < app->workflows->run_count; i++)
        if (run_owner_head(app, i) == SIZE_MAX) tree_run(&b, i, MAX_HEADS, 1);
    (void)kt_tree_init(&w->tree, w->nodes, b.count);
    w->tree.selected = root_selected ? 0 : b.chosen;
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
            copy_text(app->notice,sizeof(app->notice),"Recorded branch reference; Enter opens its evidence");
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
    else if (native_workspace_agent_index(w,w->layout.focus)>=0) kt_workspace_scroll(&w->layout,direction,13);
    else if (w->layout.focus==5) {
        size_t *selected=w->mode==1 && app->plan ? &app->plan->selected : &app->workflow_node;
        size_t indices[KT_GRAPH_MAX_NODES];
        size_t count=w->mode==1 && app->plan ? app->plan->graph.node_count :
            app->workflows ? workflow_nodes(app->workflows,app->workflow_run,indices) : 0;
        if (direction<0 && *selected) (*selected)--;
        if (direction>0 && *selected+1<count) (*selected)++;
    }
    else if (w->layout.focus == 1) { kt_tree_move(&w->tree, direction); native_workspace_select(app); }
    else kt_workspace_scroll(&w->layout, direction, w->layout.focus==6 ? NATIVE_PLAN_TEXT :
        app->fleet ? app->model.host_count+2 : app->model.recovery_count+8);
}

/* Pane focus is the workspace's Tab model; Shift-Tab reverses it. */
void native_workspace_focus_previous(struct app *app) {
    if (!app->workspace) return;
    kt_workspace_focus_next(&app->workspace->layout,-1);
    native_workspace_sync_terminal(app);
}

static void workspace_host_key(struct app *app) {
    const struct head *h=selected_head(app);
    size_t i;
    for (i=0;h && i<app->model.host_count;i++) if (!strcmp(h->remote_host,app->model.hosts[i].name)) {
        app->host_selected=i; break;
    }
    app->view=6;
}

/* Expand or collapse the selected head or run and remember the choice. */
static void workspace_expand_key(struct app *app, struct native_workspace *w, bool expand) {
    size_t index=w->tree.selected, k;
    kt_tree_expand(&w->tree, expand);
    if (index<w->tree.count && w->nodes[index].value!=SIZE_MAX && w->nodes[index].value>=MAX_HEADS+1)
        run_set_expanded(app,w->nodes[index].value/(MAX_HEADS+1)-1,w->nodes[index].expanded);
    if (index<w->tree.count && w->nodes[index].value<app->model.head_count && w->nodes[index].value<MAX_HEADS) {
        const char *branch=app->model.heads[w->nodes[index].value].branch;
        for (k=0;k<w->collapsed_count;k++) if (!strcmp(w->collapsed[k],branch)) break;
        if (w->nodes[index].expanded && k<w->collapsed_count) {
            w->collapsed_count--; memcpy(w->collapsed[k],w->collapsed[w->collapsed_count],TEXT);
        } else if (!w->nodes[index].expanded && k==w->collapsed_count && k<MAX_HEADS)
            copy_text(w->collapsed[w->collapsed_count++],TEXT,branch);
    }
    native_workspace_select(app);
}

bool native_workspace_key(struct app *app, char key) {
    struct native_workspace *w = app->workspace;
    if (!w) return false;
    if (key=='A' || key=='B' || key=='C') { native_workspace_mode(app,key-'A'); return true; }
    if (key=='H' && app->fleet) { workspace_host_key(app); return true; }
    if (w->mode==2 && (key=='[' || key==']')) return workflow_key(app,key);
    if (key == 'z') {
        w->zoom = !w->zoom;
        copy_text(app->notice,sizeof(app->notice),w->zoom ? "Focused pane expanded; z restores the splits" : "Pane splits restored");
        return true;
    }
    if (key == 'S') { native_workspace_split_agents(app); return true; }
    if (key == '\t') { native_workspace_focus_next(app); return true; }
    if (w->layout.focus == 1 && (key == 'h' || key == 'l')) { workspace_expand_key(app, w, key == 'l'); return true; }
    if (w->layout.focus==1 && w->run_selected && (key=='\r' || key=='\n')) {
        native_workspace_mode(app,2); w->layout.focus=6; return true;
    }
    if (w->layout.focus==1 && w->run_selected &&
        w->nodes[w->tree.selected].value%(MAX_HEADS+1)==MAX_HEADS && strchr("ac",key)) {
        copy_text(app->notice,sizeof(app->notice),"This run has no matching visible head; Enter opens its evidence");
        return true;
    }
    if ((key == '\r' || key == '\n' || key == 'a' || key == 'c') &&
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
    if (release) { kt_workspace_release(&w->layout); return; }
    if (button == 32) { (void)kt_workspace_motion(&w->layout, x, y); return; }
    if (button == 0 || button == 64 || button == 65) {
        if (!kt_workspace_press(&w->layout, x, y)) return;
        if (w->layout.drag >= 0) return;
        native_workspace_sync_terminal(app);
        if (button == 64 || button == 65) native_workspace_move(app, button == 64 ? -1 : 1);
        else if (w->layout.focus == 1) {
            (void)kt_tree_click(&w->tree, w->layout.panes[1].scroll, y - w->layout.panes[1].bounds.y - 1);
            native_workspace_select(app);
        }
    }
}

static const char *agent_name(const struct head *h) {
    return h->profile[0] == '\0' || !strcmp(h->profile, "none") || !strcmp(h->profile, "-") ? "shell (no agent)" : h->profile;
}

static void workspace_empty(struct app *app, struct kt_canvas *c) {
    int y = 0;
    dashboard_text(c, 0, y++, c->width, KT_STRONG, "%s", app->fleet ? "No remote heads observed" : "No agent work in this project yet");
    y++;
    if (app->fleet) {
        dashboard_text(c, 0, y++, c->width, KT_BASE, "Hosts shows connectivity for each machine.");
        dashboard_text(c, 0, y++, c->width, KT_BASE, "Recovery lists host problems.");
        return;
    }
    dashboard_text(c, 0, y++, c->width, KT_BASE, "Press n to start a task. Hydra creates a branch and");
    dashboard_text(c, 0, y++, c->width, KT_BASE, "worktree, opens a terminal and starts your agent in it.");
    y++;
    dashboard_text(c, 0, y++, c->width, KT_BASE, "The conversation with that agent appears in this pane;");
    dashboard_text(c, 0, y++, c->width, KT_BASE, "its plan and progress appear next to it.");
    y++;
    dashboard_text(c, 0, y++, c->width, KT_BASE, "To run agents on another machine, press H for Hosts");
    dashboard_text(c, 0, y++, c->width, KT_BASE, "and add it there.");
    y++;
    dashboard_text(c, 0, y++, c->width, KT_MUTED, "n new task   H hosts   : more actions   ? help");
}

static struct kt_style details_session(struct app *app, const struct head *h, char *text, size_t size) {
    if (app->fleet) { snprintf(text, size, "Session   %s", h->desired); return KT_BASE; }
    if (head_headless(h)) { snprintf(text, size, "Terminal  headless (no terminal)"); return KT_MUTED; }
    snprintf(text, size, "Session   %s", status_label(h));
    return status_tone(h);
}

static void details_agent(struct app *app, const struct head *h, char *text, size_t size) {
    char agent[TEXT + 96];
    if (app->fleet) { snprintf(text, size, "Agent     %s", h->profile); return; }
    if (head_headless(h)) head_agent_text(app, h, agent, sizeof(agent));
    else snprintf(agent, sizeof(agent), "%s", agent_name(h));
    snprintf(text, size, "Agent     %s", agent);
}

static void details_stale(const struct head *h, char *text, size_t size) {
    snprintf(text, size, "%s", !strcmp(display_status(h), "STALE") ? "The terminal is gone; files in the worktree are kept." : "");
}

static void details_where(struct app *app, const struct head *h, char *text, size_t size) {
    snprintf(text, size, "Where     %s%s%s", app->fleet ? h->remote_host : "this machine", dot(app),
             app->fleet ? h->remote_project : app->links ? app->links->root : "this project");
}

static struct kt_style details_changes(struct app *app, const struct head *h, char *text, size_t size) {
    if (app->fleet || !h->head_id[0]) { snprintf(text,size,"Changes   unknown"); return KT_MUTED; }
    snprintf(text,size,"Changes   %u file%s not yet committed%s%u queued",h->diff,h->diff==1 ? "" : "s",dot(app),h->queue);
    return KT_BASE;
}

static struct kt_style details_reported(struct app *app, const struct head *h, char *text, size_t size) {
    const char *reported = h->declared[0] ? h->declared : "nothing yet";
    if (app->fleet || !h->head_id[0]) { snprintf(text,size,"Reported  %s", reported); return KT_BASE; }
    snprintf(text, size, "Reported  %s", reported);
    if (h->gates) text_append(text, size, "%s%u of %u approval requests approved", dot(app), h->approved, h->gates);
    return h->gates > h->approved ? KT_WARNING : KT_BASE;
}

static void details_group(struct app *app, const struct head *h, char *text, size_t size) {
    bool has_group = h->group[0] && strcmp(h->group,"-");
    snprintf(text, size, "%s%s", has_group ? "Group     " : "", has_group ? h->group : "");
    if (h->pr[0] && strcmp(h->pr,"-")) text_append(text, size, "%sPR %.40s", text[0] ? dot(app) : "", h->pr);
}

static void details_run(struct app *app, const struct head *h, char *text, size_t size) {
    size_t record, run = head_owner_run(app, h, &record);
    if (run == SIZE_MAX || app->fleet) return;
    snprintf(text, size, "Run       %s %s%s%s%s%s", run_label_kind(&app->workflows->runs[run]), app->workflows->runs[run].name,
             dot(app), app->workflows->runs[run].state, record != SIZE_MAX ? dot(app) : "", record != SIZE_MAX ? app->workflows->heads[record].role : "");
}

/* One line of the selected-work pane; returns its tone. */
static struct kt_style details_line(struct app *app, const struct head *h, size_t index, char *text, size_t size) {
    text[0] = '\0';
    switch (index) {
    case 0: snprintf(text, size, "%s", h->branch); return KT_STRONG;
    case 1: return details_session(app, h, text, size);
    case 2: details_agent(app, h, text, size); return KT_BASE;
    case 3: details_where(app, h, text, size); return KT_BASE;
    case 4: return details_changes(app, h, text, size);
    case 5: return details_reported(app, h, text, size);
    case 6: details_group(app, h, text, size); return KT_BASE;
    case 7: details_run(app, h, text, size); return KT_BASE;
    case 8: if (!head_headless(h)) details_stale(h, text, size); return KT_WARNING;
    case 10: snprintf(text, size, "%s", head_headless(h) ? "p  step output in Details      Enter  full details" :
                      head_terminal_gone(app, h) ? "a  restart its terminal        Enter  full details" : "a  talk to the agent here      Enter  full details"); return KT_MUTED;
    case 11: snprintf(text, size, ":  more actions                d  technical details"); return KT_MUTED;
    default: return KT_BASE;
    }
}

static void native_workspace_details(struct app *app, struct kt_canvas *c, size_t scroll) {
    const struct head *h = selected_head(app);
    int row;
    if (!h) { workspace_empty(app, c); return; }
    for (row = 0; row < c->height; row++) {
        char text[1024];
        struct kt_style tone = details_line(app, h, scroll + (size_t)row, text, sizeof(text));
        kt_text(c, (struct kt_rect){0,row,c->width,1}, text, tone);
    }
}

static struct kt_style activity_host_line(struct app *app, const struct host_observation *host, char *text, size_t size) {
    char count[24]="unknown";
    if (!host) { text[0]='\0'; return KT_BASE; }
    if (strcmp(host->state,"failed")) snprintf(count,sizeof(count),"%u",host->heads);
    snprintf(text,size,"%s%s%s%s%s heads",host->name,dot(app),host->state,dot(app),count);
    if (!strcmp(host->state,"failed")) return KT_WARNING;
    return KT_BASE;
}

static struct kt_style activity_fleet_line(struct app *app, size_t index, char *text, size_t size) {
    if (!index) { snprintf(text,size,"Hosts: %zu observed", app->model.host_count); return KT_STRONG; }
    if (index<=app->model.host_count) return activity_host_line(app, &app->model.hosts[index-1], text, size);
    if (index==app->model.host_count+1) { snprintf(text,size,"CPU and memory are not measured"); return KT_MUTED; }
    return KT_BASE;
}

static void activity_summary(struct app *app, size_t attention, char *text, size_t size) {
    struct work_counts n;
    work_counts(app, &n);
    snprintf(text, size, "%zu head%s", n.heads, n.heads==1 ? "" : "s");
    if (n.run_heads) text_append(text, size, " (+%zu in runs)", n.run_heads);
    text_append(text, size, "%s%zu need attention%s%zu recovery finding%s", dot(app), attention, dot(app),
                app->model.recovery_count, app->model.recovery_count==1 ? "" : "s");
}

/* Rows 2-4: counters for the selected head with a readable record. */
static struct kt_style activity_head_line(struct app *app, const struct head *h, size_t index, char *text, size_t size) {
    if (index == 2) { snprintf(text, size, "Selected %s%s%u events%s%u messages%s%u signals", h->branch, dot(app), h->events, dot(app), h->messages, dot(app), h->signals); return KT_BASE; }
    if (index == 3) { snprintf(text, size, "Approval requests %u of %u approved%s%u queued entries", h->approved, h->gates, dot(app), h->queue); return h->gates > h->approved ? KT_WARNING : KT_BASE; }
    if (index == 4) { snprintf(text, size, "Claims %u%sscopes %u  (coordination with other heads)", h->claims, dot(app), h->scopes); return KT_MUTED; }
    return KT_BASE;
}

/* Rows 6 onward: recovery findings in plain language. */
static struct kt_style activity_recovery_line(struct app *app, size_t index, char *text, size_t size) {
    size_t findings = app->model.recovery_count;
    if (!findings) return KT_BASE;
    if (index == 6) { snprintf(text, size, "NEEDS REPAIR"); return KT_BORDER; }
    if (index >= 7 && index - 7 < findings) {
        char detail[1024];
        recovery_explain(&app->model.recovery[index - 7], text, size, detail, sizeof(detail));
        return KT_WARNING;
    }
    if (index == 7 + findings) { snprintf(text, size, "Recovery explains each finding and runs its check"); return KT_MUTED; }
    return KT_BASE;
}

/* A selected head without a readable record has no counters to show. */
static struct kt_style activity_missing_record(const struct head *h, size_t index, char *text, size_t size) {
    if (index==2 && h) { snprintf(text, size, "No readable head record for %s; counters unknown", h->branch); return KT_WARNING; }
    return KT_BASE;
}

static struct kt_style activity_line(struct app *app, const struct head *h, size_t attention, size_t index, char *text, size_t size) {
    text[0] = '\0';
    if (app->fleet) return activity_fleet_line(app, index, text, size);
    if (index == 0) { activity_summary(app, attention, text, size); return KT_STRONG; }
    if (!app->model.head_count && index == 2) { snprintf(text, size, "Nothing has happened yet."); return KT_MUTED; }
    if (index < 6 && (!h || !h->head_id[0])) return activity_missing_record(h, index, text, size);
    if (index < 6) return activity_head_line(app, h, index, text, size);
    return activity_recovery_line(app, index, text, size);
}

static void native_workspace_activity(struct app *app, struct kt_canvas *c, size_t scroll) {
    const struct head *h = selected_head(app);
    size_t attention = attention_count(app);
    int row;
    for (row = 0; row < c->height; row++) {
        char text[1024];
        struct kt_style tone = activity_line(app, h, attention, scroll + (size_t)row, text, sizeof(text));
        kt_text(c, (struct kt_rect){0,row,c->width,1}, text, tone);
    }
}

/* The conversation layout shows activity in the plan pane until a plan exists. */
static bool pane_shows_plan(const struct app *app, const struct native_workspace *w, int i) {
    return i == 6 && (w->mode == 1 || app->plan);
}

static const char *pane_title(struct app *app, struct native_workspace *w, int i, bool agent, bool attached, const char *agent_title) {
    if (i == 1) return "PROJECT";
    if (agent) return attached ? agent_title : "SELECTED WORK";
    if (i == 5) return "DEPENDENCIES";
    if (i == 6 && w->mode == 2) return "EVIDENCE";
    return pane_shows_plan(app, w, i) ? "PLAN REVIEW" : "ACTIVITY";
}

/* The longest choice that fits; the last one is the shortest fallback. */
static const char *fitting(const char *const *choices, size_t count, int width) {
    size_t i;
    for (i = 0; i + 1 < count; i++) if ((int)strlen(choices[i]) <= width) return choices[i];
    return choices[count - 1];
}

/* How to leave agent input and close the view stay visible at every width,
 * down to the 40-column minimum. A disconnected client takes no input, so its
 * keys go to Hydra and a reattaches. The keys named are the bound chords
 * (key_hint), so a keys file shows here too. */
static void attached_hints(struct app *app, const struct native_workspace *w, const struct native_terminal *t,
                           int width, char *out, size_t size) {
    static const char *const disconnected[] = {
        "Agent client disconnected; keys go to Hydra|a or {terminal.reconnect} reconnect~a reconnect|{terminal.close} close view|Tab next pane",
        "Client disconnected|a reconnect|{terminal.close} close view|Tab next pane",
        "Disconnected|a reconnect|{terminal.close} close"};
    static const char *const hidden[] = {
        "Typing goes to the agent|{terminal.leave} leave input|{terminal.close} close view|{workspace.layout.plan} plan review|{app.quit} quit",
        "Typing goes to the agent|{terminal.leave} leave|{terminal.close} close|{workspace.layout.plan} plan",
        "Typing goes to the agent|{terminal.leave} leave|{terminal.close} close",
        "{terminal.leave} leave|{terminal.close} close"};
    static const char *const live[] = {
        "Typing goes to the agent|{terminal.leave} leave input|{terminal.close} close view|{terminal.next} next agent|{terminal.history} scroll|{app.quit} quit",
        "Typing goes to the agent|{terminal.leave} leave input|{terminal.close} close view",
        "Typing goes to the agent|{terminal.leave} leave|{terminal.close} close",
        "{terminal.leave} leave|{terminal.close} close"};
    int room = w->compact ? width : width - 2;
    if (t->client.finished || t->client.eof) key_hint_fit(app, out, size, "  ", disconnected, 3, room);
    else if (workspace_review_hidden(w)) key_hint_fit(app, out, size, "  ", hidden, 4, room);
    else key_hint_fit(app, out, size, "  ", live, 4, room);
}

/* The attached pane's own first row: whether keys reach the agent and how
 * to leave input and close the view, fitted to the pane. */
static const char *attached_label(struct app *app, const struct native_terminal *t, bool focused, int width,
                                  char *out, size_t size) {
    static const char *const input[] = {
        "ATTACHED|INPUT TO AGENT|{terminal.leave} leaves input|{terminal.close} closes view",
        "ATTACHED|INPUT TO AGENT|{terminal.leave} leave|{terminal.close} close",
        "INPUT TO AGENT|{terminal.leave} leave|{terminal.close} close",
        "ATTACHED|INPUT TO AGENT|{terminal.leave}|{terminal.close}"};
    static const char *const disconnected[] = {
        "CLIENT DISCONNECTED|NO INPUT|a or {terminal.reconnect} reconnect~a reconnect|{terminal.close} close view",
        "CLIENT DISCONNECTED|NO INPUT|a reconnect|{terminal.close} close",
        "DISCONNECTED|NO INPUT|a reconnect|{terminal.close} close",
        "CLIENT DISCONNECTED|NO INPUT|{terminal.reconnect} reconnect~a reconnect"};
    static const char *const shared[] = {
        "ATTACHED / INPUT TO AGENT / another client sets the size (dots lie outside it); typing here resizes",
        "ATTACHED / INPUT TO AGENT / sized by another client; typing resizes",
        "INPUT TO AGENT / sized by another client; typing resizes",
        "ATTACHED / INPUT TO AGENT / SIZED BY ANOTHER CLIENT"};
    if (t->client.finished || t->client.eof) { key_hint_fit(app, out, size, " / ", disconnected, 4, width); return out; }
    if (t->foreign_size) return focused ? fitting(shared, 4, width) : "ATTACHED / sized by another client / Tab here and type to resize";
    if (!focused) return "ATTACHED / Tab here to talk to the agent";
    key_hint_fit(app, out, size, " / ", input, 4, width);
    return out;
}

/* An attached agent view names the agent-layer chords in effect (they are
 * read from the binding table through key_hint); every other focus has the
 * generated footer (NULL). */
static const char *workspace_hints(struct app *app, struct native_workspace *w, int width, char *buffer, size_t size) {
    struct native_terminal *t = native_workspace_terminal(app, w->layout.focus);
    if (t && t->screen) { attached_hints(app, w, t, width, buffer, size); return buffer; }
    return NULL;
}

/* The compact layout's single bottom row: a chord being entered, else a
 * notice when no agent view is focused, else the hints. */
static void compact_footer(struct app *app, struct native_workspace *w, struct kt_canvas *c) {
    const struct native_terminal *t = native_workspace_terminal(app, w->layout.focus);
    struct kt_rect row = {0, c->height - 1, c->width, 1};
    char text[512];
    if (key_pending_text(app, text, sizeof(text))) kt_text(c, row, text, KT_STRONG);
    else if (app->notice[0] && !(t && t->screen)) kt_text(c, row, app->notice, KT_WARNING);
    else if (workspace_hints(app, w, c->width, text, sizeof(text))) kt_text(c, row, text, KT_MUTED);
    else key_footer(app, c, row);
}

bool render_native_workspace(struct app *app, unsigned frame, bool headless) {
    struct native_workspace *w;
    struct kt_canvas *c;
    int width, height, i, x, y;
    bool compact;
    char status[512], hints[512];
    const char *sep = dot(app);
    if (!native_workspace_init(app) || !frame_begin(app, headless)) return false;
    c = &app->frame; width = c->width; height = c->height;
    compact = width < 65 || height < 16;
    if (width < 19 || height < 6) return false;
    w = app->workspace; w->compact=compact;
    {
        int root=w->layout.root;
        if (w->zoom || compact) w->layout.root=w->layout.focus;
        workspace_arrange(&w->layout, (struct kt_rect){0,compact ? 1 : 2,width,height-(compact ? 2 : 4)});
        w->layout.root=root;
    }
    native_workspace_tree(app);
    if (compact) {
        char title[96];
        snprintf(title, sizeof(title), "%s  %s", app->ascii ? "# hydra" : "\xe2\x97\x86 hydra", w->mode==0 ? "PLAN TOGETHER" : w->mode==1 ? "PLAN OVERVIEW" : "MONITOR");
        kt_text(c, (struct kt_rect){1,0,width-2,1}, title, KT_TITLE);
    } else chrome_header(app, c, w->mode==0 ? "PLAN TOGETHER" : w->mode==1 ? "PLAN OVERVIEW" : "MONITOR");
    for (i = 0; i < w->layout.count; i++) {
        struct kt_pane *p = &w->layout.panes[i];
        struct kt_canvas view, content;
        struct native_terminal *t=native_workspace_terminal(app,i);
        bool agent=native_workspace_agent_index(w,i)>=0, attached=t && t->screen, focused=i==w->layout.focus;
        char agent_title[TEXT+64], attention[TEXT+64], label[256];
        if (p->split != KT_LEAF) {
            for (y = 0; y < p->divider.height; y++) for (x = 0; x < p->divider.width; x++)
                kt_put(c, p->divider.x+x, p->divider.y+y, p->split == KT_COLUMNS ? (app->ascii ? '|' : 0x2502) : (app->ascii ? '-' : 0x2500), KT_BORDER);
            continue;
        }
        if (!kt_canvas_view(&view, c, p->bounds)) continue;
        if (attached) {
            native_terminal_attention(app,t,attention,sizeof(attention));
            snprintf(agent_title,sizeof(agent_title),"%.200s%s%.100s",t->label,sep,attention);
        }
        if (compact) {
            if (attached) dashboard_text(&view,0,0,view.width,KT_SELECTED,"%s / %.6s / %s",
                t->client.finished || t->client.eof ? "DISCONNECTED" : "INPUT TO AGENT",t->label,attention);
            else dashboard_text(&view,0,0,view.width,KT_SELECTED,"FOCUS / %s",i==1 ? "NAVIGATION" : agent ? "SELECTED WORK" : i==5 ? "DEPENDENCIES" : i==6 ? "PLAN / EVIDENCE" : "ACTIVITY");
        } else {
            char titled[TEXT+96];
            const char *base_title = pane_title(app,w,i,agent,attached,agent_title);
            if (!attached && p->scroll) snprintf(titled,sizeof(titled),"%s%sscroll %zu",base_title,sep,p->scroll);
            else snprintf(titled,sizeof(titled),"%s",base_title);
            pane_rule_styled(&view, (struct kt_rect){0,0,view.width,view.height}, titled, focused);
            if (attached) dashboard_text(&view,1,1,view.width-2,focused ? KT_SELECTED : KT_MUTED,
                "%s", attached_label(app, t, focused, view.width-2, label, sizeof(label)));
        }
        if (!kt_canvas_view(&content, &view, compact ? (struct kt_rect){0,1,view.width,view.height-1} :
            (struct kt_rect){1,attached ? 2 : 1,view.width-2,view.height-(attached ? 3 : 2)})) continue;
        if (i == 1) kt_tree_draw(&w->tree, &content, &w->layout.panes[1].scroll, w->layout.focus == 1);
        else if (attached) native_terminal_draw(app,t,&content,w->layout.focus==i);
        else if (agent) native_workspace_details(app, &content, p->scroll);
        else if (i==5) native_workspace_graph(app,&content,w->mode==1 || (w->mode==0 && app->plan));
        else if (i==6 && w->mode==2) native_workspace_evidence_text(app,&content,&p->scroll);
        else if (pane_shows_plan(app,w,i)) native_workspace_plan_text(app,&content,&p->scroll);
        else native_workspace_activity(app, &content, p->scroll);
    }
    {
        char fresh[96];
        const char *notice=app->snapshot_stale ? app->snapshot_error : app->notice;
        struct kt_style tone = snapshot_freshness(app, app->snapshot_at, app->snapshot_stale, headless, fresh, sizeof(fresh));
        if (!app->snapshot_stale && app->notice[0]) tone = KT_BASE;
        if (app->snapshot_stale) snprintf(status, sizeof(status), "%s%s%s", fresh, notice[0] ? sep : "", notice);
        else {
            const char *tail = app->notice[0] ? app->notice : w->zoom ? "z restores the splits" : "";
            snprintf(status, sizeof(status), "%s%s%s", fresh, tail[0] ? sep : "", tail);
        }
        if (compact) compact_footer(app, w, c);
        else chrome_footer(app, c, status, tone, workspace_hints(app, w, width, hints, sizeof(hints)));
    }
    frame_end(app, frame, headless);
    return true;
}
