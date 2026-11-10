#ifndef KETTLE_H
#define KETTLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Caller owns all storage and strings. No allocation, terminal modes, input,
 * environment, clock, subprocesses, or application state inside this library. */
/* Semantic styles. SUCCESS, MUTED and FOCUS were added after STRONG; palettes
 * that map only the first six entries should treat later values as KT_BASE. */
enum kt_style { KT_BASE, KT_BORDER, KT_TITLE, KT_SELECTED, KT_WARNING, KT_STRONG, KT_SUCCESS, KT_MUTED, KT_FOCUS };
#define KT_COMBINING_MAX 3
#define KT_COLOR_DEFAULT UINT32_C(0xffffffff)
enum kt_attributes {
    KT_BOLD = 1, KT_DIM = 2, KT_ITALIC = 4, KT_UNDERLINE = 8,
    KT_REVERSE = 16, KT_EXPLICIT = 32
};
/* Explicit RGB colors apply when KT_EXPLICIT is set; otherwise semantic style
 * belongs to the caller. width=0 is a wide-cell continuation, never emitted. */
struct kt_cell {
    uint32_t glyph, combining[KT_COMBINING_MAX], foreground, background;
    enum kt_style style;
    unsigned attributes;
    unsigned char width, combining_count;
};
struct kt_canvas { struct kt_cell *cells; int width, height, stride; bool unicode; };
struct kt_rect { int x, y, width, height; };

/* Rejects invalid dimensions, overflow, and insufficient caller storage. */
bool kt_init(struct kt_canvas *out, struct kt_cell *cells, size_t capacity,
             int width, int height, bool unicode);
/* A borrowed clipped surface shares its parent's storage and lifetime. */
bool kt_canvas_view(struct kt_canvas *out, const struct kt_canvas *parent, struct kt_rect area);
void kt_clear(struct kt_canvas *canvas, enum kt_style style);
bool kt_cell_equal(const struct kt_cell *a, const struct kt_cell *b);
/* Locale-independent Unicode 17.0 widths: ambiguous=1, W/F=2, Mn/Me=0.
 * Control/format codepoints return -1. No emoji/ZWJ grapheme shaping. */
int kt_codepoint_width(uint32_t cp);
/* How a scalar lays out after the preceding cell (NULL at a line start):
 * NONE starts a new cell; ATTACH stores it on the previous cell (combining
 * marks, ZWJ sequences, emoji modifiers); WIDEN attaches VS16 or a second
 * regional indicator and makes the narrow base two cells, as tmux 3.5 does; DROP
 * discards invisible format characters. C0/C1 controls return NONE and are
 * shown as '?' by the drawing functions. */
enum kt_join { KT_JOIN_NONE, KT_JOIN_ATTACH, KT_JOIN_WIDEN, KT_JOIN_DROP };
enum kt_join kt_cell_join(const struct kt_cell *previous, uint32_t cp);
/* ASCII approximation used by ASCII mode: box drawing keeps line direction,
 * arrows, bullets, quotes and Latin-1 letters map to readable ASCII, and
 * anything else becomes '?'. ASCII input is returned unchanged. */
uint32_t kt_ascii_fallback(uint32_t cp);
/* Decode consumes one invalid byte as '?'; 0 means incomplete/empty input. */
size_t kt_utf8_decode(const char *input, size_t size, uint32_t *cp);
size_t kt_utf8_encode(uint32_t cp, char output[4]);
/* Drawing clips to the canvas. UTF-8 text is width-aware; invalid/control bytes
 * become '?'. Up to three joined scalars attach to the preceding base cell.
 * ASCII mode replaces non-ASCII scalars with kt_ascii_fallback, keeps a wide
 * scalar's two columns and drops joined scalars; it does not emit UTF-8. */
void kt_put(struct kt_canvas *canvas, int x, int y, uint32_t glyph, enum kt_style style);
/* Lays out one scalar at column x of row area.y, clustering it with the
 * preceding cell per kt_cell_join (never with cells left of area.x). Returns
 * the columns advanced (0 when joined or dropped), or -1 without drawing
 * when the scalar needs more columns than remain before the area's end. */
int kt_put_scalar(struct kt_canvas *canvas, struct kt_rect area, int x, uint32_t glyph, enum kt_style style);
void kt_text(struct kt_canvas *canvas, struct kt_rect area, const char *text, enum kt_style style);
void kt_panel(struct kt_canvas *canvas, struct kt_rect area, const char *title);
/* Panel with explicit border and title styles, for focus indication. */
void kt_panel_styled(struct kt_canvas *canvas, struct kt_rect area, const char *title,
                     enum kt_style border, enum kt_style title_style);
void kt_bar(struct kt_canvas *canvas, struct kt_rect area, uint64_t value,
            uint64_t maximum, enum kt_style style);
/* Samples are equally spaced; invalid samples are gaps, not zeros. Range is
 * explicit, zero-based. Caller supplies labels and units outside the plot. */
void kt_plot(struct kt_canvas *canvas, struct kt_rect area, const double *values,
             const bool *valid, size_t count, double maximum, enum kt_style style);
/* Emits one row; ANSI styles are supplied by the application callback, which
 * writes a style's SGR bytes to the stream the cells go to, so styling and
 * text stay in one ordered stream. Output errors are reported; no terminal
 * escape sequences originate in data cells. */
bool kt_write_row(const struct kt_canvas *canvas, int row, FILE *output,
                  void (*style)(void *context, FILE *out, enum kt_style style), void *context);

/* Caller owns the previous-frame storage for the presenter's entire lifetime.
 * Output uses ANSI cursor addressing. Leave the outer terminal's last column
 * unused or disable automatic wrapping in the platform adapter. Invalidate after
 * any external writer, screen switch, or terminal recovery. No-op frames emit
 * nothing; successful calls flush output before accepting the frame.
 * A full repaint (first frame, new size, after invalidation) erases the display
 * with ESC[2J and then writes every cell. Setting `overwrite` after init skips
 * the erase: every cell is still written, so the terminal never shows a blank
 * frame; the caller then clears once when it takes over the screen. */
struct kt_presenter {
    struct kt_cell *previous;
    size_t capacity;
    int width, height;
    bool valid, overwrite;
};
bool kt_present_init(struct kt_presenter *p, struct kt_cell *previous, size_t capacity);
void kt_present_invalidate(struct kt_presenter *p);
bool kt_present(struct kt_presenter *p, const struct kt_canvas *c, FILE *output,
                void (*style)(void *context, FILE *out, enum kt_style style), void *context);

#define KT_GRAPH_MAX_NODES 128
#define KT_GRAPH_MAX_EDGES 512
struct kt_node { const char *label, *detail; enum kt_style style; };
struct kt_edge { size_t from, to; };
struct kt_graph_layout {
    int x[KT_GRAPH_MAX_NODES], y[KT_GRAPH_MAX_NODES];
    int width, height;
};
/* Stable layered DAG layout. Rejects cycles, missing endpoints, and limits.
 * Result coordinates are world cells; pan by changing the supplied origin. */
bool kt_graph_layout(const struct kt_edge *edges, size_t edge_count, size_t node_count,
                     struct kt_graph_layout *out);
void kt_graph(struct kt_canvas *canvas, struct kt_rect area, const struct kt_node *nodes,
              size_t node_count, const struct kt_edge *edges, size_t edge_count,
              const struct kt_graph_layout *layout, int pan_x, int pan_y, size_t selected);

#endif
