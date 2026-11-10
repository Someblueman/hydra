#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Modal text input keeps terminal clients and bounded observations moving.
 * Paste is data only; Enter is accepted only as an explicit key event. */
static size_t form_previous(const char *text, size_t cursor) {
    if (!cursor) return 0;
    do { cursor--; } while (cursor && ((unsigned char)text[cursor]&0xc0)==0x80);
    return cursor;
}
static size_t form_next(const char *text, size_t cursor, size_t length) {
    if (cursor<length) cursor++;
    while (cursor<length && ((unsigned char)text[cursor]&0xc0)==0x80) cursor++;
    return cursor;
}
static int form_columns(const char *text, size_t length, bool ascii) {
    size_t i=0;
    int columns=0;
    while (i<length) {
        uint32_t cp;
        size_t n=kt_utf8_decode(text+i,length-i,&cp);
        int width;
        if (!n) { columns++; break; }
        width=kt_codepoint_width(cp);
        columns+=ascii && cp>127 ? 1 : width<0 ? 1 : width;
        i+=n;
    }
    return columns;
}
/* 1 moves a word that does not fit to the next row, 2 drops a space that
 * would start a row, 0 places the character. A word wider than a whole row
 * still breaks where the row ends. */
static int form_break(const struct app *app, const char *text, size_t offset, size_t length, int x, int width) {
    size_t n=offset;
    if (text[offset]==' ') return (x==0 && offset) || x>=width ? 2 : 0;
    if (!x || text[offset-1]!=' ') return 0;
    while (n<length && text[n]!=' ') n++;
    return x+form_columns(text+offset,n-offset,app->ascii)>width ? 1 : 0;
}
/* Where the next character goes; with no canvas the pen only counts rows. */
struct form_pen { struct kt_canvas *c; int width, bottom, x, y; enum kt_style tone; };
/* Places one character, moving to the next row when it does not fit.
 * Returns the bytes it used, or 0 for an undecodable sequence. */
static size_t form_place(const struct app *app, struct form_pen *pen, const char *text, size_t length) {
    uint32_t cp;
    size_t used=kt_utf8_decode(text,length,&cp);
    int columns;
    if (!used) return 0;
    columns=kt_codepoint_width(cp);
    if (columns<0 || (app->ascii && cp>126)) { cp='?'; columns=1; }
    if (pen->x+columns>pen->width) { pen->x=0; pen->y++; }
    if (pen->c && pen->y<pen->bottom) kt_put(pen->c,pen->x,pen->y,cp,pen->tone);
    pen->x+=columns;
    return used;
}
/* Word-wraps text into rows [y, bottom) of c; with no canvas it only counts.
 * Returns the row after the last one used. */
static int form_wrap(const struct app *app, struct kt_canvas *c, int width, int y, int bottom, const char *text, enum kt_style tone) {
    struct form_pen pen={c,width,bottom,0,y,tone};
    size_t offset=0, length=strlen(text);
    while (offset<length && pen.y<bottom) {
        int action=form_break(app,text,offset,length,pen.x,width);
        size_t used;
        if (action==1) { pen.x=0; pen.y++; continue; }
        if (action==2) { offset++; continue; }
        used=form_place(app,&pen,text+offset,length-offset);
        if (!used) break;
        offset+=used;
    }
    return pen.y+1;
}
static void form_draw(struct app *app, const char *prompt, const char *text, size_t cursor) {
    struct kt_cell cells[512U*8U];
    struct kt_canvas c;
    int width=app->cols>512 ? 511 : app->cols-1, row, column, height, prompt_rows, limit=app->rows/2>8 ? app->rows/2 : 8;
    size_t start=0;
    if (width<5 || app->rows<4) return;
    prompt_rows=form_wrap(app,NULL,width,0,app->rows,prompt,KT_STRONG);
    height=prompt_rows+3;
    if (height>limit) height=limit;
    if (height>app->rows) height=app->rows;
    (void)kt_init(&c,cells,512U*8U,width,height,!app->ascii);
    kt_clear(&c,KT_BASE);
    while (start<cursor && form_columns(text+start,cursor-start,app->ascii)>=width-2) start=form_next(text,start,cursor);
    column=form_columns(text+start,cursor-start,app->ascii);
    kt_text(&c,(struct kt_rect){0,0,width,1},"INPUT TO HYDRA / text field",KT_SELECTED);
    (void)form_wrap(app,&c,width,1,height-2,prompt,KT_STRONG);
    kt_text(&c,(struct kt_rect){0,height-2,width,1},text+start,KT_BASE);
    if (cursor<strlen(text)) c.cells[(size_t)c.stride*(size_t)(height-2)+(size_t)column].style=KT_SELECTED;
    else kt_put(&c,column,height-2,'_',KT_SELECTED);
    kt_text(&c,(struct kt_rect){0,height-1,width,1},"Enter submit / Esc cancel",KT_BORDER);
    for (row=0;row<height;row++) {
        printf("\033[%d;1H",app->rows-height+1+row);
        (void)kt_write_row(&c,row,stdout,dashboard_style,app);
    }
    fflush(stdout);
}

static void form_insert(char *buffer, size_t size, size_t *length, size_t *cursor,
                        const struct kt_event *event) {
    size_t i;
    if (event->length >= size - *length) return;
    for (i=0; i<event->length && *length+1<size; i++) {
        unsigned char value=(unsigned char)event->bytes[i];
        if (value<32 || value==127) continue;
        memmove(buffer + *cursor + 1, buffer + *cursor, *length - *cursor + 1);
        buffer[(*cursor)++]=(char)value;
        (*length)++;
    }
}

/* Esc and Ctrl-C cancel; Enter (or Ctrl-J) submits; Backspace (or Ctrl-H) erases. */
static bool form_cancel_key(const struct kt_event *e) {
    return kt_event_is(e,KT_KEY_ESCAPE,0) || kt_event_is(e,'c',KT_MOD_CTRL);
}
static bool form_submit_key(const struct kt_event *e) {
    return kt_event_is(e,KT_KEY_ENTER,0) || kt_event_is(e,'j',KT_MOD_CTRL);
}
static bool form_erase_key(const struct kt_event *e) {
    return kt_event_is(e,KT_KEY_BACKSPACE,0) || kt_event_is(e,'h',KT_MOD_CTRL);
}

/* Applies typed or pasted text, or an editing key, to the field. */
static void form_edit(char *buffer, size_t size, size_t *length, size_t *cursor, const struct kt_event *e) {
    if (e->type==KT_PASTE || kt_event_text(e)) { form_insert(buffer,size,length,cursor,e); return; }
    if (e->type!=KT_KEY) return;
    if (form_erase_key(e)) {
        size_t previous=form_previous(buffer,*cursor);
        memmove(buffer+previous,buffer+*cursor,*length-*cursor+1);
        *length-=*cursor-previous; *cursor=previous;
    } else if (e->key==KT_KEY_LEFT) *cursor=form_previous(buffer,*cursor);
    else if (e->key==KT_KEY_RIGHT) *cursor=form_next(buffer,*cursor,*length);
    else if (e->key==KT_KEY_HOME) *cursor=0;
    else if (e->key==KT_KEY_END) *cursor=*length;
}

int prompt_text(struct app *app, const char *prompt, char *buffer, size_t size) {
    struct kt_input input;
    size_t length=0, cursor=0;
    unsigned queued=0;
    bool redraw=true;
    time_t last_refresh=time(NULL);
    int result=-1;
    if (!size) return -1;
    buffer[0]='\0';
    kt_input_init(&input);
    while (!terminal_stopped() && app->running) {
        struct kt_event e;
        char byte;
        bool event;
        if (redraw) {
            time_t now=time(NULL);
            native_observations_tick(app,now-last_refresh>=2);
            if (now-last_refresh>=2) last_refresh=now;
            native_terminals_pump(app);
            update_size(app);
            render(app,0,false);
            form_draw(app,prompt,buffer,cursor);
        }
        /* Drain a bounded burst before repainting, without flushing incomplete
         * escape sequences until the ordinary input timeout has elapsed. A
         * partial sequence beyond a lone Escape waits longer so a fragmented key
         * survives a busy host while Escape alone still dismisses promptly. */
        if (read_key(input.length>1 ? 150 : redraw ? 40 : 0,&byte)>0) {
            redraw=++queued>=64;
            if (redraw) queued=0;
            event=kt_input_feed(&input,(unsigned char)byte,&e);
        } else if (!redraw) {
            redraw=true; queued=0; continue;
        } else event=kt_input_flush(&input,&e);
        if (!event) continue;
        if (form_cancel_key(&e)) break;
        if (form_submit_key(&e)) { result=0; break; }
        form_edit(buffer,size,&length,&cursor,&e);
    }
    /* The form temporarily paints over the presented frame. */
    frame_invalidate(app);
    return result;
}

/* Wraps one line of a decision panel; returns the next free row. */
static int confirm_line(struct app *app, struct kt_canvas *c, int y, int bottom, const char *text, enum kt_style tone) {
    return form_wrap(app,c,c->width,y,bottom,text,tone);
}

static void confirm_draw(struct app *app, const char *title, const char *const lines[], size_t count, const char *footer) {
    struct kt_cell cells[512U*12U];
    struct kt_canvas c;
    int width=app->cols>512 ? 511 : app->cols-1, height=app->rows<12 ? app->rows : 12, y=1, row;
    size_t i;
    if (width<5 || height<4) return;
    (void)kt_init(&c,cells,512U*12U,width,height,!app->ascii);
    kt_clear(&c,KT_BASE);
    kt_text(&c,(struct kt_rect){0,0,width,1},title,KT_SELECTED);
    for (i=0;i<count;i++) y=confirm_line(app,&c,y,height-1,lines[i],i ? KT_BASE : KT_STRONG);
    kt_text(&c,(struct kt_rect){0,height-1,width,1},footer,KT_BORDER);
    for (row=0;row<height;row++) {
        printf("\033[%d;1H",app->rows-height+1+row);
        (void)kt_write_row(&c,row,stdout,dashboard_style,app);
    }
    fflush(stdout);
}

/* A modal decision with explicit keys. A key in `choices` accepts; n, Esc and
 * Ctrl-C decline; Enter, paste and every other key are ignored, so an
 * accidental keystroke cannot approve. Observations and clients keep moving. */
/* 1 accepts, 0 declines, -1 keeps waiting. */
static int confirm_key(const struct kt_event *e, const char *choices) {
    if (e->type!=KT_KEY) return -1;
    if (form_cancel_key(e) || kt_event_is(e,'n',0) || kt_event_is(e,'N',0)) return 0;
    return !e->mods && e->key>0 && e->key<128 && strchr(choices,(int)e->key) ? 1 : -1;
}

static void confirm_refresh(struct app *app, time_t *last_refresh) {
    time_t now=time(NULL);
    native_observations_tick(app,now-*last_refresh>=2);
    if (now-*last_refresh>=2) *last_refresh=now;
    native_terminals_pump(app);
    update_size(app);
    render(app,0,false);
}

bool confirm_choice(struct app *app, const char *title, const char *const lines[], size_t count, const char *choices) {
    struct kt_input input;
    char footer[128];
    bool redraw=true;
    int decision=-1;
    time_t last_refresh=time(NULL);
    snprintf(footer,sizeof(footer),"%s confirm   n / Esc cancel",choices);
    kt_input_init(&input);
    while (decision<0 && !terminal_stopped() && app->running) {
        struct kt_event e;
        char byte;
        if (redraw) { confirm_refresh(app,&last_refresh); confirm_draw(app,title,lines,count,footer); }
        redraw=read_key(input.length>1 ? 150 : 40,&byte)<=0;
        if (redraw ? kt_input_flush(&input,&e) : kt_input_feed(&input,(unsigned char)byte,&e)) decision=confirm_key(&e,choices);
    }
    frame_invalidate(app);
    return decision==1;
}
