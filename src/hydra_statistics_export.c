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
    fputs("{\"schema_version\":1,\"availability\":\"known\",\"cohort\":{\"runs\":", out);
    fprintf(out, "%zu,\"steps\":%zu},\"metrics\":{", cohort.runs, cohort.steps);
    for (metric = HS_QUEUE; metric < HS_METRICS; metric++) {
        struct hs_metric_summary summary;
        if (metric != HS_QUEUE) fputc(',', out);
        fprintf(out, "\"%s\":", metric_name(metric));
        hs_metric_summarize(m, filter, metric, &summary);
        write_summary(out, &summary);
    }
    fputs("}}", out);
    return !ferror(out);
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
        fprintf(out, "\"%s\":{\"state\":\"%s\"", metric_name(metric),
                (l.known && r.known) ? "known" : (!l.eligible || !r.eligible) ? "unavailable" : "unknown");
        if (l.known && r.known) {
            fprintf(out, ",\"mean\":%lld,\"max\":%lld,\"p50\":%lld,\"p95\":%lld",
                    (long long)(r.sum / r.known) - (long long)(l.sum / l.known),
                    (long long)r.maximum - (long long)l.maximum,
                    (long long)r.p50 - (long long)l.p50,
                    (long long)r.p95 - (long long)l.p95);
        } else fputs(",\"mean\":null,\"max\":null,\"p50\":null,\"p95\":null", out);
        fputc('}', out);
    }
    fputs("}}}}", out);
    return !ferror(out);
}
