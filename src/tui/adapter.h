#ifndef HYDRA_TUI_ADAPTER_H
#define HYDRA_TUI_ADAPTER_H
#include "app.h"
/* Borrow app; replace a snapshot only after complete successful parsing. */
int refresh_model(struct app *app);
void refresh_current_session(struct app *app);
void capture_preview(struct app *app);

#endif
