#include "kettle/kettle.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

int main(void) {
    struct kt_cell storage[80 * 24 + 2];
    struct kt_canvas canvas;
    struct kt_graph_layout layout;
    struct kt_edge diamond[] = {{0, 1}, {0, 2}, {1, 3}, {2, 3}};
    struct kt_node nodes[] = {{"prepare", "done", KT_BASE}, {"build", "running", KT_BASE},
                             {"test", "failed", KT_WARNING}, {"review", "blocked", KT_BASE}};
    double samples[] = {0, 10, NAN, 5};
    bool valid[] = {true, false, true, true};
    FILE *out;
    char text[81];
    size_t i;
    memset(storage, 0, sizeof(storage));
    storage[0].glyph = 12345; storage[80 * 24 + 1].glyph = 54321;
    assert(!kt_init(&canvas, storage + 1, 80 * 24, INT_MAX, INT_MAX, true));
    assert(!kt_init(&canvas, storage + 1, 1, 80, 24, true));
    assert(kt_init(&canvas, storage + 1, 80 * 24, 80, 24, true));
    kt_panel(&canvas, (struct kt_rect){INT_MIN, INT_MIN, INT_MAX, INT_MAX}, "outside");
    kt_panel(&canvas, (struct kt_rect){-2, -2, 10, 10}, "clipped");
    kt_text(&canvas, (struct kt_rect){1, 1, 30, 1}, "safe\033[2J\n\xff", KT_BASE);
    assert(canvas.cells[81 + 4].glyph == '?');
    kt_put(&canvas, 1, 2, 27, KT_BASE);
    assert(canvas.cells[161].glyph == '?');
    kt_bar(&canvas, (struct kt_rect){0, 3, 10, 1}, UINT64_MAX, UINT64_MAX, KT_BASE);
    assert(canvas.cells[240].glyph == 0x2588);
    kt_bar(&canvas, (struct kt_rect){0, 4, 10, 1}, 0, 0, KT_BASE);
    assert(canvas.cells[320].glyph == 'u');
    kt_clear(&canvas, KT_BASE);
    kt_plot(&canvas, (struct kt_rect){0, 0, 8, 4}, samples, valid, 4, 10, KT_BASE);
    /* Missing second and nonfinite third samples leave entire columns blank. */
    for (i = 0; i < 4; i++) {
        assert(canvas.cells[i * 80 + 2].glyph == ' ');
        assert(canvas.cells[i * 80 + 4].glyph == ' ');
    }
    assert(canvas.cells[3 * 80].glyph != ' '); /* zero is a real observation */
    assert(kt_graph_layout(diamond, 4, 4, &layout));
    assert(layout.x[0] == 0 && layout.x[1] == 27 && layout.x[2] == 27 && layout.x[3] == 54);
    assert(layout.y[1] != layout.y[2]);
    kt_graph(&canvas, (struct kt_rect){0, 0, 80, 24}, nodes, 4, diamond, 4, &layout, 27, 0, 1);
    assert(kt_style_equal(kt_cell_style(&canvas.cells[0]), KT_SELECTED));
    diamond[3] = (struct kt_edge){3, 0};
    assert(!kt_graph_layout(diamond, 4, 4, &layout));
    diamond[3] = (struct kt_edge){2, 4};
    assert(!kt_graph_layout(diamond, 4, 4, &layout));
    assert(!kt_graph_layout(NULL, 0, KT_GRAPH_MAX_NODES + 1, &layout));
    {
        struct kt_edge chain[KT_GRAPH_MAX_NODES - 1];
        for (i = 0; i < KT_GRAPH_MAX_NODES - 1; i++) chain[i] = (struct kt_edge){i, i + 1};
        assert(kt_graph_layout(chain, KT_GRAPH_MAX_NODES - 1, KT_GRAPH_MAX_NODES, &layout));
        assert(layout.x[KT_GRAPH_MAX_NODES - 1] == 27 * (KT_GRAPH_MAX_NODES - 1));
    }
    assert(kt_init(&canvas, storage + 1, 80 * 24, 80, 24, false));
    kt_panel(&canvas, (struct kt_rect){0, 0, 80, 24}, "ASCII");
    out = tmpfile(); assert(out);
    assert(kt_write_row(&canvas, 0, out, NULL));
    rewind(out);
    assert(fread(text, 1, 80, out) == 80);
    assert(text[0] == '+' && text[2] == ' ' && text[3] == 'A' && text[8] == ' ' && text[9] == '-' && text[79] == '+');
    fclose(out);
    assert(storage[0].glyph == 12345 && storage[80 * 24 + 1].glyph == 54321);
    puts("kettle: clipping, unsafe text, numeric bounds, missing samples, DAG layout and cycles passed");
    return 0;
}
