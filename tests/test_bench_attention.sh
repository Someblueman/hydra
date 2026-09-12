#!/bin/sh
# Observer regressions use an actual native frame, not a fabricated render model.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
PYTHONDONTWRITEBYTECODE=1 python3 - "$root" <<'PY'
import pathlib
import base64
import hashlib
import json
import runpy
import sys

root = pathlib.Path(sys.argv[1])
sys.path.insert(0, str(root / "scripts"))
from bench_attention_pty import Frames, clock_seconds

bench = runpy.run_path(str(root / "scripts/bench-attention.py"))
fixture = json.loads((root / "tests/fixtures/attention-benchmark/complete-frame.json").read_text())
raw = base64.b64decode(fixture["raw_base64"])
assert hashlib.sha256(raw).hexdigest() == fixture["raw_sha256"]
for chunk in (1, 7, 4096):
    frames = Frames(240, 40)
    # Withhold the final native footer byte: no partial frame may be accepted.
    partial = raw[:-1]
    for offset in range(0, len(partial), chunk):
        assert not frames.feed(partial[offset:offset+chunk])
    assert len(frames.feed(raw[-1:])) == 1
    completed = frames.latest.copy()
    assert "ATTENTION  3 current" in completed["text"]
    assert not frames.feed(raw[:len(raw)//2])
    assert frames.latest == completed
    assert len(frames.feed(raw[len(raw)//2:])) == 1

item = dict(zip(bench["WIRE"], ["-"] * len(bench["WIRE"])))
item.update(kind="approval", identity="1"*64, revision="2"*64)
score = bench["score"]
assert score([item], [item])["tp"] == 1
assert score([], [item])["fp"] == 1  # Supported negative cases are scored.
for changed in (dict(item, identity="3"*64), dict(item, revision="4"*64)):
    result = score([item], [changed])
    assert (result["tp"], result["fp"], result["fn"]) == (0, 1, 1)
assert score([item], [item, item])["fp"] == 1  # Multiplicity cannot hide in sets.
assert score([dict(item, kind="unknown")], [])["unknown_match"] is False
for malformed in ("", "HYDRA_ATTENTION\t1\n", "HYDRA_ATTENTION\t1\nEND\t1\t0\t0\n"):
    try:
        bench["parse_rows"](malformed)
    except (ValueError, IndexError):
        pass
    else:
        raise AssertionError("incomplete protocol accepted")
assert clock_seconds("1-02:03:04.50") == 93784.5
print("PASS benchmark: fragmented real frame, incomplete repaint, exact identity/revision, negative and duplicate cardinality, unknown separation, truncated stream, CPU clock")
PY
