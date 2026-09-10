#!/usr/bin/env python3
import csv,json,math,os,random
root=os.environ['HYDRA_WORKFLOW_INPUTS_DIR']; out=os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR']; manifest=json.load(open(os.path.join(root,'manifest')))
def q(v,p): return sorted(v)[max(0,min(len(v)-1,math.ceil(p*len(v))-1))] if v else None
def invalid(reason, rows=None):
 r={'schema_version':1,'manifest':manifest,'raw_summary':{'rows':len(rows or []),'distinct_ids':len(set(x.get('sample_id','') for x in (rows or []))),'failures':[],'invalid_reason':reason},'baseline':{'n':0,'median_ns':None,'p95_ns':None},'candidate':{'n':0,'median_ns':None,'p95_ns':None},'paired_uncertainty':{'method':'paired bootstrap of median relative change','seed':1729,'resamples':10000,'confidence':'90% percentile interval','lower':None,'upper':None},'outcome':'invalid/insufficient measurement','limits':'One synthetic line-count workload on one host; this does not establish Hydra-wide CPU, latency, scalability, or user-facing improvement.'}
 json.dump(r,open(os.path.join(out,'analysis.json'),'w'),sort_keys=True,indent=2); open(os.path.join(out,'analysis.json'),'a').write('\n')
try:
 rows=list(csv.DictReader(open(os.path.join(root,'raw'),newline='')))
 if not rows or set(rows[0]) != {'sample_id','implementation','trial','order','elapsed_ns','status','count','returncode'}: raise ValueError('unexpected raw columns')
 by={'baseline':{},'candidate':{}}; failures=[]
 for x in rows:
  if x['implementation'] not in by: raise ValueError('unknown implementation')
  t=int(x['trial']); n=int(x['elapsed_ns']);
  if t not in range(1,11) or n<0: raise ValueError('invalid trial or elapsed value')
  if x['status']!='ok' or x['count']!='4003': failures.append(x)
  elif t in by[x['implementation']]: raise ValueError('duplicate trial')
  else: by[x['implementation']][t]=n
 if any(set(by[x]) != set(range(1,11)) for x in by): raise ValueError('missing trial')
 pairs=[by['candidate'][i]/by['baseline'][i]-1 for i in range(1,11)]; rng=random.Random(1729); boots=[q([pairs[rng.randrange(10)] for _ in pairs],.5) for _ in range(10000)]
 def st(v): return {'n':len(v),'median_ns':q(v,.5),'p95_ns':q(v,.95)}
 ci={'method':'paired bootstrap of median relative change','seed':1729,'resamples':10000,'confidence':'90% percentile interval','lower':q(boots,.05),'upper':q(boots,.95)}
 warmup_ok=all(x['status']=='ok' and x['count']=='4003' for x in manifest.get('warmups',[]))
 valid=len(rows)==20 and warmup_ok and not failures
 outcome='invalid/insufficient measurement' if not valid else ('target established' if st(list(by['candidate'].values()))['median_ns']<=st(list(by['baseline'].values()))['median_ns']*.9 and ci['upper']<0 else 'target not established')
 r={'schema_version':1,'manifest':manifest,'raw_summary':{'rows':len(rows),'distinct_ids':len(set(x['sample_id'] for x in rows)),'failures':failures,'invalid_reason':None if valid else ('warmup failure' if not warmup_ok else 'trial failure')},'baseline':st(list(by['baseline'].values())),'candidate':st(list(by['candidate'].values())),'paired_uncertainty':ci,'outcome':outcome,'limits':'One synthetic line-count workload on one host; this does not establish Hydra-wide CPU, latency, scalability, or user-facing improvement.'}
 json.dump(r,open(os.path.join(out,'analysis.json'),'w'),sort_keys=True,indent=2); open(os.path.join(out,'analysis.json'),'a').write('\n')
except Exception as e: invalid(type(e).__name__+': '+str(e), locals().get('rows',[]))
