"""Standard-library PTY driver and observer for termviz's emitted ANSI subset.

The observer is independent of the C terminal parser. It only interprets cursor
addressing, screen clears, SGR, and printable UTF-8 produced by the presenter.
"""
from __future__ import annotations

import codecs
import fcntl
import html
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import termios
import time
import unicodedata


class Screen:
    def __init__(self, cols: int, rows: int, wait_for_clear: bool = False):
        self.cols, self.rows = cols, rows
        self.awaiting_clear = wait_for_clear
        self.x = self.y = self.clears = self.overflow = 0
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")
        self.escape = ""
        self.fg, self.bg = "#dddddd", "#161616"
        self.bold = self.reverse = False
        self.cells = [[(" ", self.fg, self.bg, False, 1) for _ in range(cols)] for _ in range(rows)]

    @staticmethod
    def palette(index: int) -> str:
        basic = ["#000000", "#cd3131", "#0dbc79", "#e5e510", "#2472c8", "#bc3fbc", "#11a8cd", "#e5e5e5",
                 "#666666", "#f14c4c", "#23d18b", "#f5f543", "#3b8eea", "#d670d6", "#29b8db", "#ffffff"]
        if index < 16:
            return basic[index]
        if index >= 232:
            v = 8 + (index - 232) * 10
            return f"#{v:02x}{v:02x}{v:02x}"
        n = index - 16
        levels = [0, 95, 135, 175, 215, 255]
        return "#" + "".join(f"{levels[v]:02x}" for v in (n // 36, n // 6 % 6, n % 6))

    def sgr(self, values: list[int]) -> None:
        i = 0
        while i < len(values):
            v = values[i]
            if v == 0:
                self.fg, self.bg = "#dddddd", "#161616"
                self.bold = self.reverse = False
            elif v == 1:
                self.bold = True
            elif v == 7:
                self.reverse = True
            elif v in (38, 48) and i + 2 < len(values):
                if values[i + 1] == 5:
                    color = self.palette(values[i + 2])
                    i += 2
                elif values[i + 1] == 2 and i + 4 < len(values):
                    color = "#" + "".join(f"{n:02x}" for n in values[i + 2:i + 5])
                    i += 4
                else:
                    raise AssertionError("Malformed presenter color")
                if v == 38:
                    self.fg = color
                else:
                    self.bg = color
            elif 30 <= v <= 37:
                self.fg = self.palette(v - 30)
            elif 40 <= v <= 47:
                self.bg = self.palette(v - 40)
            i += 1

    def feed(self, data: bytes) -> None:
        for char in self.decoder.decode(data):
            if self.escape:
                self.escape += char
                if self.escape == "\x1b[":
                    continue
                if len(self.escape) >= 3 and "@" <= char <= "~":
                    body = self.escape[2:-1]
                    if not body.startswith("?"):
                        values = [int(v or "0") for v in body.split(";")]
                        if char in "Hf":
                            self.y = (values[0] or 1) - 1
                            self.x = (values[1] if len(values) > 1 and values[1] else 1) - 1
                        elif char == "J" and values[0] == 2:
                            self.awaiting_clear = False
                            self.clears += 1
                            self.cells = [[(" ", self.fg, self.bg, False, 1) for _ in range(self.cols)] for _ in range(self.rows)]
                        elif char == "m":
                            self.sgr(values)
                    self.escape = ""
                if len(self.escape) > 256:
                    raise AssertionError("Unbounded presenter escape")
                continue
            if char == "\x1b":
                self.escape = char
            elif self.awaiting_clear:
                continue
            elif char == "\r":
                self.x = 0
            elif char == "\n":
                self.y += 1
            elif unicodedata.combining(char) or unicodedata.category(char) in ("Mn", "Me"):
                x = self.x - 1
                if 0 <= self.y < self.rows and x >= 0:
                    if not self.cells[self.y][x][4] and x:
                        x -= 1
                    cell = self.cells[self.y][x]
                    self.cells[self.y][x] = (cell[0] + char, *cell[1:])
            elif char >= " ":
                width = 2 if unicodedata.east_asian_width(char) in "WF" else 1
                if self.x + width > self.cols or not 0 <= self.y < self.rows:
                    self.overflow += 1
                else:
                    fg, bg = (self.bg, self.fg) if self.reverse else (self.fg, self.bg)
                    self.cells[self.y][self.x] = (char, fg, bg, self.bold, width)
                    if width == 2:
                        self.cells[self.y][self.x + 1] = ("", fg, bg, self.bold, 0)
                self.x += width

    def text(self) -> str:
        return "\n".join("".join(c[0] for c in row) for row in self.cells)

    def save(self, path: Path) -> None:
        rows = []
        for row in self.cells:
            rows.append("".join(f'<span style="color:{fg};background:{bg};font-weight:{700 if bold else 400};width:{width}ch">{html.escape(char)}</span>'
                                for char, fg, bg, bold, width in row if width))
        path.write_text('<!doctype html><meta charset="utf-8"><title>Actual termviz PTY output</title>'
                        '<style>body{background:#161616;padding:20px;color:#ddd}pre{font:14px/1.3 monospace}span{display:inline-block}</style>'
                        '<p>Actual PTY output / reconstructed terminal cells</p><pre>' + "\n".join(rows) + "</pre>")


class Session:
    def __init__(self, argv: list[str], cols: int, rows: int, env: dict[str, str] | None = None, cwd: Path | None = None):
        self.closed = False
        self.master, self.slave = pty.openpty()
        self.original = termios.tcgetattr(self.slave)
        self.screen = Screen(cols, rows)
        self.raw = bytearray()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.process = subprocess.Popen(argv, stdin=self.slave, stdout=self.slave, stderr=self.slave,
                                        start_new_session=True, cwd=cwd, env={**os.environ, "ENV": "/dev/null", "PS1": "TV$ ", "TERM": "xterm-256color", **(env or {})})

    def pump(self, seconds: float = .15) -> bytes:
        data = bytearray()
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            ready, _, _ = select.select([self.master], [], [], max(0, end - time.monotonic()))
            if ready:
                chunk = os.read(self.master, 65536)
                if not chunk:
                    break
                data.extend(chunk)
                self.screen.feed(chunk)
        self.raw.extend(data)
        return bytes(data)

    def until(self, marker: str, timeout: float = 3) -> None:
        end = time.monotonic() + timeout
        while marker not in self.screen.text() and time.monotonic() < end:
            self.pump(.05)
        assert marker in self.screen.text(), f"Missing {marker!r}:\n{self.screen.text()}"

    def until_pattern(self, pattern: str, timeout: float = 3) -> re.Match[str]:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            match = re.search(pattern, self.screen.text())
            if match:
                return match
            self.pump(.05)
        raise AssertionError(f"Missing pattern {pattern!r}:\n{self.screen.text()}")

    def send(self, text: str | bytes) -> None:
        data = text.encode() if isinstance(text, str) else text
        while data:
            count = os.write(self.master, data)
            data = data[count:]

    def resize(self, cols: int, rows: int) -> None:
        # Buffered rows were produced for the previous dimensions. The native
        # presenter's full redraw acknowledges the new size; validate every byte
        # after that boundary, rather than interpreting old rows as new overflow.
        self.screen = Screen(cols, rows, wait_for_clear=True)
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.pump(.25)
        deadline = time.monotonic() + 3
        while self.screen.awaiting_clear and time.monotonic() < deadline:
            self.pump(.05)
        assert not self.screen.awaiting_clear, "No full redraw acknowledged the resize"

    def close(self, keys: bytes | None = None, signum: int | None = None, expected: int = 0) -> None:
        if self.process.poll() is None:
            if signum:
                self.process.send_signal(signum)
            elif keys:
                self.send(keys)
            else:
                self.process.terminate()
            end = time.monotonic() + 3
            while self.process.poll() is None and time.monotonic() < end:
                self.pump(.05)
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait(timeout=1)
                raise AssertionError("Workspace did not exit within three seconds")
        try:
            assert self.process.returncode == expected, self.process.returncode
            assert termios.tcgetattr(self.slave) == self.original, "Terminal settings were not restored"
        finally:
            os.close(self.master)
            os.close(self.slave)
            self.closed = True

    def abort(self) -> None:
        if self.closed:
            return
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            end = time.monotonic() + 2
            while self.process.poll() is None and time.monotonic() < end:
                self.pump(.05)
            if self.process.poll() is None:
                self.process.kill()
            self.process.wait(timeout=2)
        os.close(self.master)
        os.close(self.slave)
