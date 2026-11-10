#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Modal text input and decisions. Each waits inside the handler that asked
 * while the loop keeps terminal clients and bounded observations moving, and
 * is drawn over the bottom rows of the frame, in the same canvas as the view.
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
struct form_pen { struct kt_canvas *c; int width, bottom, x, y; struct kt_style tone; };
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
static int form_wrap(const struct app *app, struct kt_canvas *c, int width, int y, int bottom, const char *text, struct kt_style tone) {
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
/* The bottom rows of the frame being drawn, for a panel of that height. */
static bool form_area(struct app *app, struct kt_canvas *c, int height) {
    if (!app->frame.cells || height < 1 || height > app->frame.height) return false;
    if (!kt_canvas_view(c, &app->frame, (struct kt_rect){0, app->frame.height - height, app->frame.width, height})) return false;
    kt_clear(c, KT_BASE);
    return true;
}

static void form_draw(struct app *app, const char *prompt, const char *text, size_t cursor) {
    struct kt_canvas c;
    int width=app->frame.width, column, height, prompt_rows, limit=app->rows/2>8 ? app->rows/2 : 8;
    size_t start=0;
    if (width<5 || app->rows<4) return;
    prompt_rows=form_wrap(app,NULL,width,0,app->rows,prompt,KT_STRONG);
    height=prompt_rows+3;
    if (height>limit) height=limit;
    if (height>app->rows) height=app->rows;
    if (!form_area(app,&c,height)) return;
    while (start<cursor && form_columns(text+start,cursor-start,app->ascii)>=width-2) start=form_next(text,start,cursor);
    column=form_columns(text+start,cursor-start,app->ascii);
    kt_text(&c,(struct kt_rect){0,0,width,1},"INPUT TO HYDRA / text field",KT_SELECTED);
    (void)form_wrap(app,&c,width,1,height-2,prompt,KT_STRONG);
    kt_text(&c,(struct kt_rect){0,height-2,width,1},text+start,KT_BASE);
    if (cursor<strlen(text)) kt_restyle(&c,(struct kt_rect){column,height-2,1,1},KT_SELECTED);
    else kt_put(&c,column,height-2,'_',KT_SELECTED);
    kt_text(&c,(struct kt_rect){0,height-1,width,1},"Enter submit / Esc cancel",KT_BORDER);
}

/* Inserts one character's bytes at the cursor when all of them fit; control
 * bytes are never stored. */
static void form_insert(char *buffer, size_t size, size_t *length, size_t *cursor,
                        const char *bytes, size_t count) {
    size_t i;
    if (count >= size - *length) return;
    for (i=0; i<count && *length+1<size; i++) {
        unsigned char value=(unsigned char)bytes[i];
        if (value<32 || value==127) continue;
        memmove(buffer + *cursor + 1, buffer + *cursor, *length - *cursor + 1);
        buffer[(*cursor)++]=(char)value;
        (*length)++;
    }
}

/* Pasted text goes in character by character, up to what the field holds. */
static void form_paste(char *buffer, size_t size, size_t *length, size_t *cursor,
                       const char *bytes, size_t count) {
    size_t at=0;
    while (at<count) {
        uint32_t cp;
        size_t step=kt_utf8_decode(bytes+at,count-at,&cp);
        if (!step) break;
        form_insert(buffer,size,length,cursor,bytes+at,step);
        at+=step;
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

/* Applies typed text or an editing key to the field. */
static void form_edit(char *buffer, size_t size, size_t *length, size_t *cursor, const struct kt_event *e) {
    if (kt_event_text(e)) { form_insert(buffer,size,length,cursor,e->bytes,e->length); return; }
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

struct form_field { const char *prompt; char *buffer; size_t size, length, cursor; };

static void form_overlay(struct app *app, void *context) {
    const struct form_field *field=context;
    form_draw(app,field->prompt,field->buffer,field->cursor);
}

/* 0 submits, -1 cancels, 1 keeps editing. */
static int form_input(struct form_field *field, const struct kt_msg *msg) {
    const struct kt_event *e=&msg->u.event;
    if (msg->type==KT_MSG_PASTE) {
        form_paste(field->buffer,field->size,&field->length,&field->cursor,msg->u.paste.bytes,msg->u.paste.length);
        return 1;
    }
    if (msg->type!=KT_MSG_KEY) return 1;
    if (form_cancel_key(e)) return -1;
    if (form_submit_key(e)) return 0;
    form_edit(field->buffer,field->size,&field->length,&field->cursor,e);
    return 1;
}

int prompt_text(struct app *app, const char *prompt, char *buffer, size_t size) {
    struct form_field field={prompt,buffer,size,0,0};
    struct modal modal={NULL,form_overlay,&field};
    int result=1;
    if (!size) return -1;
    buffer[0]='\0';
    if (!modal_push(app,&modal)) return -1;
    while (result>0) {
        struct kt_msg msg;
        enum modal_event event=modal_wait(app,&msg);
        if (event==MODAL_STOPPED) result=-1;
        else if (event==MODAL_INPUT) result=form_input(&field,&msg);
    }
    modal_pop(app);
    return result;
}

/* Wraps one line of a decision panel; returns the next free row. */
static int confirm_line(struct app *app, struct kt_canvas *c, int y, int bottom, const char *text, struct kt_style tone) {
    return form_wrap(app,c,c->width,y,bottom,text,tone);
}

struct confirm_panel { const char *title, *footer; const char *const *lines; size_t count; };

static void confirm_draw(struct app *app, void *context) {
    const struct confirm_panel *panel=context;
    struct kt_canvas c;
    int width=app->frame.width, height=app->rows<12 ? app->rows : 12, y=1;
    size_t i;
    if (width<5 || height<4 || !form_area(app,&c,height)) return;
    kt_text(&c,(struct kt_rect){0,0,width,1},panel->title,KT_SELECTED);
    for (i=0;i<panel->count;i++) y=confirm_line(app,&c,y,height-1,panel->lines[i],i ? KT_BASE : KT_STRONG);
    kt_text(&c,(struct kt_rect){0,height-1,width,1},panel->footer,KT_BORDER);
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

bool confirm_choice(struct app *app, const char *title, const char *const lines[], size_t count, const char *choices) {
    char footer[128];
    struct confirm_panel panel={title,footer,lines,count};
    struct modal modal={NULL,confirm_draw,&panel};
    int decision=-1;
    snprintf(footer,sizeof(footer),"%s confirm   n / Esc cancel",choices);
    if (!modal_push(app,&modal)) return false;
    while (decision<0) {
        struct kt_msg msg;
        enum modal_event event=modal_wait(app,&msg);
        if (event==MODAL_STOPPED) decision=0;
        else if (event==MODAL_INPUT && msg.type==KT_MSG_KEY) decision=confirm_key(&msg.u.event,choices);
    }
    modal_pop(app);
    return decision==1;
}
