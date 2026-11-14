#ifndef KT_OUTPUT_INTERNAL_H
#define KT_OUTPUT_INTERNAL_H
/* Shared by the row writer (output.c) and the presenter (present.c). Not an
 * application interface. */
#include "kettle.h"

/* The terminal's rendition while cells are being written. `known` is false
 * until the first sequence, which then starts from a reset. `seen` and
 * `shown` remember the last style resolved, since neighbouring cells mostly
 * share one. */
struct kt_pen {
    const struct kt_paint *paint;
    struct kt_style now, seen, shown;
    bool known, cached;
};
void kt_pen_begin(struct kt_pen *pen, const struct kt_paint *paint, bool known);
/* Writes one row's cells, changing the rendition as their styles require. */
bool kt_pen_row(struct kt_pen *pen, const struct kt_canvas *canvas, int row, FILE *output);
/* Returns the terminal to its default rendition if the pen left it. */
bool kt_pen_end(struct kt_pen *pen, FILE *output);

#endif
