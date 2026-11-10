#include "output_internal.h"
#include <string.h>

bool kt_present_init(struct kt_presenter *p, struct kt_cell *previous, size_t capacity) {
    if (!p || !previous || !capacity) return false;
    p->previous = previous; p->capacity = capacity;
    p->width = p->height = 0; p->valid = false; p->overwrite = false;
    p->paint.theme = NULL; p->paint.profile = KT_PROFILE_AUTO;
    return true;
}

void kt_present_invalidate(struct kt_presenter *p) { if (p) p->valid = false; }

/* Compare fields rather than padding. A failed output leaves the cache invalid,
 * so the next attempt cannot mistake an incomplete paint for a delivered frame. */
/* End (exclusive) of the changed run starting at x: bridge short spans of
 * unchanged cells so words stay contiguous in the output stream. */
static int present_run_end(const struct kt_presenter *p, const struct kt_canvas *c, int y, int x, bool full) {
    int end = x + 1, probe, gap = 0;
    for (probe = x; probe < c->width; probe++) {
        const struct kt_cell *pc = &c->cells[(size_t)y * (size_t)c->stride + (size_t)probe];
        const struct kt_cell *po = &p->previous[(size_t)y * (size_t)c->width + (size_t)probe];
        if (full || !kt_cell_equal(pc, po)) { end = probe + 1; gap = 0; }
        else if (++gap > 16) break;
    }
    return end;
}

/* A full repaint starts from an erased display unless the caller overwrites. */
static bool present_erase(const struct kt_presenter *p, FILE *out, bool full) {
    return !full || p->overwrite || fputs("\033[2J", out) != EOF;
}

/* The same cells look different under another theme or profile. */
static bool present_repainted(const struct kt_presenter *p, const struct kt_paint *paint) {
    static const struct kt_paint plain = {NULL, KT_PROFILE_AUTO};
    if (!paint) paint = &plain;
    return p->paint.theme != paint->theme || p->paint.profile != paint->profile;
}

struct present_frame {
    const struct kt_presenter *presenter;
    const struct kt_canvas *canvas;
    struct kt_pen pen;
    FILE *out;
    bool full;
};

/* Writes the changed run of row y that begins at x and returns its end, or
 * -1 when output failed. Coalescing a row's changed cells into one run
 * repaints only within that row's changed span, never a full screen. */
static int present_run(struct present_frame *f, int y, int x) {
    const struct kt_canvas *c = f->canvas;
    const struct kt_cell *cell = &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
    const struct kt_cell *old = &f->presenter->previous[(size_t)y * (size_t)c->width + (size_t)x];
    struct kt_canvas run;
    int start = x;
    if (x > 0 && (!cell->width || (!f->full && !old->width))) start--;
    x = present_run_end(f->presenter, c, y, x, f->full);
    if (x < c->width && c->cells[(size_t)y * (size_t)c->stride + (size_t)x - 1].width == 2) x++;
    if (fprintf(f->out, "\033[%d;%dH", y + 1, start + 1) < 0) return -1;
    run.cells = c->cells + (size_t)y * (size_t)c->stride + (size_t)start;
    run.width = x - start; run.stride = run.width;
    run.height = 1; run.unicode = c->unicode;
    return kt_pen_row(&f->pen, &run, 0, f->out) ? x : -1;
}

static bool present_cells(struct present_frame *f) {
    const struct kt_canvas *c = f->canvas;
    int x, y;
    for (y = 0; y < c->height; y++) for (x = 0; x < c->width;) {
        const struct kt_cell *cell = &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
        const struct kt_cell *old = &f->presenter->previous[(size_t)y * (size_t)c->width + (size_t)x];
        if (!f->full && kt_cell_equal(cell, old)) { x++; continue; }
        x = present_run(f, y, x);
        if (x < 0) return false;
    }
    return kt_pen_end(&f->pen, f->out);
}

bool kt_present(struct kt_presenter *p, const struct kt_canvas *c, FILE *out, const struct kt_paint *paint) {
    struct present_frame frame;
    size_t count;
    int y;
    if (!p || !c || !out || c->width < 1 || c->height < 1 ||
        c->width > 4096 || c->height > 4096) return false;
    count = (size_t)c->width * (size_t)c->height;
    if (count > p->capacity) return false;
    frame.presenter = p; frame.canvas = c; frame.out = out;
    frame.full = !p->valid || p->width != c->width || p->height != c->height || present_repainted(p, paint);
    p->valid = false;
    if (!present_erase(p, out, frame.full)) return false;
    /* A repaint cannot rely on the rendition it finds, unless it is not going
     * to use colour at all: then nothing is written that was not asked for. */
    kt_pen_begin(&frame.pen, paint, !frame.full || !paint || paint->profile <= KT_PROFILE_NONE);
    if (!present_cells(&frame) || fflush(out) != 0) return false;
    for (y = 0; y < c->height; y++)
        memcpy(p->previous + (size_t)y * (size_t)c->width,
               c->cells + (size_t)y * (size_t)c->stride, (size_t)c->width * sizeof(*c->cells));
    p->width = c->width; p->height = c->height; p->valid = true;
    p->paint.theme = paint ? paint->theme : NULL;
    p->paint.profile = paint ? paint->profile : KT_PROFILE_AUTO;
    return true;
}
