"""Real changing worker: preserve unknown edits and archive known edits before removal."""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path

from bench_i1_cleanup import restore_workload_changes
from bench_i1_fixture import Fixture, json_lines


def main() -> None:
    source, build, output = (Path(value).resolve() for value in sys.argv[1:])
    output.mkdir()
    fixture = Fixture(source, build, output, 1, "headless")
    try:
        worker = fixture.setup()[0]
        fixture.tell(
            worker,
            {
                "action": "phase",
                "id": "cleanup-control",
                "case": "one-changing",
                "change": True,
                "start_ns": time.monotonic_ns() + 100_000_000,
                "duration_ns": 500_000_000,
            },
        )
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if any(
                row["event"] == "generation-stopped"
                for row in json_lines(Path(worker["receipt"]))
            ):
                break
            time.sleep(0.03)
        else:
            raise AssertionError("changing workload did not finish")
        fixture.finish_workers()
        path = Path(worker["worktree"]) / "data-000.txt"
        changed = path.read_bytes()
        path.write_text("unexpected edit\n")
        try:
            restore_workload_changes(fixture)
        except ValueError as error:
            assert "unexpected contents preserved" in str(error), error
        else:
            raise AssertionError("unknown edit was accepted")
        assert path.read_text() == "unexpected edit\n"
        assert not (output / "cleanup-workload-changes.json").exists()
        path.write_bytes(changed)
        evidence = restore_workload_changes(fixture)
        assert len(evidence) == 1 and evidence[0]["content"].encode() == changed
        assert path.read_bytes() == b"x" * 1023 + b"\n"
        archive = (output / "cleanup-workload-changes.json").read_bytes()
        assert json.loads(archive) == evidence
        assert restore_workload_changes(fixture) == []
        assert (output / "cleanup-workload-changes.json").read_bytes() == archive
    finally:
        cleanup = fixture.cleanup()
    assert cleanup["cleanup_ok"], cleanup
    assert not Path(worker["worktree"]).exists()
    print(
        "PASS changing worker cleanup: unknown edits preserved, evidence archived, public removal"
    )


if __name__ == "__main__":
    main()
