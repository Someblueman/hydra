#!/bin/sh
# Item 10 matrix self-test: unit checks, the annotated attention corpus, the
# smallest real cells in both local modes and the evidence schema. No timing
# value is asserted; this is a contract check, never a performance gate.
set -eu
ROOT=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
if [ -z "${HYDRA_PERF_BUILD:-}" ]; then
    echo 'SKIP perf matrix self-test: run make test-perf-matrix explicitly'
    exit 0
fi
BUILD=$HYDRA_PERF_BUILD
if [ ! -x "$BUILD/test-tui-pty" ] || [ ! -x "$BUILD/hydra-fleet" ] || [ ! -x "$BUILD/hydra-tui" ]; then
    echo 'SKIP perf matrix self-test: make test-perf-matrix builds the required native helpers'
    exit 0
fi
export LANG=C.UTF-8 LC_ALL=C.UTF-8 PYTHONDONTWRITEBYTECODE=1
OUT=${HYDRA_PERF_TEST_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/hydra-perf-check.XXXXXX")}
mkdir -p "$OUT"
PYTHONPATH="$ROOT/scripts" python3 "$ROOT/tests/test_perf_matrix_units.py" "$ROOT" "$OUT/units"
python3 "$ROOT/scripts/perf_matrix_attention.py" --root "$ROOT" --build "$BUILD" --out "$OUT/attention"
python3 "$ROOT/scripts/perf-matrix.py" --source "$ROOT" --build "$BUILD" --output "$OUT/matrix" \
    --heads 1 --modes interactive headless remote-headless --clients 1 --preview closed \
    --cases quiescent one-changing --settle 1 --idle 2 --gap 0.5 --poll-ms 100 \
    --attention "$OUT/attention/report.json" --evidence-dir "$OUT/evidence" --raw-budget-bytes 200000
PYTHONPATH="$ROOT/scripts" python3 - "$OUT" <<'PY'
import gzip
import json
import sys
from pathlib import Path
from perf_matrix_report import validate_bundle
out = Path(sys.argv[1])
bundle = json.loads((out / "evidence/bundle.json").read_text())
assert validate_bundle(bundle) == [], validate_bundle(bundle)
cells = [c for f in bundle["fixtures"] for c in f["cells"]]
assert len(cells) == 4 and all(c["status"] == "measured" for c in cells), [c["status"] for c in cells]
assert {(c["mode"], c["case"]) for c in cells} == {(m, k) for m in ("interactive", "headless") for k in ("quiescent", "one-changing")}
assert all(f["cleanup_ok"] and not f["errors"] for f in bundle["fixtures"]), [f["errors"] for f in bundle["fixtures"]]
assert any(row["mode"] == "remote-headless" and row["status"] == "unmeasured" for row in bundle["unsupported_cells"])
assert any(row.get("platform") == "darwin" for row in bundle["unsupported_cells"])
for cell in cells:
    idle = cell["windows"]["idle"]
    assert idle["launches"]["by_helper"].get("hydra", 0) >= 1, idle["launches"]
    assert idle["launches"]["by_helper"].get("git", 0) >= 1, idle["launches"]
    assert "native-tui" in idle["processes"]["roles"]
    assert cell["windows"]["idle"]["families"]["families"]["hydra_tui"]["cpu_seconds"] is not None
    if cell["case"] == "one-changing":
        assert cell["update_latency"]["changes_declared"] >= 1
        assert cell["locality"]["changed_worktrees"] == ["i10-h00"]
    assert cell["pressure"]["coalesced_updates"] is None and cell["pressure"]["coalesced_updates_reason"]
    assert cell["workload_proof"][0]["expected_events"] >= 1
budgets = bundle["proposed_budgets"]
assert budgets["status"].startswith("provisional")
assert bundle["attention"]["complete"] and bundle["attention"]["false_positives_total"] == 0
assert bundle["attention"]["missed_signals_total"] == 0 and bundle["attention"]["cases_total"] == 13
published = bundle["raw_samples_published"]
assert published["used_bytes"] <= published["budget_bytes"]
for row in published["files"]:
    if row.get("file"):
        samples = json.loads(gzip.decompress((out / "evidence/raw" / row["file"]).read_bytes()))
        assert len(samples["idle"]) >= 2
assert (out / "evidence/summary.md").read_text().startswith("# Hydra performance matrix")
print("PASS perf matrix: smallest local cells measured, unsupported cells explicit, schema and evidence bundle valid")
PY
echo "Perf matrix self-test evidence: $OUT"
