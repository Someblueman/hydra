#include "hydra_statistics.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool load(const char *text, struct hs_model *m) {
    FILE *f=tmpfile(); bool ok;
    assert(f); assert(fwrite(text,1,strlen(text),f)==strlen(text)); rewind(f);
    ok=hs_load(f,m); fclose(f); return ok;
}

static void metrics(struct hs_model *m) {
    struct hs_filter filter = {0};
    struct hs_metric_summary s;
    uint64_t value;
    hs_metric_summarize(m,&filter,HS_QUEUE,&s);
    assert(s.known==7 && s.eligible==10 && s.sum==70 && s.p50==10 && s.p95==10);
    hs_metric_summarize(m,&filter,HS_ELAPSED,&s);
    assert(s.known==4 && s.eligible==5 && s.sum==1400 && s.p50==300 && s.p95==500);
    hs_metric_summarize(m,&filter,HS_VERIFIED,&s);
    assert(s.known==2 && s.eligible==3 && s.sum==490 && s.p50==190 && s.p95==300);
    hs_metric_summarize(m,&filter,HS_RECOVERIES,&s);
    assert(s.known==6 && s.eligible==8 && s.sum==3 && s.p50==0 && s.p95==2);
    filter.attention=true;
    hs_metric_summarize(m,&filter,HS_RECOVERIES,&s);
    assert(s.known==2 && s.eligible==3 && s.sum==3);
    m->runs[1].verified=m->runs[1].completed+1;
    assert(hs_sample(m,HS_VERIFIED,1,&value)==HS_MISSING);
    m->runs[1].verified=1788699790;
    m->steps[0].ready=m->steps[0].first_started+1;
    assert(hs_sample(m,HS_QUEUE,0,&value)==HS_MISSING);
    m->steps[0].ready=m->steps[0].first_started;
    assert(hs_sample(m,HS_QUEUE,0,&value)==HS_KNOWN && value==0);
    m->steps[0].ready=m->steps[0].first_started-10;
}

static void invalid_metrics(struct hs_model *m) {
    struct hs_filter f={0};
    struct hs_metric_summary s;
    assert(load("HYDRA_STATISTICS\t2\t10\nR\trun_bad\ttest\tfailed\tp\t-\tcomplete\t11\t12\t-\tinvalid\t1\nS\trun_bad\tstep\texec\tfailed\t1\t9\t10\t11\t9\nZ\t1\t1\n",m));
    hs_metric_summarize(m,&f,HS_QUEUE,&s);
    assert(s.eligible==1 && s.known==0);
    hs_metric_summarize(m,&f,HS_ELAPSED,&s);
    assert(s.eligible==1 && s.known==0);
    hs_metric_summarize(m,&f,HS_VERIFIED,&s);
    assert(s.eligible==1 && s.known==0);
    hs_metric_summarize(m,&f,HS_RECOVERIES,&s);
    assert(s.eligible==1 && s.known==0);
}

static int probe(const char *path, const char *run_id) {
    struct hs_model *m=calloc(1,sizeof(*m));
    struct hs_filter f={0};
    FILE *input=fopen(path,"r");
    unsigned metric;
    assert(input && m && hs_load(input,m)); fclose(input);
    snprintf(f.query,sizeof(f.query),"%s",run_id);
    for (metric=0;metric<HS_METRICS;metric++) {
        struct hs_metric_summary s;
        hs_metric_summarize(m,&f,(enum hs_metric)metric,&s);
        printf("%u %zu %zu %llu\n",metric,s.eligible,s.known,(unsigned long long)s.sum);
    }
    free(m); return 0;
}

static void malformed(struct hs_model *m) {
    assert(!load("HYDRA_STATISTICS\t4\t10\nZ\t0\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nZ\t1\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nZ\t0\t0\nX\tafter terminator\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nS\trun_missing\tstep\texec\trunning\t1\t1\t2\t-\t-\nZ\t0\t1\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nR\t../escape\ttest\tfailed\tp\t-\tcomplete\t-\t-\t-\t-\t0\nZ\t1\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nR\trun_a\ttest\tfailed\tp\t-\tcomplete\t-\t-\t-\t-\t0\nR\trun_a\ttest\tfailed\tp\t-\tcomplete\t-\t-\t-\t-\t0\nZ\t2\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t999999999999999999999999\nZ\t0\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nZ\t0\t0",m));
}

int main(int argc, char **argv) {
    if (argc==3) return probe(argv[1],argv[2]);
    struct hs_model *m=calloc(1,sizeof(*m));
    struct hs_filter f={0}; struct hs_summary s;
    FILE *fixture=fopen("tests/fixtures/tui/statistics-v2.tsv","r");
    uint64_t duration;
    assert(m && fixture && hs_load(fixture,m)); fclose(fixture);
    metrics(m);
    hs_summarize(m,&f,&s);
    assert(s.runs==8 && s.steps==11 && s.run_states[HS_SUCCEEDED]==3 && s.run_states[HS_FAILED]==1);
    assert(s.run_states[HS_UNKNOWN]==1 && s.undated==1 && s.retries==3 && s.attempts_known==8);
    assert(s.duration_known==6 && s.duration_sum==570 && s.duration_max==120);
    assert(s.daily[6]==4 && s.daily[5]==1 && s.daily[4]==1);
    assert(hs_duration(m,&m->steps[0],&duration) && duration==30);
    assert(!hs_duration(m,&m->steps[1],&duration));
    f.days=1; hs_summarize(m,&f,&s);
    assert(s.runs==4 && s.steps==7 && s.excluded_undated==1 && s.undated==0);
    f.days=7; hs_summarize(m,&f,&s); assert(s.runs==6);
    f.days=0; f.attention=true; hs_summarize(m,&f,&s); assert(s.runs==3);
    memcpy(f.query,"RECOVERY",9); hs_summarize(m,&f,&s); assert(s.runs==2 && s.steps==2);
    memset(&f,0,sizeof(f)); memcpy(f.workflow,"release-check",14); hs_summarize(m,&f,&s); assert(s.runs==4);
    memset(&f,0,sizeof(f)); memcpy(f.query,"absent",7); hs_summarize(m,&f,&s); assert(s.runs==0 && s.duration_known==0);
    assert(load("HYDRA_STATISTICS\t2\t1788789600\nR\trun_a\ttest\tfailed\tp\t2026-02-30T00:00:00Z\tpartial\t-\t-\t-\t-\t0\nS\trun_a\tstep\texec\tfailed\t-\t12\t11\t-\t-\nZ\t1\t1\n",m));
    assert(!m->runs[0].created && m->runs[0].partial && !m->steps[0].attempts_known);
    assert(!hs_duration(m,&m->steps[0],&duration));
    assert(load("HYDRA_STATISTICS\t2\t1788789600\nR\trun_a\ttest\tfailed\tp\t2024-02-29T00:00:00Z\tcomplete\t-\t-\t-\t-\t0\nS\trun_a\tstep\texec\tfailed\t1\t1709164800\t1709164800\t-\t-\nZ\t1\t1\n",m));
    assert(m->runs[0].created==1709164800 && hs_duration(m,&m->steps[0],&duration) && duration==0);
    malformed(m);
    invalid_metrics(m);
    free(m);
    puts("Statistics: cohort reconciliation, dates, durations, unknowns, filters and malformed framing passed");
    return 0;
}
