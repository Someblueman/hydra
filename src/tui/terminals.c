#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Hydra owns the attachment set. Each PTY owns only an attach client; tmux
 * continues owning the agent session when a client is closed or the UI exits. */





struct native_terminal *native_terminal_selected(struct app *app) {
    if (!app->terminals) return NULL;
    return &app->terminals->slots[app->terminals->selected];
}

const char *native_agent_name(const struct head *h) {
    return !h->profile[0] || !strcmp(h->profile, "none") || !strcmp(h->profile, "-") ? "shell" : h->profile;
}

static bool terminal_is_head(const struct app *app, const struct head *h, const struct native_terminal *t) {
    return !strcmp(h->head_id,t->head) && !strcmp(h->instance,t->instance) &&
        (!app->fleet || (!strcmp(h->remote_host,t->remote_host) && !strcmp(h->remote_project,t->remote_project)));
}

/* Names the agent and what Hydra observes about its session. Output activity,
 * a live tmux session and a reported outcome cannot establish whether an
 * interactive agent is waiting for a decision, so the label never claims one. */
static const struct head *terminal_head(const struct app *app, const struct native_terminal *t) {
    size_t i;
    for (i=0;i<app->model.head_count;i++) if (terminal_is_head(app,&app->model.heads[i],t)) return &app->model.heads[i];
    return NULL;
}

/* Only an exact recorded exit or failure is an outcome worth naming. */
static const char *recorded_outcome(const struct head *h) {
    if (strcmp(h->confidence,"exact")) return NULL;
    if (!strcmp(h->observed,"exited")) return "exit recorded";
    if (!strcmp(h->observed,"failed")) return "failure recorded";
    return NULL;
}

void native_terminal_attention(struct app *app, const struct native_terminal *t, char *out, size_t size) {
    const struct head *h=terminal_head(app,t);
    const char *outcome;
    if (!h) { snprintf(out,size,"%s",app->snapshot_stale ? "snapshot stale" : "old instance"); return; }
    outcome=app->snapshot_stale ? "snapshot stale" : recorded_outcome(h);
    if (outcome) snprintf(out,size,"%s%s%s",native_agent_name(h),dot(app),outcome);
    else snprintf(out,size,"%s%ssession %s",native_agent_name(h),dot(app),app->fleet ? h->desired : status_label(h));
}

struct native_terminal *native_terminal_for_head(struct app *app, const struct head *h) {
    size_t i;
    if (!app->terminals || !h) return NULL;
    for (i=0;i<NATIVE_TERMINALS;i++) {
        struct native_terminal *t=&app->terminals->slots[i];
        if (t->screen && !t->client.finished && !t->client.eof && terminal_is_head(app,h,t)) return t;
    }
    return NULL;
}

static void outbox_clear(struct native_terminal *t) {
    size_t i;
    for (i=0;i<t->outbox_count;i++) free(t->outbox[i]);
    t->outbox_count=0; t->enter_pending=false;
}

/* A tmux client draws on the alternate screen without scrollback, so the
 * pane's history lives in tmux: scrolling uses tmux copy mode on the exact
 * session, and q, Esc or Ctrl-B ] leave it. Fleet clients keep model scroll. */
bool native_terminal_history(struct app *app, struct native_terminal *t, bool enter) {
    const struct head *h = terminal_head(app, t);
    struct output_child child;
    char target[TEXT + 8U];
    char *copy[] = {(char *)"tmux", (char *)"copy-mode", (char *)"-u", (char *)"-t", target, NULL};
    char *cancel[] = {(char *)"tmux", (char *)"send-keys", (char *)"-t", target, (char *)"-X", (char *)"cancel", NULL};
    char discard[256];
    if (app->fleet || !h || !h->session[0] || t->client.finished || t->client.eof) return false;
    snprintf(target, sizeof(target), "=%s:", h->session);
    if (output_start(&child, enter ? copy : cancel, 1000L)) return false;
    while (output_read(&child, discard, sizeof(discard)) > 0) {}
    if (output_finish(&child, false) && enter) return false;
    copy_text(app->notice, sizeof(app->notice), enter ? "Agent history: PgUp/PgDn or arrows scroll; q leaves" : "Back to live agent output");
    return true;
}

void native_terminal_close(struct native_terminal *t) {
    if (!t->screen) return;
    outbox_clear(t);
    tv_pty_close(&t->client);
    free(t->screen); free(t->cells); free(t->history);
    memset(t,0,sizeof(*t)); t->client.fd=-1; t->client.pid=-1;
}

void native_terminals_destroy(struct app *app) {
    size_t i;
    if (!app->terminals) return;
    for (i=0;i<NATIVE_TERMINALS;i++) native_terminal_close(&app->terminals->slots[i]);
    free(app->terminals); app->terminals=NULL;
}

static bool fleet_target_ready(const struct app *app, const struct head *h) {
    size_t i;
    if (!app->fleet) return true;
    if (app->snapshot_stale || !h->remote_host[0] || !h->remote_project[0] || !h->remote_branch[0]) return false;
    for (i = 0; i < app->model.host_count; i++) if (!strcmp(app->model.hosts[i].name, h->remote_host))
        return !strcmp(app->model.hosts[i].connection, "reachable") && !strcmp(app->model.hosts[i].freshness, "fresh");
    return false;
}

static size_t terminal_slot(struct app *app, const struct head *h) {
    size_t i, available = NATIVE_TERMINALS;
    for (i = 0; i < NATIVE_TERMINALS; i++) {
        struct native_terminal *t = &app->terminals->slots[i];
        if (!t->screen && available == NATIVE_TERMINALS) available = i;
        if (t->screen && terminal_is_head(app, h, t)) {
            app->terminals->selected = i;
            if (!t->client.finished && !t->client.eof) return i;
            native_terminal_close(t); return i;
        }
    }
    return available;
}

/* PTY children drop nesting variables. Carry the observed server explicitly. */
static bool attachment_socket(char socket[4096]) {
    const char *parent = getenv("TMUX");
    socket[0] = '\0';
    if (!parent || !parent[0]) return true;
    if (strlen(parent) >= 4096) return false;
    copy_text(socket, 4096, parent);
    for (int i = 0; i < 2; i++) {
        char *comma = strrchr(socket, ',');
        if (!comma) return false;
        *comma = '\0';
    }
    return socket[0] == '/';
}

static bool attachment_argv(const struct app *app, const struct head *h,
                            struct native_terminal *t, char *argv[12], char socket[4096]) {
    if (app->fleet) {
        argv[0]=(char *)app->hydra; argv[1]=(char *)"fleet"; argv[2]=(char *)"attach"; argv[3]=(char *)h->remote_host;
        argv[4]=(char *)"--project"; argv[5]=(char *)h->remote_project; argv[6]=(char *)"--instance"; argv[7]=t->instance;
        argv[8]=(char *)"--"; argv[9]=(char *)h->remote_branch; argv[10]=NULL;
    } else {
        argv[0]=(char *)app->hydra; argv[1]="tui"; argv[2]="--attach"; argv[3]=t->head; argv[4]=t->instance; argv[5]=NULL;
        if (!attachment_socket(socket)) return false;
        if (socket[0]) { argv[5]=socket; argv[6]=NULL; }
    }
    return true;
}

bool native_terminal_attach(struct app *app) {
    return native_terminal_attach_head(app, selected_head(app));
}

static bool terminal_start(struct app *app, const struct head *h, size_t available);

bool native_terminal_attach_head(struct app *app, const struct head *h) {
    size_t available;
    if (h && !app->fleet && head_headless(h)) {
        copy_text(app->notice,sizeof(app->notice),"Headless head: it has no terminal to attach; p shows its step output in Details"); return false;
    }
    if (!h || !h->head_id[0] || !h->instance[0] || !strcmp(h->instance,"-") ||
        !strcmp(h->desired,"headless")) {
        copy_text(app->notice,sizeof(app->notice),app->fleet ? "Select an interactive remote head with a current instance" : "Select a recorded local head to open an interactive pane"); return false;
    }
    if (!fleet_target_ready(app, h)) {
        copy_text(app->notice,sizeof(app->notice),"Remote target is stale or lacks an exact interactive identity; refresh first"); return false;
    }
    if (!app->terminals) {
        app->terminals=calloc(1,sizeof(*app->terminals));
        if (!app->terminals) return false;
        tv_input_init(&app->terminals->input);
    }
    available = terminal_slot(app, h);
    if (available < NATIVE_TERMINALS && app->terminals->slots[available].screen &&
        !app->terminals->slots[available].client.finished && !app->terminals->slots[available].client.eof) return true;
    if (available==NATIVE_TERMINALS) {
        copy_text(app->notice,sizeof(app->notice),"Four attached panes open; Ctrl-B x closes only the selected client"); return false;
    }
    return terminal_start(app, h, available);
}

/* Starts an attach client for h in the free slot. */
static bool terminal_start(struct app *app, const struct head *h, size_t available) {
    struct native_terminal *t=&app->terminals->slots[available];
    char *argv[12], socket[4096];
    t->client.fd=-1; t->client.pid=-1;
    t->screen=calloc(1,sizeof(*t->screen));
    t->cells=calloc(NATIVE_TERMINAL_CELLS*2,sizeof(*t->cells));
    t->history=calloc(512U*256U,sizeof(*t->history));
    if (!t->screen || !t->cells || !t->history) goto fail;
    if (!tv_term_init(t->screen,t->cells,t->cells+NATIVE_TERMINAL_CELLS,512,256,t->history,256,80,24)) goto fail;
    copy_text(t->head,sizeof(t->head),h->head_id); copy_text(t->instance,sizeof(t->instance),h->instance);
    copy_text(t->label,sizeof(t->label),h->branch);
    if (app->fleet) {
        copy_text(t->remote_host,sizeof(t->remote_host),h->remote_host);
        copy_text(t->remote_project,sizeof(t->remote_project),h->remote_project);
    }
    if (!attachment_argv(app, h, t, argv, socket)) goto fail;
    if (!tv_pty_spawn(&t->client,app->hydra,argv,&app->saved,80,24)) goto fail;
    app->terminals->selected=available;
    copy_text(app->notice,sizeof(app->notice),"Attached; keys go to the agent");
    return true;
fail:
    /* screen can be absent when the first allocation failed. */
    tv_pty_close(&t->client); free(t->screen); free(t->cells); free(t->history);
    memset(t,0,sizeof(*t)); t->client.fd=-1; t->client.pid=-1;
    copy_text(app->notice,sizeof(app->notice),"Unable to allocate or start attachment client");
    return false;
}

void native_terminal_send(struct app *app, struct native_terminal *t, const void *bytes, size_t length) {
    if (!t || !t->screen || t->client.eof || !tv_pty_enqueue(&t->client,bytes,length))
        copy_text(app->notice,sizeof(app->notice),"Input not delivered: client disconnected or input queue full");
}

/* Queues Hydra-authored text for the agent's terminal. Control bytes become
 * spaces, so the text can neither submit itself nor end its paste framing. */
bool native_terminal_deliver(struct app *app, struct native_terminal *t, const char *text, bool submit) {
    char *copy;
    size_t i;
    if (!t || !t->screen || t->client.finished || t->client.eof || t->outbox_count>=4 || !(copy=strdup(text))) {
        copy_text(app->notice,sizeof(app->notice),"Input not delivered: agent client unavailable or busy");
        return false;
    }
    for (i=0;copy[i];i++) if ((unsigned char)copy[i]<32 || copy[i]==127) copy[i]=' ';
    t->outbox[t->outbox_count]=copy; t->outbox_submit[t->outbox_count++]=submit;
    return true;
}

static long outbox_elapsed(const struct timespec *since) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC,&now);
    return (long)(now.tv_sec-since->tv_sec)*1000L+(now.tv_nsec-since->tv_nsec)/1000000L;
}

static void outbox_shift(struct native_terminal *t) {
    free(t->outbox[0]);
    memmove(t->outbox,t->outbox+1,(t->outbox_count-1)*sizeof(t->outbox[0]));
    memmove(t->outbox_submit,t->outbox_submit+1,(t->outbox_count-1)*sizeof(t->outbox_submit[0]));
    t->outbox_count--;
}

/* One entry per step once the client has drawn: the text goes in as a paste,
 * framed exactly as a user's paste is forwarded, and a submitted entry sends
 * Enter separately so the agent never mistakes it for pasted text. */
static void outbox_step(struct app *app, struct native_terminal *t) {
    if (!t->outbox_count || !t->received || t->client.finished || t->client.eof || outbox_elapsed(&t->outbox_at)<300) return;
    if (t->enter_pending) {
        native_terminal_send(app,t,"\r",1);
        t->enter_pending=false; outbox_shift(t);
    } else {
        if (t->screen->bracketed_paste) native_terminal_send(app,t,"\033[200~",6);
        native_terminal_send(app,t,t->outbox[0],strlen(t->outbox[0]));
        if (t->screen->bracketed_paste) native_terminal_send(app,t,"\033[201~",6);
        if (t->outbox_submit[0]) t->enter_pending=true;
        else outbox_shift(t);
    }
    (void)clock_gettime(CLOCK_MONOTONIC,&t->outbox_at);
}

/* Reads a bounded burst of client output into the terminal model. */
static void terminal_read(struct native_terminal *t) {
    size_t chunks;
    for (chunks=0; !t->client.eof && t->client.fd>=0 && chunks<8; chunks++) {
        char bytes[4096];
        ssize_t n=tv_pty_read(&t->client,bytes,sizeof(bytes));
        if (n<=0) {
            if (!n) tv_term_finish(t->screen);
            else if (errno!=EAGAIN && errno!=EINTR) tv_pty_close(&t->client);
            return;
        }
        if (!t->received) { t->received=true; (void)clock_gettime(CLOCK_MONOTONIC,&t->outbox_at); }
        tv_term_feed(t->screen,bytes,(size_t)n);
    }
}

void native_terminals_pump(struct app *app) {
    size_t i;
    if (!app->terminals) return;
    for (i=0;i<NATIVE_TERMINALS;i++) {
        struct native_terminal *t=&app->terminals->slots[i];
        uint64_t before;
        if (!t->screen) continue;
        before=t->screen->history_serial;
        if (!tv_pty_flush(&t->client) || !tv_pty_reap(&t->client)) {
            copy_text(app->notice,sizeof(app->notice),"Attachment transport failed; recorded agent outcome is unchanged");
            tv_pty_close(&t->client);
        }
        terminal_read(t);
        if (t->scroll>t->screen->history_count) t->scroll=t->screen->history_count;
        if (t->scroll && t->screen->history_serial>before) {
            uint64_t added=t->screen->history_serial-before;
            t->scroll=added>t->screen->history_count-t->scroll ? t->screen->history_count : t->scroll+(size_t)added;
        }
        if (t->screen->reply_length && !t->client.finished && !t->client.eof) {
            char reply[256]; size_t n=tv_term_take_reply(t->screen,reply,sizeof(reply));
            native_terminal_send(app,t,reply,n);
        }
        outbox_step(app,t);
    }
}

static bool fill_glyph(uint32_t glyph) { return glyph == 0xb7U || glyph == '.'; }
static bool edge_glyph(uint32_t glyph) { return glyph == 0x2502U || glyph == 0x2518U || glyph == '|' || glyph == '+'; }

/* A row ending in tmux's out-of-window fill: a window edge, then dots to the
 * right margin. tmux draws this when another client set a smaller size. */
static bool fill_row(const struct tv_canvas *c, int y) {
    const struct tv_cell *row = c->cells + (size_t)y * (size_t)c->stride;
    int x = c->width - 1, dots = 0;
    while (x >= 0 && fill_glyph(row[x].glyph)) { x--; dots++; }
    return dots >= 4 && x >= 0 && edge_glyph(row[x].glyph);
}

/* Two such rows identify another client's window size, not agent output. */
static bool foreign_size(const struct tv_canvas *c) {
    int y, rows = 0;
    for (y = 0; y < c->height && rows < 2; y++) rows += fill_row(c, y);
    return rows >= 2;
}

static void strip_rendition(struct tv_canvas *c) {
    int x,y;
    for (y=0;y<c->height;y++) for (x=0;x<c->width;x++) {
        struct tv_cell *cell=&c->cells[(size_t)y*(size_t)c->stride+(size_t)x];
        cell->attributes=0; cell->style=TV_BASE;
    }
}

void native_terminal_draw(struct app *app, struct native_terminal *t, struct tv_canvas *c, bool focused) {
    if (!t || !t->screen) return;
    if (c->width!=t->screen->primary.canvas.width || c->height!=t->screen->primary.canvas.height) {
        if (!tv_term_resize(t->screen,c->width,c->height) ||
            (!t->client.finished && !tv_pty_resize(&t->client,c->width,c->height)))
            copy_text(app->notice,sizeof(app->notice),"Attachment resize unavailable");
    }
    tv_term_draw(t->screen,c,t->scroll,focused && !t->scrolling);
    t->foreign_size = !t->scroll && foreign_size(c);
    if (app->no_color) strip_rendition(c);
}
