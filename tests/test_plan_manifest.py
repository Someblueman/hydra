#!/usr/bin/env python3
"""Public PRE-COMPILE finite-manifest lowering and independent checker tests."""
import json, os, shutil, subprocess, tempfile, unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'examples/planning/manifest-map'
PRECOMPILER = os.environ.get('HYDRA_PLAN_PRECOMPILE_BIN', str(ROOT / 'build/plan-precompile'))

class ManifestMap(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='hydra-manifest-map-')
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name) / 'repo'
        shutil.copytree(SOURCE, self.repo)
        shutil.copy2(ROOT / "examples/planning/native/payload.sh", self.repo / "payload.sh")
        self.env = dict(os.environ, HYDRA_HOME=str(Path(self.temp.name) / 'home'),
                        HYDRA_FLEET_BIN=os.environ.get('HYDRA_FLEET_BIN', str(ROOT / 'build/hydra-fleet')))
        for args in [['git', 'init', '-q'], ['git', 'add', '.'],
                     ['git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                      '-c', 'commit.gpgSign=false', 'commit', '-qm', 'manifest source']]:
            subprocess.run(args, cwd=self.repo, check=True, capture_output=True)

    def precompile(self, manifest=None, success=True):
        if manifest is not None:
            (self.repo / 'manifest.json').write_text(json.dumps(manifest))
            subprocess.run(['git', 'add', 'manifest.json'], cwd=self.repo, check=True, capture_output=True)
            subprocess.run(['git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'manifest update'], cwd=self.repo, check=True, capture_output=True)
        output = self.repo / 'plan.json'
        result = subprocess.run([PRECOMPILER, 'manifest', 'manifest.json', output], cwd=self.repo,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0 if success else 1, result.stderr)
        return output

    def compile(self, plan):
        output = self.repo / f"compiled-{len(list(self.repo.glob('compiled-*.json')))}.json"
        result = subprocess.run([str(ROOT / 'bin/hydra'), 'workflow', 'plan', 'compile', plan, 'policy.json', output],
                                cwd=self.repo, env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads(output.read_text())

    def run_payloads(self, plan, shell):
        inputs = Path(self.temp.name) / 'payload inputs'
        outputs = Path(self.temp.name) / 'payload outputs'
        inputs.mkdir(exist_ok=True); outputs.mkdir(exist_ok=True)
        env = dict(self.env, HYDRA_WORKFLOW_INPUTS_DIR=str(inputs), HYDRA_WORKFLOW_OUTPUTS_DIR=str(outputs))
        for step in plan['steps']:
            if step['id'].startswith('work-item-'):
                subprocess.run([shell, *step['args']['argv'][1:]], cwd=self.repo, env=env,
                               check=True, capture_output=True, timeout=5)
                ident = step['id'].removeprefix('work-item-')
                shutil.copy2(outputs / f'result-{ident}.json', inputs / f'member-{ident}')
        compose = next(step['args']['argv'] for step in plan['steps'] if step['id'] == 'compose')
        result = subprocess.run([shell, *compose[1:]], cwd=self.repo, env=env,
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads((outputs / 'report.json').read_text()), compose, env

    def test_empty_bounded_and_conditional_manifests_lower_and_compile(self):
        for items in ([], [{'id': 'only', 'value': 7, 'enabled': False}],
                      [{'id': 'a', 'value': -2, 'enabled': True}, {'id': 'b', 'value': 3, 'enabled': False}],
                      [{'id': ident, 'value': 1, 'enabled': True} for ident in ['compose', 'check', 'manifest', 'enabled', 'skipped']],
                      [{'id': f'i{i}', 'value': i - 4, 'enabled': True} for i in range(8)],
                      [{'id': f'i{i}' + 'x' * 30, 'value': -10 if i % 2 else 10, 'enabled': i % 3 != 0} for i in range(8)]):
            with self.subTest(items=items):
                plan = self.precompile({'schema_version': 1, 'items': items})
                value = json.loads(plan.read_text())
                ids = [x['id'] for x in value['steps']]
                self.assertEqual(sum(x['enabled'] for x in items), sum(x.startswith('work-') for x in ids))
                self.compile(plan)
                expected = [{'id': x['id'], 'value': x['value'], 'square': x['value'] ** 2} if x['enabled']
                            else {'id': x['id'], 'value': x['value'], 'status': 'skipped'} for x in items]
                for shell in ('sh', 'dash'):
                    report, _, _ = self.run_payloads(value, shell)
                    self.assertEqual(report, {'schema_version': 1, 'members': expected})

    def test_join_refuses_missing_worker_output(self):
        path = self.precompile({'schema_version': 1, 'items': [{'id': 'a', 'value': 2, 'enabled': True}]})
        _, compose, env = self.run_payloads(json.loads(path.read_text()), 'sh')
        (Path(env['HYDRA_WORKFLOW_INPUTS_DIR']) / 'member-a').unlink()
        result = subprocess.run(compose, cwd=self.repo, env=env, capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)

    def test_manifest_rejects_closed_schema_bounds_and_unsupported_inputs(self):
        cases = [
            {'schema_version': True, 'items': []},
            {'schema_version': 1, 'items': [{'id': 'a', 'value': 1, 'enabled': True}] * 2},
            {'schema_version': 1, 'items': [{'id': 'a', 'value': 1, 'enabled': True, 'extra': 0}]},
            {'schema_version': 1, 'items': [{'id': 'a', 'value': 1.5, 'enabled': True}]},
            {'schema_version': 1, 'items': [{'id': 'a', 'value': True, 'enabled': True}]},
            {'schema_version': 1, 'items': [{'id': '../a', 'value': 1, 'enabled': True}]},
            {'schema_version': 1, 'items': [{'id': 'a', 'value': 99, 'enabled': True}]},
            {'schema_version': 1, 'items': [{'id': 'a', 'value': 1, 'enabled': 1}]},
            {'schema_version': 1, 'items': [{'id': 'a', 'value': 1, 'enabled': True}] * 9},
            {'schema_version': 1, 'items': [{'id': 'a', 'value': 1, 'enabled': True}], 'unknown': 1},
        ]
        for case in cases:
            with self.subTest(case=case): self.precompile(case, success=False)

    def test_duplicate_oversize_and_different_manifest_refused(self):
        output = self.repo / 'existing.json'
        output.write_text('preserve prior output')
        raw_cases = ['{"schema_version":1,"schema_version":1,"items":[]}',
                     '{"schema_version":1,"items":[{"id":"a","value":1,"value":2,"enabled":true}]}',
                     ' ' * 4097, '{"schema_version":1,"items":']
        for raw in raw_cases:
            (self.repo / 'manifest.json').write_text(raw)
            result = subprocess.run([PRECOMPILER, 'manifest', 'manifest.json', output],
                                    cwd=self.repo, capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_text(), 'preserve prior output')
        (self.repo / 'different.json').write_text('{"schema_version":1,"items":[]}')
        result = subprocess.run([PRECOMPILER, 'manifest', 'different.json', output],
                                cwd=self.repo, capture_output=True)
        self.assertEqual(result.returncode, 1)

    def run_checker(self, manifest, report):
        (self.repo / 'manifest.json').write_text(json.dumps(manifest))
        (self.repo / 'report.json').write_text(json.dumps(report))
        inp, out = Path(self.temp.name) / 'inputs', Path(self.temp.name) / 'outputs'
        inp.mkdir(exist_ok=True); out.mkdir(exist_ok=True)
        shutil.copy2(self.repo / 'manifest.json', inp / 'manifest'); shutil.copy2(self.repo / 'report.json', inp / 'subject')
        validation = Path(self.temp.name) / 'validation.json'
        validation.write_text(json.dumps({'data': {'check': 'a' * 64, 'check-recipe': 'b' * 64}}))
        env = dict(self.env, HYDRA_WORKFLOW_INPUTS_DIR=str(inp), HYDRA_WORKFLOW_OUTPUTS_DIR=str(out),
                   HYDRA_WORKFLOW_VALIDATION_FILE=str(validation))
        return subprocess.run(['python3', 'check.py'], cwd=self.repo, env=env, capture_output=True, text=True)

    def test_checker_rejects_missing_wrong_forged_and_unexpected_members(self):
        manifest = {'schema_version': 1, 'items': [{'id': 'a', 'value': 2, 'enabled': True}, {'id': 'b', 'value': 3, 'enabled': False}]}
        valid = {'schema_version': 1, 'members': [{'id': 'a', 'value': 2, 'square': 4}, {'id': 'b', 'value': 3, 'status': 'skipped'}]}
        for label, report in [('valid', valid), ('missing', {'schema_version': 1, 'members': valid['members'][:1]}),
                              ('wrong', {'schema_version': 1, 'members': [{'id': 'a', 'value': 2, 'square': 9}, valid['members'][1]]}),
                              ('forged-skip', {'schema_version': 1, 'members': [{'id': 'a', 'value': 2, 'status': 'skipped'}, valid['members'][1]]}),
                              ('unexpected', {'schema_version': 1, 'members': valid['members'] + [{'id': 'x', 'value': 0, 'square': 0}]})]:
            with self.subTest(label=label):
                result = self.run_checker(manifest, report)
                self.assertEqual(result.returncode, 0 if label == 'valid' else 1, result.stderr)
                evidence = json.loads((Path(self.temp.name) / 'outputs/check.json').read_text())
                self.assertEqual(evidence['schema_version'], 3)
                self.assertEqual(evidence['verdict'], 'pass' if label == 'valid' else 'fail')
                self.assertEqual(evidence['validator_sha256'], 'a' * 64)
                record = evidence['evidence_records'][0]
                self.assertEqual(record['validator_recipe_sha256'], 'b' * 64)
                self.assertEqual(record['counts'], {'executed': 1, 'failed': int(label != 'valid'), 'skipped': 0})

    def test_checker_preserves_types_and_closed_report(self):
        manifest = {'schema_version': 1, 'items': [{'id': 'a', 'value': 1, 'enabled': True}]}
        valid = {'schema_version': 1, 'members': [{'id': 'a', 'value': 1, 'square': 1}]}
        self.assertEqual(self.run_checker(manifest, valid).returncode, 0)
        for changed in [dict(valid, schema_version=True), dict(valid, unknown=1),
                        {'schema_version': 1, 'members': [{'id': 'a', 'value': True, 'square': 1}]},
                        {'schema_version': 1, 'members': [{'id': 'a', 'value': 1, 'square': True}]},
                        {'schema_version': 1, 'members': [{'id': 'a', 'value': 1, 'square': 1, 'extra': 0}]}, None, []]:
            with self.subTest(report=changed):
                self.assertEqual(self.run_checker(manifest, changed).returncode, 1)

    def test_checker_revalidates_manifest_contract(self):
        item = {'id': 'a', 'value': 1, 'enabled': False}
        for manifest in [
            {'schema_version': True, 'items': []},
            {'schema_version': 1, 'items': [dict(item, id=f'i{i}') for i in range(9)]},
            {'schema_version': 1, 'items': [item, item]},
            {'schema_version': 1, 'items': [dict(item, value=True)]},
            {'schema_version': 1, 'items': [dict(item, enabled=0)]},
        ]:
            with self.subTest(manifest=manifest):
                self.assertEqual(self.run_checker(manifest, {'schema_version': 1, 'members': []}).returncode, 2)

if __name__ == '__main__': unittest.main()
