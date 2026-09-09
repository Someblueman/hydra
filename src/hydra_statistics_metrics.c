#include "hydra_statistics.h"
#include <stdlib.h>
#include <string.h>

static bool interval(const struct hs_model *m, const struct hs_run *r,
                     uint64_t start, uint64_t end, uint64_t *out) {
    if (!start || !end || end < start || end > m->observed) return false;
    if (r->created && start < r->created) return false;
    *out = end - start;
    return true;
}

static enum hs_evidence queue_sample(const struct hs_model *m, size_t index, uint64_t *value) {
    const struct hs_step *s = &m->steps[index];
    if (!strcmp(s->kind, "approval-wait")) return HS_INELIGIBLE;
    if (!s->first_started && s->attempts_known && !s->attempts) return HS_INELIGIBLE;
    return interval(m, &m->runs[s->run], s->ready, s->first_started, value) ? HS_KNOWN : HS_MISSING;
}

static bool terminal(const char *state) {
    return !strcmp(state, "succeeded") || !strcmp(state, "failed") || !strcmp(state, "cancelled");
}

static enum hs_evidence verified_sample(const struct hs_model *m, const struct hs_run *r, uint64_t *value) {
    if (!r->planned) return HS_INELIGIBLE;
    if (strcmp(r->state, "succeeded")) return HS_MISSING;
    if (!r->completed || r->verified > r->completed) return HS_MISSING;
    return interval(m,r,r->started,r->verified,value) ? HS_KNOWN : HS_MISSING;
}

enum hs_evidence hs_sample(const struct hs_model *m, enum hs_metric metric, size_t index, uint64_t *value) {
    const struct hs_run *r;
    if (metric == HS_QUEUE) return queue_sample(m, index, value);
    r = &m->runs[index];
    if (metric == HS_RECOVERIES) {
        *value = r->recoveries;
        return r->recoveries_known ? HS_KNOWN : HS_MISSING;
    }
    if (metric == HS_ELAPSED && !terminal(r->state)) return HS_INELIGIBLE;
    if (metric == HS_VERIFIED) return verified_sample(m,r,value);
    return interval(m,r,r->started,r->completed,value) ? HS_KNOWN : HS_MISSING;
}

static int compare_value(const void *a, const void *b) {
    uint64_t left = *(const uint64_t *)a, right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

static void add_sample(const struct hs_model *m, size_t run, uint64_t value, struct hs_metric_summary *out) {
    uint64_t created = m->runs[run].created;
    out->sum += value;
    if (value > out->maximum) out->maximum = value;
    if (created && m->observed - created < 7 * 86400) {
        size_t day = 6 - (size_t)((m->observed - created) / 86400);
        out->daily_known[day]++; out->daily_sum[day] += value;
    }
}

void hs_metric_summarize(const struct hs_model *m, const struct hs_filter *filter,
                         enum hs_metric metric, struct hs_metric_summary *out) {
    uint64_t values[HS_STEPS];
    size_t i, count = metric == HS_QUEUE ? m->step_count : m->run_count;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < count; i++) {
        size_t run = metric == HS_QUEUE ? m->steps[i].run : i;
        uint64_t value;
        enum hs_evidence evidence;
        if (!hs_matches(m, run, filter)) continue;
        evidence = hs_sample(m, metric, i, &value);
        if (evidence == HS_INELIGIBLE) continue;
        out->eligible++;
        if (evidence != HS_KNOWN) continue;
        values[out->known++] = value;
        add_sample(m, run, value, out);
    }
    if (!out->known) return;
    qsort(values, out->known, sizeof(values[0]), compare_value);
    out->p50 = values[(out->known + 1) / 2 - 1];
    out->p95 = values[(out->known * 95 + 99) / 100 - 1];
}
