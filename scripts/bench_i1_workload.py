"""Validate the independently declared workload against producer and sink receipts."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from bench_i1_fixture import Fixture, json_lines


def phase_proof(
    fixture: Fixture, case: str, start_ns: int, duration_ns: int
) -> list[dict[str, Any]]:
    proofs = []
    interval = (
        31_250_000 if case in ("sustained-output", "result-output") else 2_000_000_000
    )
    count = (duration_ns + interval - 1) // interval
    for index, worker in enumerate(fixture.workers):
        records = json_lines(Path(worker["receipt"]))
        armed = [row for row in records if row["event"] == "phase-armed"]
        stopped = [row for row in records if row["event"] == "generation-stopped"]
        if len(armed) != 1 or len(stopped) != 1:
            raise RuntimeError("worker lacks one exact arm and generation-stop receipt")
        arm, stop = armed[0], stopped[0]
        if (
            arm["phase"] != "trial"
            or arm["case"] != case
            or arm["start_ns"] != start_ns
            or arm["duration_ns"] != duration_ns
            or arm["interval_ns"] != interval
            or arm["monotonic_ns"] > start_ns
            or stop["phase"] != "trial"
            or stop["scheduled_events"] != count
            or stop["monotonic_ns"] < start_ns + duration_ns
        ):
            raise RuntimeError(
                "producer phase receipt contradicts the declared schedule"
            )
        changes = [row for row in records if row["event"] == "repository-change"]
        output = [row for row in records if row["event"] == "output-scheduled"]
        expected_changes = (
            count
            if case == "all-changing" or case == "one-changing" and index == 0
            else 0
        )
        expected_output = count if case in ("sustained-output", "result-output") else 0
        if len(changes) != expected_changes or len(output) != expected_output:
            raise RuntimeError(
                "producer event count differs from its declared workload"
            )
        file_hashes = {}
        for sequence, row in enumerate(changes):
            path = Path(worker["worktree"]) / f"data-{sequence:03d}.txt"
            expected = f"{worker['token']}:trial:{sequence}\n".encode()
            if (
                row["scheduled_ns"] != start_ns + sequence * interval
                or row["monotonic_ns"] < row["scheduled_ns"]
                or path.read_bytes() != expected
            ):
                raise RuntimeError(
                    "scheduled repository change or its actual bytes differ"
                )
            file_hashes[path.name] = hashlib.sha256(expected).hexdigest()
        for sequence, row in enumerate(output):
            if (
                row["sequence"] != sequence
                or row["bytes"] != 128
                or row["scheduled_ns"] != start_ns + sequence * interval
                or row["generated_ns"] < row["scheduled_ns"]
            ):
                raise RuntimeError(
                    "output sequence, size, or absolute schedule differs"
                )
        expected_bytes = expected_output * 128
        drains = [
            row
            for row in records
            if row["event"] == "output-drained" and row["monotonic_ns"] >= start_ns
        ]
        last_drain = drains[-1] if drains else None
        if stop["generated_bytes"] != expected_bytes:
            raise RuntimeError(
                "producer generated-byte total differs from the declared rate"
            )
        if expected_bytes:
            if (
                last_drain is None
                or last_drain["total_written_bytes"] != expected_bytes
            ):
                raise RuntimeError("producer did not drain every declared byte")
        elif stop["written_bytes"] or stop["backlog_bytes"]:
            raise RuntimeError("quiet/change producer unexpectedly wrote output")
        proofs.append(
            {
                "head_id": worker["head_id"],
                "instance_id": worker["instance_id"],
                "phase_armed": arm,
                "generation_stopped": stop,
                "last_drain": last_drain,
                "expected_events": count,
                "expected_generated_bytes": expected_bytes,
                "file_hashes": file_hashes,
                "validated_output_events": len(output),
                "recovery_ns": max(
                    0, last_drain["monotonic_ns"] - start_ns - duration_ns
                )
                if last_drain
                else 0,
            }
        )
    return proofs


def retain_sink_evidence(
    fixture: Fixture, case: str, expected_events: int
) -> list[dict[str, Any]]:
    results = []
    for index, worker in enumerate(fixture.workers):
        if fixture.mode == "headless":
            state = (
                fixture.home
                / "state/v2/projects"
                / worker["project_id"]
                / "heads"
                / worker["head_id"]
            )
            paths = list((state / "agent-payloads").glob("run_*/stdout"))
            if len(paths) != 1:
                raise RuntimeError("missing exact headless retained provider output")
            raw = paths[0].read_bytes()
        else:
            capture = fixture.command(
                [
                    "tmux",
                    "-S",
                    str(fixture.socket),
                    "capture-pane",
                    "-p",
                    "-J",
                    "-S",
                    "-",
                    "-t",
                    worker["session"],
                ]
            )
            raw = capture.stdout.encode()
        path = fixture.output / f"sink-{index}.txt"
        path.write_bytes(raw)
        markers = []
        event_type = "observation" if case == "sustained-output" else "result"
        for line in raw.decode(errors="replace").splitlines():
            if line.startswith('{"schema_version":1,"type":'):
                row = json.loads(line)
                text = row.get("text", "")
                if text.startswith(worker["token"] + ":"):
                    if row.get("type") != event_type or len(line.encode()) + 1 != 128:
                        raise RuntimeError(
                            "retained pressure record has a different event type or byte size"
                        )
                    markers.append(text)
        expected = (
            expected_events if case in ("sustained-output", "result-output") else 0
        )
        sequences = [int(text.split(":", 1)[1].split(".", 1)[0]) for text in markers]
        exact = sequences == list(range(expected))
        result = {
            "head_id": worker["head_id"],
            "path": str(path),
            "sha256": hashlib.sha256(raw).hexdigest(),
            "event_type": event_type,
            "scope": "retained provider stdout"
            if fixture.mode == "headless"
            else "tmux retained pane history with joined wrapped lines",
            "expected_output_records": expected,
            "observed_output_records": len(markers),
            "exact_ordered_sequence": exact,
            "missing_from_retention": expected - len(set(sequences)),
        }
        results.append(result)
        if not exact:
            (fixture.output / "sink-proof.json").write_text(
                json.dumps(results, indent=2) + "\n"
            )
            raise RuntimeError(
                "sink retention does not prove every declared output record"
            )
    return results
