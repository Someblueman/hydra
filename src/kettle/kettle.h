#ifndef KETTLE_H
#define KETTLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "style.h"

/* Caller owns all storage and strings. No allocation, terminal modes, input,
 * environment, clock, subprocesses, or application state inside this library. */
#define KT_COMBINING_MAX 3
/* A cell holds its glyph and its style's three parts. Colours stay references
 * (style.h) until the frame is presented. width=0 is a wide-cell
 * continuation, never emitted. */
struct kt_cell {
    uint32_t glyph, combining[KT_COMBINING_MAX];
    kt_color foreground, background;
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
void kt_clear(struct kt_canvas *canvas, struct kt_style style);
bool kt_cell_equal(const struct kt_cell *a, const struct kt_cell *b);
struct kt_style kt_cell_style(const struct kt_cell *cell);
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
/* Drawing clips to the canvas. Every function takes the style to draw in; a
 * colour of KT_KEEP leaves what the cell already has. UTF-8 text is
 * width-aware; invalid/control bytes become '?'. Up to three joined scalars
 * attach to the preceding base cell. ASCII mode replaces non-ASCII scalars
 * with kt_ascii_fallback, keeps a wide scalar's two columns and drops joined
 * scalars; it does not emit UTF-8. */
void kt_put(struct kt_canvas *canvas, int x, int y, uint32_t glyph, struct kt_style style);
/* Lays out one scalar at column x of row area.y, clustering it with the
 * preceding cell per kt_cell_join (never with cells left of area.x). Returns
 * the columns advanced (0 when joined or dropped), or -1 without drawing
 * when the scalar needs more columns than remain before the area's end. */
int kt_put_scalar(struct kt_canvas *canvas, struct kt_rect area, int x, uint32_t glyph, struct kt_style style);
void kt_text(struct kt_canvas *canvas, struct kt_rect area, const char *text, struct kt_style style);
/* Blanks an area in a style: a surface for what is drawn on it next. */
void kt_fill(struct kt_canvas *canvas, struct kt_rect area, struct kt_style style);
/* Changes the look of what is already drawn, text untouched: the colours
 * replace the cells' (KT_KEEP leaves one), the attributes are added. */
void kt_restyle(struct kt_canvas *canvas, struct kt_rect area, struct kt_style style);

/* Display columns of UTF-8 text as kt_text lays it out on a Unicode canvas. */
int kt_text_width(const char *text);
enum kt_align { KT_ALIGN_LEFT, KT_ALIGN_CENTER, KT_ALIGN_RIGHT };
/* One line of text on the first row of `area`, aligned within the part of
 * the area that is on the canvas. Text that does not fit is cut between
 * cells, never inside a wide cell or a cluster of joined scalars, and ends
 * with an ellipsis: U+2026, or "..." on an ASCII canvas. Returns the columns
 * drawn, ellipsis included. */
int kt_text_aligned(struct kt_canvas *canvas, struct kt_rect area, const char *text,
                    enum kt_align align, struct kt_style style);
/* One line made of differently styled runs, drawn left to right and cut the
 * same way; the ellipsis takes the style of the run it cuts. */
struct kt_span { const char *text; struct kt_style style; };
int kt_spans(struct kt_canvas *canvas, struct kt_rect area, const struct kt_span *spans, size_t count);

/* Borders are data: the glyph for each side and corner, for Unicode and for
 * ASCII canvases. Zero leaves that side or corner undrawn, so a border can be
 * one side only: kt_border_bar is a single column at the left edge. */
struct kt_border_glyphs { uint32_t top, bottom, left, right, top_left, top_right, bottom_left, bottom_right; };
struct kt_border { struct kt_border_glyphs unicode, ascii; };
extern const struct kt_border kt_border_none, kt_border_light, kt_border_rounded,
    kt_border_heavy, kt_border_double, kt_border_bar;
/* Draws the border along the edge of `area`; the inside is left as it is. */
void kt_box(struct kt_canvas *canvas, struct kt_rect area, const struct kt_border *border, struct kt_style style);
/* A title in the top edge of a box at `area`, set off by a space on each side
 * and kept clear of the corners; cut with an ellipsis when the edge is short. */
void kt_box_title(struct kt_canvas *canvas, struct kt_rect area, const char *title,
                  enum kt_align align, struct kt_style style);
/* The area inside a border: `area` less one cell for each side it draws. */
struct kt_rect kt_box_inner(struct kt_rect area, const struct kt_border *border);
/* A horizontal rule on the first row of `area` with an optional title set
 * into it: "Title ------" (left), "--- Title ---" or "------ Title". Groups
 * what follows without boxing it in. kt_rule draws both in one style. */
void kt_rule(struct kt_canvas *canvas, struct kt_rect area, const char *title, struct kt_style style);
void kt_rule_styled(struct kt_canvas *canvas, struct kt_rect area, const char *title, enum kt_align align,
                    struct kt_style title_style, struct kt_style line_style);

/* A rounded box in KT_BORDER with a KT_STRONG title, clipped without an
 * ellipsis; kt_box and kt_box_title are the general form. */
void kt_panel(struct kt_canvas *canvas, struct kt_rect area, const char *title);
/* Panel with explicit border and title styles, for focus indication. */
void kt_panel_styled(struct kt_canvas *canvas, struct kt_rect area, const char *title,
                     struct kt_style border, struct kt_style title_style);
void kt_bar(struct kt_canvas *canvas, struct kt_rect area, uint64_t value,
            uint64_t maximum, struct kt_style style);
/* Samples are equally spaced; invalid samples are gaps, not zeros. Range is
 * explicit, zero-based. Caller supplies labels and units outside the plot. */
void kt_plot(struct kt_canvas *canvas, struct kt_rect area, const double *values,
             const bool *valid, size_t count, double maximum, struct kt_style style);
/* Emits one row. With `paint` NULL the row is plain text: glyphs only, no
 * escape sequence. Otherwise each cell's style is resolved against the theme
 * and profile (kt_style_resolve) and SGR is written only where the rendition
 * changes; the row starts from the terminal's default rendition and returns
 * to it. Output errors are reported; no terminal escape sequences originate
 * in data cells. */
bool kt_write_row(const struct kt_canvas *canvas, int row, FILE *output, const struct kt_paint *paint);

/* Caller owns the previous-frame storage for the presenter's entire lifetime.
 * Output uses ANSI cursor addressing. Leave the outer terminal's last column
 * unused or disable automatic wrapping in the platform adapter. Invalidate after
 * any external writer, screen switch, or terminal recovery. No-op frames emit
 * nothing; successful calls flush output before accepting the frame.
 * A full repaint (first frame, new size, another theme or profile, after
 * invalidation) erases the display with ESC[2J and then writes every cell.
 * Setting `overwrite` after init skips the erase: every cell is still
 * written, so the terminal never shows a blank frame; the caller then clears
 * once when it takes over the screen.
 * The presenter owns the terminal's rendition: styles are resolved as for
 * kt_write_row, the rendition is tracked across the whole frame so only
 * changes are written, and a frame that set one ends with a reset. A full
 * repaint does not trust the rendition it finds and starts from a reset;
 * with profile NONE and no attributes in use nothing but text is written. */
struct kt_presenter {
    struct kt_cell *previous;
    size_t capacity;
    int width, height;
    bool valid, overwrite;
    struct kt_paint paint;
};
bool kt_present_init(struct kt_presenter *p, struct kt_cell *previous, size_t capacity);
void kt_present_invalidate(struct kt_presenter *p);
bool kt_present(struct kt_presenter *p, const struct kt_canvas *c, FILE *output, const struct kt_paint *paint);

#define KT_GRAPH_MAX_NODES 128
#define KT_GRAPH_MAX_EDGES 512
struct kt_node { const char *label, *detail; struct kt_style style; };
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
