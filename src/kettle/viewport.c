#include "component_internal.h"
#include <string.h>

/* Read-only text that scrolls. A place in it is a line and, when lines
 * wrap, a row within that line. Lines are read from the callback whenever
 * they are needed and laid out on the fly; nothing is kept between calls. */

#define SCROLL KT_VIEWPORT_SCOPE_SCROLL
#define MOVE "Scroll"
#define SIDEWAYS_STEP 8
#define MAX_ROWS 4096

static const struct kt_binding viewport_keys[KT_VIEWPORT_ACTIONS] = {
    KT_UI_ROW(KT_VIEWPORT_UP, "viewport.up", "scroll", SCROLL, MOVE, 2, 2, KT_UI_CHORD(KT_KEY_UP, 0),
              KT_UI_CHORD('k', 0)),
    KT_UI_ROW(KT_VIEWPORT_DOWN, "viewport.down", "scroll", SCROLL, MOVE, 2, 2, KT_UI_CHORD(KT_KEY_DOWN, 0),
              KT_UI_CHORD('j', 0)),
    KT_UI_ROW(KT_VIEWPORT_PAGE_UP, "viewport.page_up", "page", SCROLL, MOVE, 1, 2, KT_UI_CHORD(KT_KEY_PAGE_UP, 0),
              KT_UI_CHORD('b', 0)),
    KT_UI_ROW(KT_VIEWPORT_PAGE_DOWN, "viewport.page_down", "page", SCROLL, MOVE, 1, 2,
              KT_UI_CHORD(KT_KEY_PAGE_DOWN, 0), KT_UI_CHORD(' ', 0)),
    KT_UI_ROW(KT_VIEWPORT_HALF_UP, "viewport.half_up", "half page", SCROLL, MOVE, -1, 2, KT_UI_CHORD('u', KT_MOD_CTRL),
              KT_UI_CHORD('u', 0)),
    KT_UI_ROW(KT_VIEWPORT_HALF_DOWN, "viewport.half_down", "half page", SCROLL, MOVE, -1, 2,
              KT_UI_CHORD('d', KT_MOD_CTRL), KT_UI_CHORD('d', 0)),
    KT_UI_ROW(KT_VIEWPORT_TOP, "viewport.top", "top", SCROLL, MOVE, 0, 2, KT_UI_CHORD(KT_KEY_HOME, 0),
              KT_UI_CHORD('g', 0)),
    KT_UI_ROW(KT_VIEWPORT_BOTTOM, "viewport.bottom", "end", SCROLL, MOVE, 0, 2, KT_UI_CHORD(KT_KEY_END, 0),
              KT_UI_CHORD('G', 0)),
    KT_UI_ROW(KT_VIEWPORT_LEFT, "viewport.left", "sideways", KT_VIEWPORT_SCOPE_SIDEWAYS, MOVE, -1, 2,
              KT_UI_CHORD(KT_KEY_LEFT, 0), KT_UI_CHORD('h', 0)),
    KT_UI_ROW(KT_VIEWPORT_RIGHT, "viewport.right", "sideways", KT_VIEWPORT_SCOPE_SIDEWAYS, MOVE, -1, 2,
              KT_UI_CHORD(KT_KEY_RIGHT, 0), KT_UI_CHORD('l', 0)),
};

/* ---- lines -------------------------------------------------------------- */

struct line { struct kt_span spans[KT_VIEWPORT_SPANS]; size_t count; };
struct pos { size_t span, byte; };

static void load(const struct kt_viewport *v, size_t index, struct line *l) {
    size_t i;
    l->count = 0;
    if (v->source || !v->line || index >= v->count) return;
    l->count = v->line(v->context, index, l->spans, KT_VIEWPORT_SPANS);
    if (l->count > KT_VIEWPORT_SPANS) l->count = KT_VIEWPORT_SPANS;
    for (i = 0; i < l->count; i++)
        if (!l->spans[i].text) l->spans[i].text = "";
}

static bool before(struct pos a, struct pos b) { return a.span < b.span || (a.span == b.span && a.byte < b.byte); }

/* Moves past the end of finished spans. */
static struct pos settle(const struct line *l, struct pos p) {
    while (p.span < l->count && !l->spans[p.span].text[p.byte]) { p.span++; p.byte = 0; }
    return p;
}

static char at(const struct line *l, struct pos p) { return p.span < l->count ? l->spans[p.span].text[p.byte] : '\0'; }

static struct pos skip_blanks(const struct line *l, struct pos p) {
    for (p = settle(l, p); at(l, p) == ' '; p = settle(l, p)) p.byte++;
    return p;
}

static struct pos one_scalar(const struct line *l, struct pos p) {
    const char *text;
    uint32_t cp;
    size_t used;
    p = settle(l, p);
    if (p.span >= l->count) return p;
    text = l->spans[p.span].text + p.byte;
    used = kt_utf8_decode(text, strlen(text), &cp);
    p.byte += used ? used : 1;
    return settle(l, p);
}

/* The last blank among the first `bytes` of a span from p. */
static void note_blank(const char *text, size_t bytes, struct pos p, struct pos *blank) {
    size_t i;
    for (i = bytes; i > 0; i--)
        if (text[i - 1] == ' ') { blank->span = p.span; blank->byte = p.byte + i - 1; return; }
}

/* Takes as much from p as fits in `room` columns; true when it stopped
 * inside the line. */
static bool fill(const struct line *l, struct pos *p, int room, struct pos *blank) {
    int used = 0;
    while (p->span < l->count) {
        const char *text = l->spans[p->span].text + p->byte;
        size_t length = strlen(text), fit;
        int width;
        fit = kt_text_fit(text, length, room - used, &width);
        if (blank) note_blank(text, fit, *p, blank);
        used += width;
        if (fit < length) { p->byte += fit; return true; }
        p->span++;
        p->byte = 0;
    }
    return false;
}

/* Where the row that starts at `start` ends, and where the next begins:
 * at the last blank that fits, else between cells, never empty. */
static struct pos row_end(const struct line *l, struct pos start, int width, struct pos *next) {
    struct pos p = start, blank = {KT_NONE, 0};
    if (!fill(l, &p, width, &blank)) { *next = p; return p; }
    if (at(l, p) == ' ') { *next = skip_blanks(l, p); return p; }
    if (blank.span != KT_NONE && before(start, blank)) { *next = skip_blanks(l, blank); return blank; }
    if (!before(start, p)) p = one_scalar(l, p);
    *next = settle(l, p);
    return p;
}

static int text_width(const struct kt_viewport *v, struct kt_rect area) {
    return area.width - (v->scrollbar ? 1 : 0);
}

static int line_rows(const struct kt_viewport *v, size_t index, int width) {
    struct line l;
    struct pos p = {0, 0}, next;
    int rows = 0;
    if (!v->wrap || v->source || width < 1) return 1;
    load(v, index, &l);
    p = settle(&l, p);
    while (p.span < l.count && rows < MAX_ROWS) {
        (void)row_end(&l, p, width, &next);
        p = next;
        rows++;
    }
    return rows ? rows : 1;
}

int kt_viewport_line_rows(const struct kt_viewport *v, size_t index, int width) { return line_rows(v, index, width); }

/* ---- places ------------------------------------------------------------- */

struct spot { size_t line; int row; };

static size_t line_count(const struct kt_viewport *v) {
    return v->source ? (size_t)(v->source->height > 0 ? v->source->height : 0) : v->count;
}

static int rows_here(const struct kt_viewport *v, size_t line) { return line_rows(v, line, text_width(v, v->area)); }

static int compare(struct spot a, struct spot b) {
    if (a.line != b.line) return a.line < b.line ? -1 : 1;
    return a.row < b.row ? -1 : a.row > b.row;
}

/* The place that shows the last rows, filling `height`. */
static struct spot end_spot(const struct kt_viewport *v, struct kt_rect area) {
    struct spot s = {0, 0};
    size_t line = line_count(v);
    int used = 0, width = text_width(v, area);
    while (line > 0 && used < area.height) {
        int rows = line_rows(v, line - 1, width);
        if (used + rows > area.height) {
            s.line = line - 1;
            s.row = rows - (area.height - used);
            return s;
        }
        used += rows;
        line--;
    }
    s.line = line;
    return s;
}

static struct spot current(const struct kt_viewport *v) {
    struct spot s;
    s.line = v->top;
    s.row = v->top_row;
    return s;
}

static void go(struct kt_viewport *v, struct spot s) {
    struct spot end = end_spot(v, v->area);
    if (compare(s, end) > 0) s = end;
    v->top = s.line;
    v->top_row = s.row;
    v->at_end = compare(s, end) >= 0;
}

static struct spot down(const struct kt_viewport *v, struct spot s, long rows) {
    size_t count = line_count(v);
    for (; rows > 0; rows--) {
        if (s.row + 1 < rows_here(v, s.line)) s.row++;
        else if (s.line + 1 < count) { s.line++; s.row = 0; }
        else break;
    }
    return s;
}

static struct spot up(const struct kt_viewport *v, struct spot s, long rows) {
    for (; rows > 0; rows--) {
        if (s.row > 0) s.row--;
        else if (s.line > 0) { s.line--; s.row = rows_here(v, s.line) - 1; }
        else break;
    }
    return s;
}

void kt_viewport_scroll(struct kt_viewport *v, long rows) {
    struct spot s = current(v), end = end_spot(v, v->area);
    if (rows > 0 && compare(s, end) >= 0) { go(v, end); return; }
    go(v, rows < 0 ? up(v, s, -rows) : down(v, s, rows));
}

void kt_viewport_top(struct kt_viewport *v) {
    struct spot s = {0, 0};
    go(v, s);
}

void kt_viewport_bottom(struct kt_viewport *v) { go(v, end_spot(v, v->area)); }

bool kt_viewport_at_bottom(const struct kt_viewport *v) { return compare(current(v), end_spot(v, v->area)) >= 0; }

int kt_viewport_percent(const struct kt_viewport *v) {
    struct spot end = end_spot(v, v->area);
    if (!end.line && !end.row) return 100;
    if (compare(current(v), end) >= 0) return 100;
    return (int)(v->top * 100 / (end.line + 1));
}

/* ---- sideways ----------------------------------------------------------- */

static int line_width(const struct kt_viewport *v, size_t index) {
    struct line l;
    int width = 0;
    size_t i;
    if (v->source) return v->source->width;
    load(v, index, &l);
    for (i = 0; i < l.count; i++) width += kt_text_width(l.spans[i].text);
    return width;
}

static void sideways(struct kt_viewport *v, int step) {
    int widest = 0, room = text_width(v, v->area), y;
    for (y = 0; y < v->area.height && v->top + (size_t)y < line_count(v); y++) {
        int width = line_width(v, v->top + (size_t)y);
        if (width > widest) widest = width;
    }
    v->left += step;
    if (v->left > widest - room) v->left = widest - room;
    if (v->left < 0) v->left = 0;
}

/* ---- input -------------------------------------------------------------- */

static long page(const struct kt_viewport *v) { return v->area.height > 1 ? v->area.height - 1 : 1; }
static long half(const struct kt_viewport *v) { return v->area.height > 1 ? v->area.height / 2 : 1; }

static void perform(struct kt_viewport *v, int action) {
    switch (action) {
    case KT_VIEWPORT_UP: kt_viewport_scroll(v, -1); break;
    case KT_VIEWPORT_DOWN: kt_viewport_scroll(v, 1); break;
    case KT_VIEWPORT_PAGE_UP: kt_viewport_scroll(v, -page(v)); break;
    case KT_VIEWPORT_PAGE_DOWN: kt_viewport_scroll(v, page(v)); break;
    case KT_VIEWPORT_HALF_UP: kt_viewport_scroll(v, -half(v)); break;
    case KT_VIEWPORT_HALF_DOWN: kt_viewport_scroll(v, half(v)); break;
    case KT_VIEWPORT_TOP: kt_viewport_top(v); break;
    case KT_VIEWPORT_BOTTOM: kt_viewport_bottom(v); break;
    case KT_VIEWPORT_LEFT: sideways(v, -SIDEWAYS_STEP); break;
    case KT_VIEWPORT_RIGHT: sideways(v, SIDEWAYS_STEP); break;
    default: break;
    }
}

enum kt_viewport_result kt_viewport_update(struct kt_viewport *v, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool consumed;
    int action, wheel = kt_ui_wheel(msg);
    (void)cmds;
    if (wheel && kt_ui_inside(v->area, msg->u.event.x, msg->u.event.y)) {
        kt_viewport_scroll(v, wheel * 3L);
        return KT_VIEWPORT_HANDLED;
    }
    action = kt_ui_action(v->keys, KT_VIEWPORT_ACTIONS, &v->chord, kt_viewport_scope(v), msg, &consumed);
    if (action >= 0) perform(v, action);
    return action >= 0 || consumed ? KT_VIEWPORT_HANDLED : KT_VIEWPORT_IGNORED;
}

uint32_t kt_viewport_scope(const struct kt_viewport *v) {
    return SCROLL | (v->wrap && !v->source ? 0 : KT_VIEWPORT_SCOPE_SIDEWAYS);
}

struct kt_keymap kt_viewport_keymap(struct kt_viewport *v) {
    struct kt_keymap map;
    map.bindings = v->keys;
    map.count = KT_VIEWPORT_ACTIONS;
    return map;
}

/* ---- set up ------------------------------------------------------------- */

void kt_viewport_init(struct kt_viewport *v, kt_viewport_line_fn line, void *context, size_t count) {
    memset(v, 0, sizeof(*v));
    v->line = line;
    v->context = context;
    v->count = count;
    v->scrollbar = true;
    v->style.scrollbar = KT_INK(KT_COLOR_FAINT, 0);
    v->style.thumb = KT_INK(KT_COLOR_MUTED, 0);
    memcpy(v->keys, viewport_keys, sizeof(viewport_keys));
}

void kt_viewport_set_canvas(struct kt_viewport *v, const struct kt_canvas *source) {
    v->source = source;
    v->top = 0;
    v->top_row = v->left = 0;
    v->at_end = false;
}

void kt_viewport_set_count(struct kt_viewport *v, size_t count) {
    bool stay = v->follow && (v->at_end || kt_viewport_at_bottom(v));
    v->count = count;
    if (v->top >= line_count(v)) { v->top = line_count(v) ? line_count(v) - 1 : 0; v->top_row = 0; }
    if (stay) kt_viewport_bottom(v);
    else go(v, current(v));
}

void kt_viewport_place(struct kt_viewport *v, struct kt_rect area) {
    bool stay = v->follow && v->at_end;
    v->area = area;
    if (v->top_row >= rows_here(v, v->top)) v->top_row = rows_here(v, v->top) - 1;
    if (stay) kt_viewport_bottom(v);
    else go(v, current(v));
}

/* ---- view --------------------------------------------------------------- */

/* Copies the runs between two places into terminated pieces. */
struct pieces { char text[2048]; struct kt_span spans[KT_VIEWPORT_SPANS]; size_t count, used; };

static void piece(struct pieces *out, const struct kt_span *span, size_t from, size_t to) {
    size_t n = to > from ? to - from : 0;
    if (!n || out->count == KT_VIEWPORT_SPANS) return;
    if (out->used + n + 1 > sizeof(out->text)) n = sizeof(out->text) - out->used - 1;
    memcpy(out->text + out->used, span->text + from, n);
    out->text[out->used + n] = '\0';
    out->spans[out->count].text = out->text + out->used;
    out->spans[out->count++].style = span->style;
    out->used += n + 1;
}

static void draw_between(struct kt_canvas *canvas, struct kt_rect row, const struct line *l, struct pos from,
                         struct pos to) {
    struct pieces out;
    size_t i;
    out.count = out.used = 0;
    for (i = from.span; i < l->count && (i < to.span || (i == to.span && to.byte > 0)); i++) {
        size_t start = i == from.span ? from.byte : 0;
        size_t end = i == to.span ? to.byte : strlen(l->spans[i].text);
        piece(&out, &l->spans[i], start, end);
    }
    if (out.count) (void)kt_spans(canvas, row, out.spans, out.count);
}

/* One line from `first_row`, wrapped, into rows from `y`; returns rows drawn. */
static int draw_wrapped(const struct kt_viewport *v, struct kt_canvas *canvas, struct kt_rect area, size_t index,
                        int first_row, int y) {
    struct line l;
    struct pos p = {0, 0}, next, end;
    int row = 0, drawn = 0, width = text_width(v, area);
    load(v, index, &l);
    p = settle(&l, p);
    if (p.span >= l.count) return 1;
    while (p.span < l.count && y + drawn < area.y + area.height) {
        end = row_end(&l, p, width, &next);
        if (row >= first_row) {
            draw_between(canvas, (struct kt_rect){area.x, y + drawn, width, 1}, &l, p, end);
            drawn++;
        }
        row++;
        p = next;
    }
    return drawn;
}

static void draw_clipped(const struct kt_viewport *v, struct kt_canvas *canvas, struct kt_rect row, size_t index) {
    struct line l;
    struct pos p = {0, 0}, end = {0, 0};
    load(v, index, &l);
    if (v->left > 0) (void)fill(&l, &p, v->left, NULL);
    p = settle(&l, p);
    end.span = l.count;
    draw_between(canvas, row, &l, p, end);
}

static void copy_row(const struct kt_viewport *v, struct kt_canvas *canvas, struct kt_rect row, int source_row) {
    const struct kt_canvas *s = v->source;
    int x;
    for (x = 0; x < row.width && v->left + x < s->width; x++) {
        const struct kt_cell *from = &s->cells[source_row * s->stride + v->left + x];
        if (row.x + x < 0 || row.x + x >= canvas->width || row.y < 0 || row.y >= canvas->height) continue;
        /* A wide cell cut by either edge shows as a blank. */
        if ((!from->width && x == 0) || (from->width == 2 && x + 1 >= row.width)) {
            kt_put(canvas, row.x + x, row.y, ' ', kt_cell_style(from));
            continue;
        }
        canvas->cells[row.y * canvas->stride + row.x + x] = *from;
    }
}

void kt_viewport_view(const struct kt_viewport *v, struct kt_canvas *canvas, struct kt_rect area) {
    struct spot s = v->follow && v->at_end ? end_spot(v, area) : current(v);
    int y = area.y, width = text_width(v, area);
    size_t line = s.line, count = line_count(v);
    if (area.width < 1 || area.height < 1) return;
    for (; line < count && y < area.y + area.height; line++) {
        struct kt_rect row = {area.x, y, width, 1};
        if (v->source) { copy_row(v, canvas, row, (int)line); y++; }
        else if (v->wrap) y += draw_wrapped(v, canvas, area, line, line == s.line ? s.row : 0, y);
        else { draw_clipped(v, canvas, row, line); y++; }
    }
    if (v->scrollbar)
        kt_ui_scrollbar(canvas, (struct kt_rect){area.x + area.width - 1, area.y, 1, area.height}, count, s.line,
                        line - s.line, v->style.scrollbar, v->style.thumb);
}
