#ifndef WORKSPACE_DEMO_H
#define WORKSPACE_DEMO_H
#include "workspace.h"
#include "input.h"
#include "tree.h"
#include "terminal.h"
#include "pty_posix.h"
struct demo {
    struct tv_workspace workspace;
    struct tv_tree tree;
    struct tv_tree_node nodes[12];
    struct tv_terminal_model *model;
    struct tv_pty child;
    bool prefix, scrolling;
    char notice[128];
};
void demo_colors(void *context, enum tv_style style);
void demo_draw(struct tv_canvas *canvas, struct demo *demo);
bool demo_handle(struct demo *demo, const struct tv_event *event);
#endif
