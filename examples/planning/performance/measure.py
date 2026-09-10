#!/usr/bin/env python3
import csv, hashlib, json, os, platform, random, subprocess, sys, time
root=os.environ.get('HYDRA_WORKFLOW_REPO_ROOT', os.getcwd()); inp=os.path.join(os.environ['HYDRA_WORKFLOW_INPUTS_DIR'],'records'); out=os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR']
def digest(path):
 h=hashlib.sha256();
 with open(path,'rb') as f:
  for b in iter(lambda:f.read(65536),b''): h.update(b)
 return h.hexdigest()
def run(cmd):
 t=time.perf_counter_ns(); p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True); elapsed=time.perf_counter_ns()-t
 val=p.stdout.strip(); ok=p.returncode==0 and val=='4003'
 return elapsed,ok,val,p.returncode
scripts={'baseline':os.path.join(root,'baseline.sh'),'candidate':os.path.join(root,'candidate.sh')}
order=['baseline','candidate']; random.Random(1729).shuffle(order)
for name in scripts:
 for _ in range(2):
  _,ok,val,rc=run([scripts[name],inp])
  if not ok: raise SystemExit('warmup failed for '+name)
rows=[]
for trial in range(1,11):
 for name in order:
  elapsed,ok,val,rc=run([scripts[name],inp]); rows.append({'sample_id':f'{trial:02d}-{name}','implementation':name,'trial':trial,'order':order.index(name),'elapsed_ns':elapsed,'status':'ok' if ok else 'fail','count':val,'returncode':rc})
with open(os.path.join(out,'raw.csv'),'w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys()); w.writeheader(); w.writerows(rows)
manifest={'schema_version':1,'workload':{'path':'records.txt','sha256':digest(inp),'bytes':os.path.getsize(inp)},'commands':{k:[v,inp] for k,v in scripts.items()},'sources':{k:{'path':os.path.basename(v),'sha256':digest(v),'bytes':os.path.getsize(v)} for k,v in scripts.items()},'environment':{'python':sys.version.split()[0],'platform':platform.platform(),'machine':platform.machine(),'cwd':root,'timer':'time.perf_counter_ns'},'protocol':{'warmups':2,'trials':10,'seed':1729,'order':order,'exclusive_process':True,'expected_count':'4003'}}
json.dump(manifest,open(os.path.join(out,'manifest.json'),'w'),sort_keys=True,indent=2); open(os.path.join(out,'manifest.json'),'a').write('\n')
