"""Run any command on a real pseudo-terminal and retain its timestamped bytes.

The native observer only launches the Hydra TUI. The U2 and U3 checks need
a plain tmux client and an interactive `hydra spawn` on a terminal of a given
width, so this driver forks the command on a PTY, records every read with a
monotonic timestamp and applies the bytes to the bounded emulator.
"""

from __future__ import annotations

import fcntl
import os
import select
import signal
import struct
import termios
import threading
import time
from pathlib import Path

from acceptance import vt


class PtyProcess:
    def __init__(self, argv: list[str], cwd: Path, env: dict[str, str], directory: Path,
                 columns: int = 80, rows: int = 24) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        self.directory = directory
        self.columns, self.rows = columns, rows
        self.screen = vt.Screen(columns, rows)
        self.chunks: list[tuple[int, int, int]] = []  # (ns, offset, length)
        self.raw = (directory / "terminal.raw").open("wb")
        self.index = (directory / "terminal.index").open("w")
        self.lock = threading.Lock()
        self.exited: int | None = None
        self.written = 0
        pid, master = os.forkpty()
        if pid == 0:  # child: size, cwd, exec
            try:
                fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
                os.chdir(cwd)
                os.execvpe(argv[0], argv, env)
            finally:
                os._exit(127)
        self.pid, self.master = pid, master
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self) -> None:
        while True:
            try:
                ready, _, _ = select.select([self.master], [], [], 0.05)
            except (OSError, ValueError):
                break
            if not ready:
                if self.exited is not None:
                    break
                continue
            try:
                data = os.read(self.master, 65536)
            except OSError:
                break
            if not data:
                break
            now = time.monotonic_ns()
            with self.lock:
                self.chunks.append((now, self.written, len(data)))
                self.index.write(f"{len(self.chunks)} {now} {self.written} {len(data)}\n")
                self.raw.write(data)
                self.written += len(data)
                self.screen.feed(data)

    def text(self) -> str:
        with self.lock:
            return self.screen.text()

    def wait_for(self, text: str, timeout: float = 15) -> str:
        deadline = time.monotonic() + timeout
        while True:
            screen = self.text()
            if text in screen:
                return screen
            if time.monotonic() >= deadline:
                raise TimeoutError(f"{text!r} not shown within {timeout}s; last screen:\n{screen}")
            time.sleep(0.05)

    def quiet(self, settle: float = 0.5, timeout: float = 10) -> None:
        """Wait until no bytes arrived for `settle` seconds."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self.lock:
                last = self.chunks[-1][0] if self.chunks else 0
            if last and time.monotonic_ns() - last > settle * 1e9:
                return
            time.sleep(0.05)

    def send(self, data: bytes) -> None:
        os.write(self.master, data)

    def resize(self, columns: int, rows: int) -> None:
        self.columns, self.rows = columns, rows
        fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
        with self.lock:
            self.screen.resize(columns, rows)

    def poll(self) -> int | None:
        if self.exited is None:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid == self.pid:
                self.exited = os.waitstatus_to_exitcode(status)
        return self.exited

    def close(self, timeout: float = 5) -> dict:
        deadline = time.monotonic() + timeout
        while self.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        if self.poll() is None:
            os.kill(self.pid, signal.SIGHUP)
            time.sleep(0.5)
            if self.poll() is None:
                os.kill(self.pid, signal.SIGKILL)
                os.waitpid(self.pid, 0)
                self.exited = -9
        self.reader.join(timeout=2)
        with self.lock:
            self.raw.close()
            self.index.close()
        try:
            os.close(self.master)
        except OSError:
            pass
        (self.directory / "final-screen.txt").write_text(self.screen.text())
        (self.directory / "final-screen.html").write_text(self.screen.html("final screen"))
        return {"exit": self.exited, "bytes": self.written, "reads": len(self.chunks)}
