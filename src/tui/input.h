#ifndef HYDRA_TUI_INPUT_H
#define HYDRA_TUI_INPUT_H
#include "app.h"
/* Borrow app for the terminal session; app must survive process exit cleanup. */
int interactive_main(struct app *app);

#endif
