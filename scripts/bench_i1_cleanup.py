"""Only private fixture cleanup; reconcile worker birth identities before signalling."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from bench_i1_fixture import Fixture


def restore_workload_changes(fixture: Fixture) -> list[dict[str, Any]]:
    """Archive completed stand-in edits before restoring exact fixture seed bytes."""
    from bench_i1_fixture import json_lines

    changes: list[tuple[Path, bytes]] = []
    evidence: list[dict[str, Any]] = []
    seed = b"x" * 1023 + b"\n"
    for worker in fixture.workers:
        worktree = Path(worker["worktree"])
        if not worktree.exists():
            continue  # A previous public cleanup already removed this head.
        records = json_lines(Path(worker["receipt"]))
        edits = {
            row["file"]: row for row in records if row["event"] == "repository-change"
        }
        if not edits:
            continue
        if not records or records[-1]["event"] != "completed":
            raise ValueError("cannot restore edits from an unfinished worker")
        for name, event in edits.items():
            index = event["version"] - 1
            if not 0 <= index < 100 or name != f"data-{index:03d}.txt":
                raise ValueError("unexpected workload file identity")
            path = worktree / name
            expected = f"{worker['token']}:{event['phase']}:{index}\n".encode()
            if event["token"] != worker["token"] or path.is_symlink():
                raise ValueError("workload edit identity changed")
            current = path.read_bytes()
            if current == seed:
                continue  # Already restored after a failed public cleanup.
            if current != expected:
                raise ValueError(f"unexpected contents preserved: {path}")
            original = fixture.command(
                ["git", "-C", str(worktree), "show", f"HEAD:{name}"]
            ).stdout.encode()
            if original != seed:
                raise ValueError("workload seed differs from committed fixture")
            evidence.append(
                {
                    "path": str(path),
                    "content": current.decode(),
                    "sha256": hashlib.sha256(current).hexdigest(),
                    "event": event,
                }
            )
            changes.append((path, current))
    if changes:
        archive = fixture.output / "cleanup-workload-changes.json"
        with archive.open("x") as stream:
            stream.write(json.dumps(evidence, indent=2) + "\n")
        for path, current in changes:
            if path.is_symlink() or path.read_bytes() != current:
                raise ValueError(f"workload changed during cleanup: {path}")
            path.write_bytes(seed)
    return evidence


def _owned_ready_processes(fixture: Fixture, receipt: dict[str, Any]) -> dict[int, int]:
    from bench_i1_fixture import json_lines

    known = {row["pid"]: row["process_start"] for row in fixture.workers}
    reconciled = []
    for path in fixture.output.glob("worker-*.jsonl"):
        try:
            records = json_lines(path)
            if not records or records[0].get("event") != "ready":
                continue
            ready = records[0]
            prompt = json.loads(path.with_name(path.stem + "-prompt.json").read_text())
            identity = ready["identity"]
            project = identity["HYDRA_PROJECT_ID"]
            head = identity["HYDRA_HEAD_ID"]
            instance = identity["HYDRA_INSTANCE_ID"]
            if any(
                not value or Path(value).name != value
                for value in (project, head, instance)
            ):
                raise ValueError("unsafe readiness identity")
            state = fixture.home / "state/v2/projects" / project / "heads" / head
            if (
                ready["token"] != prompt["token"]
                or str(path) != prompt["receipt"]
                or (state / "worktree").read_text().strip() != ready["cwd"]
                or (state / "current-instance").read_text().strip() != instance
            ):
                raise ValueError("partial-ready receipt does not match its owned head")
            pid, birth = ready["pid"], ready["process_start"]
            if (
                not isinstance(pid, int)
                or pid <= 1
                or not isinstance(birth, int)
                or birth <= 0
            ):
                raise ValueError("missing worker process creation identity")
            known[pid] = birth
            reconciled.append(
                {"pid": pid, "identity_start": birth, "receipt": str(path)}
            )
        except (OSError, ValueError, KeyError, TypeError) as error:
            receipt["errors"].append("partial-ready reconciliation: " + repr(error))
    receipt["reconciled_ready"] = reconciled
    return known


def cleanup_fixture(self: Fixture) -> dict[str, Any]:
    receipt: dict[str, Any] = {
        "socket": str(self.socket),
        "workers": [],
        "errors": [],
        "cleanup_ok": False,
    }
    known = _owned_ready_processes(self, receipt)
    receipt["owned_heads_before"] = [
        str(path) for path in (self.home / "state/v2/projects").glob("*/heads/head_*")
    ]
    try:
        if self.repo.is_dir() and self.socket.exists():
            panes = self.command(
                [
                    "tmux",
                    "-S",
                    str(self.socket),
                    "list-panes",
                    "-a",
                    "-F",
                    "#{pane_id}",
                ],
                check=False,
            ).stdout.split()
            for index, pane in enumerate(panes):
                capture = self.command(
                    [
                        "tmux",
                        "-S",
                        str(self.socket),
                        "capture-pane",
                        "-p",
                        "-S",
                        "-80",
                        "-t",
                        pane,
                    ],
                    check=False,
                )
                (self.output / f"cleanup-pane-{index}.txt").write_text(capture.stdout)
    except (OSError, subprocess.SubprocessError) as error:
        receipt["errors"].append("pane receipt: " + repr(error))
    try:
        if self.repo.is_dir() and (self.repo / ".git").is_dir():
            try:
                receipt["restored_workload_changes"] = restore_workload_changes(self)
            except (
                OSError,
                RuntimeError,
                ValueError,
                KeyError,
                TypeError,
                subprocess.SubprocessError,
            ) as error:
                receipt["errors"].append("workload preservation: " + repr(error))
            result = self.public("kill", "--all", "--force", check=False, timeout=120)
            receipt["public_exit"] = result.returncode
    except (OSError, subprocess.SubprocessError) as error:
        receipt["errors"].append("public cleanup: " + repr(error))
    for owner in self.owners:
        if owner.poll() is None:
            owner.terminate()
        try:
            owner.wait(timeout=5)
        except subprocess.TimeoutExpired:
            owner.kill()
            owner.wait(timeout=5)
    for stream in self.owner_files:
        stream.close()
    for pid, identity in known.items():
        sample = self.counters.sample(pid)
        same = (
            identity is not None
            and sample.get("available")
            and sample.get("identity_start") == identity
        )
        if same:
            try:
                os.kill(pid, 15)
            except ProcessLookupError:
                pass
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and self.counters.sample(pid).get(
                "available"
            ):
                time.sleep(0.03)
        after = self.counters.sample(pid)
        receipt["workers"].append(
            {
                "pid": pid,
                "before": sample,
                "after": after,
                "same_worker_remaining": after.get("available")
                and after.get("identity_start") == identity,
            }
        )
    sessions = ""
    try:
        if self.socket.exists():
            _cleanup_sessions(self)
        result = subprocess.CompletedProcess([], 0, "", "")
        if self.socket.exists():
            result = self.command(
                ["tmux", "-S", str(self.socket), "list-sessions", "-F", "#S"],
                check=False,
            )
        sessions = result.stdout.strip()
    except (OSError, subprocess.SubprocessError) as error:
        receipt["errors"].append("private session cleanup: " + repr(error))
    receipt["sessions_after"] = sessions
    receipt["cleanup_ok"] = (
        not receipt["errors"]
        and not sessions
        and not any(row["same_worker_remaining"] for row in receipt["workers"])
        and receipt.get("public_exit", 0) == 0
    )
    if receipt["cleanup_ok"]:
        shutil.rmtree(self.tmux_root)
    (self.output / "cleanup.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


def _cleanup_sessions(self: Fixture) -> None:
    result = self.command(
        ["tmux", "-S", str(self.socket), "list-sessions", "-F", "#S"], check=False
    )
    for session in result.stdout.splitlines():
        self.command(
            ["tmux", "-S", str(self.socket), "kill-session", "-t", session],
            check=False,
        )
