#ifndef HYDRA_TUI_SELECTION_H
#define HYDRA_TUI_SELECTION_H
#include "app.h"
/* App/model are borrowed. Returned head pointers borrow the current model. */
bool head_matches(const struct head *head, const char *search);
struct head *selected_head(struct app *app);
void retarget_selection(struct app *app);
void move_selection(struct app *app, int direction);
size_t marked_index(const struct app *app, const char *branch);
void toggle_mark(struct app *app);
void select_all_visible(struct app *app);
const struct head *head_for_branch(const struct app *app, const char *branch);

#endif
