#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/tui/statistics-v2.tsv"


def run(core, *args):
    return subprocess.run([core, *args], text=True, capture_output=True)


def main():
    core = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build/hydra-core")
    result = run(core, "statistics-json", str(FIXTURE))
    assert result.returncode == 0, result.stderr
    document = json.loads(result.stdout)
    assert document["observed"] == 1788789600
    assert document["cohort"] == {"runs": 8, "steps": 11}
    assert document["metrics"]["queue"] == {
        "state": "known", "eligible": 10, "known": 7, "sum": 70,
        "mean": 10, "max": 10, "p50": 10, "p95": 10,
    }
    assert document["metrics"]["verified"]["known"] == 2
    assert document["remote"]["state"] == "unavailable"

    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        malformed = folder / "malformed.tsv"
        malformed.write_text("HYDRA_STATISTICS\t2\t10\n")
        result = run(core, "statistics-json", str(malformed))
        assert result.returncode != 0 and result.stdout == ""
        partial = folder / "partial.tsv"
        partial.write_text(FIXTURE.read_text().replace("Z\t8\t11\n", ""))
        result = run(core, "statistics-json", str(partial))
        assert result.returncode != 0 and result.stdout == ""
        oversized = folder / "oversized.tsv"
        oversized.write_text("HYDRA_STATISTICS\t2\t10\nX\t" + "x" * (1024 * 1024) + "\n")
        result = run(core, "statistics-json", str(oversized))
        assert result.returncode != 0 and result.stdout == ""
        result = run(core, "statistics-compare", str(FIXTURE), str(FIXTURE))
        assert result.returncode == 0
        compared = json.loads(result.stdout)
        assert compared["delta"]["metrics"]["queue"] == {
            "state": "known", "mean": 0, "max": 0, "p50": 0, "p95": 0,
        }
    print("Statistics exporter JSON, comparison and fail-closed bounds passed")


if __name__ == "__main__":
    main()
