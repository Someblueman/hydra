#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "statistics_cohorts.h"
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

const char *statistics_location_label(const struct statistics_view *v) {
    return v->filter.location[0] ? v->filter.location : "local and remote";
}

/* The table shows the recorded reason token; the long sentences stay in the
 * export and in the page header. */
static const char *short_reason(const struct hs_cohort *k) {
    if (!k->reason[0]) return "no reason recorded";
    if (!strncmp(k->reason, "local step", 10)) return "no task receipt";
    if (!strncmp(k->reason, "receiver recorded no telemetry", 30)) return "no telemetry block";
    if (!strncmp(k->reason, "remote receipt could not", 24)) return "receipt unverified";
    return k->reason;
}

static void cohort_row(struct tv_canvas *c, int x, int row, int width, const struct hs_cohort *k, enum hs_cohort_kind kind, bool wide, bool selected) {
    char wall[40], cpu[40], rss[40], bytes[40], why[120];
    enum tv_style tone = selected ? TV_SELECTED : k->step_states[HS_FAILED] ? TV_WARNING : TV_BASE;
    statistics_remote_text(HS_WALL_SECONDS, k->remote[HS_WALL_SECONDS], k->remote_known[HS_WALL_SECONDS], k->remote_steps, wall, sizeof(wall));
    statistics_remote_text(HS_CPU_MICROSECONDS, k->remote[HS_CPU_MICROSECONDS], k->remote_known[HS_CPU_MICROSECONDS], k->remote_steps, cpu, sizeof(cpu));
    statistics_remote_text(HS_PEAK_RSS_BYTES, k->remote[HS_PEAK_RSS_BYTES], k->remote_known[HS_PEAK_RSS_BYTES], k->remote_steps, rss, sizeof(rss));
    statistics_remote_text(HS_TRANSPORT_BYTES, k->remote[HS_TRANSPORT_BYTES], k->remote_known[HS_TRANSPORT_BYTES], k->remote_steps, bytes, sizeof(bytes));
    if (k->unmeasured) snprintf(why, sizeof(why), "%zu %s", k->unmeasured, short_reason(k));
    else copy_text(why, sizeof(why), "all measured");
    if (wide) dashboard_text(c, x, row, width, tone, "%c%-10.10s %-18.18s %3zu %4zu %3zu %3zu  %-11.11s %-11.11s %-11.11s %-11.11s %s",
        selected ? '>' : ' ', hs_cohort_kind_name(kind), k->key, k->runs, k->steps, k->step_states[HS_SUCCEEDED], k->step_states[HS_FAILED], wall, cpu, rss, bytes, why);
    else dashboard_text(c, x, row, width, tone, "%c%-9.9s %-14.14s %3zu %3zu  %-9.9s %-9.9s  %s",
        selected ? '>' : ' ', hs_cohort_kind_name(kind), k->key, k->runs, k->steps, wall, cpu, why);
}

/* Collects every kind's cohorts into one scrollable table. */
static size_t collect(const struct hs_model *m, const struct hs_filter *f, struct hs_cohort *rows, enum hs_cohort_kind *kinds, size_t capacity, bool *truncated) {
    size_t total = 0;
    enum hs_cohort_kind kind;
    *truncated = false;
    for (kind = HS_BY_LOCATION; kind < HS_COHORT_KINDS && total < capacity; kind++) {
        size_t count = hs_cohorts(m, f, kind, rows + total, capacity - total < HS_COHORTS ? capacity - total : HS_COHORTS), i;
        if (count == HS_COHORTS || total + count == capacity) *truncated = true;
        for (i = 0; i < count; i++) kinds[total + i] = kind;
        total += count;
    }
    return total;
}

static void compact(struct statistics_view *v, struct tv_canvas *c, struct tv_rect r, const struct hs_summary *s, struct hs_cohort *rows, enum hs_cohort_kind *kinds, size_t total) {
    char wall[40], cpu[40];
    size_t i, start = v->step_scroll < total ? v->step_scroll : (total ? total - 1 : 0);
    int row = r.y + 2;
    statistics_remote_text(HS_WALL_SECONDS, s->remote[HS_WALL_SECONDS], s->remote_known[HS_WALL_SECONDS], s->remote_steps, wall, sizeof(wall));
    statistics_remote_text(HS_CPU_MICROSECONDS, s->remote[HS_CPU_MICROSECONDS], s->remote_known[HS_CPU_MICROSECONDS], s->remote_steps, cpu, sizeof(cpu));
    dashboard_text(c, 1, r.y, r.width - 2, TV_STRONG, "REMOTE COHORTS / C back");
    dashboard_text(c, 1, r.y + 1, r.width - 2, TV_BASE, "%zu local / %zu remote / wall %s / cpu %s", s->steps - s->remote_steps, s->remote_steps, wall, cpu);
    for (i = start; i < total && row < r.y + r.height; i++, row++)
        dashboard_text(c, 1, row, r.width - 2, i == start ? TV_SELECTED : TV_BASE, "%s %s / %zu steps / %zu unmeasured", hs_cohort_kind_name(kinds[i]), rows[i].key, rows[i].steps, rows[i].unmeasured);
    if (!total) dashboard_text(c, 1, row, r.width - 2, TV_WARNING, "No matching runs / 0 reset");
}

void statistics_cohorts_render(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    static struct hs_cohort rows[HS_COHORTS * HS_COHORT_KINDS];
    static enum hs_cohort_kind kinds[HS_COHORTS * HS_COHORT_KINDS];
    struct statistics_view *v = app->statistics;
    struct hs_summary s;
    char wall[40], cpu[40], rss[40], bytes[40];
    size_t total, i, start, host_count = 0;
    bool truncated, wide = r.width >= 120;
    int row = r.y + 5, available;
    hs_summarize(v->model, &v->filter, &s);
    total = collect(v->model, &v->filter, rows, kinds, HS_COHORTS * HS_COHORT_KINDS, &truncated);
    if (r.height < 12 || r.width < 65) { compact(v, c, r, &s, rows, kinds, total); return; }
    for (i = 0; i < total; i++) if (kinds[i] == HS_BY_HOST) host_count++;
    statistics_remote_text(HS_WALL_SECONDS, s.remote[HS_WALL_SECONDS], s.remote_known[HS_WALL_SECONDS], s.remote_steps, wall, sizeof(wall));
    statistics_remote_text(HS_CPU_MICROSECONDS, s.remote[HS_CPU_MICROSECONDS], s.remote_known[HS_CPU_MICROSECONDS], s.remote_steps, cpu, sizeof(cpu));
    statistics_remote_text(HS_PEAK_RSS_BYTES, s.remote[HS_PEAK_RSS_BYTES], s.remote_known[HS_PEAK_RSS_BYTES], s.remote_steps, rss, sizeof(rss));
    statistics_remote_text(HS_TRANSPORT_BYTES, s.remote[HS_TRANSPORT_BYTES], s.remote_known[HS_TRANSPORT_BYTES], s.remote_steps, bytes, sizeof(bytes));
    dashboard_text(c, 1, r.y, r.width - 2, TV_STRONG, "REMOTE COHORTS / recorded task evidence / location: %s", statistics_location_label(v));
    dashboard_text(c, 1, r.y + 1, r.width - 2, TV_BASE, "%zu local steps, %zu remote steps on %zu host%s; telemetry block on %zu/%zu receipts",
        s.steps - s.remote_steps, s.remote_steps, host_count, host_count == 1 ? "" : "s", s.remote_telemetry, s.remote_steps);
    dashboard_text(c, 1, r.y + 2, r.width - 2, TV_BASE, "Wall %s / CPU %s / peak RSS %s / transport %s (remote steps; n/N when partly reported)", wall, cpu, rss, bytes);
    dashboard_text(c, 1, r.y + 3, r.width - 2, TV_MUTED, "Local steps carry no task receipt: CPU, memory and transport unknown, never zero. Transport time is not recorded.");
    dashboard_text(c, 1, r.y + 4, r.width - 2, TV_BORDER, wide ? " KIND       COHORT             RUN STEP  OK FAIL  WALL        CPU         PEAK RSS    BYTES       UNMEASURED (n reason)"
        : " KIND      COHORT         RUN STP  WALL      CPU        UNMEASURED");
    available = r.height - 7;
    if (available < 1) return;
    start = v->step_scroll < total ? v->step_scroll : (total ? total - 1 : 0);
    if (v->step_scroll > start) v->step_scroll = start;
    for (i = start; i < total && row < r.y + r.height - 2; i++, row++) cohort_row(c, 1, row, r.width - 2, &rows[i], kinds[i], wide, i == start);
    if (!total) dashboard_text(c, 1, row, r.width - 2, TV_WARNING, "No matching runs / 0 reset filters");
    dashboard_text(c, 1, r.y + r.height - 1, r.width - 2, TV_BORDER, "%zu cohorts%s / j,k scroll / L location / C or M back / r refresh", total, truncated ? " (more than shown)" : "");
}
