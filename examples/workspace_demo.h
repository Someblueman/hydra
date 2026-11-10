#ifndef WORKSPACE_DEMO_H
#define WORKSPACE_DEMO_H
#include "workspace.h"
#include "input.h"
#include "tree.h"
#include "terminal.h"
#include "pty_posix.h"
struct demo {
    struct kt_workspace workspace;
    struct kt_tree tree;
    struct kt_tree_node nodes[12];
    struct kt_terminal_model *model;
    struct kt_pty child;
    bool prefix, scrolling;
    char notice[128];
};
void demo_colors(void *context, FILE *out, enum kt_style style);
void demo_draw(struct kt_canvas *canvas, struct demo *demo);
bool demo_handle(struct demo *demo, const struct kt_event *event);
#endif
