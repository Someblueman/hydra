#ifndef KT_LAYOUT_H
#define KT_LAYOUT_H
/* Rectangle arithmetic for laying out a view: take a strip off an edge, pad,
 * centre, and divide a length among parts by constraint. Pure functions over
 * struct kt_rect; nothing is drawn and no size ever comes back negative. The
 * pane tree in workspace.h is the stateful, resizable counterpart. */
#include "kettle.h"

/* Shrinks a rectangle by the given amount on each side. A rectangle too
 * small for its insets collapses to zero width or height where the insets
 * meet, never to a negative one. */
struct kt_rect kt_rect_inset(struct kt_rect r, int top, int right, int bottom, int left);
/* The same amount left and right, and the same top and bottom. */
struct kt_rect kt_rect_pad(struct kt_rect r, int horizontal, int vertical);

enum kt_side { KT_SIDE_TOP, KT_SIDE_BOTTOM, KT_SIDE_LEFT, KT_SIDE_RIGHT };
/* Cuts `size` rows or columns off one edge. Returns the strip; `rest`, when
 * not NULL, gets what remains. A strip larger than the rectangle takes all
 * of it and leaves an empty rest; a negative size cuts nothing. */
struct kt_rect kt_rect_cut(struct kt_rect r, enum kt_side side, int size, struct kt_rect *rest);
/* A box of the given size centred in `r`, no larger than `r`. */
struct kt_rect kt_rect_center(struct kt_rect r, int width, int height);

/* How one part of a split asks for its size. */
enum kt_size {
    KT_SIZE_FIXED,     /* exactly `value` cells                              */
    KT_SIZE_PERCENT,   /* `value` percent of the length being divided         */
    KT_SIZE_MIN,       /* at least `value` cells, and a share of what is left */
    KT_SIZE_FLEX       /* a share of what is left, with weight `value`        */
};
struct kt_constraint { enum kt_size size; int value; };
#define KT_FIXED(cells) {KT_SIZE_FIXED, (cells)}
#define KT_PERCENT(percent) {KT_SIZE_PERCENT, (percent)}
#define KT_MIN(cells) {KT_SIZE_MIN, (cells)}
#define KT_FLEX(weight) {KT_SIZE_FLEX, (weight)}

/* Divides `length` cells among `count` parts with `gap` cells between
 * neighbours, writing each part's size to `sizes`.
 *   1. Fixed, percentage and minimum sizes are granted in order. When they
 *      do not all fit, earlier parts are served first and later ones get
 *      what is left, down to zero.
 *   2. What then remains is shared among the MIN and FLEX parts by weight
 *      (a MIN part has weight 1, a FLEX part its value, at least 1); cells
 *      that do not divide evenly go to the earliest of them.
 *   3. With no MIN or FLEX part the remainder is left unused at the end.
 * Gaps are taken out first; when they alone exceed the length every part is
 * zero. Negative values count as zero. */
void kt_split(int length, int gap, const struct kt_constraint *constraints, size_t count, int *sizes);

/* kt_split along one axis of a rectangle: columns side by side, or rows
 * stacked. Parts keep their order and the gap between them; a part with no
 * room is an empty rectangle at the far edge. */
void kt_rect_columns(struct kt_rect r, int gap, const struct kt_constraint *constraints, size_t count,
                     struct kt_rect *parts);
void kt_rect_rows(struct kt_rect r, int gap, const struct kt_constraint *constraints, size_t count,
                  struct kt_rect *parts);

#endif
