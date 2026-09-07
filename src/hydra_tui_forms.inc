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
        size_t n=tv_utf8_decode(text+i,length-i,&cp);
        int width;
        if (!n) { columns++; break; }
        width=tv_codepoint_width(cp);
        columns+=ascii && cp>127 ? 1 : width<0 ? 1 : width;
        i+=n;
    }
    return columns;
}
static void form_draw(struct app *app, const char *prompt, const char *text, size_t cursor) {
    struct tv_cell cells[512U*8U];
    struct tv_canvas c;
    int width=app->cols>512 ? 511 : app->cols-1, row, column, height, prompt_rows, x=0,y=1;
    size_t start=0, offset=0, prompt_length=strlen(prompt);
    if (width<5 || app->rows<4) return;
    prompt_rows=(form_columns(prompt,prompt_length,app->ascii)+width-1)/width;
    if (prompt_rows<1) prompt_rows=1;
    height=prompt_rows+3;
    if (height>8) height=8;
    if (height>app->rows) height=app->rows;
    (void)tv_init(&c,cells,512U*8U,width,height,!app->ascii);
    tv_clear(&c,TV_BASE);
    while (start<cursor && form_columns(text+start,cursor-start,app->ascii)>=width-2) start=form_next(text,start,cursor);
    column=form_columns(text+start,cursor-start,app->ascii);
    tv_text(&c,(struct tv_rect){0,0,width,1},"INPUT TO HYDRA / text field",TV_SELECTED);
    while (offset<prompt_length && y<height-2) {
        uint32_t cp;
        size_t used=tv_utf8_decode(prompt+offset,prompt_length-offset,&cp);
        int columns;
        if (!used) break;
        offset+=used; columns=tv_codepoint_width(cp);
        if (columns<0 || (app->ascii && cp>126)) { cp='?'; columns=1; }
        if (x+columns>width) { x=0; y++; }
        if (y<height-2) tv_put(&c,x,y,cp,TV_STRONG);
        x+=columns;
    }
    tv_text(&c,(struct tv_rect){0,height-2,width,1},text+start,TV_BASE);
    if (cursor<strlen(text)) c.cells[(size_t)c.stride*(size_t)(height-2)+(size_t)column].style=TV_SELECTED;
    else tv_put(&c,column,height-2,'_',TV_SELECTED);
    tv_text(&c,(struct tv_rect){0,height-1,width,1},"Enter submit / Esc cancel",TV_BORDER);
    for (row=0;row<height;row++) {
        printf("\033[%d;1H",app->rows-height+1+row);
        (void)tv_write_row(&c,row,stdout,dashboard_style,app);
    }
    fflush(stdout);
}
static int prompt_text(struct app *app, const char *prompt, char *buffer, size_t size) {
    struct tv_input input;
    size_t length=0, cursor=0;
    time_t last_refresh=time(NULL);
    int result=-1;
    if (!size) return -1;
    buffer[0]='\0';
    tv_input_init(&input);
    while (!stop_requested && app->running) {
        struct tv_event e;
        char byte;
        bool event;
        time_t now=time(NULL);
        native_observations_tick(app,now-last_refresh>=2);
        if (now-last_refresh>=2) last_refresh=now;
        native_terminals_pump(app);
        update_size(app);
        render(app,0,false);
        form_draw(app,prompt,buffer,cursor);
        if (read_key(40,&byte)>0) event=tv_input_feed(&input,(unsigned char)byte,&e);
        else event=tv_input_flush(&input,&e);
        if (!event) continue;
        if (e.type==TV_KEY && (e.key==27 || e.key==3)) break;
        if (e.type==TV_KEY && (e.key=='\r' || e.key=='\n')) { result=0; break; }
        if (e.type==TV_KEY && (e.key==127 || e.key==8)) {
            size_t previous=form_previous(buffer,cursor);
            memmove(buffer+previous,buffer+cursor,length-cursor+1);
            length-=cursor-previous; cursor=previous;
        } else if (e.type==TV_KEY && e.key==TV_KEY_LEFT) cursor=form_previous(buffer,cursor);
        else if (e.type==TV_KEY && e.key==TV_KEY_RIGHT) cursor=form_next(buffer,cursor,length);
        else if (e.type==TV_KEY && e.key==TV_KEY_HOME) cursor=0;
        else if (e.type==TV_KEY && e.key==TV_KEY_END) cursor=length;
        else if (e.type==TV_PASTE || (e.type==TV_KEY && e.key<0x110000)) {
            size_t i;
            if (e.length>=size-length) continue;
            for (i=0;i<e.length && length+1<size;i++) {
                unsigned char value=(unsigned char)e.bytes[i];
                if (value<32 || value==127) continue;
                memmove(buffer+cursor+1,buffer+cursor,length-cursor+1);
                buffer[cursor++]=(char)value; length++;
            }
        }
    }
    /* The form temporarily paints over the workspace presenter. */
    native_workspace_invalidate(app);
    return result;
}
