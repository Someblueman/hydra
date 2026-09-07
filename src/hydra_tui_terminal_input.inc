static bool native_terminal_focused(struct app *app) {
    struct native_terminal *t=app->workspace ? native_workspace_terminal(app,app->workspace->layout.focus) : NULL;
    return app->view==7 && t && t->screen;
}

static void native_terminal_event(struct app *app, const struct tv_event *e) {
    struct native_terminals *set=app->terminals;
    struct native_terminal *t=native_terminal_selected(app);
    struct native_workspace *w=app->workspace;
    if (!set || !w) return;
    if (e->type==TV_MOUSE) {
        struct tv_rect r={0,0,0,0};
        int pane;
        int left=w->compact ? 0 : 1, top=w->compact ? 1 : 2, border=w->compact ? 0 : 1;
        for (pane=0;pane<w->layout.count;pane++) {
            struct native_terminal *candidate=native_workspace_terminal(app,pane);
            struct tv_rect bounds=w->layout.panes[pane].bounds;
            if (candidate && candidate->screen && e->x>=bounds.x+left && e->x<bounds.x+bounds.width-border &&
                e->y>=bounds.y+top && e->y<bounds.y+bounds.height-border) {
                r=bounds; t=candidate;
                if (e->button==0 && !e->release) { w->layout.focus=pane; native_workspace_sync_terminal(app); }
                break;
            }
        }
        if (t && t->screen && t->screen->mouse_mode && t->screen->mouse_sgr && !t->scrolling &&
            w->layout.drag<0 && e->x>=r.x+left && e->x<r.x+r.width-border && e->y>=r.y+top && e->y<r.y+r.height-border) {
            char bytes[64];
            int n=snprintf(bytes,sizeof(bytes),"\033[<%u;%d;%d%c",e->button,e->x-r.x-left+1,e->y-r.y-top+1,e->release ? 'm' : 'M');
            if (n>0 && (size_t)n<sizeof(bytes)) native_terminal_send(app,t,bytes,(size_t)n);
        } else native_workspace_mouse(app,e->button,e->x,e->y,e->release);
        return;
    }
    if (e->type!=TV_KEY) {
        if (native_terminal_focused(app) && (e->type==TV_PASTE || (t->screen->bracketed_paste &&
            (e->type==TV_PASTE_BEGIN || e->type==TV_PASTE_END)))) native_terminal_send(app,t,e->bytes,e->length);
        return;
    }
    if (set->prefix) {
        set->prefix=false;
        if (e->key=='q') app->running=false;
        else if (e->key=='\t') native_workspace_focus_next(app);
        else if (e->key=='D') statistics_toggle(app);
        else if (e->key=='A' || e->key=='B' || e->key=='C') native_workspace_mode(app,(int)e->key-'A');
        else if (e->key=='z') (void)native_workspace_key(app,'z');
        else if (e->key=='S') native_workspace_split_agents(app);
        else if (e->key=='n') {
            size_t i;
            for (i=1;i<=NATIVE_TERMINALS;i++) {
                size_t next=(set->selected+i)%NATIVE_TERMINALS;
                if (set->slots[next].screen) {
                    size_t head;
                    set->selected=next; app->view=7;
                    native_workspace_show_terminal(app,true);
                    for (head=0;head<app->model.head_count;head++)
                        if (!strcmp(app->model.heads[head].head_id,set->slots[next].head) &&
                            !strcmp(app->model.heads[head].instance,set->slots[next].instance)) { app->selected=head; break; }
                    break;
                }
            }
        } else if (e->key=='r' && t && t->screen) {
            size_t head;
            for (head=0;head<app->model.head_count;head++)
                if (!strcmp(app->model.heads[head].head_id,t->head) && !strcmp(app->model.heads[head].instance,t->instance)) break;
            if (head<app->model.head_count) { app->selected=head; (void)native_terminal_attach(app); }
            else copy_text(app->notice,sizeof(app->notice),"Original instance unavailable; select current work explicitly");
        } else if (e->key=='x' && t) { native_terminal_close(t); w->layout.focus=1; }
        else if (e->key=='[' && t) t->scrolling=true;
        else if (e->key==']' && t) { t->scrolling=false; t->scroll=0; }
        else if (e->key==2) native_terminal_send(app,t,e->bytes,e->length);
        return;
    }
    if (e->key==2) { set->prefix=true; return; }
    if (!native_terminal_focused(app)) return;
    if (t->scrolling) {
        size_t delta=(e->key==TV_KEY_PAGE_UP || e->key==TV_KEY_PAGE_DOWN) ? 10 : 1;
        if (e->key=='k' || e->key==TV_KEY_UP || e->key==TV_KEY_PAGE_UP)
            t->scroll=delta>t->screen->history_count-t->scroll ? t->screen->history_count : t->scroll+delta;
        else if (e->key=='j' || e->key==TV_KEY_DOWN || e->key==TV_KEY_PAGE_DOWN)
            t->scroll=t->scroll>delta ? t->scroll-delta : 0;
        else if (e->key==27) { t->scrolling=false; t->scroll=0; }
        return;
    }
    native_terminal_send(app,t,e->bytes,e->length);
}

static bool native_terminal_byte(struct app *app, unsigned char byte) {
    struct tv_event e;
    if (!app->terminals || (!native_terminal_focused(app) && !app->terminals->prefix && byte!=2)) return false;
    if (tv_input_feed(&app->terminals->input,byte,&e)) native_terminal_event(app,&e);
    return true;
}

static void native_terminal_flush_input(struct app *app) {
    struct tv_event e;
    if (app->terminals && tv_input_flush(&app->terminals->input,&e)) native_terminal_event(app,&e);
}
