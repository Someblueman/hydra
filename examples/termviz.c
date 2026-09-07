#include "termviz.h"
#include <string.h>

/* Explicit synthetic example: compiles without any Hydra source or runtime. */
int main(int argc, char **argv) {
    struct tv_cell cells[100 * 30];
    struct tv_canvas c;
    struct tv_graph_layout layout;
    const struct tv_node nodes[] = {{"prepare", "succeeded", TV_BASE}, {"build", "running", TV_BASE},
        {"test", "failed", TV_WARNING}, {"review", "blocked", TV_BASE}};
    const struct tv_edge edges[] = {{0, 1}, {0, 2}, {1, 3}, {2, 3}};
    const double samples[] = {0, 2, 4, 3, 5, 9, 8, 6, 5, 8, 10, 9};
    int row;
    bool unicode = !(argc > 1 && strcmp(argv[1], "--ascii") == 0);
    if (!tv_init(&c, cells, 100 * 30, 100, 30, unicode)) return 1;
    tv_panel(&c, (struct tv_rect){0, 0, 100, 30}, "TERMVIZ / synthetic standalone example");
    tv_panel(&c, (struct tv_rect){2, 2, 45, 10}, "Queue depth / samples, max 10");
    tv_plot(&c, (struct tv_rect){4, 4, 41, 6}, samples, NULL, 12, 10, TV_BASE);
    tv_panel(&c, (struct tv_rect){49, 2, 49, 10}, "Approval / 3 of 5");
    tv_bar(&c, (struct tv_rect){52, 5, 43, 1}, 3, 5, TV_BASE);
    tv_text(&c, (struct tv_rect){52, 8, 43, 1}, "Missing observations are gaps, not zero.", TV_BASE);
    tv_panel(&c, (struct tv_rect){2, 13, 96, 15}, "Dependency graph / arrows mean requires");
    if (!tv_graph_layout(edges, 4, 4, &layout)) return 1;
    tv_graph(&c, (struct tv_rect){4, 15, 92, 11}, nodes, 4, edges, 4, &layout, 0, 0, 1);
    for (row = 0; row < c.height; row++) {
        if (!tv_write_row(&c, row, stdout, NULL, NULL) || putchar('\n') == EOF) return 1;
    }
    return 0;
}
