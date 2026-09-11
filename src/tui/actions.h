#ifndef HYDRA_TUI_ACTIONS_H
#define HYDRA_TUI_ACTIONS_H
#include "app.h"
/* Borrow app and argv; shell CLI continues to own all mutations. */
int prompt_text(struct app *app, const char *prompt, char *buffer, size_t size);
void group_marked_action(struct app *app);
void kill_marked_action(struct app *app);
void execute_palette(struct app *app, const char *query);
void fleet_action(struct app *app);

#endif
