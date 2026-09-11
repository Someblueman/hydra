#ifndef HYDRA_TUI_ADAPTER_H
#define HYDRA_TUI_ADAPTER_H
#include "app.h"
/* Borrow app; replace a snapshot only after complete successful parsing. */
void refresh_current_session(struct app *app);
void capture_preview(struct app *app);

#endif
