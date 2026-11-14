#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "setup_flow.h"

static struct kt_style host_tone(const struct host_observation *host, bool selected) {
    if (selected) return KT_SELECTED;
    if (!strcmp(host->state, "failed") || !strcmp(host->freshness, "stale") || !strcmp(host->connection, "unreachable")) return KT_WARNING;
    return KT_BASE;
}

static void render_host_row(struct app *app, struct kt_canvas *canvas, const struct host_observation *host,
                            size_t index, int row, int split) {
    char count[24], state[TEXT + 128]; int name_width = split - 60;
    bool failed = strcmp(host->state, "responded") != 0;
    if (failed && !host->retained) copy_text(count, sizeof(count), "--");
    else snprintf(count, sizeof(count), "%u", host->heads);
    if (name_width < 12) name_width = 12;
    if (name_width > 18) name_width = 18;
    host_state_text(app, host, state, sizeof(state));
    canvas_text(canvas, 2, row, split - 4, app->remote_stale ? KT_WARNING : host_tone(host, index == app->host_selected), "%c %-*.*s %-12s %2s heads  %s",
                   index == app->host_selected ? '>' : ' ', name_width, name_width, host->name, failed ? "disconnected" : "connected", count, state);
    app->hit_rows[app->hit_count] = app->line + row + 1; app->hit_bottom[app->hit_count] = app->line + row + 1;
    app->hit_left[app->hit_count] = app->content_x + 3; app->hit_right[app->hit_count] = app->content_x + split - 2; app->hit_items[app->hit_count++] = index;
}

static void render_host_task(struct app *app, struct kt_canvas *canvas, const struct host_observation *host, int split, int width) {
    size_t i;
    if (!host->tasks || !app->model.task_count) return;
    for (i = 0; i < app->model.task_count; i++) {
        const struct task_observation *task = &app->model.tasks[i];
        if (strcmp(task->host, host->name)) continue;
        canvas_text(canvas, split + 3, 15, width - split - 5, KT_BASE, "Task: %s / %s", task->task_id, task->state);
        canvas_text(canvas, split + 3, 16, width - split - 5, KT_WARNING, "Wait: %s / %s", task->waiting_reason, task->next_action);
        break;
    }
}

static void render_host_detail(struct app *app, struct kt_canvas *canvas, const struct host_observation *host, int split, int width, int height) {
    bool failed = strcmp(host->state, "responded") != 0;
    char state[TEXT + 128];
    host_state_text(app, host, state, sizeof(state));
    pane_rule(canvas, (struct kt_rect){split + 1, 0, width - split - 1, height}, "HOST DETAIL");
    canvas_text(canvas, split + 3, 2, width - split - 5, KT_STRONG, "%s", host->name);
    canvas_text(canvas, split + 3, 4, width - split - 5, KT_BASE, "List response: %s", host->state);
    canvas_text(canvas, split + 3, 5, width - split - 5, failed || app->remote_stale ? KT_WARNING : KT_BASE, "Connection: %s%s", failed ? "disconnected; " : "", state);
    canvas_text(canvas, split + 3, 6, width - split - 5, strcmp(host->freshness, "fresh") ? KT_WARNING : KT_BASE,
                   "Observation: %s / age %s%s", host->freshness, host->age[0] ? host->age : "-", host->retained ? " / rows retained" : "");
    if (host->error[0] && strcmp(host->error, "-")) canvas_text(canvas, split + 3, 7, width - split - 5, KT_WARNING, "%s", host->error);
    if (height <= 10) return;
    canvas_text(canvas, split + 3, 9, width - split - 5, KT_BASE, "Last confirmed: %s", host->last_confirmed);
    canvas_text(canvas, split + 3, 10, width - split - 5, KT_BASE, "%u task observations", host->tasks);
    canvas_text(canvas, split + 3, 11, width - split - 5, KT_BASE, "Process liveness: unobserved");
    canvas_text(canvas, split + 3, 12, width - split - 5, KT_BASE, "CPU / memory / load: unavailable");
    canvas_text(canvas, split + 3, 13, width - split - 5, failed ? KT_WARNING : KT_MUTED, "%s", failed
                   ? "Disconnected: rows are the last observation; r retries." : "Enter shows this host's work; r refreshes it.");
    render_host_task(app, canvas, host, split, width);
}

/* Setup rows and the "Add a host" row below the observed hosts. */
static void render_setup_row(struct app *app, struct kt_canvas *canvas, const struct host_row *host_row, size_t index, int row, int split) {
    char name[160], state[200];
    int name_width = split - 60;
    bool selected = index == app->host_selected;
    setup_row_text(app, host_row, name, sizeof(name), state, sizeof(state));
    if (name_width < 12) name_width = 12;
    if (name_width > 18) name_width = 18;
    canvas_text(canvas, 2, row, split - 4, selected ? KT_SELECTED : host_row->kind == HOST_ROW_ADD ? KT_STRONG : KT_WARNING,
                   "%c %-*.*s %s", selected ? '>' : ' ', name_width, name_width, name, state);
    app->hit_rows[app->hit_count] = app->line + row + 1; app->hit_bottom[app->hit_count] = app->line + row + 1;
    app->hit_left[app->hit_count] = app->content_x + 3; app->hit_right[app->hit_count] = app->content_x + split - 2; app->hit_items[app->hit_count++] = index;
}

/* A finished setup whose host runs another Hydra: Enter shows the upgrade plan. */
static void render_upgrade_detail(struct kt_canvas *canvas, const char *name, const char *state, int split, int width, int height) {
    pane_rule(canvas, (struct kt_rect){split + 1, 0, width - split - 1, height}, "HYDRA UPGRADE");
    canvas_text(canvas, split + 3, 2, width - split - 5, KT_STRONG, "%s", name);
    canvas_text(canvas, split + 3, 4, width - split - 5, KT_WARNING, "%s", state);
    canvas_text(canvas, split + 3, 6, width - split - 5, KT_BASE, "%s", "Enter shows the upgrade plan: a new pinned install");
    canvas_text(canvas, split + 3, 7, width - split - 5, KT_BASE, "%s", "beside the old one, which stays; after a handshake");
    canvas_text(canvas, split + 3, 8, width - split - 5, KT_BASE, "%s", "the host switches to it. You approve it first.");
}

static void render_setup_detail(struct app *app, struct kt_canvas *canvas, const struct host_row *row, int split, int width, int height) {
    char name[160], state[200];
    setup_row_text(app, row, name, sizeof(name), state, sizeof(state));
    if (row->kind == HOST_ROW_SETUP && app->setup->records[row->index].upgrade) {
        render_upgrade_detail(canvas, name, state, split, width, height);
        return;
    }
    pane_rule(canvas, (struct kt_rect){split + 1, 0, width - split - 1, height}, row->kind == HOST_ROW_ADD ? "ADD A HOST" : "UNFINISHED SETUP");
    canvas_text(canvas, split + 3, 2, width - split - 5, KT_STRONG, "%s", name);
    canvas_text(canvas, split + 3, 4, width - split - 5, KT_BASE, "%s", state);
    canvas_text(canvas, split + 3, 6, width - split - 5, KT_BASE, "%s", row->kind == HOST_ROW_ADD
                   ? "Hydra checks the SSH host key and requirements," : "This host is not in the fleet yet.");
    canvas_text(canvas, split + 3, 7, width - split - 5, KT_BASE, "%s", row->kind == HOST_ROW_ADD
                   ? "installs Hydra and helps with agents; you approve" : "Enter shows its steps and continues setup;");
    canvas_text(canvas, split + 3, 8, width - split - 5, KT_BASE, "%s", row->kind == HOST_ROW_ADD
                   ? "every change first." : "you approve every change first.");
    if (row->kind == HOST_ROW_SETUP)
        canvas_text(canvas, split + 3, 10, width - split - 5, KT_MUTED, "%s", "e edits it, x removes its record.");
}

void render_hosts(struct app *app) {
    struct kt_canvas c;
    struct host_row rows[MAX_HEADS];
    int width, height, row, available, split;
    size_t i, start, count;
    if (!app->model.host_count) { render_setup_hub(app); return; }
    count = hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
    if (!frame_content(app, &c)) return;
    width = c.width; height = c.height;
    if (width > 300) width = 300;
    if (height > 120) height = 120;
    if (width < 38 || height < 6) { linef(app, "More space needed"); return; }
    if (app->host_selected >= count) app->host_selected = 0;
    split = width >= 110 ? width * 3 / 5 : width;
    pane_rule(&c, (struct kt_rect){0, 0, split, height}, app->remote_stale ? "HOSTS / STALE: last list response" : "HOSTS / latest list response");
    if (app->remote_stale) canvas_text(&c, 2, 1, split - 4, KT_WARNING, "Remote observation stale (%lds old): %s; r retries", remote_age(app), app->remote_error);
    available = height - 5;
    start = app->host_selected >= (size_t)available ? app->host_selected - (size_t)available + 1 : 0;
    for (i = start, row = 2; i < count && row < height - 3; i++, row++) {
        if (rows[i].kind == HOST_ROW_HOST) render_host_row(app, &c, &app->model.hosts[rows[i].index], i, row, split);
        else render_setup_row(app, &c, &rows[i], i, row, split);
    }
    canvas_text(&c, 2, height - 2, split - 4, KT_MUTED, "%zu hosts / Enter follows its work / r retries / A adds a host", app->model.host_count);
    if (rows[app->host_selected].kind != HOST_ROW_HOST) {
        if (split < width) render_setup_detail(app, &c, &rows[app->host_selected], split, width, height);
    } else if (split < width) {
        render_host_detail(app, &c, &app->model.hosts[rows[app->host_selected].index], split, width, height);
    } else {
        canvas_text(&c, 2, height - 3, split - 4, KT_WARNING, "%s", app->model.hosts[rows[app->host_selected].index].error);
    }
    app->line = app->limit;
}
