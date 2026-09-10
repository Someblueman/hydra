#!/usr/bin/env python3
import csv,hashlib,json,math,os,random,sys
inp=os.environ['HYDRA_WORKFLOW_INPUTS_DIR']; out=os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR']
def emit(verdict,evidence):
 json.dump({'schema_version':1,'verdict':verdict,'evidence':evidence},open(os.path.join(out,'assessment'),'w')); print(json.dumps({'schema_version':1,'verdict':verdict,'evidence':evidence})); sys.exit(0 if verdict=='pass' else 1)
def q(v,p): return sorted(v)[max(0,min(len(v)-1,math.ceil(p*len(v))-1))] if v else None
def dg(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(65536),b''): h.update(b)
 return h.hexdigest()
try:
 report=json.load(open(os.path.join(inp,'subject'))); sealed=json.load(open(os.path.join(inp,'manifest'))); actual=json.load(open(os.path.join(inp,'manifest')))
 rows=list(csv.DictReader(open(os.path.join(inp,'raw'),newline='')))
 if report['manifest'] != sealed: emit('fail','sealed manifest mismatch')
 if set(rows[0]) != {'sample_id','implementation','trial','order','elapsed_ns','status','count','returncode'}: emit('fail','unexpected raw columns')
 vals={'baseline':{},'candidate':{}}; fails=[]; ids=[]
 for r in rows:
  if r['implementation'] not in vals: emit('fail','unknown implementation')
  t=int(r['trial']); n=int(r['elapsed_ns']); ids.append(r['sample_id'])
  if t not in range(1,11) or n<0: emit('fail','invalid trial or elapsed')
  if r['status']!='ok' or r['count']!='4003': fails.append(r)
  elif t in vals[r['implementation']]: emit('fail','duplicate trial')
  else: vals[r['implementation']][t]=n
 if len(rows)!=20 or len(set(ids))!=20 or any(set(vals[k])!=set(range(1,11)) for k in vals): emit('fail','trial identity/count mismatch')
 if report['raw_samples'] != open(os.path.join(inp,'raw')).read().splitlines(): emit('fail','raw samples mismatch')
 if report['raw_summary']['failures'] != fails: emit('fail','failure evidence mismatch')
def stats(v): return {'n':len(v),'median_ns':q(v,.5),'p95_ns':q(v,.95)}
for k in vals:
 if report[k] != stats(list(vals[k].values())): emit('fail','summary mismatch: '+k)
 for k,v in sealed['sources'].items():
  current=os.path.join(os.environ.get('HYDRA_WORKFLOW_REPO_ROOT',os.getcwd()),v['path'])
  if dg(current)!=v['sha256']: emit('fail','source hash mismatch: '+k)
 if dg(os.path.join(inp,'records')) != sealed['workload']['sha256']: emit('fail','workload hash mismatch')
 pairs=[vals['candidate'][i]/vals['baseline'][i]-1 for i in range(1,11)]; rng=random.Random(1729); boots=[q([pairs[rng.randrange(10)] for _ in pairs],.5) for _ in range(10000)]
 ci=report['paired_uncertainty']; expected_ci=(q(boots,.05),q(boots,.95));
 if (ci['lower'],ci['upper']) != expected_ci or ci['seed']!=1729 or ci['resamples']!=10000: emit('fail','bootstrap evidence mismatch')
 expected='invalid/insufficient measurement' if fails or not all(x['status']=='ok' and x['count']=='4003' for x in sealed.get('warmups',[])) else ('target established' if report['candidate']['median_ns']<=report['baseline']['median_ns']*.9 and ci['upper']<0 else 'target not established')
 if report['outcome'] != expected: emit('fail','outcome mismatch')
 emit('pass','all independent recomputations matched')
except Exception as e: emit('fail','typed invalid evidence: '+type(e).__name__+': '+str(e))
