"""A small stdlib pseudo-terminal driver for the native TUI.

The driver keeps every byte the program wrote (``raw``) and interprets the
cumulative stream through the product's own terminal model (``termviz-cells``,
built from src/termviz/tools/cells.c) whenever a screen is needed, so a capture
taken at any point is exactly what a terminal of that size would show.
"""

from __future__ import annotations

import json
import os

import select
import signal
import struct
import subprocess
import tempfile
import termios
import time
from pathlib import Path

try:  # Python 3.11+ exposes fcntl constants through termios too.
    import fcntl
except ImportError:  # pragma: no cover - fcntl exists on every supported Unix
    fcntl = None


class Driver:
    def __init__(
        self,
        argv: list[str],
        cells: Path,
        cwd: Path,
        env: dict[str, str],
        columns: int,
        rows: int,
    ) -> None:
        self.cells_tool = cells
        self.columns, self.rows = columns, rows
        self.raw = bytearray()
        self.master, slave = os.openpty()
        self._resize(slave, columns, rows)
        self.process = subprocess.Popen(
            argv,
            cwd=cwd,
            env=env,
            stdin=slave,
            stdout=slave,
            stderr=slave,
            start_new_session=True,
            close_fds=True,
        )
        os.close(slave)
        self.last_read = time.monotonic()

    def _resize(self, fd: int, columns: int, rows: int) -> None:
        if fcntl is None:
            return
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))

    def resize(self, columns: int, rows: int) -> None:
        self.columns, self.rows = columns, rows
        self._resize(self.master, columns, rows)
        if self.process.poll() is None:
            os.kill(self.process.pid, signal.SIGWINCH)

    def pump(self, seconds: float) -> int:
        """Read what the program wrote within the next ``seconds``."""
        deadline = time.monotonic() + seconds
        got = 0
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return got
            ready, _, _ = select.select([self.master], [], [], min(remaining, 0.05))
            if not ready:
                if self.process.poll() is not None:
                    return got
                continue
            try:
                chunk = os.read(self.master, 65536)
            except OSError:
                return got
            if not chunk:
                return got
            self.raw += chunk
            self.last_read = time.monotonic()
            got += len(chunk)

    def send(self, data: bytes | str) -> None:
        if isinstance(data, str):
            data = data.encode()
        os.write(self.master, data)

    def _interpret(self, text: bool) -> str:
        with tempfile.NamedTemporaryFile(prefix="vr-raw.", delete=False) as handle:
            handle.write(self.raw)
            path = handle.name
        try:
            args = [str(self.cells_tool), "--size", f"{self.columns}x{self.rows}"]
            if text:
                args.append("--text")
            return subprocess.run(
                [*args, path], capture_output=True, text=True, check=True, timeout=30
            ).stdout
        finally:
            os.unlink(path)

    def text(self) -> str:
        return self._interpret(text=True)

    def cells(self) -> dict:
        return json.loads(self._interpret(text=False))

    def wait_for(self, *markers: str, timeout: float = 10.0, absent: str | None = None) -> str:
        """Pump until every marker is on screen (and ``absent`` is not)."""
        deadline = time.monotonic() + timeout
        screen = ""
        while True:
            self.pump(0.05)
            screen = self.text()
            if all(m in screen for m in markers) and (absent is None or absent not in screen):
                return screen
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    f"{markers!r} not shown within {timeout}s"
                    + (f" (or {absent!r} still shown)" if absent else "")
                    + "; last screen:\n" + screen
                )
            if self.process.poll() is not None and not self.pump(0.2):
                raise RuntimeError(
                    f"program exited with {self.process.returncode} before {markers!r}; last screen:\n"
                    + self.text()
                )

    def settle(self, quiet: float = 0.25, timeout: float = 5.0) -> None:
        """Wait until the program has written nothing for ``quiet`` seconds."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump(quiet)
            if time.monotonic() - self.last_read >= quiet:
                return

    def snapshot(self) -> bytes:
        return bytes(self.raw)

    def close(self, keys: bytes | str | None = None, timeout: float = 5.0) -> int:
        if keys is not None and self.process.poll() is None:
            try:
                self.send(keys)
            except OSError:
                pass
        deadline = time.monotonic() + timeout
        while self.process.poll() is None and time.monotonic() < deadline:
            self.pump(0.1)
        if self.process.poll() is None:
            try:
                os.killpg(self.process.pid, signal.SIGTERM)
            except OSError:
                pass
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=3)
        try:
            os.close(self.master)
        except OSError:
            pass
        return self.process.returncode
