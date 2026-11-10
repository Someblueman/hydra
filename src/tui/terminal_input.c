#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Input while an agent pane may hold it. Every key goes to the agent except
 * the chords of the TERMINAL key scope (keys.c), which are Hydra's own
 * commands; a chord that starts with a control key also works when no agent
 * pane holds input. */

/* Keys reach an agent only through a connected client: a disconnected pane
 * never captures input, so Tab, Esc and a still work in Hydra. */
bool native_terminal_focused(struct app *app) {
    struct native_terminal *t=app->workspace ? native_workspace_terminal(app,app->workspace->layout.focus) : NULL;
    return app->view==7 && t && t->screen && !t->client.finished && !t->client.eof;
}
static bool terminal_head_matches(const struct app *app, const struct head *h, const struct native_terminal *t) {
    return !strcmp(h->head_id,t->head) && !strcmp(h->instance,t->instance) &&
        (!app->fleet || (!strcmp(h->remote_host,t->remote_host) && !strcmp(h->remote_project,t->remote_project)));
}

static size_t terminal_head_index(const struct app *app, const struct native_terminal *t) {
    size_t head;
    for (head=0;head<app->model.head_count;head++)
        if (terminal_head_matches(app,&app->model.heads[head],t)) break;
    return head;
}

/* The next open agent view, with its head selected. */
static void next_agent(struct app *app) {
    struct native_terminals *set=app->terminals;
    size_t i;
    if (!set) return;
    for (i=1;i<=NATIVE_TERMINALS;i++) {
        size_t next=(set->selected+i)%NATIVE_TERMINALS, head;
        if (!set->slots[next].screen) continue;
        set->selected=next; app->view=7;
        native_workspace_show_terminal(app,true);
        head=terminal_head_index(app,&set->slots[next]);
        if (head<app->model.head_count) app->selected=head;
        return;
    }
}

static void reconnect(struct app *app, struct native_terminal *t) {
    size_t head=terminal_head_index(app,t);
    if (head<app->model.head_count) { app->selected=head; (void)native_terminal_attach(app); }
    else copy_text(app->notice,sizeof(app->notice),"Original instance unavailable; select current work explicitly");
}

/* Leaving input keeps the view open; closing ends only this client's view. */
static void leave_input(struct app *app) {
    native_workspace_focus_next(app);
    if (!native_terminal_focused(app))
        copy_text(app->notice,sizeof(app->notice),"Keys go to Hydra; the agent view stays open");
}

static void close_view(struct app *app, struct native_terminal *t) {
    native_terminal_close(t); app->workspace->layout.focus=1;
    copy_text(app->notice,sizeof(app->notice),"View closed; the agent keeps running (a reopens)");
}

/* Commands on the selected agent view. The literal command sends the key
 * that completed its chord, so a key Hydra keeps can still be typed. */
static void view_command(struct app *app, enum key_action action, const struct kt_event *e) {
    struct native_terminal *t=native_terminal_selected(app);
    if (!t || !t->screen) { copy_text(app->notice,sizeof(app->notice),"No agent view is open; a opens the selected one"); return; }
    switch (action) {
    case KEY_TERMINAL_RECONNECT: reconnect(app,t); break;
    case KEY_TERMINAL_CLOSE: close_view(app,t); break;
    case KEY_TERMINAL_HISTORY: native_terminal_history(app,t,true); break;
    case KEY_TERMINAL_LIVE: native_terminal_history(app,t,false); break;
    case KEY_TERMINAL_LITERAL: if (native_terminal_focused(app)) native_terminal_send(app,t,e->bytes,e->length); break;
    default: break;
    }
}

/* Runs the command a completed chord is bound to. */
static void key_command(struct app *app, const struct kt_binding *binding, const struct kt_event *e) {
    enum key_action action=(enum key_action)binding->action;
    if (action==KEY_APP_QUIT) { app->running=false; return; }
    if (action==KEY_VIEW_STATISTICS) { statistics_toggle(app); return; }
    if (!native_workspace_init(app)) return;
    switch (action) {
    case KEY_TERMINAL_LEAVE: leave_input(app); break;
    case KEY_LAYOUT_CONVERSATION: case KEY_LAYOUT_PLAN: case KEY_LAYOUT_MONITOR:
        native_workspace_mode(app,(int)action-(int)KEY_LAYOUT_CONVERSATION); break;
    case KEY_WORKSPACE_ZOOM: (void)native_workspace_key(app,'z'); break;
    case KEY_WORKSPACE_SPLIT: native_workspace_split_agents(app); break;
    case KEY_TERMINAL_NEXT: next_agent(app); break;
    default: view_command(app,action,e); break;
    }
}

/* The part of a pane that shows the agent's screen. */
static struct kt_rect pane_content(const struct native_workspace *w, int pane) {
    struct kt_rect bounds=w->layout.panes[pane].bounds;
    int left=w->compact ? 0 : 1, top=w->compact ? 1 : 2, border=w->compact ? 0 : 1;
    return (struct kt_rect){bounds.x+left,bounds.y+top,bounds.width-left-border,bounds.height-top-border};
}

static bool rect_holds(const struct kt_rect *r, int x, int y) {
    return x>=r->x && x<r->x+r->width && y>=r->y && y<r->y+r->height;
}

/* The attached view whose screen is under the pointer, with its pane. */
static struct native_terminal *terminal_under(struct app *app, const struct kt_event *e, int *pane, struct kt_rect *area) {
    struct native_workspace *w=app->workspace;
    int i;
    for (i=0;i<w->layout.count;i++) {
        struct native_terminal *t=native_workspace_terminal(app,i);
        if (!t || !t->screen) continue;
        *area=pane_content(w,i);
        if (rect_holds(area,e->x,e->y)) { *pane=i; return t; }
    }
    return NULL;
}

/* A click on an agent's screen focuses its pane. The agent receives the
 * report, in its own coordinates, when it asked for mouse input; every other
 * report is Hydra's. */
static void terminal_mouse(struct app *app, const struct kt_event *e) {
    struct native_workspace *w=app->workspace;
    struct kt_rect area={0,0,0,0};
    struct native_terminal *t;
    int pane=0;
    if (!w || !app->terminals) return;
    t=terminal_under(app,e,&pane,&area);
    if (t && e->button==0 && !e->release) { w->layout.focus=pane; native_workspace_sync_terminal(app); }
    if (t && t->screen->mouse_mode && t->screen->mouse_sgr && !t->scrolling && w->layout.drag<0) {
        char bytes[64];
        int n=snprintf(bytes,sizeof(bytes),"\033[<%u;%d;%d%c",e->button,e->x-area.x+1,e->y-area.y+1,e->release ? 'm' : 'M');
        if (n>0 && (size_t)n<sizeof(bytes)) native_terminal_send(app,t,bytes,(size_t)n);
        return;
    }
    native_workspace_mouse(app,e->button,e->x,e->y,e->release);
}

/* Pasted text is the agent's; the brackets go along when it asked for them.
 * True when this layer holds input, whether or not an agent could take it. */
bool native_terminal_paste(struct app *app, const char *bytes, size_t length) {
    struct native_terminal *t=native_terminal_selected(app);
    if (!native_terminal_focused(app)) return native_keys(app)->state.pending.count != 0;
    if (t->screen->bracketed_paste) native_terminal_send(app,t,"\033[200~",6);
    native_terminal_send(app,t,bytes,length);
    if (t->screen->bracketed_paste) native_terminal_send(app,t,"\033[201~",6);
    return true;
}

/* Keys while a fleet client's own history is shown. */
static void scroll_key(struct native_terminal *t, const struct kt_event *e) {
    size_t delta=(e->key==KT_KEY_PAGE_UP || e->key==KT_KEY_PAGE_DOWN) ? 10 : 1;
    if (kt_event_is(e,'k',0) || e->key==KT_KEY_UP || e->key==KT_KEY_PAGE_UP)
        t->scroll=delta>t->screen->history_count-t->scroll ? t->screen->history_count : t->scroll+delta;
    else if (kt_event_is(e,'j',0) || e->key==KT_KEY_DOWN || e->key==KT_KEY_PAGE_DOWN)
        t->scroll=t->scroll>delta ? t->scroll-delta : 0;
    else if (e->key==KT_KEY_ESCAPE) { t->scrolling=false; t->scroll=0; }
}

/* A chord that matched nothing is dropped whole and named, so no part of it
 * reaches the agent and the user sees why nothing happened. The notice goes
 * away with the next key. */
static void unbound_notice(struct app *app, struct native_keys *k) {
    char chord[96];
    (void)kt_chord_format(&k->state.abandoned,chord,sizeof(chord));
    snprintf(k->unbound,sizeof(k->unbound),"%s is not bound; nothing was sent to the agent",chord);
    copy_text(app->notice,sizeof(app->notice),k->unbound);
}

static void unbound_clear(struct app *app, struct native_keys *k) {
    if (k->unbound[0] && !strcmp(app->notice,k->unbound)) app->notice[0]='\0';
    k->unbound[0]='\0';
}

static void terminal_key(struct app *app, const struct kt_event *e) {
    struct native_keys *k=native_keys(app);
    const struct kt_binding *binding=NULL;
    struct native_terminal *t;
    enum kt_key_match match=kt_keymap_feed(&k->map,&k->state,KEY_SCOPE_TERMINAL,e,&binding);
    unbound_clear(app,k);
    if (match==KT_KEY_ACTION) { key_command(app,binding,e); return; }
    if (match==KT_KEY_ABANDONED) { unbound_notice(app,k); return; }
    if (match==KT_KEY_PENDING || !native_terminal_focused(app)) return;
    t=native_terminal_selected(app);
    if (t->scrolling) scroll_key(t,e);
    else native_terminal_send(app,t,e->bytes,e->length);
}

void native_terminal_event(struct app *app, const struct kt_event *e) {
    if (e->type==KT_MOUSE) terminal_mouse(app,e);
    else if (e->type==KT_KEY) terminal_key(app,e);
}

/* A control key that begins a chord. Hydra keeps these whether or not an
 * agent pane holds input, so its commands are reachable from every view. */
static bool reserved_key(const struct native_keys *k, const struct kt_event *e) {
    struct kt_keymap_state idle;
    if (e->type!=KT_KEY || e->length!=1 || !(e->mods&KT_MOD_CTRL)) return false;
    kt_keymap_reset(&idle);
    return kt_keymap_feed(&k->map,&idle,KEY_SCOPE_TERMINAL,e,NULL)!=KT_KEY_UNBOUND;
}

/* True when the event belongs to this layer: an agent pane holds input, a
 * chord is under way, or the key starts a chord. */
bool native_terminal_wants(struct app *app, const struct kt_event *e) {
    const struct native_keys *k=native_keys(app);
    return native_terminal_focused(app) || k->state.pending.count || reserved_key(k,e);
}
