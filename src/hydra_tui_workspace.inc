/* Hydra's view owns data interpretation; termviz owns layout, focus and paint. */
#define WORKSPACE_CAPACITY (512U * 256U)
struct native_workspace {
    struct tv_workspace layout;
    struct tv_workspace saved[3];
    bool saved_zoom[3], initialized[3];
    int mode;
    struct tv_tree tree;
    struct tv_tree_node nodes[MAX_HEADS + 1];
    struct tv_cell *cells, *previous;
    struct tv_presenter presenter;
    int theme;
    bool root_open, zoom, compact;
};

static bool native_workspace_monitoring(struct app *app) {
    return app->workspace && app->workspace->mode==2;
}

static void native_workspace_destroy(struct app *app) {
    if (!app->workspace) return;
    free(app->workspace->cells); free(app->workspace->previous);
    free(app->workspace); app->workspace = NULL;
}

static void native_workspace_invalidate(struct app *app) {
    if (app->workspace) tv_present_invalidate(&app->workspace->presenter);
}

static bool native_workspace_init(struct app *app) {
    struct native_workspace *w;
    if (app->workspace) return true;
    w = calloc(1, sizeof(*w));
    if (!w) return false;
    app->workspace = w;
    w->cells = calloc(WORKSPACE_CAPACITY, sizeof(*w->cells));
    w->previous = calloc(WORKSPACE_CAPACITY, sizeof(*w->previous));
    if (!w->cells || !w->previous) { native_workspace_destroy(app); return false; }
    (void)tv_present_init(&w->presenter, w->previous, WORKSPACE_CAPACITY);
    tv_workspace_init(&w->layout, 18, 5);
    (void)tv_workspace_split(&w->layout, 0, TV_COLUMNS, 300);
    (void)tv_workspace_split(&w->layout, 2, TV_ROWS, 560);
    w->layout.panes[3].min_width = 40;
    w->root_open = true; w->theme = app->theme;
    return true;
}

static void native_workspace_mode(struct app *app, int mode) {
    struct native_workspace *w;
    if (!native_workspace_init(app)) return;
    w=app->workspace;
    if (mode==1 && w->mode==1) mode=0;
    w->saved[w->mode]=w->layout; w->saved_zoom[w->mode]=w->zoom; w->initialized[w->mode]=true;
    if (w->initialized[mode]) { w->layout=w->saved[mode]; w->zoom=w->saved_zoom[mode]; }
    else {
        tv_workspace_init(&w->layout,18,5);
        (void)tv_workspace_split(&w->layout,0,TV_COLUMNS,250);
        (void)tv_workspace_split(&w->layout,2,TV_ROWS,450);
        (void)tv_workspace_split(&w->layout,4,TV_ROWS,500);
        w->layout.panes[3].min_width=40; w->layout.focus=3; w->zoom=false;
    }
    w->mode=mode; app->view=7; native_workspace_invalidate(app);
}

static void native_workspace_tree(struct app *app) {
    struct native_workspace *w = app->workspace;
    size_t i, count = 1, chosen = 0;
    bool root_selected = w->tree.count && !w->tree.selected;
    if (w->tree.count) w->root_open = w->nodes[0].expanded;
    w->nodes[0] = (struct tv_tree_node){app->fleet ? "Fleet heads" : "Local heads", SIZE_MAX, 0, w->root_open, TV_STRONG};
    retarget_selection(app);
    for (i = 0; i < app->model.head_count; i++) if (head_matches(&app->model.heads[i], app->search)) {
        const struct head *h = &app->model.heads[i];
        if (i == app->selected) chosen = count;
        w->nodes[count++] = (struct tv_tree_node){h->branch, i, 1, false, !strcmp(display_status(h), "LIVE") ? TV_BASE : TV_WARNING};
    }
    (void)tv_tree_init(&w->tree, w->nodes, count);
    w->tree.selected = root_selected ? 0 : chosen;
}

static void native_workspace_select(struct app *app) {
    struct native_workspace *w = app->workspace;
    if (w->tree.selected < w->tree.count && w->nodes[w->tree.selected].value < app->model.head_count)
        app->selected = w->nodes[w->tree.selected].value;
}

static void native_workspace_move(struct app *app, int direction) {
    struct native_workspace *w = app->workspace;
    if (!w) return;
    if (w->layout.focus == 3 && native_terminal_selected(app) && native_terminal_selected(app)->screen) {
        struct native_terminal *t=native_terminal_selected(app);
        t->scrolling=true;
        if (direction<0 && t->scroll<t->screen->history_count) t->scroll++;
        if (direction>0 && t->scroll) t->scroll--;
    }
    else if (w->layout.focus==5) {
        size_t *selected=w->mode==1 && app->plan ? &app->plan->selected : &app->workflow_node;
        size_t indices[TV_GRAPH_MAX_NODES];
        size_t count=w->mode==1 && app->plan ? app->plan->graph.node_count :
            app->workflows ? workflow_nodes(app->workflows,app->workflow_run,indices) : 0;
        if (direction<0 && *selected) (*selected)--;
        if (direction>0 && *selected+1<count) (*selected)++;
    }
    else if (w->layout.focus == 1) { tv_tree_move(&w->tree, direction); native_workspace_select(app); }
    else tv_workspace_scroll(&w->layout, direction, w->layout.focus == 3 ? 13 : w->layout.focus==6 ? NATIVE_PLAN_TEXT : app->model.recovery_count + 5);
}

static bool native_workspace_key(struct app *app, char key) {
    struct native_workspace *w = app->workspace;
    if (!w) return false;
    if (key=='A' || key=='B' || key=='C') { native_workspace_mode(app,key-'A'); return true; }
    if (w->mode==2 && (key=='[' || key==']')) return workflow_key(app,key);
    if (key == 'z') {
        w->zoom = !w->zoom;
        copy_text(app->notice,sizeof(app->notice),w->zoom ? "Focused pane expanded / z restores splits" : "Pane splits restored");
        return true;
    }
    if (key == '\t') { tv_workspace_focus_next(&w->layout, 1); return true; }
    if (w->layout.focus == 1 && (key == 'h' || key == 'l')) {
        tv_tree_expand(&w->tree, key == 'l'); native_workspace_select(app); return true;
    }
    if ((key == '\r' || key == '\n' || key == ':' || key == 'a' || key == 'c') &&
        w->layout.focus == 1 && !w->tree.selected) {
        if (key == '\r' || key == '\n') w->nodes[0].expanded = !w->nodes[0].expanded;
        else copy_text(app->notice, sizeof(app->notice), "Select a head in navigation first");
        return true;
    }
    return false;
}

static void native_workspace_mouse(struct app *app, unsigned button, int x, int y, bool release) {
    struct native_workspace *w = app->workspace;
    if (!w) return;
    if (release) { tv_workspace_release(&w->layout); return; }
    if (button == 32) { (void)tv_workspace_motion(&w->layout, x, y); return; }
    if (button == 0 || button == 64 || button == 65) {
        if (!tv_workspace_press(&w->layout, x, y)) return;
        if (w->layout.drag >= 0) return;
        if (button == 64 || button == 65) native_workspace_move(app, button == 64 ? -1 : 1);
        else if (w->layout.focus == 1) {
            (void)tv_tree_click(&w->tree, w->layout.panes[1].scroll, y - w->layout.panes[1].bounds.y - (w->compact ? 1 : 2));
            native_workspace_select(app);
        }
    }
}

static void native_workspace_details(struct app *app, struct tv_canvas *c, size_t scroll) {
    const struct head *h = selected_head(app);
    int row;
    if (!h) { tv_text(c, (struct tv_rect){0,0,c->width,1}, "No matching head", TV_WARNING); return; }
    for (row = 0; row < c->height; row++) {
        char text[1024];
        switch (scroll + (size_t)row) {
        case 0: snprintf(text, sizeof(text), "%s", h->branch); break;
        case 1: snprintf(text, sizeof(text), "Observed: %s", display_status(h)); break;
        case 2: snprintf(text, sizeof(text), "Agent: %s / %s", h->profile, h->adapter); break;
        case 3: snprintf(text, sizeof(text), "Group: %s", h->group[0] ? h->group : "none"); break;
        case 4: snprintf(text, sizeof(text), "Host: %s", app->fleet ? h->remote_host : "local"); break;
        case 5: snprintf(text, sizeof(text), "Project: %s", app->fleet ? h->remote_project : "current project"); break;
        case 6:
            if (app->fleet || !h->head_id[0]) snprintf(text,sizeof(text),"Activity counters unavailable");
            else snprintf(text,sizeof(text),"%u changed files / %u queued entries",h->diff,h->queue);
            break;
        case 7: snprintf(text, sizeof(text), "PR: %s", h->pr[0] ? h->pr : "not recorded"); break;
        case 9: snprintf(text, sizeof(text), "a interact / Enter details / d provenance"); break;
        case 10: snprintf(text, sizeof(text), ": actions / w workflow graph / D statistics"); break;
        default: text[0]='\0'; break;
        }
        tv_text(c, (struct tv_rect){0,row,c->width,1}, text, row == 0 ? TV_STRONG : TV_BASE);
    }
}

static void native_workspace_activity(struct app *app, struct tv_canvas *c, size_t scroll) {
    const struct head *h = selected_head(app);
    int row;
    for (row = 0; row < c->height; row++) {
        char text[1024]; size_t index = scroll + (size_t)row;
        if (index == 0) snprintf(text, sizeof(text), "%zu heads / %zu recovery findings", app->model.head_count, app->model.recovery_count);
        else if (index < 5 && (!h || app->fleet || !h->head_id[0]))
            snprintf(text, sizeof(text), "%s",index==1 ? "Detailed activity counters unavailable" : "");
        else if (index == 1) snprintf(text, sizeof(text), "Events %u / Messages %u / Signals %u", h->events, h->messages, h->signals);
        else if (index == 2) snprintf(text, sizeof(text), "Queue %u / Changed files %u", h->queue, h->diff);
        else if (index == 3) snprintf(text, sizeof(text), "Gates %u / Approved %u", h->gates, h->approved);
        else if (index == 4) snprintf(text, sizeof(text), "Claims %u / Scopes %u", h->claims, h->scopes);
        else if (index - 5 < app->model.recovery_count) {
            const struct recovery *r = &app->model.recovery[index - 5];
            snprintf(text, sizeof(text), "%s / %s / %s", r->kind, r->label, r->action);
        } else text[0]='\0';
        tv_text(c, (struct tv_rect){0,row,c->width,1}, text, index >= 5 && index - 5 < app->model.recovery_count ? TV_WARNING : TV_BASE);
    }
}

static bool render_native_workspace(struct app *app, unsigned frame, bool headless) {
    struct native_workspace *w;
    struct tv_canvas c;
    int width = app->cols > 512 ? 511 : app->cols - 1;
    int height = app->rows > 256 ? 256 : app->rows, i, x, y;
    bool compact = width < 65 || height < 16;
    long age = headless || !app->snapshot_at ? 0 : (long)(time(NULL) - app->snapshot_at);
    if (width < 19 || height < 6 || !native_workspace_init(app)) return false;
    w = app->workspace; w->compact=compact;
    app->paint = !headless && !app->no_color;
    if (w->theme != app->theme) { tv_present_invalidate(&w->presenter); w->theme = app->theme; }
    (void)tv_init(&c, w->cells, WORKSPACE_CAPACITY, width, height, !app->ascii);
    {
        int root=w->layout.root;
        if (w->zoom || compact) w->layout.root=w->layout.focus;
        (void)tv_workspace_layout(&w->layout, (struct tv_rect){0,compact ? 1 : 2,width,height-(compact ? 2 : 4)});
        w->layout.root=root;
    }
    native_workspace_tree(app);
    dashboard_text(&c, 1, 0, width-2, TV_TITLE, "HYDRA WORKSPACE / %s / %s", w->mode==0 ? "A CONVERSATION" : w->mode==1 ? "B PLAN OVERVIEW" : "C MONITOR", app->snapshot_stale ? "STALE" : app->fleet ? "Fleet" : "Local");
    if (!compact) dashboard_text(&c, 1, 1, width-2, TV_BORDER,
        w->mode==2 ? "[ ] runs / Y approve / N reject / R resume / X cancel / A conversation / B plan / D stats / z %s" :
        "A conversation / B plan / C monitor / P load / V validate / E approve / D stats / z %s", w->zoom ? "restore panes" : "zoom focus");
    for (i = 0; i < w->layout.count; i++) {
        struct tv_pane *p = &w->layout.panes[i];
        struct tv_canvas view, content;
        if (p->split != TV_LEAF) {
            for (y = 0; y < p->divider.height; y++) for (x = 0; x < p->divider.width; x++)
                tv_put(&c, p->divider.x+x, p->divider.y+y, p->split == TV_COLUMNS ? (app->ascii ? '|' : 0x2502) : (app->ascii ? '-' : 0x2500), TV_BORDER);
            continue;
        }
        if (!tv_canvas_view(&view, &c, p->bounds)) continue;
        {
            struct native_terminal *t=native_terminal_selected(app);
            bool attached=i==3 && t && t->screen;
            if (compact) {
                if (attached) dashboard_text(&view,0,0,view.width,TV_SELECTED,"%.14s / %s",t->label,
                    t->client.finished || t->client.eof ? "DISCONNECTED" : "INPUT TO AGENT");
                else dashboard_text(&view,0,0,view.width,TV_SELECTED,"FOCUS / %s",i==1 ? "NAVIGATION" : i==3 ? "SELECTED WORK" : i==5 ? "DEPENDENCIES" : i==6 ? "PLAN / EVIDENCE" : "ACTIVITY / RECOVERY");
            } else tv_panel(&view, (struct tv_rect){0,0,view.width,view.height}, i == 1 ? "NAVIGATION" : i == 3 ? attached ? t->label : "SELECTED WORK" : i==5 ? "DEPENDENCIES" : i==6 ? "PLAN / EVIDENCE" : "ACTIVITY / RECOVERY");
            if (!compact && attached) dashboard_text(&view,1,1,view.width-2,i==w->layout.focus ? TV_SELECTED : TV_BORDER,
                "%s / %s", t->client.finished || t->client.eof ? "CLIENT DISCONNECTED" : "ATTACHED",
                t->client.finished || t->client.eof ? "NO INPUT / Ctrl-B r reconnect" :
                i==w->layout.focus ? "INPUT TO AGENT / Ctrl-B Tab Hydra" : "Tab to interact");
        }
        if (!compact && (i!=3 || !native_terminal_selected(app) || !native_terminal_selected(app)->screen))
            dashboard_text(&view, 1, 1, view.width-2, i == w->layout.focus ? TV_SELECTED : TV_BORDER,
                           "%s / scroll %zu", i == w->layout.focus ? "FOCUS" : "Tab to focus", p->scroll);
        if (!tv_canvas_view(&content, &view, compact ? (struct tv_rect){0,1,view.width,view.height-1} :
            (struct tv_rect){1,2,view.width-2,view.height-3})) continue;
        if (i == 1) tv_tree_draw(&w->tree, &content, &w->layout.panes[1].scroll, w->layout.focus == 1);
        else if (i == 3 && native_terminal_selected(app) && native_terminal_selected(app)->screen)
            native_terminal_draw(app,&content,w->layout.focus==3);
        else if (i == 3) native_workspace_details(app, &content, p->scroll);
        else if (i==5) native_workspace_graph(app,&content,w->mode==1);
        else if (i==6 && w->mode==2) native_workspace_evidence_text(app,&content,&p->scroll);
        else if (i==6 || (i==4 && app->plan)) native_workspace_plan_text(app,&content,&p->scroll);
        else native_workspace_activity(app, &content, p->scroll);
    }
    if (!compact) {
        const char *notice=app->snapshot_stale ? app->snapshot_error : app->notice;
        dashboard_text(&c, 0, height-2, width, app->snapshot_stale ? TV_WARNING : TV_BORDER, "%s snapshot / age %lds%s%s",
            app->snapshot_stale ? "STALE: last good" : "Current", age < 0 ? 0 : age, notice[0] ? " / " : "",notice);
    }
    if (compact && app->notice[0] && !(w->layout.focus==3 && native_terminal_selected(app) && native_terminal_selected(app)->screen))
        tv_text(&c,(struct tv_rect){0,height-1,width,1},app->notice,TV_WARNING);
    else tv_text(&c, (struct tv_rect){0,height-1,width,1},
            w->layout.focus==3 && native_terminal_selected(app) && native_terminal_selected(app)->screen ?
            (native_terminal_selected(app)->client.finished || native_terminal_selected(app)->client.eof ?
             "DISCONNECTED / Ctrl-B r reconnect" : width<100 ? "Ctrl-B Tab Hydra / Ctrl-B q quit" :
             "INPUT TO AGENT / Ctrl-B Tab Hydra / Ctrl-B z zoom / Ctrl-B D statistics / Ctrl-B n next / Ctrl-B q quit") : compact && w->mode==2 ? "Y approve N reject R resume X cancel" : width < 60 ? "Tab panes j/k scroll D stats q quit" : width < 100 ? "Tab focus z zoom a interact D statistics q quit" :
            "Tab focus / j k scroll / h l tree / drag splits / a interact / D statistics / : actions / q quit", TV_STRONG);
    if (headless) {
        printf("FRAME %u %dx%d\n", frame, app->cols, app->rows);
        for (y = 0; y < height; y++) { (void)tv_write_row(&c, y, stdout, NULL, NULL); putchar('\n'); }
    } else if (!tv_present(&w->presenter, &c, stdout, dashboard_style, app)) {
        copy_text(app->notice, sizeof(app->notice), "Workspace output failed"); app->running = false;
    }
    return true;
}
