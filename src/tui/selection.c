#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
static bool contains_folded(const char *text, const char *search) {
    size_t length = strlen(search);
    if (length == 0U) return true;
    while (*text != '\0') {
        if (strncasecmp(text, search, length) == 0) return true;
        text++;
    }
    return false;
}
bool head_matches(const struct head *head, const char *search) {
    if (search[0] == '\0') return true;
    return contains_folded(head->branch, search) || contains_folded(head->session, search) ||
           contains_folded(head->group, search) || contains_folded(head->profile, search) ||
           contains_folded(head->remote_host, search) || contains_folded(head->remote_project, search);
}
struct head *selected_head(struct app *app) {
    if (app->selected >= app->model.head_count) return NULL;
    if (!head_matches(&app->model.heads[app->selected], app->search)) return NULL;
    return &app->model.heads[app->selected];
}
void retarget_selection(struct app *app) {
    size_t index;
    if (selected_head(app) != NULL) return;
    for (index = 0U; index < app->model.head_count; index++) {
        if (head_matches(&app->model.heads[index], app->search)) {
            app->selected = index;
            return;
        }
    }
    app->selected = app->model.head_count;
}
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool move_workspace_selection(struct app *app, int direction) {
    if (app->view == 8) { statistics_move(app, direction); return true; }
    if (app->view == 7) { native_workspace_move(app, direction); return true; }
    if (app->view == 5) { workflow_move(app, direction); return true; }
    if (app->view == 6) {
        if (direction > 0 && app->host_selected + 1 < app->model.host_count) app->host_selected++;
        if (direction < 0 && app->host_selected) app->host_selected--;
        return true;
    }
    if (app->fleet && app->model.task_count) {
        if (direction > 0 && app->task_selected + 1 < app->model.task_count) app->task_selected++;
        if (direction < 0 && app->task_selected) app->task_selected--;
        return true;
    }
    return false;
}

void move_selection(struct app *app, int direction) {
    size_t index;
    if (move_workspace_selection(app, direction)) return;
    if (app->view == 3) {
        if (direction > 0 && app->recovery_selected + 1U < app->model.recovery_count) app->recovery_selected++;
        else if (direction < 0 && app->recovery_selected > 0U) app->recovery_selected--;
        return;
    }
    retarget_selection(app);
    if (selected_head(app) == NULL) return;
    if (direction > 0) {
        for (index = app->selected + 1U; index < app->model.head_count; index++) {
            if (head_matches(&app->model.heads[index], app->search)) {
                app->selected = index;
                return;
            }
        }
        return;
    }
    index = app->selected;
    while (index > 0U) {
        index--;
        if (head_matches(&app->model.heads[index], app->search)) {
            app->selected = index;
            return;
        }
    }
}
size_t marked_index(const struct app *app, const char *branch) {
    size_t index;
    for (index = 0U; index < app->marked_count; index++) {
        if (strcmp(app->marked[index], branch) == 0) return index;
    }
    return MAX_HEADS;
}
void toggle_mark(struct app *app) {
    struct head *head = selected_head(app);
    size_t index;
    if (head == NULL) return;
    index = marked_index(app, head->branch);
    if (index < app->marked_count) {
        app->marked_count--;
        if (index < app->marked_count) {
            memmove(app->marked[index], app->marked[index + 1U],
                    (app->marked_count - index) * sizeof(app->marked[0]));
        }
    } else if (app->marked_count < MAX_HEADS) {
        copy_text(app->marked[app->marked_count++], TEXT, head->branch);
    }
}
void select_all_visible(struct app *app) {
    size_t index;
    app->marked_count = 0U;
    for (index = 0U; index < app->model.head_count; index++) {
        if (head_matches(&app->model.heads[index], app->search)) {
            copy_text(app->marked[app->marked_count++], TEXT, app->model.heads[index].branch);
        }
    }
}
const struct head *head_for_branch(const struct app *app, const char *branch) {
    size_t index;
    for (index = 0U; index < app->model.head_count; index++) {
        if (strcmp(app->model.heads[index].branch, branch) == 0) return &app->model.heads[index];
    }
    return NULL;
}
