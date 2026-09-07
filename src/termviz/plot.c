#include "termviz.h"
#include <math.h>

/* Each Braille cell stores a 2x4 pixel column. ASCII plots use whole cells. */
void tv_plot(struct tv_canvas *c, struct tv_rect r, const double *values,
             const bool *valid, size_t count, double maximum, enum tv_style style) {
    static const unsigned dots[2][4] = {{0, 1, 2, 6}, {3, 4, 5, 7}};
    int x, y;
    if (!values || !count || r.width <= 0 || r.height <= 0 ||
        !isfinite(maximum) || maximum <= 0) return;
    /* Iterate only visible cells, even for an enormous offscreen rectangle. */
    for (y = 0; y < c->height; y++) for (x = 0; x < c->width; x++) {
        int64_t dx = (int64_t)x - r.x, dy = (int64_t)y - r.y;
        unsigned bits = 0, sub, columns = c->unicode ? 2U : 1U;
        if (dx < 0 || dy < 0 || dx >= r.width || dy >= r.height) continue;
        for (sub = 0; sub < columns; sub++) {
            double position = ((double)dx * columns + sub) / ((double)r.width * columns);
            size_t sample = (size_t)(position * count);
            double level, scaled;
            int64_t pixel;
            unsigned rows = c->unicode ? 4U : 1U;
            if (sample >= count) sample = count - 1;
            if ((valid && !valid[sample]) || !isfinite(values[sample]) || values[sample] < 0) continue;
            level = values[sample] > maximum ? maximum : values[sample];
            scaled = level / maximum * ((double)r.height * rows - 1);
            pixel = (int64_t)r.height * rows - 1 - (int64_t)scaled;
            if (pixel / rows != dy) continue;
            bits |= 1U << dots[sub][pixel % rows];
        }
        if (bits) tv_put(c, x, y, c->unicode ? 0x2800U + bits : '*', style);
    }
}
