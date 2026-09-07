#include "termviz/termviz.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

int main(void) {
    struct tv_cell storage[80 * 24 + 2];
    struct tv_canvas canvas;
    struct tv_graph_layout layout;
    struct tv_edge diamond[] = {{0, 1}, {0, 2}, {1, 3}, {2, 3}};
    struct tv_node nodes[] = {{"prepare", "done", TV_BASE}, {"build", "running", TV_BASE},
                             {"test", "failed", TV_WARNING}, {"review", "blocked", TV_BASE}};
    double samples[] = {0, 10, NAN, 5};
    bool valid[] = {true, false, true, true};
    FILE *out;
    char text[81];
    size_t i;
    memset(storage, 0, sizeof(storage));
    storage[0].glyph = 12345; storage[80 * 24 + 1].glyph = 54321;
    assert(!tv_init(&canvas, storage + 1, 80 * 24, INT_MAX, INT_MAX, true));
    assert(!tv_init(&canvas, storage + 1, 1, 80, 24, true));
    assert(tv_init(&canvas, storage + 1, 80 * 24, 80, 24, true));
    tv_panel(&canvas, (struct tv_rect){INT_MIN, INT_MIN, INT_MAX, INT_MAX}, "outside");
    tv_panel(&canvas, (struct tv_rect){-2, -2, 10, 10}, "clipped");
    tv_text(&canvas, (struct tv_rect){1, 1, 30, 1}, "safe\033[2J\n\xff", TV_BASE);
    assert(canvas.cells[81 + 4].glyph == '?');
    tv_put(&canvas, 1, 2, 27, TV_BASE);
    assert(canvas.cells[161].glyph == '?');
    tv_bar(&canvas, (struct tv_rect){0, 3, 10, 1}, UINT64_MAX, UINT64_MAX, TV_BASE);
    assert(canvas.cells[240].glyph == 0x2588);
    tv_bar(&canvas, (struct tv_rect){0, 4, 10, 1}, 0, 0, TV_BASE);
    assert(canvas.cells[320].glyph == 'u');
    tv_clear(&canvas, TV_BASE);
    tv_plot(&canvas, (struct tv_rect){0, 0, 8, 4}, samples, valid, 4, 10, TV_BASE);
    /* Missing second and nonfinite third samples leave entire columns blank. */
    for (i = 0; i < 4; i++) {
        assert(canvas.cells[i * 80 + 2].glyph == ' ');
        assert(canvas.cells[i * 80 + 4].glyph == ' ');
    }
    assert(canvas.cells[3 * 80].glyph != ' '); /* zero is a real observation */
    assert(tv_graph_layout(diamond, 4, 4, &layout));
    assert(layout.x[0] == 0 && layout.x[1] == 27 && layout.x[2] == 27 && layout.x[3] == 54);
    assert(layout.y[1] != layout.y[2]);
    tv_graph(&canvas, (struct tv_rect){0, 0, 80, 24}, nodes, 4, diamond, 4, &layout, 27, 0, 1);
    assert(canvas.cells[0].style == TV_SELECTED);
    diamond[3] = (struct tv_edge){3, 0};
    assert(!tv_graph_layout(diamond, 4, 4, &layout));
    diamond[3] = (struct tv_edge){2, 4};
    assert(!tv_graph_layout(diamond, 4, 4, &layout));
    assert(!tv_graph_layout(NULL, 0, TV_GRAPH_MAX_NODES + 1, &layout));
    {
        struct tv_edge chain[TV_GRAPH_MAX_NODES - 1];
        for (i = 0; i < TV_GRAPH_MAX_NODES - 1; i++) chain[i] = (struct tv_edge){i, i + 1};
        assert(tv_graph_layout(chain, TV_GRAPH_MAX_NODES - 1, TV_GRAPH_MAX_NODES, &layout));
        assert(layout.x[TV_GRAPH_MAX_NODES - 1] == 27 * (TV_GRAPH_MAX_NODES - 1));
    }
    assert(tv_init(&canvas, storage + 1, 80 * 24, 80, 24, false));
    tv_panel(&canvas, (struct tv_rect){0, 0, 80, 24}, "ASCII");
    out = tmpfile(); assert(out);
    assert(tv_write_row(&canvas, 0, out, NULL, NULL));
    rewind(out);
    assert(fread(text, 1, 80, out) == 80);
    assert(text[0] == '+' && text[2] == 'A' && text[79] == '+');
    fclose(out);
    assert(storage[0].glyph == 12345 && storage[80 * 24 + 1].glyph == 54321);
    puts("termviz: clipping, unsafe text, numeric bounds, missing samples, DAG layout and cycles passed");
    return 0;
}
