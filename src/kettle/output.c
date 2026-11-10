#include "kettle.h"

static bool scalar(FILE *out, uint32_t cp) {
    char bytes[4];
    size_t length = kt_utf8_encode(cp, bytes);
    return fwrite(bytes, 1, length, out) == length;
}

static bool color(FILE *out, uint32_t rgb, bool foreground) {
    if (rgb == KT_COLOR_DEFAULT) return true;
    return fprintf(out, "\033[%d;2;%u;%u;%um", foreground ? 38 : 48,
                   (rgb >> 16) & 255U, (rgb >> 8) & 255U, rgb & 255U) >= 0;
}

bool kt_write_row(const struct kt_canvas *c, int row, FILE *out,
                  void (*style)(void *, FILE *, enum kt_style), void *context) {
    int x;
    const struct kt_cell *previous = NULL;
    if (row < 0 || row >= c->height || !out) return false;
    for (x = 0; x < c->width; x++) {
        const struct kt_cell *cell = &c->cells[(size_t)row * (size_t)c->stride + (size_t)x];
        unsigned i;
        if (!cell->width) continue;
        if (cell->attributes & KT_EXPLICIT) {
            if (!previous || previous->attributes != cell->attributes ||
                previous->foreground != cell->foreground || previous->background != cell->background) {
                if (fputs("\033[0m", out) == EOF) return false;
                if ((cell->attributes & KT_BOLD) && fputs("\033[1m", out) == EOF) return false;
                if ((cell->attributes & KT_DIM) && fputs("\033[2m", out) == EOF) return false;
                if ((cell->attributes & KT_ITALIC) && fputs("\033[3m", out) == EOF) return false;
                if ((cell->attributes & KT_UNDERLINE) && fputs("\033[4m", out) == EOF) return false;
                if ((cell->attributes & KT_REVERSE) && fputs("\033[7m", out) == EOF) return false;
                if (!color(out, cell->foreground, true) || !color(out, cell->background, false)) return false;
            }
        } else {
            if (previous && (previous->attributes & KT_EXPLICIT) && fputs("\033[0m", out) == EOF) return false;
            if (style && (!previous || previous->style != cell->style || (previous->attributes & KT_EXPLICIT)))
                style(context, out, cell->style);
        }
        if (!scalar(out, cell->glyph)) return false;
        for (i = 0; i < cell->combining_count && i < KT_COMBINING_MAX; i++)
            if (!scalar(out, cell->combining[i])) return false;
        previous = cell;
    }
    /* Do not leave an embedded child's attributes active in the outer UI. */
    if (previous && (previous->attributes & KT_EXPLICIT) && fputs("\033[0m", out) == EOF) return false;
    return !ferror(out);
}
