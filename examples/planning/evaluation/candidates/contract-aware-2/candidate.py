#!/usr/bin/env python3
import csv,hashlib,json,sys
def solve(t,x):
 if t=='feature.normalize_records.v1':return ''.join(''.join(chr(ord(c)+32) if 'A'<=c<='Z' else c for c in s.strip(' \t\r\n'))+'\n' for s in x['records'])
 if t=='feature.reject_duplicate_config.v1':
  d={}; z=x['file'] if isinstance(x,dict) else x
  for l in z.splitlines():
   k,v=l.split('=',1)
   if k in d:return {'error':'duplicate key: '+k,'verdict':'fail'}
   d[k]=v
  return {'config':d,'verdict':'pass'}
 if t=='feature.compose_two_members.v1':
  f=x.get('files',x);return f['member_a']+f['member_b']
 if t=='research.schedule_metrics.v1':
  z=x['file'] if isinstance(x,dict) else x;j=[(r['job'],int(r['duration']),i) for i,r in enumerate(csv.DictReader(z.splitlines()))]
  def m(a):
   n=0;w=[]
   for _,d,_ in a:w.append(n);n+=d
   return {'mean_wait':round(sum(w)/len(w),6),'max_wait':max(w)}
  return {'fcfs':m(j),'sjf':m(sorted(j,key=lambda q:(q[1],q[2])))}
 if t=='research.threshold_claim.v1':
  q=x['thresholds'];o={};p=[]
  for k,v in x['metrics'].items():
   a=v['p95_turnaround']<=q['p95_turnaround_max'];b=v['max_wait']<=q['max_wait_max'];o[k]='pass' if a and b else 'fail_both' if not a and not b else 'fail_p95_turnaround' if not a else 'fail_max_wait';p += [k] if a and b else []
  o['claim']='passing_policies:'+','.join(p) if p else 'no_policy_satisfies_both_thresholds';return o
 if t=='research.preserve_unknowns.v1':
  o=x['observations'];return {'completed':o['completed'],'rework_probability':None if o['rework_probability']=='unknown' else o['rework_probability'],'transfer_bytes':None if o['transfer_bytes']=='unknown' else o['transfer_bytes'],'claim':'finite_observation_only'}
 if t=='manifest.exact_members.v1':
  d=x['declared_members'];m=x['manifest_members'];a=[q for q in d if q not in m];b=[q for q in m if q not in d];return {'members':[q for q in d if q in m],'missing':a,'extra':b,'verdict':'pass' if len(m)==len(set(m)) and not a and not b else 'fail'}
 if t=='manifest.hash_bound_members.v1':
  c=sorted(x['manifest']);b=[]
  for k in c:
   v=x['files'].get(k);h=hashlib.sha256(v.encode() if isinstance(v,str) else v).hexdigest() if v is not None else None
   if h!=x['manifest'][k]:b.append(k)
  return {'checked':c,'mismatches':b,'verdict':'pass' if not b else 'fail'}
 if t=='manifest.selected_skipped.v1':
  d=x['declared_members'];s=x['selection'];return {'selected':[q for q in d if s[q]],'skipped':[{'id':q,'reason':'selection=false'} for q in d if not s[q]],'missing_records':[],'verdict':'pass'}
 raise ValueError('unsupported task_type')
for l in sys.stdin:
 try:
  q=json.loads(l);print(json.dumps(solve(q['task_type'],q['inputs']),ensure_ascii=False,separators=(',',':')),flush=True)
 except Exception as e:print(json.dumps({'error':str(e)}),flush=True)
