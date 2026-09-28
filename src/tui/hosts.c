#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"

static enum tv_style host_tone(const struct host_observation *host, bool selected) {
    if (selected) return TV_SELECTED;
    if (!strcmp(host->state, "failed") || !strcmp(host->freshness, "stale") || !strcmp(host->connection, "unreachable")) return TV_WARNING;
    return TV_BASE;
}

static void render_host_row(struct app *app, struct tv_canvas *canvas, const struct host_observation *host,
                            size_t index, int row, int split) {
    char count[24]; int name_width = split - 39;
    if (!strcmp(host->state, "failed")) copy_text(count, sizeof(count), "--");
    else snprintf(count, sizeof(count), "%u", host->heads);
    if (name_width < 1) name_width = 1;
    dashboard_text(canvas, 2, row, split - 4, host_tone(host, index == app->host_selected), "%c %-*.*s %-9s %s heads %s",
                   index == app->host_selected ? '>' : ' ', name_width, name_width, host->name, host->state, count, host->freshness);
    app->hit_rows[app->hit_count] = app->line + row + 1; app->hit_bottom[app->hit_count] = app->line + row + 1;
    app->hit_left[app->hit_count] = app->content_x + 3; app->hit_right[app->hit_count] = app->content_x + split - 2; app->hit_items[app->hit_count++] = index;
}

static void render_host_task(struct app *app, struct tv_canvas *canvas, const struct host_observation *host, int split, int width) {
    size_t i;
    if (!host->tasks || !app->model.task_count) return;
    for (i = 0; i < app->model.task_count; i++) {
        const struct task_observation *task = &app->model.tasks[i];
        if (strcmp(task->host, host->name)) continue;
        dashboard_text(canvas, split + 3, 15, width - split - 5, TV_BASE, "Task: %s / %s", task->task_id, task->state);
        dashboard_text(canvas, split + 3, 16, width - split - 5, TV_WARNING, "Wait: %s / %s", task->waiting_reason, task->next_action);
        break;
    }
}

static void render_host_detail(struct app *app, struct tv_canvas *canvas, const struct host_observation *host, int split, int width, int height) {
    tv_panel(canvas, (struct tv_rect){split + 1, 0, width - split - 1, height}, "HOST DETAIL");
    dashboard_text(canvas, split + 3, 2, width - split - 5, TV_STRONG, "%s", host->name);
    dashboard_text(canvas, split + 3, 4, width - split - 5, TV_BASE, "List response: %s", host->state);
    dashboard_text(canvas, split + 3, 5, width - split - 5, TV_BASE, "Connection: %s", host->connection);
    dashboard_text(canvas, split + 3, 6, width - split - 5, strcmp(host->freshness, "fresh") ? TV_WARNING : TV_BASE,
                   "Observation: %s / age %s", host->freshness, host->age[0] ? host->age : "-");
    dashboard_text(canvas, split + 3, 7, width - split - 5, TV_WARNING, "%s", host->error);
    if (height <= 10) return;
    dashboard_text(canvas, split + 3, 9, width - split - 5, TV_BASE, "Last confirmed: %s", host->last_confirmed);
    dashboard_text(canvas, split + 3, 10, width - split - 5, TV_BASE, "%u task observations", host->tasks);
    dashboard_text(canvas, split + 3, 11, width - split - 5, TV_BASE, "Process liveness: unobserved");
    dashboard_text(canvas, split + 3, 12, width - split - 5, TV_BASE, "CPU / memory / load: unavailable");
    dashboard_text(canvas, split + 3, 13, width - split - 5, TV_BASE, "An empty host still has a row.");
    render_host_task(app, canvas, host, split, width);
}

/* Setup rows and the "Add a host" row below the observed hosts. */
static void render_setup_row(struct app *app, struct tv_canvas *canvas, const struct host_row *host_row, size_t index, int row, int split) {
    char name[160], state[200];
    int name_width = split - 39;
    bool selected = index == app->host_selected;
    setup_row_text(app, host_row, name, sizeof(name), state, sizeof(state));
    if (name_width < 12) name_width = 12;
    dashboard_text(canvas, 2, row, split - 4, selected ? TV_SELECTED : host_row->kind == HOST_ROW_ADD ? TV_STRONG : TV_WARNING,
                   "%c %-*.*s %s", selected ? '>' : ' ', name_width, name_width, name, state);
    app->hit_rows[app->hit_count] = app->line + row + 1; app->hit_bottom[app->hit_count] = app->line + row + 1;
    app->hit_left[app->hit_count] = app->content_x + 3; app->hit_right[app->hit_count] = app->content_x + split - 2; app->hit_items[app->hit_count++] = index;
}

static void render_setup_detail(struct app *app, struct tv_canvas *canvas, const struct host_row *row, int split, int width, int height) {
    char name[160], state[200];
    setup_row_text(app, row, name, sizeof(name), state, sizeof(state));
    tv_panel(canvas, (struct tv_rect){split + 1, 0, width - split - 1, height}, row->kind == HOST_ROW_ADD ? "ADD A HOST" : "UNFINISHED SETUP");
    dashboard_text(canvas, split + 3, 2, width - split - 5, TV_STRONG, "%s", name);
    dashboard_text(canvas, split + 3, 4, width - split - 5, TV_BASE, "%s", state);
    dashboard_text(canvas, split + 3, 6, width - split - 5, TV_BASE, "%s", row->kind == HOST_ROW_ADD
                   ? "Hydra checks the SSH host key and requirements," : "This host is not in the fleet yet.");
    dashboard_text(canvas, split + 3, 7, width - split - 5, TV_BASE, "%s", row->kind == HOST_ROW_ADD
                   ? "installs Hydra and helps with agents; you approve" : "Enter shows its steps and continues setup;");
    dashboard_text(canvas, split + 3, 8, width - split - 5, TV_BASE, "%s", row->kind == HOST_ROW_ADD
                   ? "every change first." : "you approve every change first.");
}

void render_hosts(struct app *app) {
    struct tv_canvas c;
    struct host_row rows[MAX_HEADS];
    int width, height, row, available, split;
    size_t i, start, count;
    if (!app->fleet) { render_setup_hub(app); return; }
    count = hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
    if (!frame_content(app, &c)) return;
    width = c.width; height = c.height;
    if (width > 300) width = 300;
    if (height > 120) height = 120;
    if (width < 38 || height < 6) { linef(app, "More space needed"); return; }
    if (app->host_selected >= count) app->host_selected = 0;
    split = width >= 110 ? width * 3 / 5 : width;
    tv_panel(&c, (struct tv_rect){0, 0, split, height}, "HOSTS / latest list response");
    if (!app->model.host_count) dashboard_text(&c, 2, 1, split - 4, TV_MUTED, "No host observations yet; A adds a host.");
    available = height - 5;
    start = app->host_selected >= (size_t)available ? app->host_selected - (size_t)available + 1 : 0;
    for (i = start, row = 2; i < count && row < height - 3; i++, row++) {
        if (rows[i].kind == HOST_ROW_HOST) render_host_row(app, &c, &app->model.hosts[rows[i].index], i, row, split);
        else render_setup_row(app, &c, &rows[i], i, row, split);
    }
    dashboard_text(&c, 2, height - 2, split - 4, TV_MUTED, "%zu hosts / Enter opens / A adds a host", app->model.host_count);
    if (rows[app->host_selected].kind != HOST_ROW_HOST) {
        if (split < width) render_setup_detail(app, &c, &rows[app->host_selected], split, width, height);
    } else if (split < width) {
        render_host_detail(app, &c, &app->model.hosts[rows[app->host_selected].index], split, width, height);
    } else {
        dashboard_text(&c, 2, height - 3, split - 4, TV_WARNING, "%s", app->model.hosts[rows[app->host_selected].index].error);
    }
    app->line = app->limit;
}
