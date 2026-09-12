#!/usr/bin/env python3
"""Seeded active stand-in for public Hydra benchmark heads; no provider claim."""

from __future__ import annotations

import json
import os
import select
import signal
import sys
import termios
import time
from pathlib import Path
from typing import Any


def main() -> int:
    if sys.argv[1:] in (["--help"], ["--version"]):
        print("hydra-item10-worker-v2")
        return 0
    if len(sys.argv) != 2:
        return 2
    from bench_i1_process import ProcessCounters

    config = json.loads(sys.argv[1])
    token = config["token"]
    if not isinstance(token, str) or not token.isalnum() or not 1 <= len(token) <= 20:
        return 2
    control_path, receipt_path = Path(config["control"]), Path(config["receipt"])
    if not control_path.is_absolute() or not receipt_path.is_absolute():
        return 2
    interactive = config["mode"] == "interactive"
    control = os.open(control_path, os.O_RDWR | os.O_NONBLOCK)
    buffer = b""
    pending = bytearray()
    phase: dict[str, Any] | None = None
    next_event = 0
    generated = written = count = blocked = 0
    queue_nonempty_ns = 0
    pending_since = 0
    max_pending = 0
    finished = False
    original_terminal = None
    deadline = time.monotonic() + 1800

    def record(event: str, **fields: Any) -> None:
        with receipt_path.open("a") as stream:
            stream.write(
                json.dumps(
                    {
                        "event": event,
                        "token": token,
                        "pid": os.getpid(),
                        "ppid": os.getppid(),
                        "cwd": os.getcwd(),
                        "monotonic_ns": time.monotonic_ns(),
                        **fields,
                    },
                    sort_keys=True,
                )
                + "\n"
            )

    def stop(signum: int, _frame: object) -> None:
        record("signal", signal=signum)
        raise SystemExit(128 + signum)

    def canonical(kind: str, **fields: Any) -> bytes:
        return (
            json.dumps(
                {"schema_version": 1, "type": kind, **fields}, separators=(",", ":")
            )
            + "\n"
        ).encode()

    def enqueue(payload: bytes) -> None:
        nonlocal max_pending, pending_since
        if len(pending) + len(payload) > 256 * 1024:
            raise RuntimeError("producer backlog cap reached")
        if not pending:
            pending_since = time.monotonic_ns()
        pending.extend(payload)
        max_pending = max(max_pending, len(pending))

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    if interactive and sys.stdin.isatty():
        original_terminal = termios.tcgetattr(sys.stdin.fileno())
        quiet_terminal = original_terminal.copy()
        quiet_terminal[3] &= ~termios.ECHO
        termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, quiet_terminal)
    os.set_blocking(sys.stdout.fileno(), False)
    record(
        "ready",
        process_start=ProcessCounters().sample(os.getpid())["identity_start"],
        mode=config["mode"],
        python=sys.executable,
        version=sys.version,
        clock=vars(time.get_clock_info("monotonic")),
        identity=config["binding"]
        if config["mode"] == "headless"
        else {
            key: os.environ.get(key)
            for key in ("HYDRA_PROJECT_ID", "HYDRA_HEAD_ID", "HYDRA_INSTANCE_ID")
        },
    )
    enqueue(canonical("observation", status="running"))
    try:
        while not finished and time.monotonic() < deadline:
            now = time.monotonic_ns()
            if phase is not None and phase["start_ns"] <= now:
                end = phase["start_ns"] + phase["duration_ns"]
                while next_event < end and next_event <= now:
                    if phase["case"] in ("sustained-output", "result-output"):
                        marker = f"{token}:{count:04d}"
                        kind = (
                            "observation"
                            if phase["case"] == "sustained-output"
                            else "result"
                        )
                        fields = (
                            {"status": "running", "text": marker}
                            if kind == "observation"
                            else {"text": marker}
                        )
                        payload = canonical(kind, **fields)
                        padding = 128 - len(payload)
                        if padding < 0:
                            raise RuntimeError(
                                "canonical line exceeds declared 128-byte size"
                            )
                        fields["text"] = marker + "." * padding
                        payload = canonical(kind, **fields)
                        assert len(payload) == 128
                        enqueue(payload)
                        generated += len(payload)
                        record(
                            "output-scheduled",
                            sequence=count,
                            event_type=kind,
                            scheduled_ns=next_event,
                            generated_ns=now,
                            bytes=len(payload),
                            backlog_bytes=len(pending),
                        )
                    elif (
                        phase["case"] in ("one-changing", "all-changing")
                        and phase["change"]
                    ):
                        path = Path(f"data-{count:03d}.txt")
                        if not path.is_file() or count >= 100:
                            raise RuntimeError("fixture file contract is unavailable")
                        path.write_text(f"{token}:{phase['id']}:{count}\n")
                        record(
                            "repository-change",
                            phase=phase["id"],
                            file=str(path),
                            version=count + 1,
                            scheduled_ns=next_event,
                        )
                    count += 1
                    next_event += phase["interval_ns"]
                if now >= end:
                    record(
                        "generation-stopped",
                        phase=phase["id"],
                        generated_bytes=generated,
                        written_bytes=written,
                        backlog_bytes=len(pending),
                        scheduled_events=count,
                        write_eagain_count=blocked,
                        queue_nonempty_ns=queue_nonempty_ns,
                        peak_backlog_bytes=max_pending,
                    )
                    phase = None
            reads = [control]
            if interactive:
                reads.append(sys.stdin.fileno())
            wait = 1.0
            if phase is not None:
                wait = max(0.0, min(wait, (next_event - time.monotonic_ns()) / 1e9))
            ready, writable, _ = select.select(
                reads, [sys.stdout.fileno()] if pending else [], [], wait
            )
            if writable:
                try:
                    size = os.write(sys.stdout.fileno(), pending)
                except BlockingIOError:
                    blocked += 1
                else:
                    written += size
                    del pending[:size]
                    if not pending:
                        queue_nonempty_ns += time.monotonic_ns() - pending_since
                        record("output-drained", total_written_bytes=written)
            if interactive and sys.stdin.fileno() in ready:
                line = sys.stdin.readline()
                if not line:
                    interactive = False
                else:
                    command = line.strip()
                    if (
                        command[:5] in ("ECHO ", "LATE ")
                        and command[5:].isalnum()
                        and len(command[5:]) <= 24
                    ):
                        record("input", text=command[5:])
                        if command.startswith("LATE "):
                            time.sleep(0.1)
                        enqueue(f"\r\033[2KACK {token} {command[5:]}\r".encode())
            if control in ready:
                buffer += os.read(control, 4096)
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    if not line:
                        continue
                    command = json.loads(line)
                    if command["action"] == "phase":
                        if phase is not None or pending:
                            raise RuntimeError(
                                "phase started before prior work drained"
                            )
                        if command["case"] not in (
                            "quiescent",
                            "one-changing",
                            "all-changing",
                            "sustained-output",
                            "result-output",
                        ):
                            raise ValueError("unknown workload case")
                        duration = int(command["duration_ns"])
                        if not 0 < duration <= 30_000_000_000:
                            raise ValueError(
                                "phase exceeds the declared 30-second bound"
                            )
                        phase = command
                        phase["interval_ns"] = (
                            31_250_000
                            if command["case"] in ("sustained-output", "result-output")
                            else 2_000_000_000
                        )
                        next_event = int(command["start_ns"])
                        generated = written = count = blocked = queue_nonempty_ns = (
                            max_pending
                        ) = 0
                        record(
                            "phase-armed",
                            phase=command["id"],
                            start_ns=next_event,
                            case=command["case"],
                            duration_ns=duration,
                            interval_ns=phase["interval_ns"],
                            change=command["change"],
                        )
                    elif command["action"] == "finish":
                        if phase is not None or pending:
                            raise RuntimeError("finish before producer recovery")
                        Path("item10-result.txt").write_text(token)
                        enqueue(canonical("result", text=token))
                        enqueue(canonical("observation", status="idle"))
                        while pending:
                            _, writable, _ = select.select(
                                [], [sys.stdout.fileno()], [], 2
                            )
                            if not writable:
                                raise TimeoutError("final result output blocked")
                            del pending[: os.write(sys.stdout.fileno(), pending)]
                        record(
                            "completed",
                            artifact=str(Path("item10-result.txt").resolve()),
                        )
                        finished = True
                    else:
                        raise ValueError("unknown worker action")
        if not finished:
            record("fixture-deadline")
            return 124
        return 0
    finally:
        if original_terminal is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, original_terminal)
        os.close(control)


if __name__ == "__main__":
    raise SystemExit(main())
