#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "statistics.h"
/* Remote cohorts are a view over the recorded task evidence of the matched
 * runs. Nothing here samples processes or hosts; a cohort without a
 * measurement says so and why. */

static void bytes_text(uint64_t value, char *out, size_t size) {
    if (value >= 1024ULL * 1024ULL * 1024ULL) snprintf(out, size, "%.1fG", (double)value / 1073741824.0);
    else if (value >= 1024ULL * 1024ULL) snprintf(out, size, "%.1fM", (double)value / 1048576.0);
    else if (value >= 1024ULL) snprintf(out, size, "%.1fk", (double)value / 1024.0);
    else snprintf(out, size, "%llu", (unsigned long long)value);
}

void statistics_remote_text(enum hs_remote_count count, uint64_t value, size_t known, size_t eligible, char *out, size_t size) {
    char number[32];
    if (!eligible) { copy_text(out, size, "n/a"); return; }
    if (!known) { copy_text(out, size, "unknown"); return; }
    if (count == HS_WALL_SECONDS) snprintf(number, sizeof(number), "%llus", (unsigned long long)value);
    else if (count == HS_CPU_MICROSECONDS) snprintf(number, sizeof(number), "%.1fs", (double)value / 1e6);
    else if (count == HS_TRANSPORT_CALLS) snprintf(number, sizeof(number), "%llu", (unsigned long long)value);
    else bytes_text(value, number, sizeof(number));
    if (known == eligible) copy_text(out, size, number);
    else if (snprintf(out, size, "%s (%zu/%zu)", number, known, eligible) >= (int)size) out[size - 1] = '\0';
}

/* All, local, remote, then every host alias recorded in the sample. */
static size_t location_options(const struct hs_model *m, const char **names, size_t capacity) {
    size_t count = 3, i, j;
    names[0] = ""; names[1] = "local"; names[2] = "remote";
    for (i = 0; m && i < m->step_count && count < capacity; i++) {
        const struct hs_remote *r = &m->steps[i].remote;
        if (!r->recorded || !r->host[0]) continue;
        for (j = 3; j < count; j++) if (!strcmp(names[j], r->host)) break;
        if (j == count) names[count++] = r->host;
    }
    return count;
}

void statistics_location_cycle(struct statistics_view *v, int direction) {
    const char *names[HS_COHORTS + 3];
    size_t count = location_options(v->model, names, HS_COHORTS + 3), selected = 0, i;
    for (i = 1; i < count; i++) if (!strcmp(names[i], v->filter.location)) selected = i;
    selected = (selected + (direction > 0 ? 1 : count - 1)) % count;
    copy_text(v->filter.location, sizeof(v->filter.location), names[selected]);
}

bool statistics_cohorts_recorded(const struct hs_model *m) {
    size_t i;
    for (i = 0; m && i < m->step_count; i++) if (m->steps[i].remote.recorded) return true;
    return false;
}

/* ---- the cohorts page ---------------------------------------------------- */

#define COHORT_ROWS (HS_COHORTS * HS_COHORT_KINDS)

struct cohort_table {
    struct hs_cohort rows[COHORT_ROWS];
    enum hs_cohort_kind kinds[COHORT_ROWS];
    size_t count;
    bool truncated;
};

/* Every kind's cohorts, in kind order, as one table. */
static void collect(const struct hs_model *m, const struct hs_filter *f, struct cohort_table *t) {
    enum hs_cohort_kind kind;
    t->count = 0; t->truncated = false;
    for (kind = HS_BY_LOCATION; kind < HS_COHORT_KINDS && t->count < COHORT_ROWS; kind++) {
        size_t room = COHORT_ROWS - t->count, i;
        size_t count = hs_cohorts(m, f, kind, t->rows + t->count, room < HS_COHORTS ? room : HS_COHORTS);
        if (count == HS_COHORTS || t->count + count == COHORT_ROWS) t->truncated = true;
        for (i = 0; i < count; i++) t->kinds[t->count + i] = kind;
        t->count += count;
    }
}

static struct cohort_table cohorts;

size_t statistics_cohort_count(struct statistics_view *v) {
    collect(v->model, &v->filter, &cohorts);
    if (v->step_scroll >= cohorts.count) v->step_scroll = cohorts.count ? cohorts.count - 1 : 0;
    return cohorts.count;
}

enum { COHORT_KIND, COHORT_KEY, COHORT_RUNS, COHORT_STEPS, COHORT_OK, COHORT_FAIL, COHORT_WALL, COHORT_CPU,
       COHORT_RSS, COHORT_BYTES, COHORT_UNMEASURED };
static const struct stats_column cohort_columns[] = {
    {"KIND", 10, 0}, {"COHORT", 18, 0}, {"RUN", 3, 56}, {"STEP", 4, 56}, {"OK", 2, 110}, {"FAIL", 4, 110},
    {"WALL", 11, 66}, {"CPU", 11, 78}, {"PEAK RSS", 11, 110}, {"BYTES", 11, 122}, {"UNMEASURED", 24, 0}};

/* The table shows the recorded reason token; the long sentences stay in the
 * export. */
static const char *short_reason(const struct hs_cohort *k) {
    if (!k->reason[0]) return "no reason recorded";
    if (!strncmp(k->reason, "local step", 10)) return "no task receipt";
    if (!strncmp(k->reason, "receiver recorded no telemetry", 30)) return "no telemetry block";
    if (!strncmp(k->reason, "remote receipt could not", 24)) return "receipt unverified";
    return k->reason;
}

static void remote_cell(const struct hs_cohort *k, enum hs_remote_count count, char *out, size_t size) {
    statistics_remote_text(count, k->remote[count], k->remote_known[count], k->remote_steps, out, size);
}

static void count_cell(size_t value, char *out, size_t size) { (void)snprintf(out, size, "%zu", value); }

static void cohort_cell(void *context, size_t row, int column, char *out, size_t size, kt_color *color) {
    static const enum hs_remote_count measured[] = {HS_WALL_SECONDS, HS_CPU_MICROSECONDS, HS_PEAK_RSS_BYTES, HS_TRANSPORT_BYTES};
    const struct cohort_table *t = context;
    const struct hs_cohort *k = &t->rows[row];
    if (column != COHORT_KEY) *color = KT_COLOR_MUTED;
    switch (column) {
    case COHORT_KIND: copy_text(out, size, hs_cohort_kind_name(t->kinds[row])); break;
    case COHORT_KEY: copy_text(out, size, k->key); break;
    case COHORT_RUNS: count_cell(k->runs, out, size); break;
    case COHORT_STEPS: count_cell(k->steps, out, size); break;
    case COHORT_OK: count_cell(k->step_states[HS_SUCCEEDED], out, size); break;
    case COHORT_FAIL:
        count_cell(k->step_states[HS_FAILED], out, size);
        if (k->step_states[HS_FAILED]) *color = KT_COLOR_WARNING;
        break;
    case COHORT_UNMEASURED:
        if (k->unmeasured) (void)snprintf(out, size, "%zu %s", k->unmeasured, short_reason(k));
        else copy_text(out, size, "all measured");
        break;
    default: remote_cell(k, measured[column - COHORT_WALL], out, size); break;
    }
}

struct remote_totals { char wall[40], cpu[40], rss[40], bytes[40]; size_t hosts; };

static void remote_totals(const struct hs_summary *s, const struct cohort_table *t, struct remote_totals *r) {
    size_t i;
    statistics_remote_text(HS_WALL_SECONDS, s->remote[HS_WALL_SECONDS], s->remote_known[HS_WALL_SECONDS], s->remote_steps,
                           r->wall, sizeof(r->wall));
    statistics_remote_text(HS_CPU_MICROSECONDS, s->remote[HS_CPU_MICROSECONDS], s->remote_known[HS_CPU_MICROSECONDS],
                           s->remote_steps, r->cpu, sizeof(r->cpu));
    statistics_remote_text(HS_PEAK_RSS_BYTES, s->remote[HS_PEAK_RSS_BYTES], s->remote_known[HS_PEAK_RSS_BYTES],
                           s->remote_steps, r->rss, sizeof(r->rss));
    statistics_remote_text(HS_TRANSPORT_BYTES, s->remote[HS_TRANSPORT_BYTES], s->remote_known[HS_TRANSPORT_BYTES],
                           s->remote_steps, r->bytes, sizeof(r->bytes));
    r->hosts = 0;
    for (i = 0; i < t->count; i++) if (t->kinds[i] == HS_BY_HOST) r->hosts++;
}

static void cohort_tiles(struct kt_canvas *c, const struct stats_frame *f, const struct hs_summary *s,
                         const struct remote_totals *r) {
    char steps[24], hosts[64], rss[64];
    struct stats_tile tiles[4] = {
        {"Remote steps", steps, hosts, KT_BASE}, {"Wall", r->wall, "remote steps, latest attempts", KT_BASE},
        {"CPU", r->cpu, rss, KT_BASE}, {"Transport", r->bytes, "bytes, time not recorded", KT_BASE}};
    (void)snprintf(steps, sizeof(steps), "%zu", s->remote_steps);
    (void)snprintf(hosts, sizeof(hosts), "on %zu host%s, %zu local", r->hosts, r->hosts == 1 ? "" : "s",
                   s->steps - s->remote_steps);
    (void)snprintf(rss, sizeof(rss), "peak RSS %s", r->rss);
    stats_tiles(c, f->top, f->width, tiles);
}

static void cohort_notes(struct kt_canvas *c, const struct stats_frame *f, const struct hs_summary *s,
                         const struct remote_totals *r) {
    char text[256];
    (void)snprintf(text, sizeof(text), "%zu local steps, %zu remote steps on %zu host%s; telemetry block on %zu/%zu receipts. "
                   "n/N: reported by n of N remote steps.", s->steps - s->remote_steps, s->remote_steps, r->hosts,
                   r->hosts == 1 ? "" : "s", s->remote_telemetry, s->remote_steps);
    stats_note(c, f->top + 4, f->width, text);
    stats_note(c, f->top + 5, f->width, "Local steps carry no task receipt: CPU, memory and transport unknown, never zero. "
               "Transport time is not recorded.");
}

/* One line in place of the tiles and notes when narrow. */
static void cohort_summary(const struct app *app, struct kt_canvas *c, int y, int width, const struct hs_summary *s,
                           const struct remote_totals *r) {
    char text[256];
    const char *sep = dot(app);
    (void)snprintf(text, sizeof(text), "%zu remote%s%zu local%swall %s%scpu %s", s->remote_steps, sep,
                   s->steps - s->remote_steps, sep, r->wall, sep, r->cpu);
    (void)kt_text_aligned(c, (struct kt_rect){1, y, width, 1}, text, KT_ALIGN_LEFT, KT_BASE);
}

static void cohort_list(struct app *app, struct kt_canvas *c, struct kt_rect area) {
    struct statistics_view *v = app->statistics;
    struct stats_list l;
    if (!cohorts.count) {
        stats_empty(app, c, area, "Remote cohorts", v->model->run_count, "No remote steps recorded yet", NULL);
        return;
    }
    memset(&l, 0, sizeof(l));
    l.title = "Remote cohorts"; l.order = cohorts.truncated ? "more than shown" : "by kind";
    l.columns = cohort_columns; l.column_count = sizeof(cohort_columns) / sizeof(cohort_columns[0]);
    l.count = cohorts.count; l.selected = v->step_scroll; l.cell = cohort_cell; l.context = &cohorts; l.focused = true;
    stats_list_view(app, c, area, &l);
}

void stats_cohorts_page(struct app *app, struct kt_canvas *c) {
    struct statistics_view *v = app->statistics;
    struct hs_summary summary;
    struct remote_totals totals;
    struct stats_frame f;
    struct kt_rect list, unused;
    int row;
    hs_summarize(v->model, &v->filter, &summary);
    (void)statistics_cohort_count(v);
    remote_totals(&summary, &cohorts, &totals);
    stats_layout(app, c, 2, false, &f);
    stats_strip(app, c, &f);
    row = stats_list_row(&f, 2);
    if (f.tiles) {
        cohort_tiles(c, &f, &summary, &totals);
        cohort_notes(c, &f, &summary, &totals);
    } else if (c->height >= 8) {
        cohort_summary(app, c, f.top, f.width, &summary, &totals);
        if (!f.summary) row++;
    }
    stats_areas(app, c, &f, row, &list, &unused);
    cohort_list(app, c, list);
}
