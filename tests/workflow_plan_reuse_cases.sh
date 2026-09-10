#!/bin/sh
# Sourced by the real headless public plan-task acceptance fixture.
workflow_plan_reuse_setup() {
    : "${root:?}" "${fixture:?}"
    cp "$root/tests/fixtures/plan-task/repo/produce.sh" produce.sh
    python3 - "$fixture/plan.json" <<'PY'
import hashlib, json, os, pathlib, shutil, sys
path = pathlib.Path(sys.argv[1]); plan = json.loads(path.read_text())
plan['reuse_policy'] = {'schema_version': 1, 'mode': 'sealed_artifacts', 'steps': {}}
plan['data']['inputs']['environment'] = {'path':'environment.json','type':'object','max_bytes':4096}
for name in ['produce','inspect']:
    plan['reuse_policy']['steps'][name] = {'dependencies':'complete','effects':'artifact_only','environment_input':'environment'}
    inputs = plan['data']['steps'][name]['inputs']; inputs.pop('repair')
    inputs['environment'] = {'input':'environment'}
path.write_text(json.dumps(plan))
pathlib.Path('environment.json').write_text(json.dumps({'schema_version':1,
    'scope':'fixed local text fixture; frozen historical outputs',
    'dependencies':['bound source scripts','declared inputs','POSIX shell and text tools'],
    'external_observations':False,'effects':'artifact_only',
    'platform':dict(zip(['system','release','machine'],[os.uname().sysname,os.uname().release,os.uname().machine])),
    'tools':{name:{'path':str(pathlib.Path(shutil.which(name)).resolve()),
                   'sha256':hashlib.sha256(pathlib.Path(shutil.which(name)).read_bytes()).hexdigest()}
             for name in ['sh','cat','cmp','sed','cut','shasum','sha256sum','perl'] if shutil.which(name)},
    'variables':{name:('C' if name=='LC_ALL' else os.environ.get(name,''))
                 for name in ['PATH','LANG','LC_ALL','LC_CTYPE','TZ']}}))
PY
}
workflow_plan_reuse_assert() {
    : "${root:?}" "${fixture:?}" "${code:?}" "${run_dir:?}" "${run:?}"
    [ "$code" = 0 ]
    [ "$(cat "$run_dir/state")" = succeeded ]
    [ "$(cat "$run_dir/repair-round")" = 2 ]
    [ ! -e "$run_dir/repair-pending.json" ]
    for node in produce inspect; do
        [ "$(cat "$run_dir/steps/$node/attempts")" = 1 ]
        [ "$(cat "$run_dir/steps/$node/authoritative-attempt")" = 1 ]
        [ ! -e "$run_dir/steps/$node/attempt-2" ]
    done
    for node in compose verify; do
        [ "$(cat "$run_dir/steps/$node/authoritative-attempt")" = 2 ]
    done
    [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 6 ]
    "$root/bin/hydra" workflow plan result "$run" > "$fixture/result.json"
    python3 - "$run_dir/repair-2.json" "$run_dir" <<'PY'
import json,sys
from pathlib import Path
record=json.load(open(sys.argv[1])); kept=record['evidence']['reuse']['steps']
assert set(kept)=={'produce','inspect'}, kept
assert all(p['attempt']=='attempt-1' and p['inputs']['environment'] for p in kept.values())
run = Path(sys.argv[2])
for node in ['produce', 'inspect', 'compose', 'verify']:
    step = run / 'steps' / node
    ready, first = [int((step / field).read_text()) for field in ['initial-ready-at', 'initial-started-at']]
    attempt = step / ('attempt-' + (step / 'authoritative-attempt').read_text().strip())
    start, end = [int((attempt / field).read_text()) for field in ['started-at', 'completed-at']]
    assert ready <= first <= start <= end, (node, ready, first, start, end)
PY
    printf 'candidate\ncomposed\n' > "$fixture/expected"
    cmp "$fixture/expected" "$run_dir/steps/compose/attempt-2/artifacts/result"
    python3 "$root/tests/test_plan_reuse_invalidation.py" "$fixture" --fleet-bin "$HYDRA_FLEET_BIN"
    python3 "$root/tests/statistics_evidence.py" "$root/bin/hydra" "$run_dir" "${HYDRA_TEST_PLAN_REPAIR_FAULT:-0}" verified
    printf 'Selective repair retained two original receipts and accepted two fresh affected tasks\n'
}
