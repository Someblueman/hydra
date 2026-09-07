#include "termviz.h"
#include <limits.h>
#include <string.h>

bool tv_init(struct tv_canvas *out, struct tv_cell *cells, size_t capacity,
             int width, int height, bool unicode) {
    if (!out || !cells || width < 1 || height < 1 || width > 4096 || height > 4096 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > capacity) return false;
    out->cells = cells; out->width = width; out->height = height; out->unicode = unicode;
    tv_clear(out, TV_BASE);
    return true;
}

void tv_clear(struct tv_canvas *c, enum tv_style style) {
    size_t i, count = (size_t)c->width * (size_t)c->height;
    for (i = 0; i < count; i++) { c->cells[i].glyph = ' '; c->cells[i].style = style; }
}

void tv_put(struct tv_canvas *c, int x, int y, uint32_t glyph, enum tv_style style) {
    struct tv_cell *cell;
    if (x < 0 || y < 0 || x >= c->width || y >= c->height) return;
    /* Only printable ASCII and our single-cell generated graphics alphabet. */
    if (!((glyph >= 32 && glyph <= 126) || (glyph >= 0x2500 && glyph <= 0x259f) ||
          (glyph >= 0x2800 && glyph <= 0x28ff))) glyph = '?';
    cell = &c->cells[(size_t)y * (size_t)c->width + (size_t)x];
    cell->glyph = glyph; cell->style = style;
}

void tv_text(struct tv_canvas *c, struct tv_rect r, const char *text, enum tv_style style) {
    int i;
    if (!text || r.width <= 0 || r.height <= 0 || r.y < 0 || r.y >= c->height) return;
    for (i = 0; i < r.width && text[i]; i++) {
        int64_t x = (int64_t)r.x + i;
        unsigned char ch = (unsigned char)text[i];
        if (x >= 0 && x < c->width) tv_put(c, (int)x, r.y, ch >= 32 && ch <= 126 ? ch : '?', style);
        if (x >= c->width) break;
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

bool tv_write_row(const struct tv_canvas *c, int row, FILE *output,
                  void (*style)(void *, enum tv_style), void *context) {
    int x;
    enum tv_style previous = (enum tv_style)-1;
    if (row < 0 || row >= c->height || !output) return false;
    for (x = 0; x < c->width; x++) {
        const struct tv_cell *cell = &c->cells[(size_t)row * (size_t)c->width + (size_t)x];
        uint32_t g = cell->glyph;
        if (style && cell->style != previous) { style(context, cell->style); previous = cell->style; }
        if (g < 128) { if (fputc((int)g, output) == EOF) return false; }
        else {
            unsigned char bytes[3] = {(unsigned char)(0xe0U | (g >> 12)),
                (unsigned char)(0x80U | ((g >> 6) & 63U)), (unsigned char)(0x80U | (g & 63U))};
            if (fwrite(bytes, 1, 3, output) != 3) return false;
        }
    }
    return !ferror(output);
}
