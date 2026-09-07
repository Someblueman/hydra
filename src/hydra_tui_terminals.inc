/* Hydra owns the attachment set. Each PTY owns only an attach client; tmux
 * continues owning the agent session when a client is closed or the UI exits. */
#define NATIVE_TERMINALS 4
#define NATIVE_TERMINAL_CELLS (512U * 256U)
struct native_terminal {
    struct tv_pty client;
    struct tv_terminal_model *screen;
    struct tv_cell *cells, *history;
    char head[TEXT], instance[TEXT], label[TEXT];
    size_t scroll;
    bool scrolling;
};
struct native_terminals {
    struct native_terminal slots[NATIVE_TERMINALS];
    size_t selected;
    struct tv_input input;
    bool prefix;
};

static struct native_terminal *native_terminal_selected(struct app *app) {
    if (!app->terminals) return NULL;
    return &app->terminals->slots[app->terminals->selected];
}

static void native_terminal_close(struct native_terminal *t) {
    if (!t->screen) return;
    tv_pty_close(&t->client);
    free(t->screen); free(t->cells); free(t->history);
    memset(t,0,sizeof(*t)); t->client.fd=-1; t->client.pid=-1;
}

static void native_terminals_destroy(struct app *app) {
    size_t i;
    if (!app->terminals) return;
    for (i=0;i<NATIVE_TERMINALS;i++) native_terminal_close(&app->terminals->slots[i]);
    free(app->terminals); app->terminals=NULL;
}

static bool native_terminal_attach(struct app *app) {
    const struct head *h=selected_head(app);
    struct native_terminal *t;
    size_t i, available=NATIVE_TERMINALS;
    char *argv[6];
    if (!h || app->fleet || !h->head_id[0] || !h->instance[0]) {
        copy_text(app->notice,sizeof(app->notice),"Select a recorded local head to open an interactive pane"); return false;
    }
    if (!app->terminals) {
        app->terminals=calloc(1,sizeof(*app->terminals));
        if (!app->terminals) return false;
        tv_input_init(&app->terminals->input);
    }
    for (i=0;i<NATIVE_TERMINALS;i++) {
        t=&app->terminals->slots[i];
        if (!t->screen && available==NATIVE_TERMINALS) available=i;
        if (t->screen && !strcmp(t->head,h->head_id) && !strcmp(t->instance,h->instance)) {
            app->terminals->selected=i;
            if (!t->client.finished && !t->client.eof) return true;
            native_terminal_close(t); available=i; break;
        }
    }
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
    argv[0]=(char *)app->hydra; argv[1]="tui"; argv[2]="--attach"; argv[3]=t->head; argv[4]=t->instance; argv[5]=NULL;
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

static void native_terminal_send(struct app *app, struct native_terminal *t, const void *bytes, size_t length) {
    if (!t || !t->screen || t->client.eof || !tv_pty_enqueue(&t->client,bytes,length))
        copy_text(app->notice,sizeof(app->notice),"Input not delivered: client disconnected or input queue full");
}

static void native_terminals_pump(struct app *app) {
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

static void native_terminal_draw(struct app *app, struct native_terminal *t, struct tv_canvas *c, bool focused) {
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
