#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Execution location is an attribute of every row and one filter over the
 * same views: all work, local work, remote work, or one host. The filter
 * never changes what Hydra knows; it changes what the list shows. */

bool location_remote_only(const struct app *app) {
    return app->location[0] && strcmp(app->location, "local");
}

static bool head_in_location(const struct app *app, const struct head *h) {
    if (!app->location[0]) return true;
    if (!strcmp(app->location, "local")) return !head_remote(h);
    if (!strcmp(app->location, "remote")) return head_remote(h);
    return !strcmp(app->location, h->remote_host);
}

static bool task_in_location(const struct app *app, const struct task_observation *t) {
    if (!app->location[0] || !strcmp(app->location, "remote")) return true;
    if (!strcmp(app->location, "local")) return false;
    return !strcmp(app->location, t->host);
}

bool head_visible(const struct app *app, const struct head *h) {
    return head_in_location(app, h) && head_matches(h, app->search);
}

bool task_matches(const struct task_observation *t, const char *search) {
    if (!search[0]) return true;
    return text_matches(t->task_id, search) || text_matches(t->host, search) ||
        text_matches(t->profile, search) || text_matches(t->workspace, search);
}

bool task_visible(const struct app *app, const struct task_observation *t) {
    return task_in_location(app, t) && task_matches(t, app->search);
}

const struct task_observation *selected_task(const struct app *app) {
    if (!app->task_row || app->task_selected >= app->model.task_count) return NULL;
    if (!task_visible(app, &app->model.tasks[app->task_selected])) return NULL;
    return &app->model.tasks[app->task_selected];
}

const char *location_label(const struct app *app, char *out, size_t size) {
    if (!app->location[0]) return "all work";
    if (!strcmp(app->location, "local")) return "local work";
    if (!strcmp(app->location, "remote")) return "remote work";
    snprintf(out, size, "host %.100s", app->location);
    return out;
}

void location_set(struct app *app, const char *value) {
    char label[160];
    copy_text(app->location, sizeof(app->location), value ? value : "");
    app->run_row = false;
    if (app->task_row && !selected_task(app)) app->task_row = false;
    retarget_selection(app);
    snprintf(app->notice, sizeof(app->notice), "Showing %s%sL cycles the location, Esc shows all", location_label(app, label, sizeof(label)), dot(app));
}

/* all -> local -> remote -> each observed host -> all. */
void location_cycle(struct app *app) {
    size_t i;
    if (!app->location[0]) { location_set(app, "local"); return; }
    if (!strcmp(app->location, "local")) { location_set(app, app->model.host_count ? "remote" : ""); return; }
    if (!strcmp(app->location, "remote")) { location_set(app, app->model.host_count ? app->model.hosts[0].name : ""); return; }
    for (i = 0; i < app->model.host_count; i++) if (!strcmp(app->model.hosts[i].name, app->location)) break;
    location_set(app, i + 1 < app->model.host_count ? app->model.hosts[i + 1].name : "");
}
