#ifndef WORKSPACE_DEMO_H
#define WORKSPACE_DEMO_H
#include "workspace.h"
#include "input.h"
#include "tree.h"
#include "terminal.h"
#include "pty_posix.h"
#include "program.h"
/* The application model. update (workspace.c, workspace_input.c) is the only
 * code that changes it; demo_draw reads it. `model` is the embedded shell's
 * terminal screen, NULL when the demo runs without a shell. */
struct demo {
    struct kt_workspace workspace;
    struct kt_tree tree;
    struct kt_tree_node nodes[12];
    struct kt_terminal_model *model;
    struct kt_pty child;
    bool prefix, scrolling;
    char notice[128];
    int columns, rows;          /* terminal size, from KT_MSG_RESIZE */
    uint64_t watch;             /* kt_cmd_watch on the shell's PTY */
    short watching;             /* the poll events that watch asks for */
    char **child_argv;
    struct termios settings;    /* what the shell's terminal starts with */
    int spawn_error;
};
/* Bold text in a colour the stock vocabulary has no name for. */
enum { DEMO_TOKEN_STRONG = KT_TOKEN_USER };
#define DEMO_STRONG KT_STYLE(KT_TOKEN(DEMO_TOKEN_STRONG), KT_COLOR_SURFACE, KT_BOLD)
extern const struct kt_theme demo_theme;
void demo_draw(struct kt_canvas *canvas, const struct demo *demo);
/* False when the key asks to quit. */
bool demo_handle(struct demo *demo, const struct kt_event *event);
#endif
