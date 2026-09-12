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

const char *native_terminal_attention(struct app *app, const struct native_terminal *t) {
    size_t i;
    if (app->snapshot_stale) return "STATE STALE";
    for (i=0;i<app->model.head_count;i++) {
        const struct head *h=&app->model.heads[i];
        if (strcmp(h->head_id,t->head) || strcmp(h->instance,t->instance) ||
            (app->fleet && (strcmp(h->remote_host,t->remote_host) || strcmp(h->remote_project,t->remote_project)))) continue;
        if (!strcmp(h->confidence,"exact")) {
            if (!strcmp(h->observed,"exited")) return "EXIT RECORDED";
            if (!strcmp(h->observed,"failed")) return "FAIL RECORDED";
        }
        /* Output activity, a live tmux session and a reported outcome cannot
         * establish whether an interactive agent is waiting for a decision. */
        return "AGENT UNKNOWN";
    }
    return "OLD INSTANCE";
}

void native_terminal_close(struct native_terminal *t) {
    if (!t->screen) return;
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
        if (t->screen && !strcmp(t->head, h->head_id) && !strcmp(t->instance, h->instance) &&
            (!app->fleet || (!strcmp(t->remote_host, h->remote_host) && !strcmp(t->remote_project, h->remote_project)))) {
            app->terminals->selected = i;
            if (!t->client.finished && !t->client.eof) return i;
            native_terminal_close(t); return i;
        }
    }
    return available;
}

bool native_terminal_attach(struct app *app) {
    const struct head *h=selected_head(app);
    struct native_terminal *t;
    size_t available;
    char *argv[12];
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
    t=&app->terminals->slots[available];
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
    if (app->fleet) {
        argv[0]=(char *)app->hydra; argv[1]=(char *)"fleet"; argv[2]=(char *)"attach"; argv[3]=(char *)h->remote_host;
        argv[4]=(char *)"--project"; argv[5]=(char *)h->remote_project; argv[6]=(char *)"--instance"; argv[7]=t->instance;
        argv[8]=(char *)"--"; argv[9]=(char *)h->remote_branch; argv[10]=NULL;
    } else {
        argv[0]=(char *)app->hydra; argv[1]="tui"; argv[2]="--attach"; argv[3]=t->head; argv[4]=t->instance; argv[5]=NULL;
    }
    if (!tv_pty_spawn(&t->client,app->hydra,argv,&app->saved,80,24)) goto fail;
    app->terminals->selected=available;
    copy_text(app->notice,sizeof(app->notice),"Attached client / Ctrl-B Tab returns input to Hydra");
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

void native_terminals_pump(struct app *app) {
    size_t i;
    if (!app->terminals) return;
    for (i=0;i<NATIVE_TERMINALS;i++) {
        struct native_terminal *t=&app->terminals->slots[i];
        size_t chunks;
        uint64_t before;
        if (!t->screen) continue;
        before=t->screen->history_serial;
        if (!tv_pty_flush(&t->client) || !tv_pty_reap(&t->client)) {
            copy_text(app->notice,sizeof(app->notice),"Attachment transport failed; recorded agent outcome is unchanged");
            tv_pty_close(&t->client);
        }
        for (chunks=0; !t->client.eof && t->client.fd>=0 && chunks<8; chunks++) {
            char bytes[4096];
            ssize_t n=tv_pty_read(&t->client,bytes,sizeof(bytes));
            if (n>0) tv_term_feed(t->screen,bytes,(size_t)n);
            else {
                if (!n) tv_term_finish(t->screen);
                else if (errno!=EAGAIN && errno!=EINTR) tv_pty_close(&t->client);
                break;
            }
        }
        if (t->scroll>t->screen->history_count) t->scroll=t->screen->history_count;
        if (t->scroll && t->screen->history_serial>before) {
            uint64_t added=t->screen->history_serial-before;
            t->scroll=added>t->screen->history_count-t->scroll ? t->screen->history_count : t->scroll+(size_t)added;
        }
        if (t->screen->reply_length && !t->client.finished && !t->client.eof) {
            char reply[256]; size_t n=tv_term_take_reply(t->screen,reply,sizeof(reply));
            native_terminal_send(app,t,reply,n);
        }
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
    if (app->no_color) {
        int x,y;
        for (y=0;y<c->height;y++) for (x=0;x<c->width;x++) {
            struct tv_cell *cell=&c->cells[(size_t)y*(size_t)c->stride+(size_t)x];
            cell->attributes=0; cell->style=TV_BASE;
        }
    }
}
