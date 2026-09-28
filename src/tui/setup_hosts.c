#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "setup_flow.h"
#include <dirent.h>
/* Hosts tab entries for remote setup: an "Add a host" row and one row per
 * setup record. Record names come from the private setup directory; their
 * progress only from `hydra remote setup status NAME --json`. */
#define SETUP_SWEEP_BUDGET_MS 30000L

static bool setup_home(char *out, size_t size) {
    const char *home = getenv("HYDRA_HOME"), *user = getenv("HOME");
    if (home && home[0]) return snprintf(out, size, "%s/fleet/setup", home) < (int)size;
    return user && user[0] && snprintf(out, size, "%s/.hydra/fleet/setup", user) < (int)size;
}

/* NAME.json with a valid setup NAME; other files are not setup records. */
static bool record_name(const char *file, char *name, size_t size) {
    size_t length = strlen(file);
    if (length < 6 || length - 5 >= size || strcmp(file + length - 5, ".json")) return false;
    memcpy(name, file, length - 5); name[length - 5] = '\0';
    return !setup_name_problem(name);
}

static int record_compare(const void *left, const void *right) {
    return strcmp(((const struct setup_record *)left)->name, ((const struct setup_record *)right)->name);
}

static void keep_summary(const struct native_setup *s, const struct setup_record *old, size_t count, struct setup_record *r) {
    size_t i;
    (void)s;
    for (i = 0; i < count; i++) if (!strcmp(old[i].name, r->name)) { *r = old[i]; r->checked = false; return; }
}

static void scan_records(struct native_setup *s) {
    struct setup_record old[SETUP_RECORDS];
    size_t old_count = s->record_count;
    char path[4096], name[128];
    struct dirent *entry;
    DIR *directory;
    memcpy(old, s->records, sizeof(old));
    s->record_count = 0;
    if (!setup_home(path, sizeof(path)) || !(directory = opendir(path))) return;
    while ((entry = readdir(directory)) != NULL && s->record_count < SETUP_RECORDS) {
        struct setup_record *r;
        if (!record_name(entry->d_name, name, sizeof(name))) continue;
        r = &s->records[s->record_count++];
        memset(r, 0, sizeof(*r));
        copy_text(r->name, sizeof(r->name), name);
        keep_summary(s, old, old_count, r);
    }
    closedir(directory);
    qsort(s->records, s->record_count, sizeof(s->records[0]), record_compare);
}

void native_setup_hosts_refresh(struct app *app) {
    struct native_setup *s = native_setup_state(app);
    if (!s) return;
    scan_records(s);
    s->sweep_index = 0; s->sweep_pending = s->record_count > 0;
}

static void sweep_finish(struct native_setup *s) {
    size_t length, i;
    int exit_status;
    char *text = setup_capture_finish(&s->sweep, &length, &exit_status);
    bool parsed = text && !setup_envelope_parse(s->sweep_result, text, length, exit_status);
    free(text);
    for (i = 0; i < s->record_count; i++) {
        struct setup_record *r = &s->records[i];
        if (strcmp(r->name, s->sweep_name)) continue;
        if (parsed) setup_summary(s->sweep_result, r->summary, sizeof(r->summary));
        else copy_text(r->summary, sizeof(r->summary), "status unavailable");
        r->complete = parsed && s->sweep_result->ok && !setup_open_step(s->sweep_result);
        r->checked = true;
    }
}

static void sweep_next(struct app *app, struct native_setup *s) {
    char *argv[] = {(char *)app->hydra, (char *)"remote", (char *)"setup", (char *)"status", s->sweep_name, (char *)"--json", NULL};
    while (s->sweep_index < s->record_count && s->records[s->sweep_index].checked) s->sweep_index++;
    if (s->sweep_index >= s->record_count) { s->sweep_pending = false; return; }
    copy_text(s->sweep_name, sizeof(s->sweep_name), s->records[s->sweep_index++].name);
    if (!setup_capture_start(&s->sweep, argv, SETUP_SWEEP_BUDGET_MS)) s->sweep_pending = false;
}

void native_setup_sweep_tick(struct app *app) {
    struct native_setup *s = app->setup;
    if (!s) return;
    if (s->sweep.active) {
        if (!setup_capture_step(&s->sweep)) return;
        sweep_finish(s);
    }
    if (s->sweep_pending) sweep_next(app, s);
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

static void hub_row(struct app *app, const struct host_row *row, size_t index) {
    char name[160], state[200], line[400];
    bool selected = index == app->host_selected;
    int width = app->content_width;
    setup_row_text(app, row, name, sizeof(name), state, sizeof(state));
    if (app->hit_count < MAX_HEADS) {
        app->hit_rows[app->hit_count] = app->hit_bottom[app->hit_count] = app->line + 1;
        app->hit_left[app->hit_count] = 3; app->hit_right[app->hit_count] = app->cols - 3;
        app->hit_items[app->hit_count++] = index;
    }
    snprintf(line, sizeof(line), "%c %s", selected ? '>' : ' ', name);
    if (selected) column(app, 0, width, TV_SELECTED, "");
    column(app, 0, width < 60 ? width : 24, selected ? TV_SELECTED : row->kind == HOST_ROW_ADD ? TV_STRONG : TV_BASE, line);
    if (width >= 40) column(app, width < 60 ? 2 : 26, width - (width < 60 ? 2 : 26), selected ? TV_SELECTED : TV_MUTED, state);
    app->line++;
    if (width < 60 && app->line < app->limit) { column(app, 4, width - 4, TV_MUTED, state); app->line++; }
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
    paragraph(app, count > 1 ? "Enter continues a host's setup. Follow work on a set-up host with hydra fleet tui."
                             : "Press Enter or A to add a host.", TV_MUTED);
}
