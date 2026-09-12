#!/bin/sh
# Explicit native benchmark contract checks; no timed matrix is run here.
set -eu
ROOT=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
if [ -z "${HYDRA_ITEM10_BUILD:-}" ]; then
    echo 'SKIP item10 native fixture gates: run make test-bench-i1 explicitly'
    exit 0
fi
BUILD=$HYDRA_ITEM10_BUILD
SOURCE=${HYDRA_ITEM10_SOURCE:-$ROOT}
MANIFEST=${HYDRA_ITEM10_SOURCE_MANIFEST:-}
if [ ! -x "$BUILD/test-tui-pty" ] || [ ! -x "$BUILD/hydra-fleet" ]; then
    echo 'SKIP item10 public/PTY gates: make test-bench-i1 builds the required native helpers'
    exit 0
fi
OUT=${HYDRA_ITEM10_TEST_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/hydra-item10-check.XXXXXX")}
mkdir -p "$OUT"
benchmark() {
    if [ -n "$MANIFEST" ]; then
        set -- --source-manifest "$MANIFEST" "$@"
    fi
    python3 "$ROOT/scripts/bench-i1.py" --source "$SOURCE" "$@"
}
"$BUILD/test-tui-pty" --item10-semantic-controls > "$OUT/semantic-controls.json"
env -i PATH="$PATH" PYTHONPATH="$ROOT/scripts" python3 "$ROOT/tests/test_bench_i1_environment.py" "$SOURCE" "$BUILD" "$OUT/environment"
PYTHONPATH="$ROOT/scripts" python3 "$ROOT/tests/test_bench_i1_readiness.py" "$ROOT" "$BUILD" "$OUT" "$SOURCE"
PYTHONPATH="$ROOT/scripts" python3 "$ROOT/tests/test_bench_i1_cleanup.py" "$SOURCE" "$BUILD" "$OUT/changing-cleanup"
benchmark --build "$BUILD" --output "$OUT/interactive" --stage readiness
benchmark --build "$BUILD" --output "$OUT/headless" --stage readiness --mode headless
benchmark --build "$BUILD" --output "$OUT/missing-observer" --stage readiness --observer /usr/bin/false --failure-observer
PYTHONPATH="$ROOT/scripts" python3 - "$ROOT" "$BUILD" "$OUT" "$SOURCE" <<'PY'
import json
import os
from pathlib import Path
import sys
from bench_i1_pty import Observer, signal_owned_tui
from bench_i1_process import ProcessCounters
import subprocess
from bench_i1_fixture import Fixture, json_lines
from unittest.mock import patch
root, build, out, source = map(lambda value: Path(value).resolve(), sys.argv[1:])
for name in ('interactive', 'headless', 'missing-observer'):
    record = json.loads((out/name/'report.json').read_text())
    assert record['cleanup']['cleanup_ok'] and record['hashes_unchanged'], record
    assert record['status'] == ('incomplete' if name == 'missing-observer' else 'passed')
client = Observer(build/'test-tui-pty', build/'hydra-tui', source/'tests/fixtures/tui/fake-hydra.sh',
                  out/'invalid-command', root, {**os.environ, 'HYDRA_HOME': str(out/'fake-home')})
try:
    client.wait('raw-ready', timeout=15)
    client.send('INVALID failure')
    receipt = client.wait('exit', timeout=5)
    assert receipt['forced'] and receipt['reaped'], receipt
finally:
    cleanup = client.close()
assert cleanup['observer_exit'] != 0, cleanup
# Fail the ready-receipt reader after the real public worker has announced itself,
# before registration; the subsequent public cleanup command actually rejects a flag.
partial = out/'partial-readiness'
partial.mkdir()
fixture = Fixture(source, build, partial, 1, 'interactive')
def fail_ready(path):
    records = json_lines(path)
    if records:
        raise RuntimeError('forced readiness failure before registration')
    return records
try:
    with patch('bench_i1_fixture.json_lines', side_effect=fail_ready):
        fixture.setup()
except RuntimeError as error:
    assert str(error) == 'forced readiness failure before registration', error
else:
    raise AssertionError('forced readiness failure did not occur')
assert not fixture.workers
public = fixture.public
def rejected_cleanup(*args, **kwargs):
    if args[:3] == ('kill', '--all', '--force'):
        args += ('--item10-deliberate-invalid-option',)
    return public(*args, **kwargs)
try:
    with patch.object(fixture, 'public', side_effect=rejected_cleanup):
        failed_cleanup = fixture.cleanup()
    (partial/'failed-cleanup.json').write_text(json.dumps(failed_cleanup, indent=2)+'\n')
    assert not failed_cleanup['cleanup_ok'] and failed_cleanup['public_exit'] != 0
    assert len(failed_cleanup['reconciled_ready']) == 1, failed_cleanup
    assert len(failed_cleanup['workers']) == 1 and not failed_cleanup['workers'][0]['same_worker_remaining']
    assert failed_cleanup['sessions_after'] == '', failed_cleanup
finally:
    completed_cleanup = fixture.cleanup()
    assert completed_cleanup['cleanup_ok'], completed_cleanup
from bench_i1_workload import phase_proof, retain_sink_evidence
from bench_i1_actions import wait_until
import time
pressure = out/'pressure-control'
pressure.mkdir()
fixture = Fixture(source, build, pressure, 1, 'headless')
try:
    worker = fixture.setup()[0]
    start = time.monotonic_ns()+300_000_000
    fixture.tell(worker, {'action':'phase','id':'trial','case':'result-output',
                         'start_ns':start,'duration_ns':3_000_000_000,'change':False})
    wait_until(start+4_000_000_000)
    proof = phase_proof(fixture, 'result-output', start, 3_000_000_000)
    assert proof[0]['expected_generated_bytes'] == 96*128
    (pressure/'producer-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
    receipt_path = Path(worker['receipt'])
    original = receipt_path.read_text()
    receipt_path.write_text('\n'.join(line for line in original.splitlines() if json.loads(line)['event'] != 'generation-stopped')+'\n')
    try:
        phase_proof(fixture, 'result-output', start, 3_000_000_000)
    except RuntimeError:
        pass
    else:
        raise AssertionError('missing generation-stop receipt was accepted')
    finally:
        receipt_path.write_text(original)
    fixture.finish_workers()
    sinks = retain_sink_evidence(fixture, 'result-output', 96)
    (pressure/'proof.json').write_text(json.dumps({'producer':proof,'sink':sinks},indent=2)+'\n')
finally:
    assert fixture.cleanup()['cleanup_ok']
counters = ProcessCounters()
child = subprocess.Popen(['/bin/sleep','10'], start_new_session=True)
try:
    birth = counters.sample(child.pid)['identity_start']
    denied = signal_owned_tui(child.pid, birth-1, counters)
    assert not denied['signalled'] and child.poll() is None, denied
    accepted = signal_owned_tui(child.pid, birth, counters)
    assert accepted['signalled'] and child.wait(timeout=3) == -15, accepted
    (out/'pid-birth-guard.json').write_text(json.dumps({'mismatch':denied,'matched':accepted},indent=2)+'\n')
finally:
    if child.poll() is None:
        child.kill()
        child.wait(timeout=3)
print('PASS item10: real worker inputs, semantic misses, headless results, observer failures and owned cleanup')
PY
echo "Item10 gate evidence: $OUT"
