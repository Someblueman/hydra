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
