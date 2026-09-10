#!/usr/bin/env python3
import csv,hashlib,json,sys
def solve(k,v):
 if k=='feature.normalize_records.v1':return ''.join(''.join(chr(ord(c)+32) if 'A'<=c<='Z' else c for c in z.strip(' \t\r\n'))+'\n' for z in v['records'])
 if k=='feature.reject_duplicate_config.v1':
  d={};s=v if isinstance(v,str) else v['file']
  for line in s.splitlines():
   a,b=line.split('=',1)
   if a in d:return {'error':'duplicate key: '+a,'verdict':'fail'}
   d[a]=b
  return {'config':d,'verdict':'pass'}
 if k=='feature.compose_two_members.v1':
  q=v.get('files',v);return q['member_a']+q['member_b']
 if k=='research.schedule_metrics.v1':
  s=v if isinstance(v,str) else v['file'];rows=list(csv.DictReader(s.splitlines()));j=[(r['job'],int(r['duration']),i) for i,r in enumerate(rows)]
  def f(q):
   n=0;w=[]
   for _,d,_ in q:w.append(n);n+=d
   return {'mean_wait':round(sum(w)/len(w),6),'max_wait':max(w)}
  return {'fcfs':f(j),'sjf':f(sorted(j,key=lambda a:(a[1],a[2])))}
 if k=='research.threshold_claim.v1':
  t=v['thresholds'];o={};good=[]
  for n,m in v['metrics'].items():
   a=m['p95_turnaround']<=t['p95_turnaround_max'];b=m['max_wait']<=t['max_wait_max'];o[n]='pass' if a and b else 'fail_both' if not a and not b else 'fail_p95_turnaround' if not a else 'fail_max_wait';good += [n] if a and b else []
  o['claim']='passing_policies:'+','.join(good) if good else 'no_policy_satisfies_both_thresholds';return o
 if k=='research.preserve_unknowns.v1':
  o=v['observations'];return {'completed':o['completed'],'rework_probability':None if o['rework_probability']=='unknown' else o['rework_probability'],'transfer_bytes':None if o['transfer_bytes']=='unknown' else o['transfer_bytes'],'claim':'finite_observation_only'}
 if k=='manifest.exact_members.v1':
  d=v['declared_members'];m=v['manifest_members'];return {'members':[x for x in d if x in m],'missing':[x for x in d if x not in m],'extra':[x for x in m if x not in d],'verdict':'pass' if len(m)==len(set(m)) and set(d)==set(m) else 'fail'}
 if k=='manifest.hash_bound_members.v1':
  keys=sorted(v['manifest']);bad=[]
  for x in keys:
   y=v['files'].get(x);h=hashlib.sha256(y.encode() if isinstance(y,str) else y).hexdigest() if y is not None else ''
   if h!=v['manifest'][x]:bad.append(x)
  return {'checked':keys,'mismatches':bad,'verdict':'pass' if not bad else 'fail'}
 if k=='manifest.selected_skipped.v1':
  d=v['declared_members'];s=v['selection'];return {'selected':[x for x in d if s[x]],'skipped':[{'id':x,'reason':'selection=false'} for x in d if not s[x]],'missing_records':[],'verdict':'pass'}
 raise ValueError('unsupported task_type')
for line in sys.stdin:
 try:
  q=json.loads(line);print(json.dumps(solve(q['task_type'],q['inputs']),ensure_ascii=False,separators=(',',':')),flush=True)
 except Exception as e:print(json.dumps({'error':str(e)}),flush=True)
