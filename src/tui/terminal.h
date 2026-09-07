#ifndef HYDRA_TUI_TERMINAL_H
#define HYDRA_TUI_TERMINAL_H
#include "app.h"
/* app stays caller-owned and alive through registered exit cleanup. */
void terminal_watch(struct app *app);
void terminal_pipe_signal(void);
bool terminal_stopped(void);
int terminal_exit_status(void);
void restore_terminal(struct app *app);
int enter_raw(struct app *app);
void update_size(struct app *app);

#endif
