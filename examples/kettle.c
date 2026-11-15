#include "kettle.h"
#include <string.h>

/* Explicit synthetic example: compiles without any Hydra source or runtime. */
int main(int argc, char **argv) {
    struct kt_cell cells[100 * 30];
    struct kt_canvas c;
    struct kt_graph_layout layout;
    const struct kt_node nodes[] = {{"prepare", "succeeded", KT_BASE}, {"build", "running", KT_BASE},
        {"test", "failed", KT_WARNING}, {"review", "blocked", KT_BASE}};
    const struct kt_edge edges[] = {{0, 1}, {0, 2}, {1, 3}, {2, 3}};
    const double samples[] = {0, 2, 4, 3, 5, 9, 8, 6, 5, 8, 10, 9};
    int row;
    bool unicode = !(argc > 1 && strcmp(argv[1], "--ascii") == 0);
    if (!kt_init(&c, cells, 100 * 30, 100, 30, unicode)) return 1;
    kt_panel(&c, (struct kt_rect){0, 0, 100, 30}, "KETTLE / synthetic standalone example");
    kt_panel(&c, (struct kt_rect){2, 2, 45, 10}, "Queue depth / samples, max 10");
    kt_plot(&c, (struct kt_rect){4, 4, 41, 6}, samples, NULL, 12, 10, KT_BASE);
    kt_panel(&c, (struct kt_rect){49, 2, 49, 10}, "Approval / 3 of 5");
    kt_bar(&c, (struct kt_rect){52, 5, 43, 1}, 3, 5, KT_BASE);
    kt_text(&c, (struct kt_rect){52, 8, 43, 1}, "Missing observations are gaps, not zero.", KT_BASE);
    kt_panel(&c, (struct kt_rect){2, 13, 96, 15}, "Dependency graph / arrows mean requires");
    if (!kt_graph_layout(edges, 4, 4, &layout)) return 1;
    kt_graph(&c, (struct kt_rect){4, 15, 92, 11}, nodes, 4, edges, 4, &layout, 0, 0, 1);
    for (row = 0; row < c.height; row++) {
        if (!kt_write_row(&c, row, stdout, NULL) || putchar('\n') == EOF) return 1;
    }
    return 0;
}
