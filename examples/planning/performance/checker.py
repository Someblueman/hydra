#!/usr/bin/env python3
import csv,json,math,os,sys
report=json.load(open(sys.argv[1])); rows=list(csv.DictReader(open(sys.argv[2],newline='')))
def q(v,p):
 s=sorted(v); return s[max(0,min(len(s)-1,math.ceil(p*len(s))-1))]
vals={"baseline":{},"candidate":{}}
for r in rows:
 if r['status']=='ok' and r['count']=='4003': vals[r['implementation']][int(r['trial'])]=int(r['elapsed_ns'])
fail=[r for r in rows if r['status']!='ok' or r['count']!='4003']
checks=[]
checks.append(('row_count',len(rows)==20)); checks.append(('failures_preserved',report['raw_summary']['failures']==fail))
for impl in ('baseline','candidate'):
 v=list(vals[impl].values()); got=report[impl]; checks += [(impl+'_n',got['n']==len(v)),(impl+'_median',got['median_ns']==q(v,.5) if v else False),(impl+'_p95',got['p95_ns']==q(v,.95) if v else False)]
pairs=[vals['candidate'][i]/vals['baseline'][i]-1.0 for i in sorted(set(vals['baseline'])&set(vals['candidate']))]
pu=report['paired_uncertainty']['values']; checks += [('paired_n',pu['n']==len(pairs)),('paired_p05',pu['p05_relative_change']==q(pairs,.05) if pairs else False),('paired_p95',pu['p95_relative_change']==q(pairs,.95) if pairs else False)]
valid=(len(rows)==20 and not fail and len(vals['baseline'])==10 and len(vals['candidate'])==10 and len(pairs)==10)
expected='invalid/insufficient measurement' if not valid else ('target established' if report['candidate']['median_ns']<=report['baseline']['median_ns']*.9 and pu['p95_relative_change']<0 else 'target not established')
checks.append(('outcome',report['outcome']==expected)); checks.append(('raw_binding',report['raw_samples']==open(sys.argv[2]).read().splitlines()))
passed=all(x[1] for x in checks)
json.dump({'schema_version':1,'verdict':'pass' if passed else 'fail','evidence':'; '.join(k for k,v in checks if not v) or 'all independent recomputations matched'},sys.stdout); print()
sys.exit(0 if passed else 1)
