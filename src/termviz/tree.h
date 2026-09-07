#ifndef TV_TREE_H
#define TV_TREE_H
#include "termviz.h"
#define TV_TREE_MAX_NODES 2048
/* Preorder nodes; depth may increase by at most one. Caller owns array/labels.
 * value is opaque application identity. Expansion belongs to these nodes. */
struct tv_tree_node { const char *label; size_t value; unsigned depth; bool expanded; enum tv_style style; };
struct tv_tree { struct tv_tree_node *nodes; size_t count, selected; };
bool tv_tree_init(struct tv_tree *tree, struct tv_tree_node *nodes, size_t count);
void tv_tree_move(struct tv_tree *tree, int direction);
void tv_tree_expand(struct tv_tree *tree, bool expand);
/* Surface is already clipped by the caller; scroll belongs to its pane. */
void tv_tree_draw(struct tv_tree *tree, struct tv_canvas *surface, size_t *scroll, bool focused);
bool tv_tree_click(struct tv_tree *tree, size_t scroll, int row);
#endif
