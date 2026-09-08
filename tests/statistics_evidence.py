"""Reconcile the native aggregates with the durable evidence of one real run."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

hydra = Path(sys.argv[1]).resolve()
run = Path(sys.argv[2])
expected_recoveries = sys.argv[3]
verified = sys.argv[4] == "verified"
root = hydra.parent.parent
build = Path(os.environ.get("BUILD_DIR", root / "build"))


def scalar(path):
    return path.read_text().strip() if path.is_file() and not path.is_symlink() else "-"


def number(path):
    text = scalar(path)
    return int(text) if text.isdecimal() else None


assert scalar(run / "recovery-count") == expected_recoveries
start = number(run / "started-at")
end = number(run / "completed-at")
assert start and end and end >= start
queue = []
eligible = 0
for row in (run / "graph.tsv").read_text().splitlines():
    fields = row.split("\t")
    if fields[0] != "step" or fields[2] == "approval-wait":
        continue
    step = run / "steps" / fields[1]
    if number(step / "attempts") == 0:
        continue
    eligible += 1
    ready = number(step / "initial-ready-at")
    first = number(step / "initial-started-at")
    if ready is not None and first is not None:
        assert first >= ready
        queue.append(first - ready)
expected = [(0, eligible, len(queue), sum(queue)), (1, 1, 1, end-start)]
if verified:
    passed = number(run / "verified-at")
    assert start <= passed <= end
    assert scalar(run / "verification-plan-sha256") == scalar(run / "plan-accepted")
    expected.append((2, 1, 1, passed-start))
else:
    assert not (run / "verified-at").exists()
    expected.append((2, int((run / "compiled.json").exists()), 0, 0))
known = expected_recoveries.isdecimal()
expected.append((3, 1, int(known), int(expected_recoveries) if known else 0))
if not (build / "test-statistics").is_file():
    print("SKIP native aggregate comparison: build test-statistics; durable boundaries passed")
    sys.exit(0)
feed = subprocess.check_output([str(hydra), "workflow", "statistics-data"], text=True)
with tempfile.NamedTemporaryFile(mode="w") as stream:
    stream.write(feed)
    stream.flush()
    observed = subprocess.check_output([str(build / "test-statistics"), stream.name, run.name], text=True)
assert [tuple(map(int, line.split())) for line in observed.splitlines()] == expected, (observed, expected)
print("PASS durable-to-native metric reconciliation:", run.name)
