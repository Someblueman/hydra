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

void tv_put(struct tv_canvas *c, int x, int y, uint32_t glyph, enum tv_style style) {
    struct tv_cell *cell;
    int width = tv_codepoint_width(glyph);
    if (x < 0 || y < 0 || x > c->width || y >= c->height) return;
    if (!c->unicode && glyph > 126) { glyph = '?'; width = 1; }
    if (width < 0) { glyph = '?'; width = 1; }
    if (width == 0) {
        if (x < 1) return;
        cell = &c->cells[(size_t)y * (size_t)c->stride + (size_t)x - 1];
        if (!cell->width && x > 1) cell--;
        if (cell->combining_count < TV_COMBINING_MAX)
            cell->combining[cell->combining_count++] = glyph;
        return;
    }
    if (x == c->width) return;
    if (width == 2 && x + 1 >= c->width) { glyph = ' '; width = 1; }
    erase_partner(c, x, y);
    if (width == 2) erase_partner(c, x + 1, y);
    cell = &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
    *cell = blank(style); cell->glyph = glyph; cell->width = (unsigned char)width;
    if (width == 2) { cell[1] = blank(style); cell[1].width = 0; }
}

void tv_text(struct tv_canvas *c, struct tv_rect r, const char *text, enum tv_style style) {
    int64_t x = r.x, end = (int64_t)r.x + r.width;
    size_t remaining;
    if (!text || r.width <= 0 || r.height <= 0 || r.y < 0 || r.y >= c->height) return;
    remaining = strlen(text);
    while (remaining && x <= end && x <= c->width) {
        uint32_t cp;
        size_t used = tv_utf8_decode(text, remaining, &cp);
        int width;
        if (!used) { used = 1; cp = '?'; }
        width = tv_codepoint_width(cp);
        if (width < 0 || (!c->unicode && cp > 126)) { cp = '?'; width = 1; }
        if (width && (x == end || x == c->width)) break;
        if (x >= 0 && x + width <= end && !(width == 0 && x == r.x))
            tv_put(c, (int)x, r.y, cp, style);
        x += width; text += used; remaining -= used;
    }
}

void tv_panel(struct tv_canvas *c, struct tv_rect r, const char *title) {
    int x, y;
    int64_t right = (int64_t)r.x + r.width - 1, bottom = (int64_t)r.y + r.height - 1;
    if (r.width < 2 || r.height < 2) return;
    for (y = 0; y < c->height; y++) for (x = 0; x < c->width; x++) {
        bool horizontal = (y == r.y || y == bottom) && x >= r.x && x <= right;
        bool vertical = (x == r.x || x == right) && y >= r.y && y <= bottom;
        uint32_t glyph;
        if (!horizontal && !vertical) continue;
        glyph = horizontal ? (c->unicode ? 0x2500U : '-') : (c->unicode ? 0x2502U : '|');
        if (horizontal && vertical) {
            glyph = !c->unicode ? '+' : y == r.y ? (x == r.x ? 0x256dU : 0x256eU) : (x == r.x ? 0x2570U : 0x256fU);
        }
        tv_put(c, x, y, glyph, TV_BORDER);
    }
    if (r.x <= INT_MAX - 2 && r.width > 5)
        tv_text(c, (struct tv_rect){r.x + 2, r.y, r.width - 4, 1}, title, TV_STRONG);
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

