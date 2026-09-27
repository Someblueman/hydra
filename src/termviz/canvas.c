#include "termviz.h"
#include <limits.h>
#include <string.h>

bool tv_init(struct tv_canvas *out, struct tv_cell *cells, size_t capacity,
             int width, int height, bool unicode) {
    if (!out || !cells || width < 1 || height < 1 || width > 4096 || height > 4096 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > capacity) return false;
    out->cells = cells; out->stride = width; out->width = width; out->height = height; out->unicode = unicode;
    tv_clear(out, TV_BASE);
    return true;
}

static struct tv_cell blank(enum tv_style style) {
    struct tv_cell cell;
    memset(&cell, 0, sizeof(cell));
    cell.glyph = ' '; cell.style = style; cell.width = 1;
    cell.foreground = cell.background = TV_COLOR_DEFAULT;
    return cell;
}

bool tv_canvas_view(struct tv_canvas *out, const struct tv_canvas *parent, struct tv_rect r) {
    if (!out || !parent || r.x < 0 || r.y < 0 || r.width < 1 || r.height < 1 ||
        (int64_t)r.x + r.width > parent->width || (int64_t)r.y + r.height > parent->height) return false;
    *out = *parent;
    out->cells = parent->cells + (size_t)r.y * (size_t)parent->stride + (size_t)r.x;
    out->width = r.width; out->height = r.height;
    return true;
}

bool tv_cell_equal(const struct tv_cell *a, const struct tv_cell *b) {
    return a->glyph == b->glyph && a->style == b->style && a->width == b->width &&
        a->foreground == b->foreground && a->background == b->background &&
        a->attributes == b->attributes && a->combining_count == b->combining_count &&
        !memcmp(a->combining, b->combining, sizeof(a->combining));
}

void tv_clear(struct tv_canvas *c, enum tv_style style) {
    int x, y;
    struct tv_cell cell = blank(style);
    for (y = 0; y < c->height; y++) for (x = 0; x < c->width; x++)
        c->cells[(size_t)y * (size_t)c->stride + (size_t)x] = cell;
}

static void erase_partner(struct tv_canvas *c, int x, int y) {
    struct tv_cell *cell = &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
    if (!cell->width && x > 0) cell[-1] = blank(cell->style);
    if (cell->width == 2 && x + 1 < c->width) cell[1] = blank(cell->style);
}

/* The base cell a joining scalar at column x would attach to, if any. */
static struct tv_cell *join_base(struct tv_canvas *c, struct tv_rect r, int x) {
    struct tv_cell *cell;
    if (x <= r.x || x - 1 >= c->width) return NULL;
    cell = &c->cells[(size_t)r.y * (size_t)c->stride + (size_t)x - 1];
    if (cell->width) return cell;
    return x - 2 >= r.x ? cell - 1 : NULL;
}

/* ATTACH/WIDEN: ASCII mode drops joined scalars; WIDEN takes the next column. */
static int join_scalar(struct tv_canvas *c, struct tv_rect r, int x, struct tv_cell *base, uint32_t cp, bool widen) {
    struct tv_cell *next;
    if (!c->unicode) return 0;
    if (base->combining_count < TV_COMBINING_MAX) base->combining[base->combining_count++] = cp;
    if (!widen || x >= c->width || x >= r.x + r.width) return 0;
    erase_partner(c, x, r.y);
    next = &c->cells[(size_t)r.y * (size_t)c->stride + (size_t)x];
    *next = *base; next->width = 0; next->glyph = ' '; next->combining_count = 0;
    base->width = 2;
    return 1;
}

int tv_put_scalar(struct tv_canvas *c, struct tv_rect r, int x, uint32_t cp, enum tv_style style) {
    struct tv_cell *cell, *base;
    enum tv_join join;
    int width;
    if (r.y < 0 || r.y >= c->height || x < r.x || x < 0) return -1;
    if (cp == 0xadU) cp = '-'; /* tmux and wcwidth show a soft hyphen in one column */
    base = join_base(c, r, x);
    join = tv_cell_join(base, cp);
    if (join == TV_JOIN_DROP) return 0;
    if (join != TV_JOIN_NONE) return join_scalar(c, r, x, base, cp, join == TV_JOIN_WIDEN);
    width = tv_codepoint_width(cp);
    if (width < 0) { cp = '?'; width = 1; }
    if ((int64_t)x + width > (int64_t)r.x + r.width || x + width > c->width) return -1;
    erase_partner(c, x, r.y);
    if (width == 2) erase_partner(c, x + 1, r.y);
    cell = &c->cells[(size_t)r.y * (size_t)c->stride + (size_t)x];
    *cell = blank(style); cell->glyph = cp; cell->width = (unsigned char)width;
    if (!c->unicode && cp > 126) {
        /* The approximation keeps a wide scalar's columns so alignment holds. */
        cell->glyph = tv_ascii_fallback(cp); cell->width = 1;
        if (width == 2) cell[1] = blank(style);
    } else if (width == 2) { cell[1] = blank(style); cell[1].width = 0; }
    return width;
}

void tv_put(struct tv_canvas *c, int x, int y, uint32_t glyph, enum tv_style style) {
    struct tv_rect row = {0, y, c->width, 1};
    if (x < 0 || y < 0 || x > c->width || y >= c->height) return;
    /* A wide scalar at the last column is replaced by a space. */
    if (tv_put_scalar(c, row, x, glyph, style) < 0 && x < c->width) (void)tv_put_scalar(c, row, x, ' ', style);
}

void tv_text(struct tv_canvas *c, struct tv_rect r, const char *text, enum tv_style style) {
    int64_t x = r.x, end = (int64_t)r.x + r.width;
    struct tv_rect area;
    size_t remaining;
    if (!text || r.width <= 0 || r.height <= 0 || r.y < 0 || r.y >= c->height || end <= 0) return;
    area.x = r.x < 0 ? 0 : r.x; area.y = r.y;
    area.width = (int)((end < c->width ? end : c->width) - area.x);
    if (area.width <= 0) return;
    remaining = strlen(text);
    while (remaining && x <= (int64_t)area.x + area.width) {
        uint32_t cp;
        size_t used = tv_utf8_decode(text, remaining, &cp);
        int advance;
        if (!used) { used = 1; cp = '?'; }
        text += used; remaining -= used;
        if (x < 0) { int width = tv_codepoint_width(cp); x += width < 0 ? 1 : width; continue; }
        advance = tv_put_scalar(c, area, (int)x, cp, style);
        if (advance < 0) break;
        x += advance;
    }
}

static uint32_t panel_corner(bool unicode, bool top, bool left) {
    if (!unicode) return '+';
    if (top) return left ? 0x256dU : 0x256eU;
    return left ? 0x2570U : 0x256fU;
}

static void panel_cell(struct tv_canvas *c, struct tv_rect r, int64_t right, int64_t bottom,
                       int x, int y, enum tv_style border) {
    bool horizontal = (y == r.y || y == bottom) && x >= r.x && x <= right;
    bool vertical = (x == r.x || x == right) && y >= r.y && y <= bottom;
    uint32_t glyph;
    if (!horizontal && !vertical) return;
    if (horizontal && vertical) glyph = panel_corner(c->unicode, y == r.y, x == r.x);
    else if (horizontal) glyph = c->unicode ? 0x2500U : '-';
    else glyph = c->unicode ? 0x2502U : '|';
    tv_put(c, x, y, glyph, border);
}

void tv_panel_styled(struct tv_canvas *c, struct tv_rect r, const char *title,
                     enum tv_style border, enum tv_style title_style) {
    int x, y;
    int64_t right = (int64_t)r.x + r.width - 1, bottom = (int64_t)r.y + r.height - 1;
    if (r.width < 2 || r.height < 2) return;
    for (y = 0; y < c->height; y++) for (x = 0; x < c->width; x++) panel_cell(c, r, right, bottom, x, y, border);
    if (title && title[0] && r.x <= INT_MAX - 2 && r.width > 5) {
        char padded[512];
        int used = snprintf(padded, sizeof(padded), " %s ", title);
        if (used > 0) tv_text(c, (struct tv_rect){r.x + 2, r.y, r.width - 4, 1}, padded, title_style);
    }
}

void tv_panel(struct tv_canvas *c, struct tv_rect r, const char *title) {
    tv_panel_styled(c, r, title, TV_BORDER, TV_STRONG);
}

void tv_bar(struct tv_canvas *c, struct tv_rect r, uint64_t value, uint64_t maximum,
            enum tv_style style) {
    int x;
    if (r.width <= 0 || r.height <= 0 || r.y < 0 || r.y >= c->height) return;
    if (maximum == 0) { tv_text(c, r, "unavailable", TV_WARNING); return; }
    if (value > maximum) value = maximum;
    for (x = 0; x < c->width; x++) {
        int64_t offset = (int64_t)x - r.x;
        double fill;
        unsigned part;
        if (offset < 0 || offset >= r.width) continue;
        fill = (double)value / (double)maximum * r.width - (double)offset;
        part = fill >= 1 ? 8U : fill <= 0 ? 0U : (unsigned)(fill * 8);
        tv_put(c, x, r.y, c->unicode ? (part == 0 ? 0x2591U : 0x2590U - part) : (part ? '#' : '.'), style);
    }
}

