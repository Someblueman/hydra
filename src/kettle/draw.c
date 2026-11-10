#include "kettle.h"
#include <string.h>

/* Measured text, borders and rules: the pieces a view is laid out with. */

/* ---- measuring ---------------------------------------------------------- */

/* Walks text the way kt_put_scalar lays it out, without a canvas. */
struct walk {
    struct kt_cell previous;   /* the cell a joining scalar would attach to */
    bool unicode, started;
    int width;                 /* columns so far                            */
};

/* The cell the walk has just laid out, as the canvas would hold it. */
static void walk_cell(struct walk *w, uint32_t cp, int width) {
    memset(&w->previous, 0, sizeof(w->previous));
    w->previous.glyph = cp; w->previous.width = (unsigned char)width;
    /* An ASCII canvas holds the approximation, and a blank after a wide one. */
    if (!w->unicode && cp > 126) {
        w->previous.glyph = width == 2 ? ' ' : kt_ascii_fallback(cp);
        w->previous.width = 1;
    }
    w->started = true;
    w->width += width;
}

/* Attached scalars are stored on the base cell; VS16 and a second regional
 * indicator also widen it. An ASCII canvas drops them. */
static void walk_join(struct walk *w, uint32_t cp, bool widen) {
    struct kt_cell *base = &w->previous;
    if (!w->unicode) return;
    if (base->combining_count < KT_COMBINING_MAX) base->combining[base->combining_count++] = cp;
    if (widen && base->width == 1) { base->width = 2; w->width++; }
}

/* Takes one scalar; true when it started a new cell rather than joining. */
static bool walk_scalar(struct walk *w, uint32_t cp) {
    enum kt_join join;
    int width;
    if (cp == 0xadU) cp = '-';
    join = kt_cell_join(w->started ? &w->previous : NULL, cp);
    if (join == KT_JOIN_DROP) return false;
    if (join != KT_JOIN_NONE) { walk_join(w, cp, join == KT_JOIN_WIDEN); return false; }
    width = kt_codepoint_width(cp);
    walk_cell(w, width < 0 ? '?' : cp, width < 0 ? 1 : width);
    return true;
}

static size_t next_scalar(const char *text, size_t remaining, uint32_t *cp) {
    size_t used = kt_utf8_decode(text, remaining, cp);
    if (!used) { used = 1; *cp = '?'; }
    return used;
}

/* What of a text fits in `limit` columns without cutting a cell or a
 * cluster: `bytes` of it, `width` columns wide. `total` is the whole width. */
struct fit { size_t bytes; int width, total; };

static struct fit measure(const char *text, bool unicode, int limit) {
    struct walk w;
    struct fit fit = {0, 0, 0};
    size_t length = text ? strlen(text) : 0, at = 0;
    memset(&w, 0, sizeof(w));
    w.unicode = unicode;
    while (at < length) {
        uint32_t cp;
        size_t used = next_scalar(text + at, length - at, &cp);
        int before = w.width;
        /* The text up to a new cell is whole: it fits if it is short enough. */
        if (walk_scalar(&w, cp) && before <= limit) { fit.bytes = at; fit.width = before; }
        at += used;
    }
    if (w.width <= limit) { fit.bytes = length; fit.width = w.width; }
    fit.total = w.width;
    return fit;
}

int kt_text_width(const char *text) {
    return measure(text, true, 0).total;
}

/* ---- aligned text ------------------------------------------------------- */

/* The first row of an area, cut down to what is on the canvas. */
static struct kt_rect row_on_canvas(const struct kt_canvas *c, struct kt_rect r) {
    int64_t end = (int64_t)r.x + r.width;
    struct kt_rect row = {0, r.y, 0, 1};
    if (r.width <= 0 || r.height <= 0 || r.y < 0 || r.y >= c->height || end <= 0 || r.x >= c->width) return row;
    row.x = r.x < 0 ? 0 : r.x;
    row.width = (int)(end < c->width ? end : c->width) - row.x;
    return row;
}

/* Draws the first `bytes` of text from column x; returns the next column. */
static int draw_bytes(struct kt_canvas *c, struct kt_rect row, int x, const char *text, size_t bytes,
                      struct kt_style style) {
    size_t at = 0;
    while (at < bytes) {
        uint32_t cp;
        int advance;
        at += next_scalar(text + at, bytes - at, &cp);
        advance = kt_put_scalar(c, row, x, cp, style);
        if (advance < 0) break;
        x += advance;
    }
    return x;
}

static int ellipsis_width(const struct kt_canvas *c) { return c->unicode ? 1 : 3; }

/* As much of the ellipsis as `room` columns hold; returns the next column. */
static int draw_ellipsis(struct kt_canvas *c, struct kt_rect row, int x, int room, struct kt_style style) {
    int i;
    if (room < 1) return x;
    if (c->unicode) { kt_put(c, x, row.y, 0x2026U, style); return x + 1; }
    for (i = 0; i < room && i < 3; i++) kt_put(c, x + i, row.y, '.', style);
    return x + i;
}

static int align_offset(enum kt_align align, int spare) {
    if (spare <= 0 || align == KT_ALIGN_LEFT) return 0;
    return align == KT_ALIGN_CENTER ? spare / 2 : spare;
}

/* How wide a text is drawn in `room` columns: all of it, or cut with an
 * ellipsis. `fit` is the text that precedes the ellipsis. */
static int fitted_width(const struct kt_canvas *c, const char *text, int room, struct fit *fit) {
    int dots = ellipsis_width(c);
    *fit = measure(text, c->unicode, room);
    if (fit->total <= room) return fit->total;
    if (room <= dots) { fit->bytes = 0; fit->width = 0; return room < 0 ? 0 : room; }
    *fit = measure(text, c->unicode, room - dots);
    return fit->width + dots;
}

int kt_text_aligned(struct kt_canvas *c, struct kt_rect r, const char *text, enum kt_align align,
                    struct kt_style style) {
    struct kt_rect row = row_on_canvas(c, r);
    struct fit fit;
    int used, x;
    if (!text || row.width < 1) return 0;
    used = fitted_width(c, text, row.width, &fit);
    x = draw_bytes(c, row, row.x + align_offset(align, row.width - used), text, fit.bytes, style);
    if (fit.total > row.width) (void)draw_ellipsis(c, row, x, used - fit.width, style);
    return used;
}

static int spans_width(const struct kt_canvas *c, const struct kt_span *spans, size_t count) {
    int width = 0;
    size_t i;
    for (i = 0; i < count; i++) width += measure(spans[i].text, c->unicode, 0).total;
    return width;
}

int kt_spans(struct kt_canvas *c, struct kt_rect r, const struct kt_span *spans, size_t count) {
    struct kt_rect row = row_on_canvas(c, r);
    int x = row.x, end = row.x + row.width;
    bool cut;
    size_t i;
    if (!spans || !count || row.width < 1) return 0;
    cut = spans_width(c, spans, count) > row.width;
    /* When the line is cut, the runs share what the ellipsis leaves. */
    if (cut) end -= ellipsis_width(c) < row.width ? ellipsis_width(c) : row.width;
    for (i = 0; i < count; i++) {
        const char *text = spans[i].text ? spans[i].text : "";
        struct fit fit = measure(text, c->unicode, end - x);
        x = draw_bytes(c, row, x, text, fit.bytes, spans[i].style);
        if (fit.total <= fit.width) continue;
        x = draw_ellipsis(c, row, x, row.x + row.width - x, spans[i].style);
        break;
    }
    return x - row.x;
}

/* ---- borders ------------------------------------------------------------ */

const struct kt_border kt_border_none = {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}};
const struct kt_border kt_border_light = {
    {0x2500, 0x2500, 0x2502, 0x2502, 0x250c, 0x2510, 0x2514, 0x2518}, {'-', '-', '|', '|', '+', '+', '+', '+'}};
const struct kt_border kt_border_rounded = {
    {0x2500, 0x2500, 0x2502, 0x2502, 0x256d, 0x256e, 0x2570, 0x256f}, {'-', '-', '|', '|', '+', '+', '+', '+'}};
const struct kt_border kt_border_heavy = {
    {0x2501, 0x2501, 0x2503, 0x2503, 0x250f, 0x2513, 0x2517, 0x251b}, {'#', '#', '#', '#', '#', '#', '#', '#'}};
const struct kt_border kt_border_double = {
    {0x2550, 0x2550, 0x2551, 0x2551, 0x2554, 0x2557, 0x255a, 0x255d}, {'=', '=', '|', '|', '+', '+', '+', '+'}};
const struct kt_border kt_border_bar = {{0, 0, 0x258c, 0, 0, 0, 0, 0}, {0, 0, '|', 0, 0, 0, 0, 0}};

static const struct kt_border_glyphs *border_glyphs(const struct kt_canvas *c, const struct kt_border *border) {
    return c->unicode ? &border->unicode : &border->ascii;
}

/* A corner needs both of its sides; where one is missing, the side that is
 * there runs through, so a border of one or two sides has no stray corners. */
static uint32_t corner_glyph(const struct kt_border_glyphs *g, bool top, bool left) {
    uint32_t corner = top ? (left ? g->top_left : g->top_right) : (left ? g->bottom_left : g->bottom_right);
    uint32_t vertical = left ? g->left : g->right, horizontal = top ? g->top : g->bottom;
    if (corner && vertical && horizontal) return corner;
    return vertical ? vertical : horizontal;
}

static void box_row(struct kt_canvas *c, struct kt_rect r, bool top, const struct kt_border_glyphs *g,
                    struct kt_style style) {
    int64_t right = (int64_t)r.x + r.width - 1, y = top ? r.y : (int64_t)r.y + r.height - 1;
    int x, first = r.x < 0 ? 0 : r.x, last = right < c->width ? (int)right : c->width - 1;
    if (y < 0 || y >= c->height) return;
    for (x = first; x <= last; x++) {
        uint32_t glyph = x == r.x || x == right ? corner_glyph(g, top, x == r.x) : top ? g->top : g->bottom;
        if (glyph) kt_put(c, x, (int)y, glyph, style);
    }
}

static void box_sides(struct kt_canvas *c, struct kt_rect r, const struct kt_border_glyphs *g, struct kt_style style) {
    int64_t right = (int64_t)r.x + r.width - 1, bottom = (int64_t)r.y + r.height - 1;
    int y, first = r.y < -1 ? -1 : r.y, last = bottom < c->height ? (int)bottom : c->height;
    for (y = first + 1; y < last; y++) {
        if (g->left && r.x >= 0) kt_put(c, r.x, y, g->left, style);
        if (g->right && right > r.x && right < c->width) kt_put(c, (int)right, y, g->right, style);
    }
}

void kt_box(struct kt_canvas *c, struct kt_rect r, const struct kt_border *border, struct kt_style style) {
    const struct kt_border_glyphs *g;
    if (!border || r.width < 1 || r.height < 1) return;
    g = border_glyphs(c, border);
    box_sides(c, r, g, style);
    box_row(c, r, true, g, style);
    if (r.height > 1) box_row(c, r, false, g, style);
}

struct kt_rect kt_box_inner(struct kt_rect r, const struct kt_border *border) {
    const struct kt_border_glyphs *g = &border->unicode;
    int left = g->left ? 1 : 0, top = g->top ? 1 : 0;
    r.x += left; r.width -= left + (g->right ? 1 : 0);
    r.y += top; r.height -= top + (g->bottom ? 1 : 0);
    if (r.width < 0) r.width = 0;
    if (r.height < 0) r.height = 0;
    return r;
}

void kt_box_title(struct kt_canvas *c, struct kt_rect r, const char *title, enum kt_align align,
                  struct kt_style style) {
    struct kt_rect row;
    struct fit fit;
    int used, x;
    /* Room for a corner and one cell of edge at each end, a space on each
     * side of the title, and at least one column of it. */
    if (!title || !title[0] || r.width < 7 || r.height < 1) return;
    row = row_on_canvas(c, (struct kt_rect){r.x + 2, r.y, r.width - 4, 1});
    if (row.width < 3) return;
    used = fitted_width(c, title, row.width - 2, &fit);
    x = row.x + align_offset(align, row.width - 2 - used);
    kt_put(c, x, row.y, ' ', style);
    (void)kt_text_aligned(c, (struct kt_rect){x + 1, row.y, used, 1}, title, KT_ALIGN_LEFT, style);
    kt_put(c, x + 1 + used, row.y, ' ', style);
}

/* ---- rules -------------------------------------------------------------- */

static void rule_line(struct kt_canvas *c, int y, int from, int to, struct kt_style style) {
    int x;
    for (x = from; x < to; x++) kt_put(c, x, y, 0x2500U, style);
}

void kt_rule_styled(struct kt_canvas *c, struct kt_rect r, const char *title, enum kt_align align,
                    struct kt_style title_style, struct kt_style line_style) {
    struct kt_rect row = row_on_canvas(c, r);
    struct fit fit;
    int end = row.x + row.width, used, x;
    if (row.width < 1) return;
    if (!title || !title[0]) { rule_line(c, row.y, row.x, end, line_style); return; }
    used = fitted_width(c, title, row.width, &fit);
    x = row.x + align_offset(align, row.width - used);
    /* One space separates the title from the line on either side of it. */
    rule_line(c, row.y, row.x, x - 1, line_style);
    (void)kt_text_aligned(c, (struct kt_rect){x, row.y, used, 1}, title, KT_ALIGN_LEFT, title_style);
    rule_line(c, row.y, x + used + 1, end, line_style);
}

void kt_rule(struct kt_canvas *c, struct kt_rect r, const char *title, struct kt_style style) {
    kt_rule_styled(c, r, title, KT_ALIGN_LEFT, style, style);
}
