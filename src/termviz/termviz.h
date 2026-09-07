#ifndef TERMVIZ_H
#define TERMVIZ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Caller owns all storage and strings. No allocation, terminal modes, input,
 * environment, clock, subprocesses, or application state inside this library. */
enum tv_style { TV_BASE, TV_BORDER, TV_TITLE, TV_SELECTED, TV_WARNING, TV_STRONG };
#define TV_COMBINING_MAX 3
#define TV_COLOR_DEFAULT UINT32_C(0xffffffff)
enum tv_attributes {
    TV_BOLD = 1, TV_DIM = 2, TV_ITALIC = 4, TV_UNDERLINE = 8,
    TV_REVERSE = 16, TV_EXPLICIT = 32
};
/* Explicit RGB colors apply when TV_EXPLICIT is set; otherwise semantic style
 * belongs to the caller. width=0 is a wide-cell continuation, never emitted. */
struct tv_cell {
    uint32_t glyph, combining[TV_COMBINING_MAX], foreground, background;
    enum tv_style style;
    unsigned attributes;
    unsigned char width, combining_count;
};
struct tv_canvas { struct tv_cell *cells; int width, height, stride; bool unicode; };
struct tv_rect { int x, y, width, height; };

/* Rejects invalid dimensions, overflow, and insufficient caller storage. */
bool tv_init(struct tv_canvas *out, struct tv_cell *cells, size_t capacity,
             int width, int height, bool unicode);
/* A borrowed clipped surface shares its parent's storage and lifetime. */
bool tv_canvas_view(struct tv_canvas *out, const struct tv_canvas *parent, struct tv_rect area);
void tv_clear(struct tv_canvas *canvas, enum tv_style style);
bool tv_cell_equal(const struct tv_cell *a, const struct tv_cell *b);
/* Locale-independent Unicode 17.0 widths: ambiguous=1, W/F=2, Mn/Me=0.
 * Control/format codepoints return -1. No emoji/ZWJ grapheme shaping. */
int tv_codepoint_width(uint32_t cp);
/* Decode consumes one invalid byte as '?'; 0 means incomplete/empty input. */
size_t tv_utf8_decode(const char *input, size_t size, uint32_t *cp);
size_t tv_utf8_encode(uint32_t cp, char output[4]);
/* Drawing clips to the canvas. UTF-8 text is width-aware; invalid/control bytes
 * become '?'. Up to three combining marks attach to the preceding base cell.
 * ASCII mode replaces non-ASCII scalars; it does not emit UTF-8. */
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

/* Caller owns the previous-frame storage for the presenter's entire lifetime.
 * Output uses ANSI cursor addressing. Leave the outer terminal's last column
 * unused or disable automatic wrapping in the platform adapter. Invalidate after
 * any external writer, screen switch, or terminal recovery. No-op frames emit
 * nothing; successful calls flush output before accepting the frame. */
struct tv_presenter {
    struct tv_cell *previous;
    size_t capacity;
    int width, height;
    bool valid;
};
bool tv_present_init(struct tv_presenter *p, struct tv_cell *previous, size_t capacity);
void tv_present_invalidate(struct tv_presenter *p);
bool tv_present(struct tv_presenter *p, const struct tv_canvas *c, FILE *output,
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
