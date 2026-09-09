#include "termviz.h"

static bool scalar(FILE *out, uint32_t cp) {
    char bytes[4];
    size_t length = tv_utf8_encode(cp, bytes);
    return fwrite(bytes, 1, length, out) == length;
}

static bool color(FILE *out, uint32_t rgb, bool foreground) {
    if (rgb == TV_COLOR_DEFAULT) return true;
    return fprintf(out, "\033[%d;2;%u;%u;%um", foreground ? 38 : 48,
                   (rgb >> 16) & 255U, (rgb >> 8) & 255U, rgb & 255U) >= 0;
}

bool tv_write_row(const struct tv_canvas *c, int row, FILE *out,
                  void (*style)(void *, enum tv_style), void *context) {
    int x;
    const struct tv_cell *previous = NULL;
    if (row < 0 || row >= c->height || !out) return false;
    for (x = 0; x < c->width; x++) {
        const struct tv_cell *cell = &c->cells[(size_t)row * (size_t)c->stride + (size_t)x];
        unsigned i;
        if (!cell->width) continue;
        if (cell->attributes & TV_EXPLICIT) {
            if (!previous || previous->attributes != cell->attributes ||
                previous->foreground != cell->foreground || previous->background != cell->background) {
                if (fputs("\033[0m", out) == EOF) return false;
                if ((cell->attributes & TV_BOLD) && fputs("\033[1m", out) == EOF) return false;
                if ((cell->attributes & TV_DIM) && fputs("\033[2m", out) == EOF) return false;
                if ((cell->attributes & TV_ITALIC) && fputs("\033[3m", out) == EOF) return false;
                if ((cell->attributes & TV_UNDERLINE) && fputs("\033[4m", out) == EOF) return false;
                if ((cell->attributes & TV_REVERSE) && fputs("\033[7m", out) == EOF) return false;
                if (!color(out, cell->foreground, true) || !color(out, cell->background, false)) return false;
            }
        } else {
            if (previous && (previous->attributes & TV_EXPLICIT) && fputs("\033[0m", out) == EOF) return false;
            if (style && (!previous || previous->style != cell->style || (previous->attributes & TV_EXPLICIT)))
                style(context, cell->style);
        }
        if (!scalar(out, cell->glyph)) return false;
        for (i = 0; i < cell->combining_count && i < TV_COMBINING_MAX; i++)
            if (!scalar(out, cell->combining[i])) return false;
        previous = cell;
    }
    /* Do not leave an embedded child's attributes active in the outer UI. */
    if (previous && (previous->attributes & TV_EXPLICIT) && fputs("\033[0m", out) == EOF) return false;
    return !ferror(out);
}
