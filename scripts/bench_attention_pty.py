"""Small real-PTY observer for the native ASCII attention and review views."""

from __future__ import annotations

import fcntl
import json
import os
import re
import resource
import select
import signal
import struct
import subprocess
import termios
import threading
import time
from pathlib import Path

ATTENTION_FOOTER = (
    "j/k select  Enter details  r review  s seen  I refresh  Esc heads  q quit"
)
REVIEW_FOOTER = "j/k scroll i IDs f refs r load Esc/q"
CLEAR = b"\x1b[H\x1b[2J"


def clock_seconds(value: str) -> float:
    days, _, rest = value.rpartition("-")
    fields = (rest if days else value).split(":")
    return (int(days) * 86400 if days else 0) + sum(
        float(part) * 60**index for index, part in enumerate(reversed(fields))
    )


def observer_cpu() -> dict:
    usage = resource.getrusage(resource.RUSAGE_SELF)
    return {
        "pid": os.getpid(),
        "monotonic_ns": time.monotonic_ns(),
        "user_seconds": usage.ru_utime,
        "system_seconds": usage.ru_stime,
    }


def process_tree(roots: list[int]) -> dict[int, dict]:
    """ps time is cumulative *direct* CPU. Short-lived processes may be missed."""
    output = subprocess.check_output(
        ["ps", "-axo", "pid=,ppid=,pgid=,time=,stat=,command="], text=True
    )
    entries = {}
    for line in output.splitlines():
        fields = line.split(None, 5)
        if len(fields) == 6:
            pid, ppid, pgid, cpu, state, command = fields
            entries[int(pid)] = {
                "ppid": int(ppid),
                "pgid": int(pgid),
                "cpu_raw": cpu,
                "cpu_seconds": clock_seconds(cpu),
                "state": state,
                "command": command,
            }
    owned = set(roots)
    while True:
        found = {pid for pid, row in entries.items() if row["ppid"] in owned}
        if found <= owned:
            return {pid: entries[pid] for pid in owned if pid in entries}
        owned |= found


class Frames:
    """Accept only a complete known native footer on the terminal's last row.

    The native no-color renderer clears/home, emits exactly rows-1 newlines,
    then its full footer without a newline. A partial repaint cannot satisfy
    this boundary. Unsupported controls or dimensions fail closed.
    """

    def __init__(self, columns: int, rows: int):
        self.columns, self.rows = columns, rows
        self.buffer = b""
        self.offset = 0
        self.sequence = 0
        self.accepted = -1
        self.latest: dict = {}

    def feed(self, data: bytes) -> list[dict]:
        self.buffer += data
        frames = []
        while True:
            start = self.buffer.find(CLEAR)
            if start < 0:
                if len(self.buffer) > 65536:
                    raise RuntimeError("native frame clear not found within 64KiB")
                break
            if start:
                self.offset += start
                self.buffer = self.buffer[start:]
            following = self.buffer.find(CLEAR, len(CLEAR))
            body = self.buffer[len(CLEAR) : following if following >= 0 else None]
            text = body.decode("ascii", errors="strict").replace("\r", "")
            lines = text.split("\n")
            if (
                len(lines) == self.rows
                and lines[-1] in (ATTENTION_FOOTER, REVIEW_FOOTER)
                and all(len(line) < self.columns for line in lines)
                and "\x1b" not in text
                and self.accepted != self.sequence
            ):
                self.latest = {
                    "sequence": self.sequence,
                    "observed_ns": time.monotonic_ns(),
                    "raw_start": self.offset,
                    "raw_end": self.offset + len(CLEAR) + len(body),
                    "text": "\n".join(line.ljust(self.columns) for line in lines),
                }
                self.accepted = self.sequence
                frames.append(self.latest)
            if following < 0:
                break
            self.offset += following
            self.buffer = self.buffer[following:]
            self.sequence += 1
        return frames


class Session:
    def __init__(self, tui: Path, hydra: Path, env: dict, cwd: Path, directory: Path):
        self.directory = directory
        directory.mkdir()
        self.frames = Frames(240, 40)
        self.master, self.slave = os.openpty()
        self.before = termios.tcgetattr(self.slave)
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 240, 0, 0))
        self.command = [
            str(tui),
            "--hydra",
            str(hydra),
            "--fleet",
            "--view",
            "attention",
            "--ascii",
            "--no-color",
        ]

        def child_setup():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        if threading.active_count() != 1:
            raise RuntimeError(
                "PTY sessions must be created before schedule threads start"
            )
        self.child = subprocess.Popen(
            self.command,
            stdin=self.slave,
            stdout=self.slave,
            stderr=self.slave,
            env=dict(env, TERM="xterm-256color", NO_COLOR="1"),
            cwd=cwd,
            preexec_fn=child_setup,  # noqa: PLW1509 - guarded single-threaded fork
            close_fds=True,
        )
        os.set_blocking(self.master, False)
        self.raw = (directory / "raw.pty").open("wb")
        self.frame_log = (directory / "frames.jsonl").open("w")
        self.inputs: list[dict] = []
        self.children: dict[int, dict] = {}
        self.bytes = 0

    @property
    def text(self) -> str:
        return self.frames.latest.get("text", "")

    def key(self, value: str) -> int:
        sent = time.monotonic_ns()
        os.write(self.master, value.encode())
        self.inputs.append({"sent_ns": sent, "key": value})
        return sent

    def pump(self, timeout: float = 0.02) -> None:
        if not select.select([self.master], [], [], timeout)[0]:
            return
        try:
            data = os.read(self.master, 65536)
        except BlockingIOError:
            return
        except OSError as error:
            if error.errno == 5 and self.child.poll() is not None:
                return
            raise
        self.bytes += len(data)
        if self.bytes > 64 * 1024 * 1024:
            raise RuntimeError("PTY capture exceeded 64MiB bound")
        self.raw.write(data)
        for frame in self.frames.feed(data):
            row = {key: value for key, value in frame.items() if key != "text"}
            self.frame_log.write(json.dumps(row) + "\n")

    def wait(self, predicate, seconds: float = 8, after: int = 0) -> dict:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.pump()
            if self.frames.latest.get("observed_ns", 0) >= after and predicate(
                self.text
            ):
                return dict(self.frames.latest)
            if self.child.poll() is not None:
                raise RuntimeError(f"TUI exited {self.child.returncode}")
        self.capture("timeout")
        raise TimeoutError(
            f"complete native frame did not match in {seconds}s: {self.directory}"
        )

    def capture(self, label: str) -> dict:
        path = self.directory / f"{label}.txt"
        path.write_text(self.text + "\n")
        self.raw.flush()
        self.frame_log.flush()
        return {
            "path": str(path),
            "pid": self.child.pid,
            **{
                key: value for key, value in self.frames.latest.items() if key != "text"
            },
        }

    def refresh(self) -> dict:
        """Synchronize untimed corpus frames on the actual producer's exit/reap.

        Avoid mistaking a key-triggered repaint of the old snapshot for a refresh.
        Timed trials instead match a changed revision/freshness without this wait.
        """
        deadline = time.monotonic() + 8
        sent = 0
        observed_pids = set()
        while time.monotonic() < deadline:
            self.pump(0.005)
            tree = process_tree([self.child.pid])
            self.children.update(tree)
            active = {
                pid for pid, row in tree.items() if "attention-data" in row["command"]
            }
            if not sent and not active:
                sent = self.key("I")
            elif sent:
                observed_pids.update(active)
                if observed_pids and not active:
                    reaped_after = time.monotonic_ns()
                    self.wait(lambda text: "ATTENTION" in text, after=reaped_after)
                    return {
                        "sent_ns": sent,
                        "producer_pids": sorted(observed_pids),
                        "no_producer_observed_ns": reaped_after,
                        "frame_observed_ns": self.frames.latest["observed_ns"],
                    }
        raise TimeoutError("attention producer did not launch and finish within 8s")

    def close(self) -> dict:
        self.children.update(process_tree([self.child.pid]))
        self.key("\x1b")
        self.key("q")
        end = time.monotonic() + 5
        while self.child.poll() is None and time.monotonic() < end:
            self.pump()
        forced = self.child.poll() is None
        if forced:
            self.child.send_signal(signal.SIGTERM)
            try:
                self.child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.child.pid, signal.SIGKILL)
                self.child.wait(timeout=3)
        # Keep a live endpoint until termios is checked; errors are never passes.
        restored = False
        terminal_error = None
        try:
            restored = termios.tcgetattr(self.slave) == self.before
        except termios.error:
            try:
                restored = termios.tcgetattr(self.master) == self.before
            except termios.error as error:
                terminal_error = str(error)
        end = time.monotonic() + 2
        survivors = {}
        while time.monotonic() < end:
            survivors = process_tree(list(self.children))
            survivors.pop(self.child.pid, None)
            # A reaped process can already have a reused PID: compare command/parent.
            survivors = {
                pid: row
                for pid, row in survivors.items()
                if pid in self.children
                and row["command"] == self.children[pid]["command"]
            }
            if not survivors:
                break
            time.sleep(0.02)
        self.raw.close()
        self.frame_log.close()
        os.close(self.master)
        os.close(self.slave)
        row = {
            "pid": self.child.pid,
            "argv": self.command,
            "returncode": self.child.returncode,
            "terminal_restored": restored,
            "terminal_error": terminal_error,
            "forced_termination": forced,
            "surviving_observed_children": survivors,
            "observed_children": self.children,
            "inputs": self.inputs,
            "raw_bytes": self.bytes,
        }
        (self.directory / "session.json").write_text(json.dumps(row, indent=2) + "\n")
        return row


def selected_line(text: str) -> str:
    return next((line for line in text.splitlines() if re.match(r"\| >", line)), "")
