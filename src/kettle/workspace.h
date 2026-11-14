#ifndef KT_WORKSPACE_H
#define KT_WORKSPACE_H
#include "kettle.h"

#define KT_PANES_MAX 32
/* Split axes describe the divider: COLUMNS is a vertical divider. */
enum kt_split { KT_LEAF, KT_COLUMNS, KT_ROWS };
struct kt_pane {
    enum kt_split split;
    int first, second, parent, ratio;
    int min_width, min_height;
    struct kt_rect bounds, divider;
    size_t scroll;
};
/* Value object; caller owns it. Node IDs remain stable until reinitialization.
 * Leaf labels/content are application data. Scroll is retained independently.
 * When minima cannot fit, the focused subtree occupies the available space.
 * Cycling focus reaches hidden leaves and the next layout exposes them. */
struct kt_workspace {
    struct kt_pane panes[KT_PANES_MAX];
    int count, root, focus, drag;
    struct kt_rect bounds;
};
void kt_workspace_init(struct kt_workspace *w, int min_width, int min_height);
/* Replace a leaf with a split and two new leaves. Returns first child or -1.
 * Existing content can follow the returned first child; second is first + 1. */
int kt_workspace_split(struct kt_workspace *w, int leaf, enum kt_split axis, int ratio);
bool kt_workspace_layout(struct kt_workspace *w, struct kt_rect bounds);
void kt_workspace_focus_next(struct kt_workspace *w, int direction);
/* Coordinates are zero-based cells. Press focuses a leaf or begins a divider
 * drag. Motion only changes layout during a drag; release always ends it. */
bool kt_workspace_press(struct kt_workspace *w, int x, int y);
bool kt_workspace_motion(struct kt_workspace *w, int x, int y);
void kt_workspace_release(struct kt_workspace *w);
void kt_workspace_scroll(struct kt_workspace *w, int delta, size_t maximum);
#endif
