#!/usr/bin/env python3
import json, os, shutil, subprocess, tempfile, unittest
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN=os.path.join(ROOT,'bin','hydra'); SRC=os.path.join(ROOT,'examples','planning','patterns')
FILES=['worker_a.sh','worker_b.sh','compose.sh','check.sh','policy.json']
class PlanPatterns(unittest.TestCase):
 def fixture(self,kind):
  d=tempfile.mkdtemp();
  for f in FILES+[kind+'.json']: shutil.copy(os.path.join(SRC,f),d)
  subprocess.run(['git','init','-q'],cwd=d,check=True); subprocess.run(['git','add','.'],cwd=d,check=True); subprocess.run(['git','-c','user.name=test','-c','user.email=test@example.invalid','commit','-qm','fixture'],cwd=d,check=True); return d
 def cli(self,args,cwd): return subprocess.run([BIN,*args],cwd=cwd,text=True,capture_output=True)
 def test_both_compile_and_inspect(self):
  for kind in ('serial','forkjoin'):
   d=self.fixture(kind); v=self.cli(['workflow','plan','validate',kind+'.json','policy.json'],d); self.assertEqual(v.returncode,0,v.stderr+v.stdout); self.assertTrue(json.loads(v.stdout)['ok'])
   c=tempfile.mktemp(prefix=kind+'-pattern-',suffix='.json'); x=self.cli(['workflow','plan','compile',kind+'.json','policy.json',c],d); self.assertEqual(x.returncode,0,x.stderr); self.assertEqual(self.cli(['workflow','plan','explain',c],d).returncode,0)
 def test_only_dependency_edges_differ(self):
  a=json.load(open(os.path.join(SRC,'serial.json'))); b=json.load(open(os.path.join(SRC,'forkjoin.json')))
  for d in (a,b): d['steps']=[dict(s,needs=[]) for s in d['steps']]
  self.assertEqual(a['objective'],b['objective']); self.assertEqual(a['envelope'],b['envelope']); self.assertEqual(a['data'],b['data']); self.assertEqual(a['requirements'],b['requirements'])
 def test_missing_artifact_contract_fails(self):
  d=self.fixture('serial'); p=os.path.join(d,'serial.json'); x=json.load(open(p)); x['data']['steps']['compose']['inputs']['a']['output']='missing'; open(p,'w').write(json.dumps(x)); v=self.cli(['workflow','plan','validate',p,os.path.join(d,'policy.json')],d); self.assertNotEqual(v.returncode,0)
 def test_heldout_checker_cases(self):
  with tempfile.TemporaryDirectory() as d:
   validation=os.path.join(d,'validation'); json.dump({'data':{'check':'a'*64,'check-recipe':'b'*64}},open(validation,'w'))
   for name,content,want in [('correct',b'A:validated input\nB:validated input\n',0),('wrong',b'A:validated input\nB:wrong\n',1),('missing',b'A:validated input\n',1),('reordered',b'B:validated input\nA:validated input\n',1),('extra',b'A:validated input\nB:validated input\nextra\n',1)]:
    subject=os.path.join(d,'subject'); out=os.path.join(d,'check'); open(subject,'wb').write(content); env=dict(os.environ,HYDRA_WORKFLOW_INPUTS_DIR=d,HYDRA_WORKFLOW_OUTPUTS_DIR=d,HYDRA_WORKFLOW_VALIDATION_FILE=validation); shutil.copy(os.path.join(SRC,'check.sh'),os.path.join(d,'check.sh')); os.chmod(os.path.join(d,'check.sh'),0o755); p=subprocess.run(['sh',os.path.join(d,'check.sh')],env=env); self.assertEqual(p.returncode,want,name); self.assertEqual(json.load(open(out))['verdict'],'pass' if want==0 else 'fail')
if __name__=='__main__': unittest.main()
