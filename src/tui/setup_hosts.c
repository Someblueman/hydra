#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "setup_flow.h"
/* The Hosts rows: This machine, the observed hosts, one row per setup
 * record not finished yet, and "Add a host". Records, their destinations and
 * progress come from the read-only `hydra remote setup list --json`; the TUI
 * never reads the private setup directory itself. */
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
        r->upgrade = e->listed[i].upgrade[0] != '\0';
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

static bool row_matches(const struct app *app, const char *name) {
    return !app->hosts || !app->hosts->filter[0] || strstr(name, app->hosts->filter) != NULL;
}

/* This machine first (the control centre's own), the observed hosts, the
 * unfinished setups, then "Add a host". */
size_t hosts_rows(const struct app *app, struct host_row *rows, size_t capacity) {
    const struct native_setup *s = app->setup;
    size_t count = 0, i;
    if (!app->fleet && count < capacity) rows[count++] = (struct host_row){HOST_ROW_MACHINE, 0};
    for (i = 0; i < app->model.host_count && count < capacity; i++)
        if (row_matches(app, app->model.hosts[i].name)) rows[count++] = (struct host_row){HOST_ROW_HOST, i};
    for (i = 0; s && i < s->record_count && count < capacity; i++) {
        /* A finished setup is an ordinary fleet host; list it once. */
        if (s->records[i].complete && host_for_name(app, s->records[i].name)) continue;
        if (row_matches(app, s->records[i].name)) rows[count++] = (struct host_row){HOST_ROW_SETUP, i};
    }
    if (count < capacity) rows[count++] = (struct host_row){HOST_ROW_ADD, 0};
    return count;
}

size_t hosts_row_count(const struct app *app) {
    struct host_row rows[MAX_HEADS];
    return hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
}

bool hosts_selected_row(const struct app *app, struct host_row *row) {
    struct host_row rows[MAX_HEADS];
    size_t count = hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
    if (!count) return false;
    *row = rows[app->host_selected < count ? app->host_selected : 0];
    return true;
}

bool hosts_setup_selected(const struct app *app, size_t *record) {
    struct host_row row;
    if (!hosts_selected_row(app, &row) || row.kind != HOST_ROW_SETUP) return false;
    *record = row.index;
    /* A finished setup offering an upgrade is neither edited nor removed here. */
    return !app->setup->records[*record].complete && !app->setup->records[*record].upgrade;
}

void setup_row_text(const struct app *app, const struct host_row *row, char *name, size_t name_size, char *state, size_t state_size) {
    const struct setup_record *r;
    if (row->kind == HOST_ROW_ADD) {
        copy_text(name, name_size, "Add a host");
        copy_text(state, state_size, "set up a machine over SSH");
        return;
    }
    r = &app->setup->records[row->index];
    copy_text(name, name_size, r->name);
    if (app->setup->job.active && !strcmp(app->setup->name, r->name)) copy_text(state, state_size, "setup running...");
    else copy_text(state, state_size, r->summary[0] ? r->summary : "checking setup status...");
}
