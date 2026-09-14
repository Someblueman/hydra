#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Hydra maps observations to reusable widgets. These functions do not collect
 * data or infer remote liveness from desired state. */
void dashboard_style(void *context, enum tv_style tone) {
    emit_style(context, (enum tone)tone);
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
    tv_panel(c, r, app->fleet ? "REMOTE HEADS / desired state" : "HEADS / what is running");
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
        tone = i == app->selected ? TV_SELECTED : TV_BASE;
        dashboard_text(c, r.x + 2, row, r.width - 4, tone, "%c %-*.*s", i == app->selected ? '>' : ' ',
                       r.width - 21, r.width - 21, h->branch);
        dashboard_text(c, r.x + r.width - 18, row, 15, i == app->selected ? TV_SELECTED : app->fleet ? TV_BASE : status_tone(h),
                       "%s", app->fleet ? h->desired : status_label(h));
        if (app->hit_count < MAX_HEADS) {
            app->hit_rows[app->hit_count] = app->line + row + 1;
            app->hit_bottom[app->hit_count] = app->line + row + 1;
            app->hit_left[app->hit_count] = app->content_x + r.x + 3;
            app->hit_right[app->hit_count] = app->content_x + r.x + r.width - 2;
            app->hit_items[app->hit_count++] = i;
        }
        row++;
    }
    dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER,
                   count ? "%zu heads / Enter opens details" : "No matching heads", count);
}

static void dashboard_detail(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    const struct head *h = selected_head(app);
    tv_panel(c, r, "SELECTED / what it needs");
    if (!h) { dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_BASE, "No head selected"); return; }
    dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_STRONG, "%s", h->branch);
    dashboard_text(c, r.x + 2, r.y + 4, r.width - 4, TV_BASE, "%s: %s", app->fleet ? "Host" : "Agent", app->fleet ? h->remote_host : h->profile);
    dashboard_text(c, r.x + 2, r.y + 5, r.width - 4, app->fleet ? TV_BASE : status_tone(h), "Session: %s", app->fleet ? h->desired : status_label(h));
    if (r.height < 9) return;
    if (app->fleet) {
        dashboard_text(c, r.x + 2, r.y + 6, r.width - 4, TV_BASE, "Desired: %s", h->desired);
        dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, TV_MUTED, "CPU / memory: not measured");
        if (r.height > 10) dashboard_text(c, r.x + 2, r.y + 9, r.width - 4, TV_BASE, "%s", h->remote_project);
    } else if (!h->head_id[0]) {
        dashboard_text(c, r.x + 2, r.y + 6, r.width - 4, TV_WARNING, "No head record; counters unknown");
        dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, TV_MUTED, "Recovery explains what is missing");
    } else {
        unsigned pending = h->gates > h->approved ? h->gates - h->approved : 0;
        dashboard_text(c, r.x + 2, r.y + 6, r.width - 4, TV_BASE, "Changed files %u   Reported: %s", h->diff, h->declared[0] ? h->declared : "nothing yet");
        dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, pending ? TV_WARNING : TV_BASE, pending ? "Approvals %u of %u, %u waiting for you" : "Approvals %u of %u", h->approved, h->gates, pending);
        if (r.height > 10) tv_bar(c, (struct tv_rect){r.x + 2, r.y + 9, r.width - 4, 1}, h->approved, h->gates ? h->gates : 1, TV_BORDER);
    }
}

static void fleet_binding_line(struct tv_canvas *c, struct tv_rect r, int *row,
                               const char *label, const char *value, enum tv_style tone) {
    char text[512];
    snprintf(text, sizeof(text), "%s%s", label, value);
    size_t used = 0, length = strlen(text), width = (size_t)(r.width - 4);
    while (used < length && *row < r.y + r.height - 2) {
        dashboard_text(c, r.x + 2, (*row)++, (int)width, tone, "%.*s", (int)width, text + used);
        used += width;
    }
}
static void selected_task_details(struct tv_canvas *c, struct tv_rect r, int *row,
                                 const struct task_observation *task) {
    char text[512];
    snprintf(text, sizeof(text), "%s / owner %s / %s / host %s", task->state, task->owner, task->freshness, task->host);
    fleet_binding_line(c, r, row, "State: ", text, TV_BASE);
    snprintf(text, sizeof(text), "%s / scope %s / requested %s", task->cancellation, task->cancellation_scope, task->cancel_requested_at);
    fleet_binding_line(c, r, row, "Cancellation: ", text, TV_WARNING);
    fleet_binding_line(c, r, row, "Wait: ", task->waiting_reason, TV_WARNING);
    fleet_binding_line(c, r, row, "Next: ", task->next_action, TV_BASE);
    fleet_binding_line(c, r, row, "Spec: ", task->spec_sha256, TV_BORDER);
    fleet_binding_line(c, r, row, "Request: ", task->request_id, TV_BORDER);
    snprintf(text, sizeof(text), "%s / verification %s", task->result_state, task->verification_state);
    fleet_binding_line(c, r, row, "Result: ", text, TV_BASE);
}
static void dashboard_fleet_tasks(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    int row = r.y + 2, available = r.height - 14;
    size_t selected = app->task_selected, start;
    tv_panel(c, r, "REMOTE TASKS / receiver-owned observations");
    if (available < 1) available = 1;
    if (available > 5) available = 5;
    start = selected < app->model.task_count && selected >= (size_t)available ? selected - (size_t)available + 1 : 0;
    for (size_t i = start; i < app->model.task_count && i < start + (size_t)available; i++)
        dashboard_text(c, r.x + 2, row++, r.width - 4, i == selected ? TV_SELECTED : TV_BASE,
                       "%c %s", i == selected ? '>' : ' ', app->model.tasks[i].task_id);
    row++;
    if (selected < app->model.task_count) selected_task_details(c, r, &row, &app->model.tasks[selected]);
    else fleet_binding_line(c, r, &row, "Selection changed: ", "use j/k to select a current task", TV_WARNING);
    dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER,
                   "%zu tasks / j k select / Y approve N reject R resume X cancel", app->model.task_count);
}

static void dashboard_fleet_hosts(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    size_t i; int row = r.y + 2;
    for (i = 0; i < app->model.host_count && row < r.y + r.height - 2; i++) {
        const struct host_observation *host = &app->model.hosts[i];
        if (!strcmp(host->state, "failed")) dashboard_text(c, r.x + 2, row++, r.width - 4, TV_WARNING,
                       "%s / failed / -- heads / %s / %s", host->name, host->error, host->freshness);
        else dashboard_text(c, r.x + 2, row++, r.width - 4, TV_BASE,
                       "%s / responded / %u heads / %s", host->name, host->heads, host->freshness);
    }
    dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER, "H hosts / %zu total / CPU and memory unavailable", app->model.host_count);
    if (row == r.y + 2) dashboard_text(c, r.x + 2, row, r.width - 4, TV_WARNING, "No host observations; inspect Recovery");
}

static void dashboard_fleet_charts(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    if (app->model.task_count) dashboard_fleet_tasks(app, c, r);
    else dashboard_fleet_hosts(app, c, r);
}

static void dashboard_queue_chart(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    size_t i; double maximum = 1;
    for (i = 0; i < app->history_count; i++) if (app->history_valid[i] && app->queue_history[i] > maximum) maximum = app->queue_history[i];
    dashboard_text(c, r.x + 2, r.y + 1, r.width - 4, TV_MUTED, "0..%.0f queued entries over %zu samples", maximum, app->history_count);
    dashboard_text(c, r.x + 2, r.y + 2, r.width - 4, TV_MUTED, "A gap means the queue was unknown at that sample");
    if (app->history_count < 2) dashboard_text(c, r.x + 2, r.y + 3, r.width - 4, TV_BASE, "Collecting history; the chart fills in after the next refresh");
    else tv_plot(c, (struct tv_rect){r.x + 2, r.y + 3, r.width - 4, r.height - 5},
                 app->queue_history, app->history_valid, app->history_count, maximum, TV_BORDER);
}

static void dashboard_charts(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    tv_panel(c, r, app->fleet ? "HOST OBSERVATIONS" : "QUEUE DEPTH / waiting work");
    if (app->fleet) dashboard_fleet_charts(app, c, r);
    else dashboard_queue_chart(app, c, r);
}

static void dashboard_distribution(struct app *app, struct tv_canvas *c, struct tv_rect r, bool changes) {
    static const char *labels[] = {"LIVE", "STALE", "STOPPED", "UNAVAILABLE"};
    static const char *names[] = {"running", "terminal gone", "stopped", "unknown"};
    static const enum tv_style tones[] = {TV_SUCCESS, TV_WARNING, TV_MUTED, TV_WARNING};
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
            dashboard_text(c, r.x + 2, row, r.width - bar - 7, tones[i], "%s %zu", names[i], counts[i]);
            tv_bar(c, (struct tv_rect){r.x + r.width - bar - 2, row, bar, 1}, counts[i],
                   app->model.head_count ? app->model.head_count : 1, tones[i]);
        }
    } else {
        for (i = 0; i < app->model.head_count && row < r.y + r.height - 2; i++, row += 2) {
            const struct head *h = &app->model.heads[i];
            dashboard_text(c, r.x + 2, row, r.width - bar - 7, TV_BASE, "%s", h->branch);
            if (h->head_id[0]) {
                dashboard_text(c, r.x + r.width - bar - 6, row, 4, TV_STRONG, "%u", h->diff);
                tv_bar(c, (struct tv_rect){r.x + r.width - bar - 2, row, bar, 1}, h->diff, maximum, TV_BORDER);
            } else dashboard_text(c, r.x + r.width - bar - 2, row, bar, TV_MUTED, "unknown");
        }
    }
    dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, TV_BORDER,
                   changes ? "0..%u files / %zu unknown" : "%u heads in this snapshot", changes ? maximum : (unsigned)app->model.head_count, unknown);
}

static int dashboard_middle_height(const struct app *app, int height) {
    int middle = height / 2;
    if (middle > 12 && app->model.head_count < 8) middle = 12;
    if (app->fleet && app->model.task_count && height - middle - 7 < 6) middle = 10;
    if (middle < 10) middle = 10;
    return middle;
}

static void dashboard_empty(struct app *app, struct tv_canvas *c, int width) {
    dashboard_text(c, 1, 1, width - 2, TV_STRONG, "%s", app->fleet ? "No remote heads observed." : "No agent work in this project yet.");
    dashboard_text(c, 1, 3, width - 2, TV_BASE, "%s", app->fleet ? "Hosts shows connectivity for each machine; Recovery lists host problems."
                                                    : "Overview summarises what is running, what needs a decision, what changed and what finished.");
    if (!app->fleet) dashboard_text(c, 1, 4, width - 2, TV_BASE, "Press n to start a task; it appears here as soon as its terminal opens.");
}

void render_dashboard(struct app *app) {
    struct tv_canvas c;
    size_t i, live = 0, attention = 0, finished = 0, changed = 0;
    int width, height, half, middle;
    if (!frame_content(app, &c)) return;
    width = c.width; height = c.height;
    if (width > 300) width = 300;
    if (height > 120) height = 120;
    if (width < 38 || height < 7) { linef(app, "Overview needs more space"); return; }
    for (i = 0; i < app->model.head_count; i++) {
        const struct head *h = &app->model.heads[i];
        if (strcmp(display_status(h), "LIVE") == 0) live++; else attention++;
        if (h->declared[0] && (!strcmp(h->declared, "done") || !strcmp(h->declared, "succeeded") || !strcmp(h->declared, "completed"))) finished++;
        changed += h->diff;
    }
    if (!app->model.head_count && !(app->fleet && app->model.task_count)) { dashboard_empty(app, &c, width); app->line = app->limit; return; }
    if (app->fleet && app->model.task_count) {
        dashboard_fleet_tasks(app, &c, (struct tv_rect){0, 0, width, height});
    } else if (width >= 90 && height >= 20) {
        int card = width / 4;
        dashboard_card(&c, 0, card - 1, "RUNNING", app->fleet ? attention : live, app->fleet ? "remote, liveness unobserved" : "agent sessions active", TV_SUCCESS);
        dashboard_card(&c, card, card - 1, "NEED ATTENTION", app->fleet ? app->model.recovery_count : attention_count(app),
                       app->fleet ? "recovery findings" : app->model.recovery_count ? "not running or awaiting approval; see Recovery too" : "not running or awaiting approval", attention_count(app) ? TV_WARNING : TV_BASE);
        dashboard_card(&c, card * 2, card - 1, "CHANGED FILES", app->fleet ? SIZE_MAX : changed, app->fleet ? "not observed remotely" : "uncommitted, all heads", TV_STRONG);
        dashboard_card(&c, card * 3, width - card * 3, "FINISHED", app->fleet ? SIZE_MAX : finished, app->fleet ? "not observed remotely" : "reported done by agents", TV_STRONG);
        half = width * 3 / 5;
        middle = dashboard_middle_height(app, height);
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
        if (app->fleet) dashboard_text(&c, 1, 0, width - 2, TV_STRONG, "%zu remote heads / liveness unobserved / %zu recovery findings",
                                      app->model.head_count, app->model.recovery_count);
        else dashboard_text(&c, 1, 0, width - 2, TV_STRONG, "%zu running / %zu need attention / %zu to repair / %zu changed files",
                            live, attention_count(app), app->model.recovery_count, changed);
        dashboard_heads(app, &c, (struct tv_rect){0, 2, width, list_height});
        dashboard_charts(app, &c, (struct tv_rect){0, list_height + 3, width, height - list_height - 3});
    } else {
        if (app->fleet) dashboard_text(&c, 1, 0, width - 2, TV_STRONG, "%zu remote heads / liveness unobserved", app->model.head_count);
        else dashboard_text(&c, 1, 0, width - 2, TV_STRONG, "%zu running / %zu need attention / %zu to repair", live, attention_count(app), app->model.recovery_count);
        dashboard_heads(app, &c, (struct tv_rect){0, 2, width, height - 2});
    }
    app->line = app->limit;
}
