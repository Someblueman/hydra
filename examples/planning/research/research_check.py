#!/usr/bin/env python3
"""Independent fixed-trace schedule and claim checks; no prose judge."""
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import sys

LIMITS = ['12 synthetic jobs', 'one server', 'exact known durations', 'non-preemptive', 'no production evidence']
QUESTION = 'For this supplied 12-job trace, compare non-preemptive FCFS and SJF under p95 turnaround <=22 and max wait <=16.'
EXPLANATIONS = ['mean and tail metrics can disagree because a few long waits dominate tails', 'observed long wait is not proof of starvation']
LOCATIONS = ['/claims/recommendation', '/policies/FCFS', '/policies/SJF']
METRICS = ('mean_wait', 'mean_turnaround', 'p95_turnaround', 'max_wait', 'worst_wait_job')
CASE_IDS = ['fcfs', 'sjf', 'recommendation', 'scope', 'provenance']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(',', ':')).replace('/', r'\/').encode()


def jobs_from(path):
    with path.open(newline='') as source:
        rows = list(csv.DictReader(source))
    if len(rows) != 12 or set(rows[0]) != {'id', 'arrival', 'duration'}:
        raise ValueError('expected the complete 12-job source table')
    jobs = []
    for index, row in enumerate(rows):
        arrival, duration = int(row['arrival']), int(row['duration'])
        if not row['id'] or any(job['id'] == row['id'] for job in jobs) or arrival < 0 or duration <= 0:
            raise ValueError('invalid or duplicate job')
        jobs.append(dict(id=row['id'], arrival=arrival, duration=duration, order=index))
    return jobs


def simulate(jobs, policy):
    remaining, now, schedule = list(jobs), 0, []
    while remaining:
        available = [job for job in remaining if job['arrival'] <= now]
        if not available:
            now = min(job['arrival'] for job in remaining)
            available = [job for job in remaining if job['arrival'] <= now]
        key = (lambda job: (job['arrival'], job['order'])) if policy == 'FCFS' else (
            lambda job: (job['duration'], job['arrival'], job['order']))
        job = min(available, key=key)
        remaining.remove(job)
        finish = now + job['duration']
        schedule.append(dict(id=job['id'], start=now, finish=finish,
                             wait=now-job['arrival'], turnaround=finish-job['arrival']))
        now = finish
    waits, turns = [row['wait'] for row in schedule], [row['turnaround'] for row in schedule]
    return dict(schedule=schedule, mean_wait=sum(waits)/len(waits), mean_turnaround=sum(turns)/len(turns),
                p95_turnaround=sorted(turns)[math.ceil(.95*len(turns))-1], max_wait=max(waits),
                worst_wait_job=max(schedule, key=lambda row: row['wait'])['id'])


def assess(report, jobs, data_hash):
    actual = {policy: simulate(jobs, policy) for policy in ['FCFS', 'SJF']}
    qualified = {policy: row['p95_turnaround'] <= 22 and row['max_wait'] <= 16 for policy, row in actual.items()}
    recommendation = next((policy for policy, passes in qualified.items() if passes), 'neither qualifies')
    claimed = report.get('claims', {})
    policies = report.get('policies', {})
    if not isinstance(claimed, dict) or not isinstance(policies, dict):
        raise ValueError('claims and policies must be objects')
    raws, failures = {}, []
    for policy in ['FCFS', 'SJF']:
        agrees = policies.get(policy) == actual[policy]
        raws[policy.lower()] = {'actual': {key: actual[policy][key] for key in METRICS} if agrees else None,
                               'recomputed': actual[policy], 'claimed': policies.get(policy)}
        if not agrees: failures.append(policy.lower())
    expected_claim = dict(recommendation=recommendation, constraint_checks=qualified)
    observed_claim = {key: claimed.get(key) for key in expected_claim}
    raws['recommendation'] = {'actual': observed_claim, 'recomputed': expected_claim}
    if observed_claim != expected_claim: failures.append('recommendation')
    scope = (set(report) == {'schema_version','question','provenance','policies','claims','claim_locations','limitations'} and
             report.get('schema_version') == 3 and report.get('question') == QUESTION and
             set(claimed) == {'question','recommendation','constraint_checks','limits','competing_explanations'} and
             claimed.get('question') == QUESTION and claimed.get('limits') == LIMITS and
             claimed.get('competing_explanations') == EXPLANATIONS and
             report.get('claim_locations') == LOCATIONS and report.get('limitations') == LIMITS)
    raws['scope'] = {'actual': 'pass' if scope else 'fail', 'question': report.get('question'), 'claims': claimed,
                    'claim_locations': report.get('claim_locations'), 'limitations': report.get('limitations')}
    if not scope: failures.append('scope')
    expected_provenance = dict(data_path='jobs.csv', data_sha256=data_hash,
                              method='deterministic non-preemptive simulation; tie arrival then input order')
    provenance = report.get('provenance') == expected_provenance
    raws['provenance'] = {'actual': 'pass' if provenance else 'fail',
                          'claimed': report.get('provenance'), 'expected': expected_provenance}
    if not provenance: failures.append('provenance')
    return raws, failures


def main():
    inputs, output = Path(os.environ['HYDRA_WORKFLOW_INPUTS_DIR']), Path(os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR'])
    bindings = json.loads(Path(os.environ['HYDRA_WORKFLOW_VALIDATION_FILE']).read_text())['data']
    subject = (inputs/'subject').read_bytes()
    instrumentation_valid = True
    try:
        report = json.loads(subject)
        if not isinstance(report, dict): raise ValueError('subject must be an object')
        raws, failures = assess(report, jobs_from(inputs/'jobs'), sha((inputs/'jobs').read_bytes()))
    except (OSError, ValueError, TypeError, KeyError) as error:
        instrumentation_valid = False
        failures = list(CASE_IDS)
        raws = {case: {'actual': None, 'error': f'{type(error).__name__}: {error}'} for case in CASE_IDS}
    observations = [dict(id=case, raw=raws[case], raw_sha256=sha(canonical(raws[case]))) for case in CASE_IDS]
    passed = instrumentation_valid and not failures
    records = []
    for obligation in ['means','tails','fairness','recommendation','reproduce','limits']:
        records.append(dict(obligation_id=obligation, subject_manifest_sha256=sha(subject),
            validator_identity='research-checker-v5', validator_recipe_sha256=bindings['assessment-recipe'],
            invocation=dict(argv=['python3','research_check.py'], exit_code=0 if passed else 1),
            environment=dict(host='local',toolchain=sys.version), case_inventory=CASE_IDS, observations=observations,
            raw_evidence_sha256=sha(canonical(observations)),
            counts=dict(executed=len(CASE_IDS), failed=len(failures), skipped=0),
            limitations=['Fixed finite trace and exact claim schema; no general prose or production inference.']))
    result = dict(schema_version=3, execution_status='completed', evidence_status='valid' if instrumentation_valid else 'invalid',
        domain_verdict='pass' if passed else 'fail', verdict='pass' if passed else 'fail', subject_sha256=sha(subject),
        validator_sha256=bindings['assessment'], requirements=['means','tails','fairness','recommendation','reproduce','limits'],
        evidence='Independent schedules, metrics, source and fixed-scope claim checks. Failed cases: '+(', '.join(failures) or 'none'),
        limitations=records[0]['limitations'], evidence_records=records)
    output.mkdir(exist_ok=True)
    (output/'assessment').write_text(json.dumps(result,separators=(',',':'))+'\n')
    return 0 if passed else 1

if __name__ == '__main__':
    sys.exit(main())
