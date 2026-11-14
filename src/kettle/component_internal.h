#ifndef KT_COMPONENT_INTERNAL_H
#define KT_COMPONENT_INTERNAL_H
/* Pieces the components share: default binding rows, key dispatch, row
 * scrolling, selection marks, highlighted text, scroll bars and the mouse. */
#include "components.h"

/* A binding row with one or two chords, written as constant data so that
 * defaults need no parsing at init. */
#define KT_UI_CHORD(key, mods) {{{(key), (mods)}}, 1}
#define KT_UI_CHORD2(key1, mods1, key2, mods2) {{{(key1), (mods1)}, {(key2), (mods2)}}, 2}
#define KT_UI_NO_CHORD {{{0, 0}}, 0}
#define KT_UI_ROW(action, name, label, scope, group, priority, count, first, second) \
    {(name), (label), (action), (scope), {first, second}, (count), 0, (group), (priority)}

/* Matches a key event against a component's bindings in `scope`. Returns
 * the action of a completed chord, or -1; *consumed says whether the key
 * belonged to a chord at all (completed, pending or abandoned). */
int kt_ui_action(const struct kt_binding *keys, size_t count, struct kt_keymap_state *chord, uint32_t scope,
                 const struct kt_msg *msg, bool *consumed);

/* Rows of differing heights, for keeping a selection in view. */
struct kt_ui_rows {
    size_t count;
    int (*height)(const void *context, size_t row);   /* NULL: every row is one */
    const void *context;
};
int kt_ui_row_height(const struct kt_ui_rows *rows, size_t row);
/* The first row that keeps `selected` in `height` rows when the view starts
 * at `top` now; top itself when it already does. */
size_t kt_ui_scroll_to(const struct kt_ui_rows *rows, size_t top, size_t selected, int height);
/* How many rows from `top` fit, a partly shown last row included. */
size_t kt_ui_rows_shown(const struct kt_ui_rows *rows, size_t top, int height);
/* The top that shows the last rows, filling the height. */
size_t kt_ui_bottom_top(const struct kt_ui_rows *rows, int height);
/* `top` moved by `delta` rows, kept between 0 and kt_ui_bottom_top. */
size_t kt_ui_scroll_by(const struct kt_ui_rows *rows, size_t top, long delta, int height);
/* The row at screen line `line` of a view starting at `top`, or KT_NONE. */
size_t kt_ui_row_at(const struct kt_ui_rows *rows, size_t top, int line, int height);

/* A selected row: filled, with a bar at its left edge; unfocused, the bar
 * alone in its own style. */
void kt_ui_selection(struct kt_canvas *canvas, struct kt_rect row, bool focused, struct kt_style fill,
                     struct kt_style bar, struct kt_style bar_unfocused);
/* Text with the characters matching `pattern` drawn in `mark`, cut with an
 * ellipsis; returns the columns drawn. */
int kt_ui_marked(struct kt_canvas *canvas, struct kt_rect area, const char *text, const char *pattern,
                 struct kt_style style, struct kt_style mark);
/* A vertical scroll bar in a one-column area: a track, and a thumb sized and
 * placed for `shown` of `total` rows from `first`. Nothing when all fit. */
void kt_ui_scrollbar(struct kt_canvas *canvas, struct kt_rect column, size_t total, size_t first, size_t shown,
                     struct kt_style track, struct kt_style thumb);

/* -1 for a wheel step up, 1 down, 0 for anything else. */
int kt_ui_wheel(const struct kt_msg *msg);
/* A press of the first button inside `area`: true with the cell relative to
 * the area's top-left. */
bool kt_ui_click(const struct kt_msg *msg, struct kt_rect area, int *x, int *y);
bool kt_ui_inside(struct kt_rect area, int x, int y);
/* A dialog's outline: a raised panel with a rounded border, its title set
 * into the top edge and key hints into the bottom edge. Returns the room
 * inside, padded one row and two columns. */
struct kt_rect kt_ui_dialog(struct kt_canvas *canvas, struct kt_rect area, const char *title, struct kt_style border,
                            struct kt_style title_style, const struct kt_help_source *hints, size_t hint_count,
                            const struct kt_help_style *hint_style);
/* The stock look of key hints: bold keys, muted labels, faint separators. */
struct kt_help_style kt_ui_hint_style(void);

#endif
