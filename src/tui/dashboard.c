#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Hydra maps observations to reusable widgets. These functions do not collect
 * data or infer remote liveness from desired state. */
void dashboard_style(void *context, enum tv_style tone) {
    style(context, (enum tone)tone);
}

void dashboard_text(struct tv_canvas *c, int x, int y, int width,
                           enum tv_style tone, const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format); vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    tv_text(c, (struct tv_rect){x, y, width, 1}, buffer, tone);
}

void dashboard_card(struct tv_canvas *c, int x, int width, const char *title,
                           size_t count, const char *caption, enum tv_style tone) {
    tv_panel(c, (struct tv_rect){x, 0, width, 5}, title);
    if (count == SIZE_MAX) dashboard_text(c, x + 2, 1, width - 4, tone, "--");
    else dashboard_text(c, x + 2, 1, width - 4, tone, "%zu", count);
    dashboard_text(c, x + 2, 3, width - 4, TV_BASE, "%s", caption);
}

static void dashboard_heads(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    size_t i, ordinal = 0, selected = 0, count = 0, start;
    int row = r.y + 2, available = r.height - 4;
    tv_panel(c, r, app->fleet ? "REMOTE HEADS / durable state" : "HEADS / observed session state");
    retarget_selection(app);
    for (i = 0; i < app->model.head_count; i++) if (head_matches(&app->model.heads[i], app->search)) {
        if (i == app->selected) selected = count;
        count++;
    }
    if (available < 1) return;
    start = selected >= (size_t)available ? selected - (size_t)available + 1 : 0;
    for (i = 0; i < app->model.head_count && row < r.y + r.height - 2; i++) {
        const struct head *h = &app->model.heads[i];
        enum tv_style tone;
        if (!head_matches(h, app->search) || ordinal++ < start) continue;
        tone = i == app->selected ? TV_SELECTED : strcmp(display_status(h), "LIVE") == 0 ? TV_BASE : TV_WARNING;
        dashboard_text(c, r.x + 2, row, r.width - 4, tone, "%c %-*.*s %s", i == app->selected ? '>' : ' ',
                       r.width - 21, r.width - 21, h->branch, app->fleet ? h->desired : display_status(h));
        if (app->hit_count < MAX_HEADS) {
            app->hit_rows[app->hit_count] = app->line + row + 1;
            app->hit_bottom[app->hit_count] = app->line + row + 1;
            app->hit_left[app->hit_count] = r.x + 3;
            app->hit_right[app->hit_count] = r.x + r.width - 2;
            app->hit_items[app->hit_count++] = i;
        }
        row++;
    }
    dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER,
                   count ? "%zu matching / j k select / Enter details" : "No matching heads", count);
}

static void dashboard_detail(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    const struct head *h = selected_head(app);
    tv_panel(c, r, "INSPECT / selected head");
    if (!h) { dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_BASE, "No head selected"); return; }
    dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_STRONG, "%s", h->branch);
    dashboard_text(c, r.x + 2, r.y + 4, r.width - 4, TV_BASE, "%s: %s", app->fleet ? "Host" : "Agent", app->fleet ? h->remote_host : h->profile);
    dashboard_text(c, r.x + 2, r.y + 5, r.width - 4, TV_BASE, "Observed: %s", display_status(h));
    if (r.height < 9) return;
    if (app->fleet) {
        dashboard_text(c, r.x + 2, r.y + 6, r.width - 4, TV_BASE, "Desired: %s", h->desired);
        dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, TV_WARNING, "CPU / memory: unavailable");
        if (r.height > 10) dashboard_text(c, r.x + 2, r.y + 9, r.width - 4, TV_BASE, "%s", h->remote_project);
    } else {
        dashboard_text(c, r.x + 2, r.y + 6, r.width - 4, TV_BASE, "Changes %u   Messages %u", h->diff, h->messages);
        dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, TV_BASE, "Gates %u / %u approved", h->approved, h->gates);
        if (r.height > 10) tv_bar(c, (struct tv_rect){r.x + 2, r.y + 9, r.width - 4, 1}, h->approved, h->gates, TV_BORDER);
    }
}

static void dashboard_charts(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    size_t i;
    double maximum = 1;
    tv_panel(c, r, app->fleet ? "HOST OBSERVATIONS" : "QUEUE DEPTH / known heads");
    if (app->fleet) {
        int row = r.y + 2;
        if (app->model.task_count) {
            tv_panel(c, r, "REMOTE TASKS / receiver-owned observations");
            for (i = 0; i < app->model.task_count && row < r.y + r.height - 2; i++) {
                const struct task_observation *task = &app->model.tasks[i];
                dashboard_text(c, r.x + 2, row++, r.width - 4, !strcmp(task->freshness, "stale") ? TV_WARNING : TV_BASE,
                               "%s / %s / owner %s / %s", task->task_id, task->state, task->owner, task->freshness);
                if (row < r.y + r.height - 2)
                    dashboard_text(c, r.x + 4, row++, r.width - 6, !strcmp(task->waiting_reason, "none") ? TV_BASE : TV_WARNING,
                                   "wait %s / %s / next: %s", task->waiting_reason, task->waiting_detail, task->next_action);
            }
            dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER,
                           "%zu task observations / owner state and freshness are receiver evidence", app->model.task_count);
            return;
        }
        for (i = 0; i < app->model.host_count && row < r.y + r.height - 2; i++) {
            const struct host_observation *host = &app->model.hosts[i];
            if (!strcmp(host->state, "failed")) dashboard_text(c, r.x + 2, row++, r.width - 4, TV_WARNING,
                           "%s / failed / -- heads / %s / %s", host->name, host->error, host->freshness);
            else dashboard_text(c, r.x + 2, row++, r.width - 4, TV_BASE,
                           "%s / responded / %u heads / %s", host->name, host->heads, host->freshness);
        }
        dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER, "H hosts / %zu total / CPU and memory unavailable", app->model.host_count);
        if (row == r.y + 2) dashboard_text(c, r.x + 2, row, r.width - 4, TV_WARNING, "No host observations; inspect Recovery");
        return;
    }
    for (i = 0; i < app->history_count; i++) if (app->history_valid[i] && app->queue_history[i] > maximum) maximum = app->queue_history[i];
    dashboard_text(c, r.x + 2, r.y + 1, r.width - 4, TV_BORDER, "0..%.0f entries / %zu samples", maximum, app->history_count);
    dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_BORDER, "Known heads only / gaps = unavailable");
    if (app->history_count < 2) {
        dashboard_text(c, r.x + 2, r.y + 3, r.width - 4, TV_BASE, "Collecting history; waiting for next refresh");
    } else tv_plot(c, (struct tv_rect){r.x + 2, r.y + 3, r.width - 4, r.height - 5},
                   app->queue_history, app->history_valid, app->history_count, maximum, TV_BORDER);
}

static void dashboard_distribution(struct app *app, struct tv_canvas *c, struct tv_rect r, bool changes) {
    static const char *labels[] = {"LIVE", "STALE", "STOPPED", "UNAVAILABLE"};
    size_t counts[4] = {0}, i, j, unknown = 0;
    unsigned maximum = 1;
    int row = r.y + 2, bar = r.width / 3;
    tv_panel(c, r, changes ? "CHANGED FILES / by head" : "SESSION STATES / snapshot");
    for (i = 0; i < app->model.head_count; i++) {
        const struct head *h = &app->model.heads[i];
        for (j = 0; j < 4; j++) if (!strcmp(display_status(h), labels[j])) counts[j]++;
        if (h->diff > maximum) maximum = h->diff;
        if (!h->head_id[0]) unknown++;
    }
    if (!changes) {
        for (i = 0; i < 4 && row < r.y + r.height - 2; i++, row += 2) {
            dashboard_text(c, r.x + 2, row, r.width - bar - 7, i ? TV_WARNING : TV_BASE, "%s %zu", labels[i], counts[i]);
            tv_bar(c, (struct tv_rect){r.x + r.width - bar - 2, row, bar, 1}, counts[i],
                   app->model.head_count ? app->model.head_count : 1, i ? TV_WARNING : TV_BORDER);
        }
    } else {
        for (i = 0; i < app->model.head_count && row < r.y + r.height - 2; i++, row += 2) {
            const struct head *h = &app->model.heads[i];
            dashboard_text(c, r.x + 2, row, r.width - bar - 7, TV_BASE, "%s", h->branch);
            if (h->head_id[0]) {
                dashboard_text(c, r.x + r.width - bar - 6, row, 4, TV_STRONG, "%u", h->diff);
                tv_bar(c, (struct tv_rect){r.x + r.width - bar - 2, row, bar, 1}, h->diff, maximum, TV_BORDER);
            } else dashboard_text(c, r.x + r.width - bar - 2, row, bar, TV_WARNING, "unknown");
        }
    }
    dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER,
                   changes ? "0..%u files / %zu unknown / j k select" : "%u total heads / snapshot counts", changes ? maximum : (unsigned)app->model.head_count, unknown);
}

void render_dashboard(struct app *app) {
    struct tv_canvas c;
    struct tv_cell *cells;
    size_t i, live = 0, attention = 0, gates = 0;
    int width = app->cols - 1, height = app->limit - app->line, row, half, middle;
    if (width > 300) width = 300;
    if (height > 120) height = 120;
    if (width < 38 || height < 7) { linef(app, "Overview needs more space; v views"); return; }
    cells = malloc((size_t)width * (size_t)height * sizeof(*cells));
    if (!cells) { linef(app, "Overview allocation unavailable"); return; }
    (void)tv_init(&c, cells, (size_t)width * (size_t)height, width, height, !app->ascii);
    for (i = 0; i < app->model.head_count; i++) {
        const struct head *h = &app->model.heads[i];
        if (strcmp(display_status(h), "LIVE") == 0) live++; else attention++;
        gates += h->gates >= h->approved ? h->gates - h->approved : 0;
    }
    if (width >= 90 && height >= 20) {
        int card = width / 4;
        dashboard_card(&c, 0, card - 1, "HEADS", app->model.head_count, "current snapshot", TV_STRONG);
        dashboard_card(&c, card, card - 1, app->fleet ? "UNOBSERVED" : "LIVE", app->fleet ? attention : live, app->fleet ? "remote liveness" : "local sessions", TV_STRONG);
        dashboard_card(&c, card * 2, card - 1, "RECOVERY", app->model.recovery_count, "findings to inspect", TV_WARNING);
        dashboard_card(&c, card * 3, width - card * 3, "GATES", app->fleet ? SIZE_MAX : gates, app->fleet ? "unavailable remotely" : "pending approval", TV_WARNING);
        half = width * 3 / 5;
        middle = height / 2;
        if (middle > 12 && app->model.head_count < 8) middle = 12;
        if (app->fleet && app->model.task_count && height - middle - 7 < 6) middle = 10;
        if (middle < 10) middle = 10;
        dashboard_heads(app, &c, (struct tv_rect){0, 6, half - 1, middle});
        dashboard_detail(app, &c, (struct tv_rect){half, 6, width - half, middle});
        if (height - middle - 7 >= 6) {
            int bottom = middle + 7, remaining = height - bottom, third = width / 3;
            if (!app->fleet && remaining >= 11 && width >= 120) {
                dashboard_charts(app, &c, (struct tv_rect){0, bottom, third - 1, remaining});
                dashboard_distribution(app, &c, (struct tv_rect){third, bottom, third - 1, remaining}, false);
                dashboard_distribution(app, &c, (struct tv_rect){third * 2, bottom, width - third * 2, remaining}, true);
            } else dashboard_charts(app, &c, (struct tv_rect){0, bottom, width, remaining});
        }
    } else if (width >= 70 && height >= 16) {
        int list_height = height / 2;
        if (app->model.head_count + 4 < (size_t)list_height) list_height = (int)app->model.head_count + 4;
        if (list_height < 6) list_height = 6;
        if (app->fleet) dashboard_text(&c, 1, 0, width - 2, TV_STRONG, "%zu remote heads / liveness unobserved / %zu recovery",
                                      app->model.head_count, app->model.recovery_count);
        else dashboard_text(&c, 1, 0, width - 2, TV_STRONG, "%zu heads / %zu live / %zu recovery / %zu pending gates",
                            app->model.head_count, live, app->model.recovery_count, gates);
        dashboard_heads(app, &c, (struct tv_rect){0, 2, width, list_height});
        dashboard_charts(app, &c, (struct tv_rect){0, list_height + 3, width, height - list_height - 3});
    } else {
        if (app->fleet) dashboard_text(&c, 1, 0, width - 2, TV_STRONG, "%zu remote heads / liveness unobserved", app->model.head_count);
        else dashboard_text(&c, 1, 0, width - 2, TV_STRONG, "%zu heads / %zu live / %zu recovery", app->model.head_count, live, app->model.recovery_count);
        dashboard_heads(app, &c, (struct tv_rect){0, 2, width, height - 2});
    }
    for (row = 0; row < height; row++) {
        (void)tv_write_row(&c, row, stdout, dashboard_style, app);
        putchar('\n'); app->line++;
    }
    style(app, TONE_BASE);
    free(cells);
}
