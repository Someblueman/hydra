#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
void render_hosts(struct app *app) {
    struct tv_canvas c;
    struct tv_cell *cells;
    int width = app->cols - 1, height = app->limit - app->line, row, available, split;
    size_t i, start;
    if (!app->fleet) { linef(app, "Remote host observations are available in hydra fleet tui."); return; }
    if (!app->model.host_count) { linef(app, "No host observations. Check remote configuration and Recovery."); return; }
    if (width > 300) width = 300;
    if (height > 120) height = 120;
    if (width < 38 || height < 6) { linef(app, "More space needed / Esc heads"); return; }
    if (app->host_selected >= app->model.host_count) app->host_selected = 0;
    cells = malloc((size_t)width * (size_t)height * sizeof(*cells));
    if (!cells) { linef(app, "Host view allocation unavailable"); return; }
    (void)tv_init(&c, cells, (size_t)width * (size_t)height, width, height, !app->ascii);
    split = width >= 110 ? width * 3 / 5 : width;
    tv_panel(&c, (struct tv_rect){0, 0, split, height}, "HOSTS / latest bounded list response");
    available = height - 5;
    start = app->host_selected >= (size_t)available ? app->host_selected - (size_t)available + 1 : 0;
    for (i = start, row = 2; i < app->model.host_count && row < height - 3; i++, row++) {
        const struct host_observation *host = &app->model.hosts[i];
        char count[24];
        enum tv_style tone = i == app->host_selected ? TV_SELECTED :
            (!strcmp(host->state, "failed") || !strcmp(host->freshness, "stale") || !strcmp(host->connection, "unreachable")) ? TV_WARNING : TV_BASE;
        if (!strcmp(host->state, "failed")) copy_text(count, sizeof(count), "--");
        else snprintf(count, sizeof(count), "%u", host->heads);
        {
            int name_width = split - 39;
            if (name_width < 1) name_width = 1;
            dashboard_text(&c, 2, row, split - 4, tone, "%c %-*.*s %-9s %s heads %s", i == app->host_selected ? '>' : ' ',
                           name_width, name_width, host->name, host->state, count, host->freshness);
        }
        app->hit_rows[app->hit_count] = app->line + row + 1;
        app->hit_bottom[app->hit_count] = app->line + row + 1;
        app->hit_left[app->hit_count] = 3; app->hit_right[app->hit_count] = split - 2;
        app->hit_items[app->hit_count++] = i;
    }
    dashboard_text(&c, 2, height - 2, split - 4, TV_BORDER, "%zu hosts / Enter filters heads / j k select", app->model.host_count);
    if (split < width) {
        const struct host_observation *host = &app->model.hosts[app->host_selected];
        tv_panel(&c, (struct tv_rect){split + 1, 0, width - split - 1, height}, "HOST DETAIL");
        dashboard_text(&c, split + 3, 2, width - split - 5, TV_STRONG, "%s", host->name);
        dashboard_text(&c, split + 3, 4, width - split - 5, TV_BASE, "List response: %s", host->state);
        dashboard_text(&c, split + 3, 5, width - split - 5, TV_BASE, "Connection: %s", host->connection);
        dashboard_text(&c, split + 3, 6, width - split - 5, strcmp(host->freshness, "fresh") ? TV_WARNING : TV_BASE,
                       "Observation: %s / age %s", host->freshness, host->age[0] ? host->age : "-");
        dashboard_text(&c, split + 3, 7, width - split - 5, TV_WARNING, "%s", host->error);
        if (height > 10) {
            dashboard_text(&c, split + 3, 9, width - split - 5, TV_BASE, "Last confirmed: %s", host->last_confirmed);
            dashboard_text(&c, split + 3, 10, width - split - 5, TV_BASE, "%u task observations", host->tasks);
            dashboard_text(&c, split + 3, 11, width - split - 5, TV_BASE, "Process liveness: unobserved");
            dashboard_text(&c, split + 3, 12, width - split - 5, TV_BASE, "CPU / memory / load: unavailable");
            dashboard_text(&c, split + 3, 13, width - split - 5, TV_BASE, "An empty host still has a row.");
            if (host->tasks && app->model.task_count) {
                size_t task_index;
                for (task_index = 0; task_index < app->model.task_count; task_index++) {
                    const struct task_observation *task = &app->model.tasks[task_index];
                    if (strcmp(task->host, host->name)) continue;
                    dashboard_text(&c, split + 3, 15, width - split - 5, TV_BASE, "Task: %s / %s", task->task_id, task->state);
                    dashboard_text(&c, split + 3, 16, width - split - 5, TV_WARNING, "Wait: %s / %s", task->waiting_reason, task->next_action);
                    break;
                }
            }
        }
    } else {
        dashboard_text(&c, 2, height - 3, split - 4, TV_WARNING, "%s", app->model.hosts[app->host_selected].error);
    }
    for (row = 0; row < height; row++) {
        (void)tv_write_row(&c, row, stdout, dashboard_style, app); putchar('\n'); app->line++;
    }
    style(app, TONE_BASE); free(cells);
}
