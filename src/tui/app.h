#ifndef HYDRA_TUI_APP_H
#define HYDRA_TUI_APP_H
#include "model.h"
#include <termios.h>

struct app {
    struct model model;
    const char *hydra;
    size_t selected, recovery_selected;
    int view, theme;
    int hit_rows[MAX_HEADS], hit_cols, hit_height, hit_view;
    size_t hit_items[MAX_HEADS], hit_count;
    bool hit_tabs;
    int rows, cols, line, limit;
    bool raw, no_color, preview, help, running, fleet, diagnostics, paint, boxed;
    char marked[MAX_HEADS][TEXT];
    size_t marked_count;
    char current_session[TEXT];
    char search[TEXT];
    char notice[TEXT];
    char preview_text[4096];
    struct termios saved;
};


#endif
