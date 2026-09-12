"""Drive the existing native PTY test observer; retain every protocol receipt."""

from __future__ import annotations

import json
import os
import queue
import re
import subprocess
import threading
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any

from bench_i1_process import ProcessCounters


def owned_heads_visible(
    screen: str, parser_complete: bool, owned_branches: set[str]
) -> bool:
    """Match an exact model total and owned viewport rows without assuming order."""
    if not parser_complete or not owned_branches or "[Heads]" not in screen:
        return False
    lines = screen.splitlines()
    tables = [
        index
        for index, line in enumerate(lines)
        if line.startswith("|") and line.split("|")[1].strip() == "HEAD"
    ]
    summaries = [
        (index, match)
        for index, line in enumerate(lines)
        if (
            match := re.fullmatch(
                r"\|\s*(\d+) heads\s*\|\s*Row (\d+) of (\d+)\s*\|\s*", line
            )
        )
    ]
    details = [
        index
        for index, line in enumerate(lines)
        if line.startswith("+- SELECTED HEAD ")
    ]
    if len(tables) != 1 or len(summaries) != 1 or len(details) != 1:
        return False
    summary_index, summary = summaries[0]
    count, position, total = map(int, summary.groups())
    if not (
        count == total == len(owned_branches)
        and 1 <= position <= count
        and tables[0] < summary_index < details[0] < len(lines) - 1
    ):
        return False
    rows, selected = [], []
    for line in lines[tables[0] + 1 : summary_index]:
        if not line.startswith("|"):
            return False
        cells = line.split("|")
        if len(cells) != 6:
            return False
        branch = cells[1].strip()
        if branch.startswith("> "):
            branch = branch[1:].strip()
            selected.append(branch)
        rows.append(branch)
    selected_cells = lines[details[0] + 1].split("|")
    return (
        bool(rows)
        and len(rows) == len(set(rows))
        and set(rows) <= owned_branches
        and len(selected) == 1
        and len(selected_cells) == 3
        and selected_cells[1].strip() == selected[0]
    )


def signal_owned_tui(
    pid: int, birth: int | None, counters: ProcessCounters
) -> dict[str, Any]:
    current = counters.sample(pid)
    receipt: dict[str, Any] = {
        "pid": pid,
        "recorded_birth": birth,
        "current": current,
        "signalled": False,
    }
    if (
        birth is None
        or not current.get("available")
        or current.get("identity_start") != birth
    ):
        receipt["reason"] = "process birth identity unavailable or changed"
        return receipt
    try:
        if os.getpgid(pid) != pid:
            receipt["reason"] = "recorded child no longer owns its session group"
            return receipt
        os.killpg(pid, 15)
        receipt["signalled"] = True
    except OSError as error:
        receipt["error"] = str(error)
    return receipt


class Observer:
    def __init__(
        self,
        binary: Path,
        tui: Path,
        hydra: Path,
        directory: Path,
        cwd: Path,
        env: dict[str, str],
        columns: int = 120,
        rows: int = 40,
    ) -> None:
        directory.mkdir()
        self.started_ns = time.monotonic_ns()
        self.directory = directory
        self.rows = rows
        self.columns = columns
        self.records: list[dict[str, Any]] = []
        self.pending: queue.Queue[dict[str, Any]] = queue.Queue()
        self.lock = threading.Lock()
        self.serial = 0
        self.closed = False
        self.counters = ProcessCounters()
        self.native_births: dict[int, int | None] = {}
        self.fallback_receipts: list[dict[str, Any]] = []
        self.stderr = (directory / "observer.stderr").open("w")
        self.log = (directory / "observer.jsonl").open("w")
        self.process = subprocess.Popen(
            [
                str(binary),
                "--item10-session",
                str(tui),
                str(hydra),
                str(directory),
                str(columns),
                str(rows),
            ],
            cwd=cwd,
            env=env,
            text=True,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.stderr,
            bufsize=1,
            start_new_session=True,
        )
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            self.log.write(line)
            self.log.flush()
            try:
                record = json.loads(line)
            except ValueError:
                record = {"event": "protocol-error", "line": line}
            record["controller_received_ns"] = time.monotonic_ns()
            if record.get("event") == "spawn":
                sampled = self.counters.sample(record["tui_pid"])
                self.native_births[record["tui_pid"]] = sampled.get("identity_start")
                (self.directory / "native-identity.json").write_text(
                    json.dumps(sampled, indent=2) + "\n"
                )
            self.records.append(record)
            self.pending.put(record)

    def send(self, command: str) -> int:
        with self.lock:
            if self.process.poll() is not None or self.process.stdin is None:
                raise RuntimeError("native observer exited")
            sent = time.monotonic_ns()
            with (self.directory / "controller.jsonl").open("a") as stream:
                stream.write(json.dumps({"sent_ns": sent, "command": command}) + "\n")
            self.process.stdin.write(command + "\n")
            self.process.stdin.flush()
            return sent

    def wait(
        self,
        event: str,
        identifier: str | None = None,
        timeout: float = 4,
        *,
        deadline_ns: int | None = None,
    ) -> dict[str, Any]:
        if deadline_ns is None:
            deadline_ns = time.monotonic_ns() + int(timeout * 1e9)
        while time.monotonic_ns() < deadline_ns:
            for record in self.records:
                if record.get("event") == event and (
                    identifier is None or record.get("id") == identifier
                ):
                    if (
                        record["controller_received_ns"] >= deadline_ns
                        or time.monotonic_ns() >= deadline_ns
                    ):
                        raise TimeoutError(f"late {event}/{identifier}")
                    return record
            if self.process.poll() is not None:
                self.reader.join(
                    timeout=min(1, max(0, (deadline_ns - time.monotonic_ns()) / 1e9))
                )
                if any(
                    record.get("event") == event
                    and (identifier is None or record.get("id") == identifier)
                    for record in self.records
                ):
                    continue
                raise RuntimeError(f"observer exited before {event}/{identifier}")
            remaining = (deadline_ns - time.monotonic_ns()) / 1e9
            if remaining <= 0:
                break
            try:
                self.pending.get(timeout=min(0.02, remaining))
            except queue.Empty:
                pass
        raise TimeoutError(f"no {event}/{identifier} before deadline {deadline_ns}")

    def ready(
        self, owned_branches: set[str], timeout: float = 15
    ) -> tuple[str, dict[str, Any]]:
        deadline = self.started_ns + int(timeout * 1e9)
        self.wait("raw-ready", deadline_ns=deadline)
        return self._visible(
            lambda screen, record: owned_heads_visible(
                screen, record["parser_complete"], owned_branches
            ),
            deadline,
            f"{len(owned_branches)} owned heads and exact model cardinality",
        )

    def calibrate_clock(self) -> dict[str, Any]:
        samples = []
        for index in range(3):
            identifier = f"clock-{index}"
            before = self.send(f"C {identifier}")
            receipt = self.wait("clock-sync", identifier)
            after, observed = receipt["controller_received_ns"], receipt["observer_ns"]
            samples.append(
                {
                    "controller_before_ns": before,
                    "observer_ns": observed,
                    "controller_after_ns": after,
                    "offset_lower_ns": observed - after,
                    "offset_upper_ns": observed - before,
                }
            )
            if not before <= observed <= after:
                raise RuntimeError(
                    "observer/controller clocks do not share the declared epoch"
                )
        narrowest = min(
            samples,
            key=lambda row: row["controller_after_ns"] - row["controller_before_ns"],
        )
        return {
            "samples": samples,
            "offset_bound": [
                narrowest["offset_lower_ns"],
                narrowest["offset_upper_ns"],
            ],
            "scope": "same host bracket bound; neither cross-host alignment nor physical clock accuracy",
        }

    def snapshot(self, *, deadline_ns: int | None = None) -> tuple[str, dict[str, Any]]:
        if deadline_ns is None:
            deadline_ns = time.monotonic_ns() + 4_000_000_000
        if time.monotonic_ns() >= deadline_ns:
            raise TimeoutError("snapshot deadline already expired")
        self.serial += 1
        identifier = f"screen-{self.serial}"
        self.send(f"S {identifier}")
        record = self.wait("snapshot", identifier, deadline_ns=deadline_ns)
        screen = (self.directory / f"snapshot-{identifier}.txt").read_text()
        if time.monotonic_ns() >= deadline_ns:
            raise TimeoutError("snapshot read completed after its deadline")
        return screen, record

    def visible(
        self, text: str, timeout: float = 15, *, deadline_ns: int | None = None
    ) -> tuple[str, dict[str, Any]]:
        if deadline_ns is None:
            deadline_ns = time.monotonic_ns() + int(timeout * 1e9)
        return self._visible(
            lambda screen, record: text in screen and record["parser_complete"],
            deadline_ns,
            repr(text),
        )

    def _visible(
        self,
        predicate: Callable[[str, dict[str, Any]], bool],
        deadline_ns: int,
        description: str,
    ) -> tuple[str, dict[str, Any]]:
        latest = ""
        while time.monotonic_ns() < deadline_ns:
            latest, record = self.snapshot(deadline_ns=deadline_ns)
            if predicate(latest, record):
                if time.monotonic_ns() >= deadline_ns:
                    break
                return latest, record
            time.sleep(min(0.04, max(0, (deadline_ns - time.monotonic_ns()) / 1e9)))
        raise TimeoutError(
            f"visible region not ready: {description}; last screen saved"
        )

    def input(self, identifier: str, value: bytes) -> int:
        return self.send(f"I {identifier} {value.hex()}")

    def expect(
        self,
        identifier: str,
        identity: tuple[int, int, int, str],
        response: tuple[int, int, int, str],
        timeout_ms: int = 4000,
    ) -> None:
        ix, iy, iw, it = identity
        rx, ry, rw, rt = response
        self.send(
            f"E {identifier} {ix} {iy} {iw} {it.encode().hex()} {rx} {ry} {rw} {rt.encode().hex()} {timeout_ms}"
        )

    def resize(self, identifier: str, columns: int, rows: int) -> int:
        self.columns, self.rows = columns, rows
        return self.send(f"R {identifier} {columns} {rows}")

    def close(self, attached: bool = False) -> dict[str, Any]:
        if self.closed:
            return next(
                (r for r in reversed(self.records) if r.get("event") == "exit"), {}
            )
        self.closed = True
        try:
            if self.process.poll() is None:
                self.send("Q cleanup " + (b"\x02q" if attached else b"\x1b\x1bq").hex())
            self.process.wait(timeout=5)
        except (BrokenPipeError, RuntimeError, subprocess.TimeoutExpired):
            if not any(
                record.get("event") == "exit" and record.get("reaped")
                for record in self.records
            ):
                for pid, birth in self.native_births.items():
                    self.fallback_receipts.append(
                        signal_owned_tui(pid, birth, self.counters)
                    )
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        finally:
            if self.process.stdin:
                try:
                    self.process.stdin.close()
                except BrokenPipeError:
                    pass
            self.reader.join(timeout=2)
            if self.process.stdout and not self.reader.is_alive():
                self.process.stdout.close()
            self.log.close()
            self.stderr.close()
        receipt = next(
            (r for r in reversed(self.records) if r.get("event") == "exit"), {}
        )
        receipt["observer_exit"] = self.process.returncode
        receipt["python_fallback"] = self.fallback_receipts
        receipt["reader_complete"] = not self.reader.is_alive()
        (self.directory / "cleanup.json").write_text(
            json.dumps(receipt, indent=2) + "\n"
        )
        return receipt


def region(screen: str, text: str) -> tuple[int, int, int, str]:
    """Require a unique, untruncated exact ASCII token in the applied screen."""
    matches = [
        (line.index(text), y, len(text), text)
        for y, line in enumerate(screen.splitlines())
        if text in line
    ]
    if len(matches) != 1 or not text.isascii():
        raise ValueError(f"expected unique visible token: {text!r}")
    return matches[0]
