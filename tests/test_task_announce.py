#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def page(events=None, *, available=True, gap=False, reset=False, truncated=False):
    task_id = "task_" + "a" * 64
    events = events if events is not None else [{
        "schema_version": 1, "sequence": 1,
        "occurred_at": "2026-01-01T00:00:00Z", "run_id": "run_a",
        "step_id": None, "type": "run.created", "detail": "hello",
    }]
    data = {
        "snapshot_schema_version": 1, "receiver_observed_at": 123,
        "task": {
            "task_id": task_id, "run_id": None, "step_id": None,
            "attempt_id": None, "assigned_host": None, "workspace": None,
            "agent_profile": "codex", "execution_state": "running",
            "execution_owner": {"kind": "receiver", "state": "running", "recorded_state": None, "failure": None},
            "waiting": {"reason": "none", "detail": "", "next_action": ""},
            "observation_timestamps": {"accepted_at": 1, "started_at": 2, "resumed_at": None, "finished_at": None, "receiver_observed_at": 123},
            "effective_configuration": {},
            "contract": {"availability": "available", "reason": "", "missing_evidence": []},
            "steps": [], "pending_requests": [],
        },
        "event_observation": {
            "schema_version": 1, "events": events, "available": available,
            "stream_id": "dev:ino", "scan_truncated": truncated,
            "next_byte_offset": 100, "oldest_cursor": 0,
            "next_cursor": len(events), "head_cursor": len(events),
            "retention_gap": gap, "stream_reset": reset,
            "duplicate_policy": "sequence-cursor",
        },
    }
    return {"schema_version": 1, "ok": True, "command": "fleet-observation", "data": data}


def run(binary, path):
    return subprocess.run([binary, "fleet", "task", "announce", "--input", str(path)], text=True, capture_output=True)


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build/hydra-fleet")
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        good = folder / "good.json"
        good.write_text(json.dumps(page()))
        result = run(binary, good)
        assert result.returncode == 0, result.stderr
        announcement = json.loads(result.stdout)["data"]["announcement"]
        assert "task id=task_" in announcement and "receiver_observed_at=123" in announcement
        assert "event time=2026-01-01T00:00:00Z run=run_a step=- sequence=1 type=run.created detail=hello" in announcement
        assert "resume cursor=1 byte_offset=100 stream_id=dev:ino" in announcement

        for name, kwargs, expected in (("gap", {"gap": True}, "retention_gap=true"),
                                       ("reset", {"reset": True}, "stream_reset=true"),
                                       ("truncated", {"truncated": True}, "truncated=true"),
                                       ("missing", {"available": False, "events": []}, "history unavailable")):
            path = folder / (name + ".json")
            path.write_text(json.dumps(page(**kwargs)))
            result = run(binary, path)
            assert result.returncode == 0, result.stderr
            assert expected in json.loads(result.stdout)["data"]["announcement"]

        bad = page(events=[dict(page()["data"]["event_observation"]["events"][0], sequence=2),
                           dict(page()["data"]["event_observation"]["events"][0], sequence=4)])
        path = folder / "out-of-order.json"; path.write_text(json.dumps(bad))
        result = run(binary, path)
        assert result.returncode != 0 and "announcement" not in result.stdout

        cursor_gap = page()
        cursor_gap["data"]["event_observation"]["next_cursor"] = 2
        path = folder / "cursor-gap.json"; path.write_text(json.dumps(cursor_gap))
        result = run(binary, path)
        assert result.returncode != 0

        wrong_envelope = page()
        wrong_envelope["schema_version"] = 2
        path = folder / "wrong-envelope.json"; path.write_text(json.dumps(wrong_envelope))
        result = run(binary, path)
        assert result.returncode != 0
        for value in (None, 7):
            missing_command = page()
            missing_command.pop("command", None)
            if value is not None:
                missing_command["command"] = value
            path = folder / "missing-command.json"; path.write_text(json.dumps(missing_command))
            result = run(binary, path)
            assert result.returncode != 0

        below_oldest = page()
        below_oldest["data"]["event_observation"]["oldest_cursor"] = 2
        path = folder / "below-oldest.json"; path.write_text(json.dumps(below_oldest))
        result = run(binary, path)
        assert result.returncode != 0

        escaped = page(events=[dict(page()["data"]["event_observation"]["events"][0], detail="x\n\x1b[31m")])
        path = folder / "escaped.json"; path.write_text(json.dumps(escaped))
        result = run(binary, path)
        assert result.returncode == 0
        text = json.loads(result.stdout)["data"]["announcement"]
        assert "x??[31m" in text and "\x1b" not in text and "\n\x1b" not in text

        non_ascii = page(events=[dict(page()["data"]["event_observation"]["events"][0], detail="caf\u00e9")])
        path = folder / "non-ascii.json"; path.write_text(json.dumps(non_ascii))
        result = run(binary, path)
        assert result.returncode == 0
        text = json.loads(result.stdout)["data"]["announcement"]
        assert "caf?" in text and all(ord(char) < 128 for char in text)

        many = []
        for sequence in range(1, 130):
            many.append(dict(page()["data"]["event_observation"]["events"][0], sequence=sequence))
        path = folder / "too-many.json"; path.write_text(json.dumps(page(events=many)))
        result = run(binary, path)
        assert result.returncode != 0

        path = folder / "malformed.json"; path.write_text("{}")
        result = run(binary, path)
        assert result.returncode != 0
        path.write_text("{" + "x" * 270000)
        result = run(binary, path)
        assert result.returncode != 0
    print("Task observation announcer validation, gaps, resets and sanitization passed")


if __name__ == "__main__":
    main()
