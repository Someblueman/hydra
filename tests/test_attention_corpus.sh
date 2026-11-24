#!/bin/sh
# Annotated attention corpus: real `hydra workflow attention` rows scored
# against the 14 cases in tests/fixtures/perf/attention-cases.json. Any missed
# signal, false positive or incomplete case fails; no timing is asserted.
set -eu
ROOT=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
: "${BUILD_DIR:?BUILD_DIR is required (run through make test or make test-one)}"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/hydra-attention-corpus.XXXXXX")
trap 'rm -rf "$OUT"' EXIT HUP INT TERM

LANG=C.UTF-8 LC_ALL=C.UTF-8 PYTHONDONTWRITEBYTECODE=1 \
    python3 "$ROOT/scripts/attention_corpus.py" --root "$ROOT" --build "$BUILD_DIR" --out "$OUT/run"

PYTHONDONTWRITEBYTECODE=1 python3 - "$ROOT" "$OUT/run/report.json" <<'PY'
import json
import sys
from pathlib import Path

root, report_path = Path(sys.argv[1]), Path(sys.argv[2])
report = json.loads(report_path.read_text())
spec = json.loads((root / "tests/fixtures/perf/attention-cases.json").read_text())
assert report["complete"] and report["error"] is None, report["error"]
assert report["cases_total"] == len(spec["cases"]) == report["cases_scored"], report["cases_total"]
assert report["false_positives_total"] == 0, report["false_positives_total"]
assert report["missed_signals_total"] == 0, report["missed_signals_total"]
assert report["wire_parity_all"], "attention --json and attention-data disagree"
assert report["cleanup"]["ok"], report["cleanup"]
print("PASS attention corpus: %d annotated cases, no missed signal, no false positive" % report["cases_total"])
PY
