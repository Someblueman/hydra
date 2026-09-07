#ifndef TERMVIZ_H
#define TERMVIZ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Caller owns all storage and strings. No allocation, terminal modes, input,
 * environment, clock, subprocesses, or application state inside this library. */
enum tv_style { TV_BASE, TV_BORDER, TV_TITLE, TV_SELECTED, TV_WARNING, TV_STRONG };
struct tv_cell { uint32_t glyph; enum tv_style style; };
struct tv_canvas { struct tv_cell *cells; int width, height; bool unicode; };
struct tv_rect { int x, y, width, height; };

/* Rejects invalid dimensions, overflow, and insufficient caller storage. */
bool tv_init(struct tv_canvas *out, struct tv_cell *cells, size_t capacity,
             int width, int height, bool unicode);
void tv_clear(struct tv_canvas *canvas, enum tv_style style);
/* Drawing clips to the canvas. Text accepts printable ASCII; other bytes become
 * '?'. Generated line/chart glyphs are separate from untrusted text. */
void tv_put(struct tv_canvas *canvas, int x, int y, uint32_t glyph, enum tv_style style);
void tv_text(struct tv_canvas *canvas, struct tv_rect area, const char *text, enum tv_style style);
void tv_panel(struct tv_canvas *canvas, struct tv_rect area, const char *title);
void tv_bar(struct tv_canvas *canvas, struct tv_rect area, uint64_t value,
            uint64_t maximum, enum tv_style style);
/* Samples are equally spaced; invalid samples are gaps, not zeros. Range is
 * explicit, zero-based. Caller supplies labels and units outside the plot. */
void tv_plot(struct tv_canvas *canvas, struct tv_rect area, const double *values,
             const bool *valid, size_t count, double maximum, enum tv_style style);
/* Emits one row; ANSI styles are supplied by the application callback. Output
 * errors are reported; no terminal escape sequences originate in data cells. */
bool tv_write_row(const struct tv_canvas *canvas, int row, FILE *output,
                  void (*style)(void *, enum tv_style), void *context);

#define TV_GRAPH_MAX_NODES 128
#define TV_GRAPH_MAX_EDGES 512
struct tv_node { const char *label, *detail; enum tv_style style; };
struct tv_edge { size_t from, to; };
struct tv_graph_layout {
    int x[TV_GRAPH_MAX_NODES], y[TV_GRAPH_MAX_NODES];
    int width, height;
};
/* Stable layered DAG layout. Rejects cycles, missing endpoints, and limits.
 * Result coordinates are world cells; pan by changing the supplied origin. */
bool tv_graph_layout(const struct tv_edge *edges, size_t edge_count, size_t node_count,
                     struct tv_graph_layout *out);
void tv_graph(struct tv_canvas *canvas, struct tv_rect area, const struct tv_node *nodes,
              size_t node_count, const struct tv_edge *edges, size_t edge_count,
              const struct tv_graph_layout *layout, int pan_x, int pan_y, size_t selected);

#endif
