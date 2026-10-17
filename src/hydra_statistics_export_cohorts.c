#include "hydra_statistics.h"
#include <stdio.h>
#include <string.h>

/* Export schema 3 sections: remote cohorts and telemetry. Every measurement
 * is {state, eligible, known, sum|max}; a value is a number only when every
 * eligible step reported it ("known"), otherwise null with the state saying
 * whether some ("partial"), none ("unknown") or nothing applied
 * ("unavailable"). Reasons are the recorded ones, never inferred. */

void hs_json_string(FILE *out, const char *value) {
    const unsigned char *p;
    fputc('"', out);
    for (p = (const unsigned char *)value; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', out);
        if (*p < 0x20) fputc(' ', out); else fputc(*p, out);
    }
    fputc('"', out);
}

static const char *coverage(size_t eligible, size_t known) {
    if (!eligible) return "unavailable";
    if (!known) return "unknown";
    return known == eligible ? "known" : "partial";
}

static void write_measure(FILE *out, const char *name, size_t eligible, size_t known, uint64_t value, bool maximum) {
    const char *state = coverage(eligible, known);
    fprintf(out, "\"%s\":{\"state\":\"%s\",\"eligible\":%zu,\"known\":%zu,\"%s\":", name, state, eligible, known, maximum ? "max" : "sum");
    if (!strcmp(state, "known")) fprintf(out, "%llu", (unsigned long long)value); else fputs("null", out);
    fputc('}', out);
}

static void write_remote_measures(FILE *out, size_t remote_steps, const size_t *known, const uint64_t *values) {
    write_measure(out, "wall_seconds", remote_steps, known[HS_WALL_SECONDS], values[HS_WALL_SECONDS], false); fputc(',', out);
    write_measure(out, "cpu_microseconds", remote_steps, known[HS_CPU_MICROSECONDS], values[HS_CPU_MICROSECONDS], false); fputc(',', out);
    write_measure(out, "peak_rss_bytes", remote_steps, known[HS_PEAK_RSS_BYTES], values[HS_PEAK_RSS_BYTES], true); fputc(',', out);
    write_measure(out, "transport_calls", remote_steps, known[HS_TRANSPORT_CALLS], values[HS_TRANSPORT_CALLS], false); fputc(',', out);
    write_measure(out, "transport_bytes", remote_steps, known[HS_TRANSPORT_BYTES], values[HS_TRANSPORT_BYTES], false);
}

static void write_usage_measures(FILE *out, size_t agent_steps, const size_t *known, const uint64_t *values) {
    static const char *names[] = {"tokens_in", "tokens_cached", "tokens_out", "cost_microusd"};
    size_t i;
    for (i = 0; i < HS_USAGE_COUNTS; i++) {
        if (i) fputc(',', out);
        write_measure(out, names[i], agent_steps, known[i], values[i], false);
    }
}

static void write_cohort(FILE *out, const struct hs_cohort *c) {
    fputs("{\"key\":", out); hs_json_string(out, c->key);
    fprintf(out, ",\"runs\":%zu,\"steps\":%zu,\"succeeded\":%zu,\"failed\":%zu,\"duration\":", c->runs, c->steps,
            c->step_states[HS_SUCCEEDED], c->step_states[HS_FAILED]);
    fprintf(out, "{\"state\":\"%s\",\"eligible\":%zu,\"known\":%zu,\"sum\":", coverage(c->steps, c->duration_known), c->steps, c->duration_known);
    if (c->duration_known == c->steps && c->steps) fprintf(out, "%llu", (unsigned long long)c->duration_sum); else fputs("null", out);
    fprintf(out, "},\"remote_steps\":%zu,\"telemetry_steps\":%zu,\"telemetry\":{", c->remote_steps, c->remote_telemetry);
    write_remote_measures(out, c->remote_steps, c->remote_known, c->remote);
    fprintf(out, "},\"agent_steps\":%zu,\"usage\":{", c->agent_steps);
    write_usage_measures(out, c->agent_steps, c->usage_known, c->usage);
    fprintf(out, "},\"unmeasured_steps\":%zu,\"unmeasured_reason\":", c->unmeasured);
    if (c->reason[0]) hs_json_string(out, c->reason); else fputs("null", out);
    fputc('}', out);
}

void hs_write_cohorts_json(FILE *out, const struct hs_model *m, const struct hs_filter *filter) {
    static struct hs_cohort cohorts[HS_COHORTS];
    enum hs_cohort_kind kind;
    fputs("\"cohorts\":{", out);
    for (kind = HS_BY_LOCATION; kind < HS_COHORT_KINDS; kind++) {
        size_t count = hs_cohorts(m, filter, kind, cohorts, HS_COHORTS), i;
        if (kind != HS_BY_LOCATION) fputc(',', out);
        fprintf(out, "\"%s\":{\"truncated\":%s,\"cohorts\":[", hs_cohort_kind_name(kind), count == HS_COHORTS ? "true" : "false");
        for (i = 0; i < count; i++) { if (i) fputc(',', out); write_cohort(out, &cohorts[i]); }
        fputs("]}", out);
    }
    fputc('}', out);
}

void hs_write_telemetry_json(FILE *out, const struct hs_summary *s) {
    fprintf(out, "\"telemetry\":{\"contract\":\"fleet task telemetry 1\",\"measured_by\":\"task_owner\",\"scope\":\"execution_phase\","
            "\"local_steps\":%zu,\"remote_steps\":%zu,\"telemetry_steps\":%zu,\"measurements\":{",
            s->steps - s->remote_steps, s->remote_steps, s->remote_telemetry);
    write_remote_measures(out, s->remote_steps, s->remote_known, s->remote);
    fputs("},\"unmeasured\":{\"local_steps\":\"no task receipt: CPU, memory and transport are not measured for local steps\"", out);
    if (s->remote_steps > s->remote_telemetry)
        fprintf(out, ",\"remote_steps_without_telemetry\":%zu", s->remote_steps - s->remote_telemetry);
    fputs(",\"transport_seconds\":\"transport metrics 1 record calls and bytes, not time\",\"provider_cost\":\"only when the exec receipt reports it\"}}", out);
}

/* The schema 1/2 `remote` block keeps its scalar keys: numbers only when every
 * remote step reported the measurement, null otherwise. */
void hs_write_remote_json(FILE *out, const struct hs_summary *s) {
    size_t n = s->remote_steps, i;
    const char *state;
    size_t known = 0;
    for (i = 0; i < HS_REMOTE_COUNTS; i++) known += s->remote_known[i] > 0;
    state = !n ? "unavailable" : s->remote_known[HS_WALL_SECONDS] == n && s->remote_known[HS_CPU_MICROSECONDS] == n &&
        s->remote_known[HS_PEAK_RSS_BYTES] == n && s->remote_known[HS_TRANSPORT_BYTES] == n ? "known" : known ? "partial" : "unknown";
    fprintf(out, "\"remote\":{\"state\":\"%s\",\"remote_steps\":%zu,\"transfer_bytes\":", state, n);
    if (n && s->remote_known[HS_TRANSPORT_BYTES] == n) fprintf(out, "%llu", (unsigned long long)s->remote[HS_TRANSPORT_BYTES]); else fputs("null", out);
    fputs(",\"provider_cost\":null,\"cpu_seconds\":", out);
    if (n && s->remote_known[HS_CPU_MICROSECONDS] == n) fprintf(out, "%.6f", (double)s->remote[HS_CPU_MICROSECONDS] / 1e6); else fputs("null", out);
    fputs(",\"memory_bytes\":", out);
    if (n && s->remote_known[HS_PEAK_RSS_BYTES] == n) fprintf(out, "%llu", (unsigned long long)s->remote[HS_PEAK_RSS_BYTES]); else fputs("null", out);
    fputs(",\"tokens\":null}", out);
}
