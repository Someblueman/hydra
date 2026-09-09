#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
fixture=$(mktemp -d)
export HYDRA_HOME="$fixture/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
active_branch=
cleanup() {
    [ -z "$active_branch" ] || "$root/bin/hydra" kill "$active_branch" --force >/dev/null 2>&1 || true
    rm -rf "$fixture"
}
trap cleanup EXIT HUP INT TERM
mkdir "$fixture/repo"
cp "$root/tests/fixtures/plan/repo/compose.sh" "$fixture/repo/"
cp "$root/tests/fixtures/plan/repo/expected.txt" "$fixture/repo/"
cat > "$fixture/repo/check.sh" <<'CHECK'
#!/bin/sh
set -eu
python3 - "$HYDRA_WORKFLOW_INPUTS_DIR/subject" "$HYDRA_WORKFLOW_VALIDATION_FILE" "$HYDRA_WORKFLOW_OUTPUTS_DIR/check.json" <<'PY'
import hashlib, json, os, sys
subject, validation, output = sys.argv[1:]
data = open(subject, 'rb').read(); context = json.load(open(validation))['data']
mode = os.environ.get('HYDRA_V3_FAULT', '')
actual = 'Wrong candidate artifact' if mode == 'wrong-artifact' else data.decode()
raw = {'actual': actual, 'measurement': len(data)}
raw_hash = hashlib.sha256(json.dumps(raw, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
obs = [{'id': 'case-1', 'raw': raw, 'raw_sha256': raw_hash}]
inventory = ['case-1']
if mode == 'drop': obs = []; inventory = []
if mode == 'raw-tampered': obs[0]['raw']['actual'] = 'Tampered after hashing'
if mode == 'missing-measurement':
    obs[0]['raw'].pop('measurement')
    obs[0]['raw_sha256'] = hashlib.sha256(json.dumps(obs[0]['raw'], sort_keys=True, separators=(',', ':')).encode()).hexdigest()
evidence_hash = hashlib.sha256(json.dumps(obs, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
subject_hash = hashlib.sha256(data).hexdigest()
if mode == 'stale-subject': subject_hash = '0' * 64
exit_code = 7 if mode == 'nonzero' else 0
failed = 1 if mode == 'wrong-artifact' else 0
recipe_hash = '0' * 64 if mode == 'changed-predicate' else context['check-recipe']
report = {'schema_version': 3, 'execution_status': 'completed', 'evidence_status': 'valid',
 'domain_verdict': 'fail' if failed else 'pass', 'verdict': 'fail' if failed else 'pass',
 'subject_sha256': subject_hash, 'validator_sha256': context['check'], 'requirements': ['content'],
 'evidence': 'structured fixture', 'limitations': [], 'evidence_records': [{
 'obligation_id': 'content-check', 'subject_manifest_sha256': subject_hash,
 'validator_identity': 'fixture-v3', 'validator_recipe_sha256': recipe_hash,
 'invocation': {'argv': ['python3', 'check.sh'], 'exit_code': exit_code}, 'environment': {'host': 'local', 'toolchain': 'python3'},
 'case_inventory': inventory, 'observations': obs, 'raw_evidence_sha256': evidence_hash,
 'counts': {'executed': len(obs), 'failed': failed, 'skipped': 0}, 'limitations': []}]}
open(output, 'w').write(json.dumps(report, separators=(',', ':')) + '\n')
PY
CHECK
chmod +x "$fixture/repo/check.sh"
cat > "$fixture/repo/compose.sh" <<'COMPOSE'
#!/bin/sh
set -eu
if [ "${HYDRA_V3_FAULT:-}" = wrong-artifact ]; then
    printf 'Wrong candidate artifact\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.txt"
else
    printf 'Delivered report\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.txt"
fi
COMPOSE
chmod +x "$fixture/repo/compose.sh"
cp "$root/tests/fixtures/plan/plan.json" "$fixture/plan.json"
cp "$root/tests/fixtures/plan/policy.json" "$fixture/policy.json"
python3 - "$fixture/plan.json" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
p['envelope']['timeout_seconds'] = 40
for step in p['steps']:
    if step['kind'] == 'exec':
        step['args']['timeout'] = 10
p['checks'][0]['definition'] = json.dumps({'predicate':'equals','cases':[{'id':'case-1','expected':'Delivered report\n'}]}, separators=(',', ':'))
p['obligations'] = [{'id':'content-check','requirement':'content','intent_ref':'objective','subject':{'deliverable':'report','step':'compose','output':'report'},'criterion':'The sealed report has the expected content','evaluation':{'method':'executable','check':'check'},'required_evidence':['subject_sha256','verdict','evidence','measurements'],'environment':{'hosts':['local'],'tools':['sh'],'effects':['execute']},'completion_rule':'verdict=pass','limitations':['fixture-only']}]
json.dump(p, open(sys.argv[1], 'w'), separators=(',', ':'))
PY
cp "$fixture/plan.json" "$fixture/plan.base.json"
cd "$fixture/repo"
git init -q; git config user.name Test; git config user.email test@example.invalid; git add .; git commit -qm fixture
"$root/bin/hydra" init --no-agent --trust >/dev/null
run_case() {
    mode=$1; expected=$2
    branch="plan-smoke-${mode:-ok}"
    active_branch=$branch
    cp "$fixture/plan.base.json" "$fixture/plan.json"
    BRANCH="$branch" PLAN="$fixture/plan.json" python3 - <<'PY'
import os
path = os.environ['PLAN']
text = open(path).read().replace('plan-smoke', os.environ['BRANCH'])
open(path, 'w').write(text)
PY
    if [ "$mode" = malformed-recipe ]; then
        PLAN="$fixture/plan.json" python3 - <<'PY'
import json, os
path = os.environ['PLAN']
plan = json.load(open(path))
plan['checks'][0]['definition'] = '{}'
json.dump(plan, open(path, 'w'), separators=(',', ':'))
PY
    fi
    rm -f "$fixture/compiled.json"
    "$root/bin/hydra" workflow plan compile "$fixture/plan.json" "$fixture/policy.json" "$fixture/compiled.json" > "$fixture/compile-$mode.json" 2> "$fixture/compile-$mode.err" || { cat "$fixture/compile-$mode.err"; cat "$fixture/compile-$mode.json"; cat "$fixture/plan.json"; exit 1; }
    "$root/bin/hydra" workflow plan check-definition "$fixture/compiled.json" check >/dev/null
    "$root/bin/hydra" workflow plan check-recipe "$fixture/compiled.json" check >/dev/null
    digest=$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/compile-$mode.json")
    HYDRA_V3_FAULT="$mode" "$root/bin/hydra" workflow plan run "$fixture/compiled.json" --accept "$digest" > "$fixture/run-$mode.out" 2>/dev/null || true
    [ -n "$(sed -n '1p' "$fixture/run-$mode.out")" ]
    run=$(sed -n '1p' "$fixture/run-$mode.out")
    run_dir=$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print -quit)
    state=$(cat "$run_dir/state")
    [ -s "$run_dir/steps/verify/attempt-1/outputs/check.json" ]
    grep -q '"type":"run\.' "$run_dir/events.jsonl"
    "$root/bin/hydra" kill "$branch" --force >/dev/null 2>&1 || true
    active_branch=
    [ "$state" = "$expected" ] || exit 1
}
run_case '' succeeded
run_case wrong-artifact failed
run_case drop failed
run_case raw-tampered failed
run_case stale-subject failed
run_case missing-measurement failed
run_case changed-predicate failed
run_case malformed-recipe failed
run_case nonzero failed
echo 'Public workflow v3 evidence controls passed'
