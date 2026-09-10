#!/usr/bin/env python3
import csv,json,math,os,random
root=os.environ['HYDRA_WORKFLOW_INPUTS_DIR']; out=os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR']; rows=list(csv.DictReader(open(os.path.join(root,'raw'),newline=''))); manifest=json.load(open(os.path.join(root,'manifest')))
def q(v,p): return sorted(v)[max(0,min(len(v)-1,math.ceil(p*len(v))-1))] if v else None
by={'baseline':{},'candidate':{}}; failures=[]; ids=[]
for r in rows:
 ids.append(r['sample_id']);
 if r['status']!='ok' or r['count']!='4003': failures.append(r)
 else: by[r['implementation']][int(r['trial'])]=int(r['elapsed_ns'])
pairs=[by['candidate'][i]/by['baseline'][i]-1 for i in sorted(set(by['baseline'])&set(by['candidate']))]
def st(v): return {'n':len(v),'median_ns':q(v,.5),'p95_ns':q(v,.95)}
rng=random.Random(1729); boots=[]
if pairs:
 for _ in range(10000): boots.append(q([pairs[rng.randrange(len(pairs))] for _ in pairs],.5))
ci={'method':'paired bootstrap of median relative change','seed':1729,'resamples':10000,'confidence':'90% percentile interval','lower':q(boots,.05),'upper':q(boots,.95)}
valid=len(rows)==20 and len(set(ids))==20 and not failures and all(len(by[x])==10 for x in by) and len(pairs)==10
outcome='invalid/insufficient measurement' if not valid else ('target established' if st(list(by['candidate'].values()))['median_ns']<=st(list(by['baseline'].values()))['median_ns']*.9 and ci['upper']<0 else 'target not established')
r={'schema_version':1,'manifest':manifest,'raw_summary':{'rows':len(rows),'distinct_ids':len(set(ids)),'failures':failures},'baseline':st(list(by['baseline'].values())),'candidate':st(list(by['candidate'].values())),'paired_uncertainty':ci,'outcome':outcome,'limits':'One synthetic line-count workload on one host; this does not establish Hydra-wide CPU, latency, scalability, or user-facing improvement.'}
json.dump(r,open(os.path.join(out,'analysis.json'),'w'),sort_keys=True,indent=2); open(os.path.join(out,'analysis.json'),'a').write('\n')
