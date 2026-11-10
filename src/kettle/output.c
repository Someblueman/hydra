#include "output_internal.h"

static bool scalar(FILE *out, uint32_t cp) {
    char bytes[4];
    size_t length = kt_utf8_encode(cp, bytes);
    return fwrite(bytes, 1, length, out) == length;
}

static bool glyphs(FILE *out, const struct kt_cell *cell) {
    unsigned i;
    if (!scalar(out, cell->glyph)) return false;
    for (i = 0; i < cell->combining_count && i < KT_COMBINING_MAX; i++)
        if (!scalar(out, cell->combining[i])) return false;
    return true;
}

void kt_pen_begin(struct kt_pen *pen, const struct kt_paint *paint, bool known) {
    static const struct kt_style plain = {KT_DEFAULT, KT_DEFAULT, 0};
    pen->paint = paint; pen->now = plain; pen->known = known; pen->cached = false;
}

/* Moves the terminal's rendition to a cell's style. */
static bool pen_move(struct kt_pen *pen, const struct kt_cell *cell, FILE *out) {
    struct kt_style style = kt_cell_style(cell);
    char sequence[KT_SGR_MAX];
    if (!pen->cached || !kt_style_equal(style, pen->seen)) {
        pen->seen = style; pen->shown = kt_style_resolve(style, pen->paint); pen->cached = true;
    }
    if (!kt_sgr(sequence, pen->known ? &pen->now : NULL, pen->shown)) return true;
    pen->now = pen->shown; pen->known = true;
    return fputs(sequence, out) != EOF;
}

bool kt_pen_row(struct kt_pen *pen, const struct kt_canvas *c, int row, FILE *out) {
    int x;
    for (x = 0; x < c->width; x++) {
        const struct kt_cell *cell = &c->cells[(size_t)row * (size_t)c->stride + (size_t)x];
        if (!cell->width) continue;
        if (pen->paint && !pen_move(pen, cell, out)) return false;
        if (!glyphs(out, cell)) return false;
    }
    return !ferror(out);
}

bool kt_pen_end(struct kt_pen *pen, FILE *out) {
    static const struct kt_style plain = {KT_DEFAULT, KT_DEFAULT, 0};
    if (!pen->paint || !pen->known || kt_style_equal(pen->now, plain)) return true;
    pen->now = plain;
    return fputs("\033[0m", out) != EOF;
}

bool kt_write_row(const struct kt_canvas *c, int row, FILE *out, const struct kt_paint *paint) {
    struct kt_pen pen;
    if (row < 0 || row >= c->height || !out) return false;
    kt_pen_begin(&pen, paint, true);
    return kt_pen_row(&pen, c, row, out) && kt_pen_end(&pen, out) && !ferror(out);
}
