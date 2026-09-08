#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"

static const char *metric_name(enum hs_metric metric) {
    static const char *names[] = {"QUEUE DELAY", "TOTAL EXECUTION", "RECORDED VERIFICATION", "OWNER RECOVERIES"};
    return names[metric];
}

static const char *metric_boundary(enum hs_metric metric) {
    static const char *text[] = {
        "First ready -> first start / executed steps; excludes approval waits",
        "First drive -> terminal / terminal runs; includes retries and waits",
        "First drive -> independent pass / accepted plans; historical record",
        "Accepted owner resumptions / all runs; excludes approval and step retries"
    };
    return text[metric];
}

static void metric_value(const struct hs_model *m, enum hs_metric metric, size_t index, char *out, size_t capacity) {
    uint64_t value;
    enum hs_evidence evidence = hs_sample(m, metric, index, &value);
    if (evidence == HS_KNOWN) snprintf(out, capacity, "%llu%s", (unsigned long long)value, metric == HS_RECOVERIES ? "" : "s");
    else copy_text(out, capacity, evidence == HS_MISSING ? "-- unknown" : "not eligible");
}

static void metric_evidence(struct app *app, struct tv_canvas *c, struct tv_rect r, enum hs_metric metric) {
    struct statistics_view *v = app->statistics;
    size_t run, i, ordinal = 0;
    int row = r.y;
    char value[40];
    if (!v->count) { dashboard_text(c,1,row,r.width-2,TV_WARNING,"No matching runs / 0 reset"); return; }
    run = v->visible[v->selected];
    dashboard_text(c,1,row++,r.width-2,TV_SELECTED,"%zu/%zu %s / %s",v->selected+1,v->count,v->detail ? v->model->runs[run].id : v->model->runs[run].name,v->detail ? v->model->runs[run].state : v->model->runs[run].id);
    if (metric != HS_QUEUE) {
        metric_value(v->model,metric,run,value,sizeof(value));
        dashboard_text(c,1,row,r.width-2,TV_BASE,"%s / %s",v->model->runs[run].state,value);
        return;
    }
    for (i = 0; i < v->model->step_count && row < r.y+r.height; i++) {
        const struct hs_step *s = &v->model->steps[i];
        if (s->run != run || ordinal++ < v->step_scroll) continue;
        metric_value(v->model,metric,i,value,sizeof(value));
        dashboard_text(c,1,row++,r.width-2,TV_BASE,"%s / %s / %s",s->id,s->state,value);
    }
    if (row == r.y+1) dashboard_text(c,1,row,r.width-2,TV_WARNING,"No step at this offset / k back");
}

static void metric_trend(struct tv_canvas *c, struct tv_rect r, const struct hs_metric_summary *s) {
    size_t day;
    double maximum = 1;
    for (day=0;day<7;day++) if (s->daily_known[day]) {
        double mean = (double)s->daily_sum[day]/(double)s->daily_known[day];
        if (mean > maximum) maximum = mean;
    }
    dashboard_text(c,1,r.y,r.width-2,TV_BORDER,"MEAN BY RUN CREATION / 7 x 24h / known samples");
    for (day=0;day<7;day++) {
        int row = r.y+1+(int)day;
        char mean[32] = "--";
        double value = 0;
        if (s->daily_known[day]) {
            value = (double)s->daily_sum[day]/(double)s->daily_known[day];
            snprintf(mean,sizeof(mean),"%.1f",value);
        }
        dashboard_text(c,1,row,32,TV_BASE,"-%zud %10s / n=%zu",6-day,mean,s->daily_known[day]);
        tv_bar(c,(struct tv_rect){34,row,r.width-36,1},(size_t)(value*100),(size_t)(maximum*100),TV_BORDER);
    }
}

void statistics_metrics_render(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    struct statistics_view *v = app->statistics;
    enum hs_metric metric = (enum hs_metric)(v->metric_page-1);
    struct hs_metric_summary s;
    char mean[40]="--", percentiles[80]="p50 -- / p95 -- / max --";
    int evidence_row = r.y+4;
    time_t now = time(NULL);
    uint64_t age = now > 0 && (uint64_t)now > v->model->observed ? (uint64_t)now-v->model->observed : 0;
    hs_metric_summarize(v->model,&v->filter,metric,&s);
    if (s.known) {
        snprintf(mean,sizeof(mean),"%.1f%s",(double)s.sum/(double)s.known,metric==HS_RECOVERIES ? "" : "s");
        snprintf(percentiles,sizeof(percentiles),"p50 %llu / p95 %llu / max %llu",
            (unsigned long long)s.p50,(unsigned long long)s.p95,(unsigned long long)s.maximum);
    }
    dashboard_text(c,1,r.y,r.width-2,TV_STRONG,"%s / M next",metric_name(metric));
    dashboard_text(c,1,r.y+1,r.width-2,TV_BASE,"Mean %s / known %zu/%zu",mean,s.known,s.eligible);
    dashboard_text(c,1,r.y+2,r.width-2,TV_BASE,"%s",percentiles);
    dashboard_text(c,1,r.y+3,r.width-2,TV_BORDER,"Sample age %llus / -- unknown",(unsigned long long)age);
    if (r.height>6) {
        dashboard_text(c,1,r.y+4,r.width-2,TV_BORDER,"%s",metric_boundary(metric));
        dashboard_text(c,1,r.y+5,r.width-2,TV_BORDER,"%zu unknown / j,k runs / Enter then j,k steps / r refresh",s.eligible-s.known);
        evidence_row+=2;
    }
    if (r.height>=20 && r.width>=65 && !v->detail) {
        metric_trend(c,(struct tv_rect){0,evidence_row,r.width,8},&s); evidence_row+=9;
    }
    metric_evidence(app,c,(struct tv_rect){0,evidence_row,r.width,r.y+r.height-evidence_row},metric);
}
