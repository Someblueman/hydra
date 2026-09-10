#!/usr/bin/env python3
import csv, json, math, os, statistics
raw=os.path.join(os.environ['HYDRA_WORKFLOW_INPUTS_DIR'],'raw')
rows=list(csv.DictReader(open(raw, newline='')))
by={"baseline":{},"candidate":{}}
failures=[]
for r in rows:
    impl=r['implementation']; trial=int(r['trial'])
    if r['status'] != 'ok' or r['count'] != '4003': failures.append(r); continue
    by.setdefault(impl,{})[trial]=int(r['elapsed_ns'])
def quant(v,p):
    if not v:return None
    s=sorted(v); return s[max(0,min(len(s)-1,math.ceil(p*len(s))-1))]
def stats(v):
    return {'n':len(v),'median_ns':quant(v,.5),'p95_ns':quant(v,.95)}
b=by['baseline']; c=by['candidate']; pairs=[]
for i in sorted(set(b)&set(c)):
    pairs.append((i,c[i]/b[i]-1.0))
rel=[x[1] for x in pairs]
bs=stats(list(b.values())); cs=stats(list(c.values()))
paired={'n':len(rel),'median_relative_change':quant(rel,.5) if rel else None,
        'p05_relative_change':quant(rel,.05) if rel else None,
        'p95_relative_change':quant(rel,.95) if rel else None}
valid=(len(rows)==20 and not failures and len(b)==10 and len(c)==10 and len(pairs)==10)
if not valid: outcome='invalid/insufficient measurement'
elif cs['median_ns'] <= bs['median_ns']*.90 and paired['p95_relative_change'] < 0: outcome='target established'
else: outcome='target not established'
report={'schema_version':1,'binding':{'baseline':'baseline.sh','candidate':'candidate.sh','workload':'records.txt','units':'nanoseconds','expected_count':4003},'protocol':{'warmups_per_implementation':2,'trials_per_implementation':10,'order':'baseline,candidate per trial','exclusive_process':True,'stopping_rule':'ten paired trials; any failure invalidates measurement'},'raw_summary':{'rows':len(rows),'failures':failures},'baseline':bs,'candidate':cs,'paired_uncertainty':{'method':'nearest-rank empirical interval over paired relative changes','confidence':'central 90% interval','values':paired},'outcome':outcome,'limits':'One synthetic line-count workload on one host; this does not establish Hydra-wide CPU, latency, scalability, or user-facing improvement.'}
json.dump(report,open(os.path.join(os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR'],'analysis.json'),'w'),sort_keys=True,indent=2); open(os.path.join(os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR'],'analysis.json'),'a').write('\n')
