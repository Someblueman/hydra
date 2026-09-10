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
    fputs("},\"unmeasured\":{\"unknown_receiver_outcomes\":null,\"manual_interventions\":null,\"network_transfer_bytes\":null}", out);
}

static bool write_one(FILE *out, const struct hs_model *m,
                      const struct hs_filter *filter) {
    enum hs_metric metric;
    struct hs_summary cohort;
    hs_summarize(m, filter, &cohort);
    fprintf(out, "{\"schema_version\":1,\"availability\":\"known\",\"observed\":%llu,\"filter\":{\"days\":%u,\"attention\":%s,\"query\":",
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
    fputs("{\"schema_version\":1,\"availability\":\"known\",\"left\":", out);
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
    fputs("unmeasured: receiver_unknown_outcomes, total_manual_interventions, network_transfer_bytes, provider_usage.\n", out);
    return !ferror(out);
}
