#include "termviz.h"
#include <string.h>

bool tv_present_init(struct tv_presenter *p, struct tv_cell *previous, size_t capacity) {
    if (!p || !previous || !capacity) return false;
    p->previous = previous; p->capacity = capacity;
    p->width = p->height = 0; p->valid = false;
    return true;
}

void tv_present_invalidate(struct tv_presenter *p) { if (p) p->valid = false; }

/* Compare fields rather than padding. A failed output leaves the cache invalid,
 * so the next attempt cannot mistake an incomplete paint for a delivered frame. */
bool tv_present(struct tv_presenter *p, const struct tv_canvas *c, FILE *out,
                void (*style)(void *, enum tv_style), void *context) {
    int x, y;
    bool full;
    size_t count;
    if (!p || !c || !out || c->width < 1 || c->height < 1 ||
        c->width > 4096 || c->height > 4096) return false;
    count = (size_t)c->width * (size_t)c->height;
    if (count > p->capacity) return false;
    full = !p->valid || p->width != c->width || p->height != c->height;
    p->valid = false;
    if (full && fputs("\033[2J", out) == EOF) return false;
    for (y = 0; y < c->height; y++) for (x = 0; x < c->width;) {
        size_t offset = (size_t)y * (size_t)c->stride + (size_t)x;
        size_t old_offset = (size_t)y * (size_t)c->width + (size_t)x;
        const struct tv_cell *cell = &c->cells[offset], *old = &p->previous[old_offset];
        int start = x;
        struct tv_canvas run;
        if (!full && tv_cell_equal(cell, old)) { x++; continue; }
        if (x > 0 && (!cell->width || (!full && !old->width))) start--;
        do {
            x++;
            if (x == c->width) break;
            cell++; old++;
        } while (full || !tv_cell_equal(cell, old));
        if (x < c->width && c->cells[(size_t)y * (size_t)c->stride + (size_t)x - 1].width == 2) x++;
        if (fprintf(out, "\033[%d;%dH", y + 1, start + 1) < 0) return false;
        run.cells = c->cells + (size_t)y * (size_t)c->stride + (size_t)start;
        run.width = x - start; run.stride = run.width;
        run.height = 1; run.unicode = c->unicode;
        if (!tv_write_row(&run, 0, out, style, context)) return false;
    }
    if (fflush(out) != 0) return false;
    for (y = 0; y < c->height; y++)
        memcpy(p->previous + (size_t)y * (size_t)c->width,
               c->cells + (size_t)y * (size_t)c->stride, (size_t)c->width * sizeof(*c->cells));
    p->width = c->width; p->height = c->height; p->valid = true;
    return true;
}
