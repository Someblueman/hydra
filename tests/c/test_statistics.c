#include "hydra_statistics.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool load(const char *text, struct hs_model *m) {
    FILE *f=tmpfile(); bool ok;
    assert(f); assert(fwrite(text,1,strlen(text),f)==strlen(text)); rewind(f);
    ok=hs_load(f,m); fclose(f); return ok;
}

int main(void) {
    struct hs_model *m=calloc(1,sizeof(*m));
    struct hs_filter f={0}; struct hs_summary s;
    FILE *fixture=fopen("tests/fixtures/tui/statistics-v1.tsv","r");
    uint64_t duration;
    assert(m && fixture && hs_load(fixture,m)); fclose(fixture);
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
    assert(load("HYDRA_STATISTICS\t1\t1788789600\nR\trun_a\ttest\tfailed\tp\t2026-02-30T00:00:00Z\tpartial\nS\trun_a\tstep\texec\tfailed\t-\t12\t11\nZ\t1\t1\n",m));
    assert(!m->runs[0].created && m->runs[0].partial && !m->steps[0].attempts_known);
    assert(!hs_duration(m,&m->steps[0],&duration));
    assert(load("HYDRA_STATISTICS\t1\t1788789600\nR\trun_a\ttest\tfailed\tp\t2024-02-29T00:00:00Z\tcomplete\nS\trun_a\tstep\texec\tfailed\t1\t1709164800\t1709164800\nZ\t1\t1\n",m));
    assert(m->runs[0].created==1709164800 && hs_duration(m,&m->steps[0],&duration) && duration==0);
    assert(!load("HYDRA_STATISTICS\t2\t10\nZ\t0\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t1\t10\n",m));
    assert(!load("HYDRA_STATISTICS\t1\t10\nZ\t1\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t1\t10\nZ\t0\t0\nX\tafter terminator\n",m));
    assert(!load("HYDRA_STATISTICS\t1\t10\nS\trun_missing\tstep\texec\trunning\t1\t1\t2\nZ\t0\t1\n",m));
    assert(!load("HYDRA_STATISTICS\t1\t10\nR\t../escape\ttest\tfailed\tp\t-\tcomplete\nZ\t1\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t1\t10\nR\trun_a\ttest\tfailed\tp\t-\tcomplete\nR\trun_a\ttest\tfailed\tp\t-\tcomplete\nZ\t2\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t1\t999999999999999999999999\nZ\t0\t0\n",m));
    assert(!load("HYDRA_STATISTICS\t1\t10\nZ\t0\t0",m));
    free(m);
    puts("Statistics: cohort reconciliation, dates, durations, unknowns, filters and malformed framing passed");
    return 0;
}
