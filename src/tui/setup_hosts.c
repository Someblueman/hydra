#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "setup_flow.h"
/* Hosts tab entries for remote setup: an "Add a host" row and one row per
 * setup record. Records, their destinations and progress come from the
 * read-only `hydra remote setup list --json`; the TUI never reads the private
 * setup directory itself. */
#define SETUP_SWEEP_BUDGET_MS 30000L

/* Replaces the records with the listed ones; a failed listing keeps them. */
static void sweep_finish(struct native_setup *s) {
    size_t length, i;
    int exit_status;
    char *text = setup_capture_finish(&s->sweep, &length, &exit_status);
    struct setup_envelope *e = s->sweep_result;
    bool parsed = text && !setup_envelope_parse(e, text, length, exit_status) && e->ok;
    free(text);
    if (!parsed) return;
    s->record_count = 0;
    for (i = 0; i < e->listed_count && s->record_count < SETUP_RECORDS; i++) {
        struct setup_record *r = &s->records[s->record_count++];
        memset(r, 0, sizeof(*r));
        copy_text(r->name, sizeof(r->name), e->listed[i].name);
        copy_text(r->destination, sizeof(r->destination), e->listed[i].destination);
        copy_text(r->config, sizeof(r->config), e->listed[i].ssh_config);
        setup_listed_summary(&e->listed[i], r->summary, sizeof(r->summary));
        r->complete = e->listed[i].complete;
        r->remote_changed = e->listed[i].remote_changed;
    }
}

void native_setup_hosts_refresh(struct app *app) {
    struct native_setup *s = native_setup_state(app);
    if (s) s->sweep_pending = true;
}

void native_setup_sweep_tick(struct app *app) {
    char *argv[] = {(char *)app->hydra, (char *)"remote", (char *)"setup", (char *)"list", (char *)"--json", NULL};
    struct native_setup *s = app->setup;
    if (!s) return;
    if (s->sweep.active) {
        if (!setup_capture_step(&s->sweep)) return;
        sweep_finish(s);
    }
    if (!s->sweep_pending) return;
    s->sweep_pending = false;
    (void)setup_capture_start(&s->sweep, argv, SETUP_SWEEP_BUDGET_MS);
}

/* ---- Rows ---- */

size_t hosts_rows(const struct app *app, struct host_row *rows, size_t capacity) {
    const struct native_setup *s = app->setup;
    size_t count = 0, i;
    if (!app->fleet && count < capacity) rows[count++] = (struct host_row){HOST_ROW_ADD, 0};
    for (i = 0; app->fleet && i < app->model.host_count && count < capacity; i++) rows[count++] = (struct host_row){HOST_ROW_HOST, i};
    for (i = 0; s && i < s->record_count && count < capacity; i++) {
        /* A finished setup is an ordinary fleet host; list it once. */
        if (app->fleet && s->records[i].complete) continue;
        rows[count++] = (struct host_row){HOST_ROW_SETUP, i};
    }
    if (app->fleet && count < capacity) rows[count++] = (struct host_row){HOST_ROW_ADD, 0};
    return count;
}

size_t hosts_row_count(const struct app *app) {
    struct host_row rows[MAX_HEADS];
    return hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
}

void hosts_open_row(struct app *app) {
    struct host_row rows[MAX_HEADS];
    size_t count = hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
    const struct host_row *row;
    if (app->host_selected >= count) app->host_selected = 0;
    if (!count) return;
    row = &rows[app->host_selected];
    if (row->kind == HOST_ROW_ADD) native_setup_open_form(app);
    else if (row->kind == HOST_ROW_SETUP) native_setup_resume(app, app->setup->records[row->index].name);
    else {
        copy_text(app->search, sizeof(app->search), app->model.hosts[row->index].name);
        enter_view(app, 0); retarget_selection(app);
    }
}

bool hosts_setup_selected(const struct app *app, size_t *record) {
    struct host_row rows[MAX_HEADS];
    size_t count = hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
    if (app->host_selected >= count || rows[app->host_selected].kind != HOST_ROW_SETUP) return false;
    *record = rows[app->host_selected].index;
    return !app->setup->records[*record].complete;
}

void setup_row_text(const struct app *app, const struct host_row *row, char *name, size_t name_size, char *state, size_t state_size) {
    const struct setup_record *r;
    if (row->kind == HOST_ROW_ADD) {
        copy_text(name, name_size, "+ Add a host");
        copy_text(state, state_size, app->fleet ? "A: set up another machine over SSH" : "set up a machine over SSH");
        return;
    }
    r = &app->setup->records[row->index];
    copy_text(name, name_size, r->name);
    if (app->setup->job.active && !strcmp(app->setup->name, r->name)) copy_text(state, state_size, "setup running...");
    else copy_text(state, state_size, r->summary[0] ? r->summary : "checking setup status...");
}

/* ---- Local Hosts tab ---- */

static void hub_hit(struct app *app, size_t index) {
    if (app->hit_count >= MAX_HEADS) return;
    app->hit_rows[app->hit_count] = app->hit_bottom[app->hit_count] = app->line + 1;
    app->hit_left[app->hit_count] = 3; app->hit_right[app->hit_count] = app->cols - 3;
    app->hit_items[app->hit_count++] = index;
}

/* Name and setup state side by side, or stacked below 60 columns. */
static void hub_row(struct app *app, const struct host_row *row, size_t index) {
    char name[160], state[200], line[400];
    bool selected = index == app->host_selected, narrow = app->content_width < 60;
    int width = app->content_width, state_x = narrow ? 4 : 26;
    enum tv_style name_tone = row->kind == HOST_ROW_ADD ? TV_STRONG : TV_BASE;
    setup_row_text(app, row, name, sizeof(name), state, sizeof(state));
    hub_hit(app, index);
    snprintf(line, sizeof(line), "%c %s", selected ? '>' : ' ', name);
    if (selected) column(app, 0, width, TV_SELECTED, "");
    column(app, 0, narrow ? width : 24, selected ? TV_SELECTED : name_tone, line);
    if (narrow) app->line++;
    if (app->line < app->limit) column(app, state_x, width - state_x, selected && !narrow ? TV_SELECTED : TV_MUTED, state);
    app->line++;
}

void render_setup_hub(struct app *app) {
    struct host_row rows[SETUP_RECORDS + 1];
    size_t count = hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0])), i;
    if (app->host_selected >= count) app->host_selected = 0;
    style(app, TONE_STRONG); linef(app, "REMOTE HOSTS"); style(app, TONE_BASE);
    paragraph(app, "Hydra can run agents on other machines over SSH. Adding a host checks its SSH host key and "
              "requirements, installs a matching Hydra there and helps you install and sign in to agents. "
              "Nothing changes on the remote until you approve it.", TV_BASE);
    linef(app, "");
    for (i = 0; i < count && app->line < app->limit - 2; i++) hub_row(app, &rows[i], i);
    linef(app, "");
    paragraph(app, count > 1 ? "Enter continues a host's setup; e edits and x removes an unfinished one. Follow work on a set-up host with hydra fleet tui."
                             : "Press Enter or A to add a host.", TV_MUTED);
}
