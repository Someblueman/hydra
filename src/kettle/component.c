#include "component_internal.h"
#include <string.h>

/* What the components share: paragraphs, key dispatch, row scrolling and
 * the marks a selected or matching row carries. */

/* ---- paragraphs --------------------------------------------------------- */

static bool blank(char c) { return c == ' ' || c == '\t'; }

/* The end of the next row of a line `length` bytes long: the last blank
 * that keeps it within `width`, or a cut between cells for a long word. At
 * least one scalar is taken, so every call makes progress. */
static size_t row_end(const char *text, size_t length, int width, size_t *next) {
    size_t fit = kt_text_fit(text, length, width, NULL), end = fit;
    if (fit >= length) { *next = length; return length; }
    while (end > 0 && !blank(text[end])) end--;
    if (end == 0) {
        size_t one = kt_text_fit(text, length, 2, NULL);
        end = fit ? fit : (one ? one : 1);
        *next = end;
        return end;
    }
    *next = end;
    while (*next < length && blank(text[*next])) (*next)++;
    while (end > 0 && blank(text[end - 1])) end--;
    return end;
}

/* Calls back for each row of a paragraph; stops early when told to. */
typedef bool (*row_fn)(void *context, const char *text, size_t length);

static void each_row(const char *text, int width, row_fn row, void *context) {
    const char *line = text ? text : "";
    for (;;) {
        size_t length = strcspn(line, "\n"), at = 0;
        do {
            size_t next, end = row_end(line + at, length - at, width < 1 ? 1 : width, &next);
            if (!row(context, line + at, end)) return;
            at += next;
        } while (at < length);
        if (!line[length]) return;
        line += length + 1;
    }
}

static bool count_row(void *context, const char *text, size_t length) {
    (void)text; (void)length;
    ++*(int *)context;
    return true;
}

int kt_paragraph_height(const char *text, int width) {
    int rows = 0;
    each_row(text, width, count_row, &rows);
    return rows;
}

struct paint_rows { struct kt_canvas *canvas; struct kt_rect area; struct kt_style style; int row; };

static bool paint_row(void *context, const char *text, size_t length) {
    struct paint_rows *p = context;
    char piece[1024];
    if (p->row >= p->area.height) return false;
    if (length >= sizeof(piece)) length = sizeof(piece) - 1;
    memcpy(piece, text, length);
    piece[length] = '\0';
    (void)kt_text_aligned(p->canvas, (struct kt_rect){p->area.x, p->area.y + p->row, p->area.width, 1}, piece,
                          KT_ALIGN_LEFT, p->style);
    p->row++;
    return true;
}

int kt_paragraph(struct kt_canvas *canvas, struct kt_rect area, const char *text, struct kt_style style) {
    struct paint_rows p;
    if (area.width < 1 || area.height < 1) return 0;
    p.canvas = canvas; p.area = area; p.style = style; p.row = 0;
    each_row(text, area.width, paint_row, &p);
    return p.row;
}

/* ---- keys --------------------------------------------------------------- */

int kt_ui_action(const struct kt_binding *keys, size_t count, struct kt_keymap_state *chord, uint32_t scope,
                 const struct kt_msg *msg, bool *consumed) {
    struct kt_keymap map;
    const struct kt_binding *matched = NULL;
    enum kt_key_match match;
    *consumed = false;
    if (msg->type != KT_MSG_KEY) return -1;
    /* The table is only read; kt_keymap names it without const. */
    map.bindings = (struct kt_binding *)(uintptr_t)keys;
    map.count = count;
    match = kt_keymap_feed(&map, chord, scope, &msg->u.event, &matched);
    *consumed = match != KT_KEY_UNBOUND;
    return match == KT_KEY_ACTION && matched ? (int)matched->action : -1;
}

/* ---- rows --------------------------------------------------------------- */

int kt_ui_row_height(const struct kt_ui_rows *rows, size_t row) {
    int height = rows->height ? rows->height(rows->context, row) : 1;
    return height < 1 ? 1 : height;
}

size_t kt_ui_rows_shown(const struct kt_ui_rows *rows, size_t top, int height) {
    size_t row = top;
    int used = 0;
    while (row < rows->count && used < height) used += kt_ui_row_height(rows, row++);
    return row - top;
}

size_t kt_ui_bottom_top(const struct kt_ui_rows *rows, int height) {
    size_t top = rows->count;
    int used = 0;
    while (top > 0 && used + kt_ui_row_height(rows, top - 1) <= height) used += kt_ui_row_height(rows, --top);
    if (top == rows->count && top > 0) top--;
    return top;
}

size_t kt_ui_scroll_to(const struct kt_ui_rows *rows, size_t top, size_t selected, int height) {
    size_t first = selected;
    int used;
    if (selected >= rows->count) return top > kt_ui_bottom_top(rows, height) ? kt_ui_bottom_top(rows, height) : top;
    if (selected < top) return selected;
    /* Walk back from the selection while the rows still fit below top. */
    used = kt_ui_row_height(rows, selected);
    while (first > top && used + kt_ui_row_height(rows, first - 1) <= height) used += kt_ui_row_height(rows, --first);
    return first > top ? first : top;
}

size_t kt_ui_scroll_by(const struct kt_ui_rows *rows, size_t top, long delta, int height) {
    size_t last = kt_ui_bottom_top(rows, height);
    if (delta < 0) return (size_t)-delta > top ? 0 : top - (size_t)-delta;
    if (top >= last) return top > last ? last : top;
    return (size_t)delta > last - top ? last : top + (size_t)delta;
}

size_t kt_ui_row_at(const struct kt_ui_rows *rows, size_t top, int line, int height) {
    size_t row = top;
    int used = 0;
    if (line < 0 || line >= height) return KT_NONE;
    while (row < rows->count) {
        used += kt_ui_row_height(rows, row);
        if (line < used) return row;
        row++;
    }
    return KT_NONE;
}

/* ---- marks -------------------------------------------------------------- */

struct kt_style kt_ui_lit(struct kt_style ink, struct kt_style fill) {
    ink.fg = fill.fg;
    return ink;
}

void kt_ui_selection(struct kt_canvas *canvas, struct kt_rect row, bool focused, struct kt_style fill,
                     struct kt_style bar, struct kt_style bar_unfocused) {
    if (focused) kt_fill(canvas, row, fill);
    kt_box(canvas, row, &kt_border_bar, focused ? kt_ui_lit(bar, fill) : bar_unfocused);
}

/* Splits text into runs at the matched characters, into caller storage. */
struct marked { char text[768]; struct kt_span spans[2 * KT_FUZZY_POSITIONS + 2]; size_t count, used; };

static void marked_run(struct marked *m, const char *text, size_t length, struct kt_style style) {
    if (!length || m->count >= sizeof(m->spans) / sizeof(m->spans[0])) return;
    if (m->used + length + 1 > sizeof(m->text)) length = sizeof(m->text) - m->used - 1;
    if (!length) return;
    memcpy(m->text + m->used, text, length);
    m->text[m->used + length] = '\0';
    m->spans[m->count].text = m->text + m->used;
    m->spans[m->count++].style = style;
    m->used += length + 1;
}

static size_t scalar_length(const char *text) {
    uint32_t cp;
    size_t used = kt_utf8_decode(text, strlen(text), &cp);
    return used ? used : 1;
}

int kt_ui_marked(struct kt_canvas *canvas, struct kt_rect area, const char *text, const char *pattern,
                 struct kt_style style, struct kt_style mark) {
    struct marked m;
    size_t positions[KT_FUZZY_POSITIONS], at = 0, i;
    int score;
    if (!text) return 0;
    for (i = 0; i < KT_FUZZY_POSITIONS; i++) positions[i] = KT_NONE;
    score = pattern && pattern[0] ? kt_fuzzy(pattern, text, positions, KT_FUZZY_POSITIONS) : -1;
    if (score < 0) return kt_text_aligned(canvas, area, text, KT_ALIGN_LEFT, style);
    m.count = m.used = 0;
    for (i = 0; i < KT_FUZZY_POSITIONS && positions[i] != KT_NONE && positions[i] >= at; i++) {
        size_t length = scalar_length(text + positions[i]);
        marked_run(&m, text + at, positions[i] - at, style);
        marked_run(&m, text + positions[i], length, mark);
        at = positions[i] + length;
    }
    marked_run(&m, text + at, strlen(text + at), style);
    return kt_spans(canvas, area, m.spans, m.count);
}

void kt_ui_scrollbar(struct kt_canvas *canvas, struct kt_rect column, size_t total, size_t first, size_t shown,
                     struct kt_style track, struct kt_style thumb) {
    size_t height = column.height > 0 ? (size_t)column.height : 0, size, start, y;
    if (!height || column.width < 1 || total <= shown) return;
    size = height * shown / total;
    if (size < 1) size = 1;
    start = total - shown ? (height - size) * first / (total - shown) : 0;
    if (start + size > height) start = height - size;
    for (y = 0; y < height; y++) {
        bool on = y >= start && y < start + size;
        kt_put(canvas, column.x, column.y + (int)y, on ? 0x2503U : 0x2502U, on ? thumb : track);
    }
}

/* ---- mouse -------------------------------------------------------------- */

bool kt_ui_inside(struct kt_rect area, int x, int y) {
    return x >= area.x && y >= area.y && x - area.x < area.width && y - area.y < area.height;
}

int kt_ui_wheel(const struct kt_msg *msg) {
    unsigned button;
    if (msg->type != KT_MSG_MOUSE || msg->u.event.release) return 0;
    button = msg->u.event.button & ~28U;   /* without Shift, Alt and Ctrl */
    if (button == 64) return -1;
    return button == 65 ? 1 : 0;
}

bool kt_ui_click(const struct kt_msg *msg, struct kt_rect area, int *x, int *y) {
    const struct kt_event *e = &msg->u.event;
    if (msg->type != KT_MSG_MOUSE || e->release || (e->button & ~28U) != 0) return false;
    if (!kt_ui_inside(area, e->x, e->y)) return false;
    *x = e->x - area.x;
    *y = e->y - area.y;
    return true;
}

/* ---- dialogs ------------------------------------------------------------ */

struct kt_help_style kt_ui_hint_style(void) {
    struct kt_help_style s;
    s.key = KT_INK(KT_COLOR_TEXT, KT_BOLD);
    s.label = KT_INK(KT_COLOR_MUTED, 0);
    s.separator = KT_INK(KT_COLOR_FAINT, 0);
    s.heading = KT_INK(KT_COLOR_ACCENT, KT_BOLD);
    return s;
}

/* Key hints set into the bottom edge, right-aligned before the corner. */
static void dialog_hints(struct kt_canvas *canvas, struct kt_rect area, const struct kt_help_source *hints,
                         size_t count, const struct kt_help_style *style, struct kt_style border) {
    struct kt_cell cells[256];
    struct kt_canvas line;
    int width = area.width - 6, used, x, y = area.y + area.height - 1;
    if (!count || width < 4 || width > 256) return;
    /* Measure on a scratch row, then draw at the measured width. */
    if (!kt_init(&line, cells, sizeof(cells) / sizeof(cells[0]), width, 1, canvas->unicode)) return;
    used = kt_help_line(&line, (struct kt_rect){0, 0, width, 1}, hints, count, NULL, NULL, style);
    if (used < 1) return;
    x = area.x + area.width - 3 - used;
    kt_put(canvas, x - 1, y, ' ', border);
    kt_fill(canvas, (struct kt_rect){x, y, used, 1}, KT_STYLE(KT_KEEP, KT_KEEP, 0));
    (void)kt_help_line(canvas, (struct kt_rect){x, y, used, 1}, hints, count, NULL, NULL, style);
    kt_put(canvas, x + used, y, ' ', border);
}

struct kt_rect kt_ui_dialog(struct kt_canvas *canvas, struct kt_rect area, const char *title, struct kt_style border,
                            struct kt_style title_style, const struct kt_help_source *hints, size_t hint_count,
                            const struct kt_help_style *hint_style) {
    struct kt_help_style stock = kt_ui_hint_style();
    kt_fill(canvas, area, KT_BASE);
    kt_box(canvas, area, &kt_border_rounded, border);
    if (title) kt_box_title(canvas, area, title, KT_ALIGN_LEFT, title_style);
    dialog_hints(canvas, area, hints, hint_count, hint_style ? hint_style : &stock, border);
    return kt_rect_inset(area, 2, 3, 2, 3);
}
