#ifndef TV_WORKSPACE_H
#define TV_WORKSPACE_H
#include "termviz.h"

#define TV_PANES_MAX 32
/* Split axes describe the divider: COLUMNS is a vertical divider. */
enum tv_split { TV_LEAF, TV_COLUMNS, TV_ROWS };
struct tv_pane {
    enum tv_split split;
    int first, second, parent, ratio;
    int min_width, min_height;
    struct tv_rect bounds, divider;
    size_t scroll;
};
/* Value object; caller owns it. Node IDs remain stable until reinitialization.
 * Leaf labels/content are application data. Scroll is retained independently.
 * When minima cannot fit, the focused subtree occupies the available space.
 * Cycling focus reaches hidden leaves and the next layout exposes them. */
struct tv_workspace {
    struct tv_pane panes[TV_PANES_MAX];
    int count, root, focus, drag;
    struct tv_rect bounds;
};
void tv_workspace_init(struct tv_workspace *w, int min_width, int min_height);
/* Replace a leaf with a split and two new leaves. Returns first child or -1.
 * Existing content can follow the returned first child; second is first + 1. */
int tv_workspace_split(struct tv_workspace *w, int leaf, enum tv_split axis, int ratio);
bool tv_workspace_layout(struct tv_workspace *w, struct tv_rect bounds);
void tv_workspace_focus_next(struct tv_workspace *w, int direction);
/* Coordinates are zero-based cells. Press focuses a leaf or begins a divider
 * drag. Motion only changes layout during a drag; release always ends it. */
bool tv_workspace_press(struct tv_workspace *w, int x, int y);
bool tv_workspace_motion(struct tv_workspace *w, int x, int y);
void tv_workspace_release(struct tv_workspace *w);
void tv_workspace_scroll(struct tv_workspace *w, int delta, size_t maximum);
#endif
