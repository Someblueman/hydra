#include "hydra_statistics.h"
#include <stdio.h>
#include <string.h>

/* Cohorts are a grouping of the matched runs' recorded steps by evidence the
 * fleet protocol supplies: where a step ran (local or as a remote task), the
 * receiving host, how the host was chosen, the assignment generation and the
 * agent provider named by the exec receipt. Aggregates add only reported
 * measurements; a cohort whose steps carry no measurement stays unknown. */

const char *hs_cohort_kind_name(enum hs_cohort_kind kind) {
    static const char *names[] = {"location", "host", "placement", "generation", "provider"};
    return kind < HS_COHORT_KINDS ? names[kind] : "unknown";
}

/* NULL excludes the step from this grouping (for example a local step from
 * the host cohorts, or a step without an exec receipt from providers). */
static const char *cohort_key(enum hs_cohort_kind kind, const struct hs_step *s, char *buffer, size_t size) {
    const struct hs_remote *r = &s->remote;
    switch (kind) {
        case HS_BY_LOCATION: return r->recorded ? "remote" : "local";
        case HS_BY_HOST: return r->recorded ? (r->host[0] ? r->host : "unknown host") : NULL;
        case HS_BY_PLACEMENT: return r->recorded ? (r->placement[0] ? r->placement : "unknown placement") : NULL;
        case HS_BY_GENERATION:
            if (!r->recorded) return NULL;
            if (!r->generation_known) return "generation unknown";
            snprintf(buffer, size, "generation %u", r->generation);
            return buffer;
        case HS_BY_PROVIDER:
            if (!s->usage.recorded) return strcmp(s->kind, "exec") && strcmp(s->kind, "task") ? NULL : "no agent receipt";
            return s->usage.profile[0] ? s->usage.profile : "agent unknown";
        default: return NULL;
    }
}

/* Why this step contributes no telemetry, or NULL when it did. */
static const char *step_reason(const struct hs_step *s) {
    const struct hs_remote *r = &s->remote;
    if (!r->recorded) return "local step: no task receipt, so CPU, memory and transport are not measured";
    if (!strcmp(r->reason, "receipt_unverified")) return "remote receipt could not be verified against its dispatch";
    if (!strcmp(r->reason, "no_telemetry_block")) return "receiver recorded no telemetry block (protocol without telemetry 1)";
    if (r->reason[0]) return r->reason;
    return NULL;
}

static struct hs_cohort *cohort_for(struct hs_cohort *out, size_t *count, size_t capacity, const char *key) {
    size_t i;
    for (i = 0; i < *count; i++) if (!strcmp(out[i].key, key)) return &out[i];
    if (*count == capacity || strlen(key) >= sizeof(out[0].key)) return NULL;
    memset(&out[*count], 0, sizeof(out[0]));
    memcpy(out[*count].key, key, strlen(key) + 1);
    return &out[(*count)++];
}

static void cohort_add(const struct hs_model *m, struct hs_cohort *c, const struct hs_step *s) {
    struct hs_summary totals;
    size_t i;
    uint64_t duration;
    const char *why = step_reason(s);
    c->steps++; c->step_states[hs_state(s->state)]++;
    if (hs_duration(m, s, &duration)) { c->duration_known++; c->duration_sum += duration; }
    memset(&totals, 0, sizeof(totals));
    hs_usage_add(&totals, s); hs_remote_add(&totals, s);
    c->agent_steps += totals.agent_steps;
    for (i = 0; i < HS_USAGE_COUNTS; i++) { c->usage_known[i] += totals.usage_known[i]; c->usage[i] += totals.usage[i]; }
    c->remote_steps += totals.remote_steps; c->remote_telemetry += totals.remote_telemetry;
    for (i = 0; i < HS_REMOTE_COUNTS; i++) {
        c->remote_known[i] += totals.remote_known[i];
        if (i == HS_PEAK_RSS_BYTES) { if (totals.remote[i] > c->remote[i]) c->remote[i] = totals.remote[i]; }
        else c->remote[i] += totals.remote[i];
    }
    if (!s->remote.recorded || !hs_remote_telemetry(&s->remote) || !s->remote.known[HS_WALL_SECONDS] ||
        !s->remote.known[HS_CPU_MICROSECONDS] || !s->remote.known[HS_PEAK_RSS_BYTES]) c->unmeasured++;
    if (why && !c->reason[0]) memcpy(c->reason, why, strlen(why) + 1 < sizeof(c->reason) ? strlen(why) + 1 : sizeof(c->reason) - 1);
}

size_t hs_cohorts(const struct hs_model *m, const struct hs_filter *f, enum hs_cohort_kind kind,
                  struct hs_cohort *out, size_t capacity) {
    size_t count = 0, i, j;
    char buffer[48];
    for (i = 0; i < m->step_count; i++) {
        const struct hs_step *s = &m->steps[i];
        const char *key;
        struct hs_cohort *c;
        if (!hs_matches(m, s->run, f)) continue;
        key = cohort_key(kind, s, buffer, sizeof(buffer));
        if (!key || !(c = cohort_for(out, &count, capacity, key))) continue;
        cohort_add(m, c, s);
    }
    /* A run belongs to every cohort one of its steps belongs to. */
    for (i = 0; i < count; i++) for (j = 0; j < m->run_count; j++) {
        size_t k;
        if (!hs_matches(m, j, f)) continue;
        for (k = 0; k < m->step_count; k++) {
            const char *key;
            if (m->steps[k].run != j) continue;
            key = cohort_key(kind, &m->steps[k], buffer, sizeof(buffer));
            if (key && !strcmp(key, out[i].key)) { out[i].runs++; break; }
        }
    }
    return count;
}
