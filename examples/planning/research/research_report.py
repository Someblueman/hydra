#!/usr/bin/env python3
import csv,json,math,hashlib,os
root=os.environ.get('HYDRA_WORKFLOW_REPO_ROOT',os.getcwd()); inp=os.path.join(os.environ['HYDRA_WORKFLOW_INPUTS_DIR'],'jobs'); out=os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR']
def digest(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()
jobs=[]
for n,r in enumerate(csv.DictReader(open(inp))): jobs.append({'id':r['id'],'arrival':int(r['arrival']),'duration':int(r['duration']),'order':n})
def sim(policy):
 t=0; left=jobs[:]; sched=[]
 while left:
  ready=[x for x in left if x['arrival']<=t]
  if not ready: t=min(x['arrival'] for x in left); ready=[x for x in left if x['arrival']<=t]
  x=min(ready,key=lambda z:(z['arrival'],z['order']) if policy=='FCFS' else (z['duration'],z['arrival'],z['order'])); left.remove(x); start=t; finish=t+x['duration']; sched.append({'id':x['id'],'start':start,'finish':finish,'wait':start-x['arrival'],'turnaround':finish-x['arrival']}); t=finish
 vals=[x['turnaround'] for x in sched]; waits=[x['wait'] for x in sched]; return {'schedule':sched,'mean_wait':sum(waits)/len(waits),'mean_turnaround':sum(vals)/len(vals),'p95_turnaround':sorted(vals)[math.ceil(.95*len(vals))-1],'max_wait':max(waits),'worst_wait_job':max(sched,key=lambda x:x['wait'])['id']}
fc, sj=sim('FCFS'),sim('SJF'); qualified={k:(v['p95_turnaround']<=22 and v['max_wait']<=16) for k,v in [('FCFS',fc),('SJF',sj)]}; rec=next((k for k,v in qualified.items() if v),'neither qualifies')
claims={'question':'For this supplied 12-job trace, compare non-preemptive FCFS and SJF under p95 turnaround <=22 and max wait <=16.','recommendation':rec,'constraint_checks':qualified,'limits':['12 synthetic jobs','one server','exact known durations','non-preemptive','no production evidence'],'competing_explanations':['mean and tail metrics can disagree because a few long waits dominate tails','observed long wait is not proof of starvation']}
r={'schema_version':3,'question':claims['question'],'provenance':{'data_path':'jobs.csv','data_sha256':digest(inp),'method':'deterministic non-preemptive simulation; tie arrival then input order'},'policies':{'FCFS':fc,'SJF':sj},'claims':claims,'claim_locations':['/claims/recommendation','/policies/FCFS','/policies/SJF'],'limitations':claims['limits']}; json.dump(r,open(os.path.join(out,'report.json'),'w'),sort_keys=True,indent=2); open(os.path.join(out,'report.json'),'a').write('\n')
