#ifndef HYDRA_TUI_RENDER_H
#define HYDRA_TUI_RENDER_H
#include "app.h"
/* Names borrow fixed internal strings; rendering borrows/mutates app layout. */
bool parse_theme(const char *name, int *theme);
const char *theme_name(int theme);
void render(struct app *app, unsigned frame, bool headless);

#endif
