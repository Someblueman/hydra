#include "hydra_statistics.h"
#include <stdio.h>
#include <string.h>

static const char *metric_name(enum hs_metric metric) {
    static const char *names[] = {"queue", "elapsed", "verified", "recoveries"};
    return metric < HS_METRICS ? names[metric] : "unknown";
}

static const char *evidence_state(const struct hs_metric_summary *s) {
    if (!s->eligible) return "unavailable";
    if (!s->known) return "unknown";
    return "known";
}

static void write_json_string(FILE *out, const char *value) {
    const unsigned char *p;
    fputc('"', out);
    for (p = (const unsigned char *)value; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', out);
        if (*p < 0x20) fputc(' ', out); else fputc(*p, out);
    }
    fputc('"', out);
}

static void write_summary(FILE *out, const struct hs_metric_summary *s) {
    fprintf(out, "{\"state\":\"%s\",\"eligible\":%zu,\"known\":%zu",
            evidence_state(s), s->eligible, s->known);
    if (s->known) {
        fprintf(out, ",\"sum\":%llu,\"mean\":%llu,\"max\":%llu,\"p50\":%llu,\"p95\":%llu",
                (unsigned long long)s->sum,
                (unsigned long long)(s->sum / s->known),
                (unsigned long long)s->maximum,
                (unsigned long long)s->p50,
                (unsigned long long)s->p95);
    } else {
        fputs(",\"sum\":null,\"mean\":null,\"max\":null,\"p50\":null,\"p95\":null", out);
    }
    fputc('}', out);
}

struct recovery_outcomes { size_t eligible, successes, failures, unknown, missing_history; };

static int recovery_outcome(const struct hs_model *m, size_t index) {
    const struct hs_run *run = &m->runs[index];
    uint64_t ignored;
    if (!strcmp(run->state, "failed") || !strcmp(run->state, "cancelled")) return 2;
    if (strcmp(run->state, "succeeded")) return 0;
    if (run->planned && hs_sample(m, HS_VERIFIED, index, &ignored) != HS_KNOWN) return 0;
    return 1;
}

static struct recovery_outcomes recovery_counts(const struct hs_model *m, const struct hs_filter *filter) {
    struct recovery_outcomes counts = {0};
    for (size_t i = 0; i < m->run_count; i++) {
        if (!hs_matches(m, i, filter)) continue;
        if (!m->runs[i].recoveries_known) { counts.missing_history++; continue; }
        if (!m->runs[i].recoveries) continue;
        counts.eligible++;
        switch (recovery_outcome(m, i)) {
            case 1: counts.successes++; break;
            case 2: counts.failures++; break;
            default: counts.unknown++; break;
        }
    }
    return counts;
}

static void write_recovery_outcomes(FILE *out, const struct hs_model *m, const struct hs_filter *filter) {
    struct recovery_outcomes c = recovery_counts(m, filter);
    size_t known = c.successes + c.failures;
    fprintf(out, ",\"recovery_outcomes\":{\"scope\":\"coordinator_owner_recovery\",\"eligible\":%zu,\"known_terminal\":%zu,\"succeeded\":%zu,\"failed_or_cancelled\":%zu,\"unknown\":%zu,\"missing_recovery_history\":%zu,\"success_fraction_among_known\":",
            c.eligible, known, c.successes, c.failures, c.unknown, c.missing_history);
    if (known) fprintf(out, "%.9g", (double)c.successes / (double)known);
    else fputs("null", out);
    fputs("}", out);
}

struct recorded_summary { size_t eligible, known; uint64_t sum; bool overflow; };
static bool recorded_value(const struct hs_run *run, int metric, uint64_t *value) {
    if (metric == 0) { *value = run->unknown_outcomes; return run->unknown_outcomes_known; }
    if (metric == 1) { *value = run->interventions; return run->interventions_known; }
    *value = run->transfer_bytes; return run->transfer_bytes_known;
}
static struct recorded_summary recorded_summary(const struct hs_model *model,
                                                 const struct hs_filter *filter, int metric) {
    struct recorded_summary summary = {0};
    for (size_t i = 0; i < model->run_count; i++) {
        uint64_t value;
        if (!hs_matches(model, i, filter)) continue;
        summary.eligible++;
        if (!recorded_value(&model->runs[i], metric, &value)) continue;
        summary.known++;
        if (UINT64_MAX - summary.sum < value) summary.overflow = true;
        else summary.sum += value;
    }
    return summary;
}
static const char *recorded_state(const struct recorded_summary *summary) {
    if (summary->overflow) return "unknown";
    if (!summary->eligible) return "unavailable";
    if (!summary->known) return "unknown";
    return summary->known == summary->eligible ? "known" : "partial";
}
static void write_recorded_metric(FILE *out, const struct hs_model *model,
                                  const struct hs_filter *filter, int metric) {
    struct recorded_summary summary = recorded_summary(model, filter, metric);
    const char *state = recorded_state(&summary);
    fprintf(out, "{\"state\":\"%s\",\"eligible\":%zu,\"known\":%zu,\"sum\":",
            state, summary.eligible, summary.known);
    if (!strcmp(state, "known")) fprintf(out, "%llu", (unsigned long long)summary.sum);
    else fputs("null", out);
    fputc('}', out);
}
static void write_recorded_text(FILE *out, const char *label, const struct hs_model *model,
                                const struct hs_filter *filter) {
    static const char *names[] = {"unknown_receiver_outcomes", "recorded_operator_actions", "transport_stdio_bytes"};
    for (int metric = 0; metric < 3; metric++) {
        struct recorded_summary summary = recorded_summary(model, filter, metric);
        const char *state = recorded_state(&summary);
        fprintf(out, "%s recorded %s state=%s eligible=%zu known=%zu sum=",
                label, names[metric], state, summary.eligible, summary.known);
        if (!strcmp(state, "known")) fprintf(out, "%llu\n", (unsigned long long)summary.sum);
        else fputs("unknown\n", out);
    }
}

/* JSON export schema: 3 adds head sessions (feed schema 5), 2 added the
 * recorded metrics (feed schema 3). */
static unsigned export_version(unsigned feed) {
    return feed >= 5 ? 3U : feed >= 3 ? 2U : 1U;
}

static const char *sum_state(size_t eligible, size_t known) {
    if (!eligible) return "unavailable";
    if (!known) return "unknown";
    return known == eligible ? "known" : "partial";
}

/* A sum over the sessions that reported it; never a number when none did. */
static void write_session_sum(FILE *out, const char *name, size_t eligible, size_t known, uint64_t sum) {
    fprintf(out, ",\"%s\":{\"state\":\"%s\",\"eligible\":%zu,\"known\":%zu,\"sum\":", name, sum_state(eligible, known), eligible, known);
    if (known) fprintf(out, "%llu}", (unsigned long long)sum);
    else fputs("null}", out);
}

static void write_fraction(FILE *out, const char *name, uint64_t part, uint64_t whole) {
    fprintf(out, ",\"%s\":", name);
    if (whole) fprintf(out, "%.9g", (double)part / (double)whole);
    else fputs("null", out);
}

/* Head sessions: lengths, and time active, quiet and unobserved. Active and
 * quiet are inferred from terminal output except for the reported seconds. */
static void write_sessions(FILE *out, const struct hs_model *m, const struct hs_filter *filter) {
    struct hs_session_summary s;
    uint64_t observed;
    size_t day;
    hs_session_summarize(m, filter, &s);
    observed = s.active_sum + s.quiet_sum;
    fprintf(out, ",\"sessions\":{\"scope\":\"head_sessions\",\"count\":%zu,\"open\":%zu,\"undated\":%zu", s.sessions, s.open, s.undated);
    fprintf(out, ",\"length_seconds\":{\"state\":\"%s\",\"eligible\":%zu,\"known\":%zu", sum_state(s.sessions, s.length_known), s.sessions, s.length_known);
    if (s.length_known) fprintf(out, ",\"sum\":%llu,\"mean\":%llu,\"max\":%llu}", (unsigned long long)s.length_sum,
                                (unsigned long long)(s.length_sum / s.length_known), (unsigned long long)s.length_max);
    else fputs(",\"sum\":null,\"mean\":null,\"max\":null}", out);
    write_session_sum(out, "active_seconds", s.sessions, s.activity_known, s.active_sum);
    write_session_sum(out, "quiet_seconds", s.sessions, s.activity_known, s.quiet_sum);
    write_session_sum(out, "unobserved_seconds", s.sessions, s.coverage_known, s.unobserved_sum);
    write_session_sum(out, "reported_seconds", s.sessions, s.coverage_known, s.reported_sum);
    write_fraction(out, "observed_fraction", observed, s.coverage_known ? observed + s.unobserved_sum : 0);
    write_fraction(out, "quiet_fraction_of_observed", s.quiet_sum, observed);
    fputs(",\"started_daily\":[", out);
    for (day = 0; day < 7; day++) fprintf(out, "%s%zu", day ? "," : "", s.daily[day]);
    fputs("],\"inference\":{\"source\":\"tmux\",\"confidence\":\"inferred\",\"exact\":\"reported_seconds\"}}", out);
}

static bool write_one(FILE *out, const struct hs_model *m,
                      const struct hs_filter *filter) {
    enum hs_metric metric;
    struct hs_summary cohort;
    hs_summarize(m, filter, &cohort);
    fprintf(out, "{\"schema_version\":%u,\"availability\":\"known\",\"observed\":%llu,\"filter\":{\"days\":%u,\"attention\":%s,\"query\":",
            export_version(m->schema_version),
            (unsigned long long)m->observed, filter->days, filter->attention ? "true" : "false");
    write_json_string(out, filter->query);
    fputs(",\"workflow\":", out); write_json_string(out, filter->workflow);
    fputs("},\"cohort\":{\"runs\":", out);
    fprintf(out, "%zu,\"steps\":%zu},\"metrics\":{", cohort.runs, cohort.steps);
    for (metric = HS_QUEUE; metric < HS_METRICS; metric++) {
        struct hs_metric_summary summary;
        if (metric != HS_QUEUE) fputc(',', out);
        fprintf(out, "\"%s\":", metric_name(metric));
        hs_metric_summarize(m, filter, metric, &summary);
        write_summary(out, &summary);
    }
    fputc('}', out);
    write_recovery_outcomes(out, m, filter);
    if (m->schema_version >= 5) write_sessions(out, m, filter);
    if (m->schema_version >= 3) {
        fputs(",\"recorded\":{\"unknown_receiver_outcomes\":", out);
        write_recorded_metric(out, m, filter, 0);
        fputs(",\"recorded_operator_actions\":", out); write_recorded_metric(out, m, filter, 1);
        fputs(",\"transport_stdio_bytes\":", out); write_recorded_metric(out, m, filter, 2); fputc('}', out);
        fputs(",\"unmeasured\":{\"total_manual_interventions\":null,\"network_transfer_bytes\":null,\"provider_usage\":null}", out);
    } else fputs(",\"unmeasured\":{\"unknown_receiver_outcomes\":null,\"manual_interventions\":null,\"network_transfer_bytes\":null}", out);
    fprintf(out, ",\"coverage\":{\"partial\":%s,\"partial_runs\":%zu,\"warnings\":%zu,\"last_warning\":",
            (m->warnings || cohort.partial_runs) ? "true" : "false", cohort.partial_runs, m->warnings);
    write_json_string(out, m->warning);
    fputs("},\"remote\":{\"state\":\"unavailable\",\"transfer_bytes\":null,\"provider_cost\":null,\"cpu_seconds\":null,\"memory_bytes\":null,\"tokens\":null}}", out);
    return !ferror(out);
}

static void write_metric_delta(FILE *out, const struct hs_metric_summary *left,
                               const struct hs_metric_summary *right) {
    bool known = left->known && right->known;
    fprintf(out, "{\"state\":\"%s\"", known ? "known" : (!left->eligible || !right->eligible) ? "unavailable" : "unknown");
    if (known) fprintf(out, ",\"mean\":%lld,\"max\":%lld,\"p50\":%lld,\"p95\":%lld",
                       (long long)(right->sum / right->known) - (long long)(left->sum / left->known),
                       (long long)right->maximum - (long long)left->maximum,
                       (long long)right->p50 - (long long)left->p50,
                       (long long)right->p95 - (long long)left->p95);
    else fputs(",\"mean\":null,\"max\":null,\"p50\":null,\"p95\":null", out);
    fputc('}', out);
}

bool hs_write_metrics_json(FILE *out, const struct hs_model *m,
                           const struct hs_filter *filter) {
    if (!out || !m || !filter) return false;
    return write_one(out, m, filter);
}

bool hs_write_metrics_compare_json(FILE *out, const struct hs_model *left,
                                   const struct hs_model *right,
                                   const struct hs_filter *filter) {
    enum hs_metric metric;
    struct hs_summary lc, rc;
    if (!out || !left || !right || !filter) return false;
    hs_summarize(left, filter, &lc); hs_summarize(right, filter, &rc);
    fprintf(out, "{\"schema_version\":%u,\"availability\":\"known\",\"left\":",
            export_version(left->schema_version > right->schema_version ? left->schema_version : right->schema_version));
    write_one(out, left, filter);
    fputs(",\"right\":", out);
    write_one(out, right, filter);
    fputs(",\"delta\":{\"metrics\":{", out);
    for (metric = HS_QUEUE; metric < HS_METRICS; metric++) {
        struct hs_metric_summary l, r;
        if (metric != HS_QUEUE) fputc(',', out);
        hs_metric_summarize(left, filter, metric, &l);
        hs_metric_summarize(right, filter, metric, &r);
        fprintf(out, "\"%s\":", metric_name(metric)); write_metric_delta(out, &l, &r);
    }
    fputs("}}}", out);
    return !ferror(out);
}

static void write_sessions_text(FILE *out, const char *label, const struct hs_model *model,
                                const struct hs_filter *filter) {
    struct hs_session_summary s;
    hs_session_summarize(model, filter, &s);
    fprintf(out, "%s sessions count=%zu open=%zu length_known=%zu length_sum=%llu activity_known=%zu active_sum=%llu quiet_sum=%llu coverage_known=%zu unobserved_sum=%llu reported_sum=%llu inferred_from=tmux\n",
            label, s.sessions, s.open, s.length_known, (unsigned long long)s.length_sum, s.activity_known,
            (unsigned long long)s.active_sum, (unsigned long long)s.quiet_sum, s.coverage_known,
            (unsigned long long)s.unobserved_sum, (unsigned long long)s.reported_sum);
}

static void write_text_cohort(FILE *out, const char *label, const struct hs_model *model,
                              const struct hs_filter *filter) {
    struct hs_summary cohort;
    struct recovery_outcomes recovered = recovery_counts(model, filter);
    hs_summarize(model, filter, &cohort);
    fprintf(out, "%s observed=%llu runs=%zu steps=%zu partial_runs=%zu warnings=%zu\n",
            label, (unsigned long long)model->observed, cohort.runs, cohort.steps,
            cohort.partial_runs, model->warnings);
    fprintf(out, "%s recovery_outcomes scope=coordinator_owner_recovery eligible=%zu known_terminal=%zu succeeded=%zu failed_or_cancelled=%zu unknown=%zu missing_history=%zu\n",
            label, recovered.eligible, recovered.successes + recovered.failures,
            recovered.successes, recovered.failures, recovered.unknown, recovered.missing_history);
    if (model->schema_version >= 3) write_recorded_text(out, label, model, filter);
    if (model->schema_version >= 5) write_sessions_text(out, label, model, filter);
    for (enum hs_metric metric = HS_QUEUE; metric < HS_METRICS; metric++) {
        struct hs_metric_summary value;
        hs_metric_summarize(model, filter, metric, &value);
        fprintf(out, "%s %s unit=%s state=%s eligible=%zu known=%zu ", label, metric_name(metric),
                metric == HS_RECOVERIES ? "count" : "seconds", evidence_state(&value), value.eligible, value.known);
        if (value.known) fprintf(out, "sum=%llu mean=%llu max=%llu p50=%llu p95=%llu\n",
            (unsigned long long)value.sum, (unsigned long long)(value.sum / value.known),
            (unsigned long long)value.maximum, (unsigned long long)value.p50, (unsigned long long)value.p95);
        else fputs("sum=unknown mean=unknown max=unknown p50=unknown p95=unknown\n", out);
    }
}

bool hs_write_metrics_compare_text(FILE *out, const struct hs_model *left,
                                   const struct hs_model *right,
                                   const struct hs_filter *filter) {
    if (!out || !left || !right || !filter) return false;
    fputs("Saved workflow statistics comparison; delta is right minus left. No ranking or causal claim.\n", out);
    write_text_cohort(out, "left", left, filter); write_text_cohort(out, "right", right, filter);
    for (enum hs_metric metric = HS_QUEUE; metric < HS_METRICS; metric++) {
        struct hs_metric_summary l, r;
        hs_metric_summarize(left, filter, metric, &l); hs_metric_summarize(right, filter, metric, &r);
        fprintf(out, "delta %s ", metric_name(metric));
        if (l.known && r.known) fprintf(out, "state=known mean=%lld max=%lld p50=%lld p95=%lld\n",
            (long long)(r.sum / r.known) - (long long)(l.sum / l.known),
            (long long)r.maximum - (long long)l.maximum,
            (long long)r.p50 - (long long)l.p50, (long long)r.p95 - (long long)l.p95);
        else fprintf(out, "state=%s mean=unknown max=unknown p50=unknown p95=unknown\n",
                     !l.eligible || !r.eligible ? "unavailable" : "unknown");
    }
    fputs("unmeasured: total_manual_interventions, network_transfer_bytes, provider_usage.\n", out);
    return !ferror(out);
}
