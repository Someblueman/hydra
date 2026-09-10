#!/usr/bin/env python3
"""Public compiler comparison plus independently specified corruption cases."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'examples/planning/patterns'
BIN = ROOT / 'bin/hydra'

class Patterns(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='hydra-pattern-check-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.repo = self.base / 'repo'
        shutil.copytree(SOURCE, self.repo, ignore=shutil.ignore_patterns('__pycache__'))
        self.env = dict(os.environ, HYDRA_HOME=str(self.base / 'home'),
                        HYDRA_FLEET_BIN=os.environ.get("HYDRA_FLEET_BIN", str(ROOT / "build/hydra-fleet")))
        for args in [['git', 'init', '-q'], ['git', 'add', '.'],
                     ['git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                      '-c', 'commit.gpgSign=false', 'commit', '-qm', 'pattern source']]:
            subprocess.run(args, cwd=self.repo, check=True, capture_output=True)

    def cli(self, *args):
        result = subprocess.run([str(BIN), 'workflow', 'plan', *map(str,args)],
                                cwd=self.repo, env=self.env, capture_output=True, text=True)
        return result, json.loads(result.stdout)

    def test_matched_compilation_and_complete_explanation(self):
        compiled = []
        for name in ['serial', 'forkjoin']:
            path = self.base / (name + '.compiled.json')
            result, value = self.cli('compile', name + '.json', 'policy.json', path)
            self.assertEqual(result.returncode, 0, value)
            result, explained = self.cli('explain', path)
            self.assertEqual(result.returncode, 0, explained)
            self.assertEqual(len(explained['data']['nodes']), 8)
            compiled.append(path)
        result, value = self.cli('compare', *compiled)
        self.assertEqual(result.returncode, 0, value)
        self.assertTrue(value['data']['matched_scope'], value)
        self.assertEqual(value['data']['left']['edges'], value['data']['right']['edges'] + 1)
        self.assertEqual(value['data']['modeled_preference'], 'unresolved')

    def test_missing_contract_output_rejected(self):
        path = self.repo / 'serial.json'
        plan = json.loads(path.read_text())
        plan['data']['steps']['compose']['inputs']['a']['output'] = 'missing'
        path.write_text(json.dumps(plan))
        result, _ = self.cli('validate', path, 'policy.json')
        self.assertNotEqual(result.returncode, 0)

    def test_heldout_artifacts(self):
        # These expected outcomes are specified independently of the checker.
        cases = {'correct': (b'A:validated input\nB:validated input\n', True),
                 'wrong': (b'A:validated input\nB:wrong\n', False),
                 'missing-member': (b'A:validated input\n', False),
                 'reordered': (b'B:validated input\nA:validated input\n', False),
                 'truncated': (b'A:validated input\nB:validated input', False),
                 'extra': (b'A:validated input\nB:validated input\nextra\n', False),
                 'empty': (b'', False)}
        validation = self.base / 'validation'
        validation.write_text(json.dumps({'data': {'check': 'a'*64, 'check-recipe': 'b'*64}}))
        env = dict(self.env, HYDRA_WORKFLOW_INPUTS_DIR=str(self.base),
                   HYDRA_WORKFLOW_OUTPUTS_DIR=str(self.base), HYDRA_WORKFLOW_VALIDATION_FILE=str(validation))
        for name, (content, expected) in cases.items():
            with self.subTest(name=name):
                (self.base / 'subject').write_bytes(content)
                result = subprocess.run(['sh', 'check.sh'], cwd=self.repo, env=env, capture_output=True)
                report = json.loads((self.base / 'check').read_text())
                self.assertEqual(result.returncode == 0, expected, result.stderr)
                self.assertEqual(report['verdict'], 'pass' if expected else 'fail')
                self.assertEqual(report['evidence_status'], 'valid')
                self.assertEqual(report['evidence_records'][0]['counts'],
                                 {'executed': 1, 'failed': 0 if expected else 1, 'skipped': 0})

if __name__ == '__main__':
    unittest.main()
