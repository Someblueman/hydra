#ifndef KT_TREE_H
#define KT_TREE_H
#include "kettle.h"
#define KT_TREE_MAX_NODES 2048
/* Preorder nodes; depth may increase by at most one. Caller owns array/labels.
 * value is opaque application identity. Expansion belongs to these nodes. */
struct kt_tree_node { const char *label; size_t value; unsigned depth; bool expanded; struct kt_style style; };
struct kt_tree { struct kt_tree_node *nodes; size_t count, selected; };
bool kt_tree_init(struct kt_tree *tree, struct kt_tree_node *nodes, size_t count);
void kt_tree_move(struct kt_tree *tree, int direction);
void kt_tree_expand(struct kt_tree *tree, bool expand);
/* Surface is already clipped by the caller; scroll belongs to its pane. */
void kt_tree_draw(struct kt_tree *tree, struct kt_canvas *surface, size_t *scroll, bool focused);
/* The scroll kt_tree_draw settles on for a surface `height` rows tall: the
 * nearest one to `scroll` that shows the selected row. For callers that keep
 * scroll in their state and must not change it while drawing. */
size_t kt_tree_scroll(const struct kt_tree *tree, size_t scroll, int height);
bool kt_tree_click(struct kt_tree *tree, size_t scroll, int row);
#endif
