#!/usr/bin/env python3
import csv,hashlib,json,math,os,random,sys
inp=os.environ['HYDRA_WORKFLOW_INPUTS_DIR']; out=os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR']; report=json.load(open(os.path.join(inp,'subject'))); rows=list(csv.DictReader(open(os.path.join(inp,'raw'),newline=''))); manifest=json.load(open(os.path.join(inp,'manifest')))
def q(v,p): return sorted(v)[max(0,min(len(v)-1,math.ceil(p*len(v))-1))] if v else None
def dg(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()
checks=[]; checks += [('raw_binding',report['raw_samples']==open(os.path.join(inp,'raw')).read().splitlines())]
for k,v in manifest['sources'].items(): checks += [('source_'+k,dg(os.path.join(os.environ.get('HYDRA_WORKFLOW_REPO_ROOT',os.getcwd()),v['path']))==v['sha256'])]
checks += [('workload',dg(os.path.join(os.environ.get('HYDRA_WORKFLOW_INPUTS_DIR'), 'records'))==manifest['workload']['sha256'])]
vals={'baseline':{},'candidate':{}}; fails=[]; ids=[]
for r in rows:
 ids.append(r['sample_id'])
 if r['status']!='ok' or r['count']!='4003': fails.append(r)
 else: vals[r['implementation']][int(r['trial'])]=int(r['elapsed_ns'])
checks += [('rows',len(rows)==20),('ids',len(set(ids))==20),('failures',report['raw_summary']['failures']==fails)]
for k in vals:
 v=list(vals[k].values()); checks += [(k+'_stats',report[k]=={'n':len(v),'median_ns':q(v,.5),'p95_ns':q(v,.95)})]
pairs=[vals['candidate'][i]/vals['baseline'][i]-1 for i in sorted(set(vals['baseline'])&set(vals['candidate']))]; rng=random.Random(1729); boots=[q([pairs[rng.randrange(len(pairs))] for _ in pairs],.5) for _ in range(10000)] if pairs else []
ci=report['paired_uncertainty']; checks += [('ci',ci['lower']==q(boots,.05) and ci['upper']==q(boots,.95) and ci['seed']==1729 and ci['resamples']==10000)]
valid=len(rows)==20 and len(set(ids))==20 and not fails and all(len(vals[x])==10 for x in vals) and len(pairs)==10
expected='invalid/insufficient measurement' if not valid else ('target established' if report['candidate']['median_ns']<=report['baseline']['median_ns']*.9 and ci['upper']<0 else 'target not established'); checks += [('outcome',report['outcome']==expected)]
json.dump({'schema_version':1,'verdict':'pass' if all(v for _,v in checks) else 'fail','evidence':'; '.join(k for k,v in checks if not v) or 'all independent recomputations matched'},open(os.path.join(out,'assessment'),'w')); print(open(os.path.join(out,'assessment')).read()); sys.exit(0 if all(v for _,v in checks) else 1)
