#!/usr/bin/env python3
"""Independent checker: reconstructs all IDs, values, squares and skips."""
import hashlib, json, os, pathlib, re, sys

def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key')
        result[key] = value
    return result

def main():
    root = pathlib.Path(os.environ['HYDRA_WORKFLOW_INPUTS_DIR'])
    out = pathlib.Path(os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR']) / 'check.json'
    manifest_bytes = (root / 'manifest').read_bytes()
    if len(manifest_bytes) > 4096:
        raise ValueError('oversized manifest')
    manifest = json.loads(manifest_bytes, object_pairs_hook=unique_object)
    if (not isinstance(manifest, dict) or set(manifest) != {'schema_version', 'items'} or
            type(manifest['schema_version']) is not int or manifest['schema_version'] != 1 or
            not isinstance(manifest['items'], list) or len(manifest['items']) > 8):
        raise ValueError('invalid manifest contract')
    seen = set()
    for item in manifest['items']:
        if (not isinstance(item, dict) or set(item) != {'id', 'value', 'enabled'} or
                not isinstance(item['id'], str) or not re.fullmatch(r'[a-z][a-z0-9-]{0,31}', item['id']) or
                item['id'] in seen or type(item['value']) is not int or not -10 <= item['value'] <= 10 or
                type(item['enabled']) is not bool):
            raise ValueError('invalid manifest member')
        seen.add(item['id'])
    bindings = json.loads(pathlib.Path(os.environ['HYDRA_WORKFLOW_VALIDATION_FILE']).read_text())['data']
    content = (root / 'subject').read_bytes()
    try:
        report = json.loads(content, object_pairs_hook=unique_object)
    except (ValueError, UnicodeError):
        report = None
    expected = []
    for item in manifest['items']:
        expected.append({'id': item['id'], 'value': item['value'], 'square': item['value'] ** 2} if item['enabled']
                        else {'id': item['id'], 'value': item['value'], 'status': 'skipped'})
    actual = report.get('members') if isinstance(report, dict) else None
    passed = digest(report) == digest({'schema_version': 1, 'members': expected})
    raw = {'actual': 'pass' if passed else 'fail', 'expected': expected, 'actual_members': actual}
    observations = [{'id': 'membership-case', 'raw': raw, 'raw_sha256': digest(raw)}]
    record = {'obligation_id': 'membership-check', 'subject_manifest_sha256': hashlib.sha256(content).hexdigest(),
              'validator_identity': 'manifest-map-check-v1', 'validator_recipe_sha256': bindings['check-recipe'],
              'invocation': {'argv': ['python3', 'check.py'], 'exit_code': 0 if passed else 1},
              'environment': {'host': 'local', 'toolchain': sys.version},
              'case_inventory': ['membership-case'], 'observations': observations,
              'raw_evidence_sha256': digest(observations), 'counts': {'executed': 1, 'failed': 0 if passed else 1, 'skipped': 0},
              'limitations': ['Finite manifest fixture; checker establishes exact membership and arithmetic only.']}
    result = {'schema_version': 3, 'execution_status': 'completed', 'evidence_status': 'valid', 'domain_verdict': 'pass' if passed else 'fail',
              'verdict': 'pass' if passed else 'fail', 'subject_sha256': record['subject_manifest_sha256'], 'validator_sha256': bindings['check'],
              'requirements': ['membership'], 'evidence': 'Reconstructed every expected enabled square and skipped member from the bound manifest.',
              'limitations': record['limitations'], 'evidence_records': [record]}
    out.write_text(json.dumps(result, separators=(',', ':')) + '\n')
    return 0 if passed else 1
if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'Invalid checker input: {error}', file=sys.stderr)
        sys.exit(2)
