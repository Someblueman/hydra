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
    assert(!load("HYDRA_STATISTICS\t6\t10\nZ\t0\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nZ\t1\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nZ\t0\t0\nX\tafter terminator\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nS\trun_missing\tstep\texec\trunning\t1\t1\t2\t-\t-\nZ\t0\t1\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nR\t../escape\ttest\tfailed\tp\t-\tcomplete\t-\t-\t-\t-\t0\nZ\t1\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nR\trun_a\ttest\tfailed\tp\t-\tcomplete\t-\t-\t-\t-\t0\nR\trun_a\ttest\tfailed\tp\t-\tcomplete\t-\t-\t-\t-\t0\nZ\t2\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t999999999999999999999999\nZ\t0\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t2\t10\nZ\t0\t0",m));
}

/* Schema 4 U rows attach exec-receipt evidence to a listed step. Unknown
 * counts stay unknown and never add zero to a total. */
#define USAGE_HEAD "HYDRA_STATISTICS\t4\t1788789600\nR\trun_a\tplan\tsucceeded\tp\t-\tcomplete\t-\t-\t-\t-\t1\t-\t-\t-\n" \
    "S\trun_a\timplement\texec\tsucceeded\t1\t1788789000\t1788789600\t-\t-\nS\trun_a\tspawn\tspawn\tsucceeded\t1\t-\t-\t-\t-\n"
static void usage_rows(struct hs_model *m) {
    struct hs_filter f = {0}; struct hs_summary s; const struct hs_usage *u;
    assert(load(USAGE_HEAD "U\trun_a\timplement\tcodex\tcodex-cli 0.99\t-\thigh\t3850000\t3740000\t23000\t-\nZ\t1\t2\n", m));
    u = &m->steps[0].usage;
    assert(u->recorded && !strcmp(u->profile, "codex") && !strcmp(u->version, "codex-cli 0.99") && !u->model[0] && !strcmp(u->effort, "high"));
    assert(u->known[HS_TOKENS_IN] && u->counts[HS_TOKENS_IN] == 3850000 && u->counts[HS_TOKENS_CACHED] == 3740000);
    assert(u->known[HS_TOKENS_OUT] && u->counts[HS_TOKENS_OUT] == 23000 && !u->known[HS_COST_MICROUSD]);
    assert(!m->steps[1].usage.recorded);
    hs_summarize(m, &f, &s);
    assert(s.agent_steps == 1 && s.usage_known[HS_TOKENS_IN] == 1 && s.usage[HS_TOKENS_IN] == 3850000);
    assert(s.usage_known[HS_COST_MICROUSD] == 0 && s.usage[HS_COST_MICROUSD] == 0);
}

/* Unknown steps, duplicates, schema 3 and malformed counts are refused. */
static void usage_malformed(struct hs_model *m) {
    assert(!load(USAGE_HEAD "U\trun_a\tmissing\tcodex\t-\t-\t-\t1\t1\t1\t-\nZ\t1\t2\n", m));
    assert(!load(USAGE_HEAD "U\trun_a\timplement\tcodex\t-\t-\t-\t1\t1\t1\t-\nU\trun_a\timplement\tcodex\t-\t-\t-\t1\t1\t1\t-\nZ\t1\t2\n", m));
    assert(!load(USAGE_HEAD "U\trun_a\timplement\tcodex\t-\t-\t-\tmany\t1\t1\t-\nZ\t1\t2\n", m));
    assert(!load("HYDRA_STATISTICS\t3\t10\nR\trun_a\tplan\tsucceeded\tp\t-\tcomplete\t-\t-\t-\t-\t1\t-\t-\t-\nS\trun_a\tx\texec\tsucceeded\t1\t-\t-\t-\t-\nU\trun_a\tx\tcodex\t-\t-\t-\t1\t1\t1\t-\nZ\t1\t1\n", m));
}

/* Schema 5 T rows attach remote task evidence to listed steps. Measurements
 * the receipt did not carry stay unknown with the recorded reason; location
 * filters and cohorts group the matched runs' steps by that evidence. */
static void remote_rows(struct hs_model *m) {
    struct hs_filter f = {0}; struct hs_summary s; struct hs_cohort c[HS_COHORTS];
    const struct hs_remote *r;
    FILE *fixture = fopen("tests/fixtures/tui/statistics-v5.tsv", "r");
    size_t n;
    assert(fixture && hs_load(fixture, m)); fclose(fixture);
    assert(m->schema_version == 5 && m->run_count == 3 && m->step_count == 7);
    r = &m->steps[3].remote;
    assert(r->recorded && !strcmp(r->host, "alpha") && !strcmp(r->transport, "ssh") && !strcmp(r->placement, "auto"));
    assert(r->generation_known && r->generation == 2 && !strcmp(r->state, "succeeded") && !r->reason[0]);
    assert(r->known[HS_WALL_SECONDS] && r->counts[HS_WALL_SECONDS] == 118 && r->known[HS_CPU_MICROSECONDS] && r->counts[HS_CPU_MICROSECONDS] == 3400000);
    assert(r->known[HS_PEAK_RSS_BYTES] && r->counts[HS_PEAK_RSS_BYTES] == 52428800 && r->counts[HS_TRANSPORT_CALLS] == 7 && r->counts[HS_TRANSPORT_BYTES] == 21970);
    assert(hs_remote_telemetry(r));
    r = &m->steps[4].remote;
    assert(r->recorded && !r->known[HS_PEAK_RSS_BYTES] && r->counts[HS_PEAK_RSS_BYTES] == 0 && !strcmp(r->reason, "peak_rss_bytes=rss_not_distinguishable"));
    r = &m->steps[5].remote;
    assert(r->recorded && !strcmp(r->placement, "pinned") && !r->known[HS_WALL_SECONDS] && !hs_remote_telemetry(r) && r->known[HS_TRANSPORT_BYTES]);
    r = &m->steps[6].remote;
    assert(r->recorded && !r->generation_known && !r->state[0] && !strcmp(r->reason, "receipt_unverified") && !hs_remote_telemetry(r));
    assert(!m->steps[0].remote.recorded && !m->steps[1].remote.recorded);
    assert(m->steps[3].usage.recorded && !strcmp(m->steps[3].usage.profile, "claude") && m->steps[3].usage.counts[HS_COST_MICROUSD] == 250000);
    hs_summarize(m, &f, &s);
    assert(s.steps == 7 && s.remote_steps == 4 && s.remote_telemetry == 2);
    assert(s.remote_known[HS_WALL_SECONDS] == 2 && s.remote[HS_WALL_SECONDS] == 227);
    assert(s.remote_known[HS_CPU_MICROSECONDS] == 2 && s.remote[HS_CPU_MICROSECONDS] == 5500000);
    assert(s.remote_known[HS_PEAK_RSS_BYTES] == 1 && s.remote[HS_PEAK_RSS_BYTES] == 52428800);
    assert(s.remote_known[HS_TRANSPORT_CALLS] == 3 && s.remote[HS_TRANSPORT_CALLS] == 17);
    assert(s.remote_known[HS_TRANSPORT_BYTES] == 3 && s.remote[HS_TRANSPORT_BYTES] == 51592);
    assert(hs_run_remote(m, 1, NULL) && hs_run_remote(m, 1, "alpha") && hs_run_remote(m, 1, "beta") && !hs_run_remote(m, 0, NULL) && !hs_run_remote(m, 2, "alpha"));
    memcpy(f.location, "local", 6); hs_summarize(m, &f, &s); assert(s.runs == 1 && s.steps == 3 && s.remote_steps == 0);
    memcpy(f.location, "remote", 7); hs_summarize(m, &f, &s); assert(s.runs == 2 && s.steps == 4 && s.remote_steps == 4);
    memcpy(f.location, "alpha", 6); hs_summarize(m, &f, &s); assert(s.runs == 1 && s.steps == 3);
    memcpy(f.location, "beta", 5); hs_summarize(m, &f, &s); assert(s.runs == 2 && s.steps == 4);
    memcpy(f.location, "gamma", 6); hs_summarize(m, &f, &s); assert(s.runs == 0 && s.steps == 0);
    memset(&f, 0, sizeof(f));
    n = hs_cohorts(m, &f, HS_BY_LOCATION, c, HS_COHORTS);
    assert(n == 2 && !strcmp(c[0].key, "local") && c[0].runs == 1 && c[0].steps == 3 && c[0].unmeasured == 3 && c[0].remote_steps == 0);
    assert(!strcmp(c[1].key, "remote") && c[1].runs == 2 && c[1].steps == 4 && c[1].unmeasured == 3 && c[1].remote_known[HS_WALL_SECONDS] == 2);
    assert(c[0].agent_steps == 1 && c[0].usage[HS_TOKENS_IN] == 3850000 && c[1].agent_steps == 1 && c[1].usage[HS_TOKENS_IN] == 120000);
    n = hs_cohorts(m, &f, HS_BY_HOST, c, HS_COHORTS);
    assert(n == 2 && !strcmp(c[0].key, "alpha") && c[0].runs == 1 && c[0].steps == 2 && c[0].remote[HS_WALL_SECONDS] == 227 && c[0].unmeasured == 1);
    assert(!strcmp(c[1].key, "beta") && c[1].runs == 2 && c[1].steps == 2 && !c[1].remote_known[HS_WALL_SECONDS] && c[1].unmeasured == 2 && strstr(c[1].reason, "no telemetry block"));
    n = hs_cohorts(m, &f, HS_BY_PLACEMENT, c, HS_COHORTS);
    assert(n == 2 && !strcmp(c[0].key, "auto") && c[0].steps == 2 && !strcmp(c[1].key, "pinned") && c[1].steps == 2);
    n = hs_cohorts(m, &f, HS_BY_GENERATION, c, HS_COHORTS);
    assert(n == 3 && !strcmp(c[0].key, "generation 2") && !strcmp(c[1].key, "generation 1") && !strcmp(c[2].key, "generation unknown") && c[2].steps == 1);
    n = hs_cohorts(m, &f, HS_BY_PROVIDER, c, HS_COHORTS);
    assert(n == 3 && !strcmp(c[0].key, "codex") && !strcmp(c[1].key, "no agent receipt") && c[1].steps == 4 && !strcmp(c[2].key, "claude") && c[2].unmeasured == 0);
    /* Capacity bounds the cohorts without touching the sample. */
    n = hs_cohorts(m, &f, HS_BY_GENERATION, c, 1);
    assert(n == 1 && c[0].steps == 2);
    memcpy(f.location, "alpha", 6);
    n = hs_cohorts(m, &f, HS_BY_HOST, c, HS_COHORTS);
    assert(n == 2 && c[0].runs == 1 && c[1].runs == 1 && c[1].steps == 1);
}

#define REMOTE_HEAD "HYDRA_STATISTICS\t5\t1788789600\nR\trun_a\tplan\tsucceeded\tp\t-\tcomplete\t-\t-\t-\t-\t1\t-\t-\t-\n" \
    "S\trun_a\twork\ttask\tsucceeded\t1\t1788789000\t1788789600\t-\t-\n"
static void remote_malformed(struct hs_model *m) {
    assert(load(REMOTE_HEAD "T\trun_a\twork\talpha\tssh\tpinned\t1\tsucceeded\t1\t2\t3\t4\t5\t-\nZ\t1\t1\n", m));
    assert(!load(REMOTE_HEAD "T\trun_a\tmissing\talpha\tssh\tpinned\t1\tsucceeded\t1\t2\t3\t4\t5\t-\nZ\t1\t1\n", m));
    assert(!load(REMOTE_HEAD "T\trun_a\twork\talpha\tssh\tpinned\t1\tsucceeded\t1\t2\t3\t4\t5\t-\nT\trun_a\twork\talpha\tssh\tpinned\t1\tsucceeded\t1\t2\t3\t4\t5\t-\nZ\t1\t1\n", m));
    assert(!load(REMOTE_HEAD "T\trun_a\twork\talpha\tssh\tmanual\t1\tsucceeded\t1\t2\t3\t4\t5\t-\nZ\t1\t1\n", m));
    assert(!load(REMOTE_HEAD "T\trun_a\twork\talpha\tssh\tpinned\tx\tsucceeded\t1\t2\t3\t4\t5\t-\nZ\t1\t1\n", m));
    assert(!load(REMOTE_HEAD "T\trun_a\twork\talpha\tssh\tpinned\t1\tsucceeded\tmany\t2\t3\t4\t5\t-\nZ\t1\t1\n", m));
    assert(!load(REMOTE_HEAD "T\trun_a\twork\talpha\tssh\tpinned\t1\tsucceeded\t1\t2\t3\t4\t5\nZ\t1\t1\n", m));
    assert(!load("HYDRA_STATISTICS\t4\t1788789600\nR\trun_a\tplan\tsucceeded\tp\t-\tcomplete\t-\t-\t-\t-\t1\t-\t-\t-\n"
                 "S\trun_a\twork\ttask\tsucceeded\t1\t-\t-\t-\t-\nT\trun_a\twork\talpha\tssh\tpinned\t1\tsucceeded\t1\t2\t3\t4\t5\t-\nZ\t1\t1\n", m));
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
    usage_rows(m);
    usage_malformed(m);
    invalid_metrics(m);
    remote_rows(m);
    remote_malformed(m);
    free(m);
    puts("Statistics: cohort reconciliation, dates, durations, unknowns, filters, remote cohorts and malformed framing passed");
    return 0;
}
