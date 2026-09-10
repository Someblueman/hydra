#!/usr/bin/env python3
"""Independent byte-level acceptance for the fixed two-member example."""
import hashlib
import json
import os
import pathlib
import sys

EXPECTED = b'A:validated input\nB:validated input\n'

def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

def main():
    subject = pathlib.Path(os.environ['HYDRA_WORKFLOW_INPUTS_DIR']) / 'subject'
    output = pathlib.Path(os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR']) / 'check'
    bindings = json.loads(pathlib.Path(os.environ['HYDRA_WORKFLOW_VALIDATION_FILE']).read_text())['data']
    content = subject.read_bytes()
    subject_hash = hashlib.sha256(content).hexdigest()
    passed = content == EXPECTED
    verdict = 'pass' if passed else 'fail'
    raw = {'actual': verdict, 'measurement': len(content), 'unit': 'bytes',
           'expected_sha256': hashlib.sha256(EXPECTED).hexdigest(),
           'actual_sha256': subject_hash}
    observations = [{'id': 'assembly-case', 'raw': raw, 'raw_sha256': digest(raw)}]
    record = {
        'obligation_id': 'assembly-check', 'subject_manifest_sha256': subject_hash,
        'validator_identity': 'pattern-check-v2', 'validator_recipe_sha256': bindings['check-recipe'],
        'invocation': {'argv': ['python3', 'check.py'], 'exit_code': 0 if passed else 1},
        'environment': {'host': 'local', 'toolchain': sys.version},
        'case_inventory': ['assembly-case'], 'observations': observations,
        'raw_evidence_sha256': digest(observations),
        'counts': {'executed': 1, 'failed': 0 if passed else 1, 'skipped': 0},
        'limitations': ['Fixed deterministic two-line task; no semantic generalization.'],
    }
    report = {
        'schema_version': 3, 'execution_status': 'completed', 'evidence_status': 'valid',
        'domain_verdict': verdict, 'verdict': verdict, 'subject_sha256': subject_hash,
        'validator_sha256': bindings['check'], 'requirements': ['assembly'],
        'evidence': 'Compared all assembled bytes against independently fixed expected bytes.',
        'limitations': record['limitations'], 'evidence_records': [record],
    }
    output.write_text(json.dumps(report, separators=(',', ':')) + '\n')
    return 0 if passed else 1

if __name__ == '__main__':
    sys.exit(main())
