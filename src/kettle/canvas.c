#include "kettle.h"
#include <limits.h>
#include <string.h>

bool kt_init(struct kt_canvas *out, struct kt_cell *cells, size_t capacity,
             int width, int height, bool unicode) {
    if (!out || !cells || width < 1 || height < 1 || width > 4096 || height > 4096 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > capacity) return false;
    out->cells = cells; out->stride = width; out->width = width; out->height = height; out->unicode = unicode;
    kt_clear(out, KT_BASE);
    return true;
}

static struct kt_cell blank(struct kt_style style) {
    struct kt_cell cell;
    memset(&cell, 0, sizeof(cell));
    cell.glyph = ' '; cell.width = 1;
    cell.foreground = style.fg; cell.background = style.bg; cell.attributes = style.attrs;
    return cell;
}

struct kt_style kt_cell_style(const struct kt_cell *cell) {
    struct kt_style style;
    style.fg = cell->foreground; style.bg = cell->background; style.attrs = cell->attributes;
    return style;
}

/* The style a cell ends up with when `style` is drawn on it: KT_KEEP leaves
 * the colour the cell has. */
static struct kt_style over(const struct kt_cell *cell, struct kt_style style) {
    if (style.fg == KT_KEEP) style.fg = cell->foreground;
    if (style.bg == KT_KEEP) style.bg = cell->background;
    return style;
}

bool kt_canvas_view(struct kt_canvas *out, const struct kt_canvas *parent, struct kt_rect r) {
    if (!out || !parent || r.x < 0 || r.y < 0 || r.width < 1 || r.height < 1 ||
        (int64_t)r.x + r.width > parent->width || (int64_t)r.y + r.height > parent->height) return false;
    *out = *parent;
    out->cells = parent->cells + (size_t)r.y * (size_t)parent->stride + (size_t)r.x;
    out->width = r.width; out->height = r.height;
    return true;
}

bool kt_cell_equal(const struct kt_cell *a, const struct kt_cell *b) {
    return a->glyph == b->glyph && a->width == b->width &&
        a->foreground == b->foreground && a->background == b->background &&
        a->attributes == b->attributes && a->combining_count == b->combining_count &&
        !memcmp(a->combining, b->combining, sizeof(a->combining));
}

/* There is no cell to keep a colour from: KT_KEEP clears to the default. */
void kt_clear(struct kt_canvas *c, struct kt_style style) {
    int x, y;
    struct kt_cell cell;
    if (style.fg == KT_KEEP) style.fg = KT_DEFAULT;
    if (style.bg == KT_KEEP) style.bg = KT_DEFAULT;
    cell = blank(style);
    for (y = 0; y < c->height; y++) for (x = 0; x < c->width; x++)
        c->cells[(size_t)y * (size_t)c->stride + (size_t)x] = cell;
}

static void erase_partner(struct kt_canvas *c, int x, int y) {
    struct kt_cell *cell = &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
    if (!cell->width && x > 0) cell[-1] = blank(kt_cell_style(cell));
    if (cell->width == 2 && x + 1 < c->width) cell[1] = blank(kt_cell_style(cell));
}

/* The part of `r` that is on the canvas; empty when none is. */
static struct kt_rect visible(const struct kt_canvas *c, struct kt_rect r) {
    int64_t right = (int64_t)r.x + r.width, bottom = (int64_t)r.y + r.height;
    struct kt_rect out = {0, 0, 0, 0};
    if (r.width <= 0 || r.height <= 0 || right <= 0 || bottom <= 0 || r.x >= c->width || r.y >= c->height) return out;
    out.x = r.x < 0 ? 0 : r.x; out.y = r.y < 0 ? 0 : r.y;
    out.width = (int)(right < c->width ? right : c->width) - out.x;
    out.height = (int)(bottom < c->height ? bottom : c->height) - out.y;
    return out;
}

void kt_fill(struct kt_canvas *c, struct kt_rect r, struct kt_style style) {
    int x, y;
    r = visible(c, r);
    for (y = r.y; y < r.y + r.height; y++) for (x = r.x; x < r.x + r.width; x++) {
        struct kt_cell *cell = &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
        /* A wide cell the edge cuts through loses its other half as well. */
        if (x == r.x || x == r.x + r.width - 1) erase_partner(c, x, y);
        *cell = blank(over(cell, style));
    }
}

void kt_restyle(struct kt_canvas *c, struct kt_rect r, struct kt_style style) {
    int x, y;
    r = visible(c, r);
    for (y = r.y; y < r.y + r.height; y++) for (x = r.x; x < r.x + r.width; x++) {
        struct kt_cell *cell = &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
        struct kt_style next = over(cell, style);
        cell->foreground = next.fg; cell->background = next.bg; cell->attributes |= next.attrs;
    }
}

/* The base cell a joining scalar at column x would attach to, if any. */
static struct kt_cell *join_base(struct kt_canvas *c, struct kt_rect r, int x) {
    struct kt_cell *cell;
    if (x <= r.x || x - 1 >= c->width) return NULL;
    cell = &c->cells[(size_t)r.y * (size_t)c->stride + (size_t)x - 1];
    if (cell->width) return cell;
    return x - 2 >= r.x ? cell - 1 : NULL;
}

/* ATTACH/WIDEN: ASCII mode drops joined scalars; WIDEN takes the next column. */
static int join_scalar(struct kt_canvas *c, struct kt_rect r, int x, struct kt_cell *base, uint32_t cp, bool widen) {
    struct kt_cell *next;
    if (!c->unicode) return 0;
    if (base->combining_count < KT_COMBINING_MAX) base->combining[base->combining_count++] = cp;
    if (!widen || x >= c->width || x >= r.x + r.width) return 0;
    erase_partner(c, x, r.y);
    next = &c->cells[(size_t)r.y * (size_t)c->stride + (size_t)x];
    *next = *base; next->width = 0; next->glyph = ' '; next->combining_count = 0;
    base->width = 2;
    return 1;
}

int kt_put_scalar(struct kt_canvas *c, struct kt_rect r, int x, uint32_t cp, struct kt_style style) {
    struct kt_cell *cell, *base;
    enum kt_join join;
    int width;
    if (r.y < 0 || r.y >= c->height || x < r.x || x < 0) return -1;
    if (cp == 0xadU) cp = '-'; /* tmux and wcwidth show a soft hyphen in one column */
    base = join_base(c, r, x);
    join = kt_cell_join(base, cp);
    if (join == KT_JOIN_DROP) return 0;
    if (join != KT_JOIN_NONE) return join_scalar(c, r, x, base, cp, join == KT_JOIN_WIDEN);
    width = kt_codepoint_width(cp);
    if (width < 0) { cp = '?'; width = 1; }
    if ((int64_t)x + width > (int64_t)r.x + r.width || x + width > c->width) return -1;
    erase_partner(c, x, r.y);
    if (width == 2) erase_partner(c, x + 1, r.y);
    cell = &c->cells[(size_t)r.y * (size_t)c->stride + (size_t)x];
    if (width == 2) cell[1] = blank(over(&cell[1], style));
    *cell = blank(over(cell, style)); cell->glyph = cp; cell->width = (unsigned char)width;
    if (!c->unicode && cp > 126) {
        /* The approximation keeps a wide scalar's columns so alignment holds. */
        cell->glyph = kt_ascii_fallback(cp); cell->width = 1;
    } else if (width == 2) cell[1].width = 0;
    return width;
}

void kt_put(struct kt_canvas *c, int x, int y, uint32_t glyph, struct kt_style style) {
    struct kt_rect row = {0, y, c->width, 1};
    if (x < 0 || y < 0 || x > c->width || y >= c->height) return;
    /* A wide scalar at the last column is replaced by a space. */
    if (kt_put_scalar(c, row, x, glyph, style) < 0 && x < c->width) (void)kt_put_scalar(c, row, x, ' ', style);
}

void kt_text(struct kt_canvas *c, struct kt_rect r, const char *text, struct kt_style style) {
    int64_t x = r.x, end = (int64_t)r.x + r.width;
    struct kt_rect area;
    size_t remaining;
    if (!text || r.width <= 0 || r.height <= 0 || r.y < 0 || r.y >= c->height || end <= 0) return;
    area.x = r.x < 0 ? 0 : r.x; area.y = r.y;
    area.width = (int)((end < c->width ? end : c->width) - area.x);
    if (area.width <= 0) return;
    remaining = strlen(text);
    while (remaining && x <= (int64_t)area.x + area.width) {
        uint32_t cp;
        size_t used = kt_utf8_decode(text, remaining, &cp);
        int advance;
        if (!used) { used = 1; cp = '?'; }
        text += used; remaining -= used;
        if (x < 0) { int width = kt_codepoint_width(cp); x += width < 0 ? 1 : width; continue; }
        advance = kt_put_scalar(c, area, (int)x, cp, style);
        if (advance < 0) break;
        x += advance;
    }
}

static uint32_t panel_corner(bool unicode, bool top, bool left) {
    if (!unicode) return '+';
    if (top) return left ? 0x256dU : 0x256eU;
    return left ? 0x2570U : 0x256fU;
}

static void panel_cell(struct kt_canvas *c, struct kt_rect r, int64_t right, int64_t bottom,
                       int x, int y, struct kt_style border) {
    bool horizontal = (y == r.y || y == bottom) && x >= r.x && x <= right;
    bool vertical = (x == r.x || x == right) && y >= r.y && y <= bottom;
    uint32_t glyph;
    if (!horizontal && !vertical) return;
    if (horizontal && vertical) glyph = panel_corner(c->unicode, y == r.y, x == r.x);
    else if (horizontal) glyph = c->unicode ? 0x2500U : '-';
    else glyph = c->unicode ? 0x2502U : '|';
    kt_put(c, x, y, glyph, border);
}

void kt_panel_styled(struct kt_canvas *c, struct kt_rect r, const char *title,
                     struct kt_style border, struct kt_style title_style) {
    int x, y;
    int64_t right = (int64_t)r.x + r.width - 1, bottom = (int64_t)r.y + r.height - 1;
    if (r.width < 2 || r.height < 2) return;
    for (y = 0; y < c->height; y++) for (x = 0; x < c->width; x++) panel_cell(c, r, right, bottom, x, y, border);
    if (title && title[0] && r.x <= INT_MAX - 2 && r.width > 5) {
        char padded[512];
        int used = snprintf(padded, sizeof(padded), " %s ", title);
        if (used > 0) kt_text(c, (struct kt_rect){r.x + 2, r.y, r.width - 4, 1}, padded, title_style);
    }
}

void kt_panel(struct kt_canvas *c, struct kt_rect r, const char *title) {
    kt_panel_styled(c, r, title, KT_BORDER, KT_STRONG);
}

void kt_bar(struct kt_canvas *c, struct kt_rect r, uint64_t value, uint64_t maximum,
            struct kt_style style) {
    int x;
    if (r.width <= 0 || r.height <= 0 || r.y < 0 || r.y >= c->height) return;
    if (maximum == 0) { kt_text(c, r, "unavailable", KT_WARNING); return; }
    if (value > maximum) value = maximum;
    for (x = 0; x < c->width; x++) {
        int64_t offset = (int64_t)x - r.x;
        double fill;
        unsigned part;
        if (offset < 0 || offset >= r.width) continue;
        fill = (double)value / (double)maximum * r.width - (double)offset;
        part = fill >= 1 ? 8U : fill <= 0 ? 0U : (unsigned)(fill * 8);
        kt_put(c, x, r.y, c->unicode ? (part == 0 ? 0x2591U : 0x2590U - part) : (part ? '#' : '.'), style);
    }
}
