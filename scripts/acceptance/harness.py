"""Case harness: an installed-build journey that records matrix checks.

A Case is a usability Journey (fresh HOME, HYDRA_HOME, tmux socket namespace
and disposable repository on the prefix-installed product) whose checks are
tagged with the acceptance bullet they establish, each with retained evidence.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import time
from collections.abc import Callable
from pathlib import Path

import usability
from acceptance import vt
from acceptance.ptyrun import PtyProcess

CASES: list[dict] = []


def case(name: str, bullets: tuple[str, ...], columns: tuple[int, ...] = (80,), seconds: int = 120):
    """Register a case: name, the bullets it reports on, the widths it runs at."""
    def register(function: Callable) -> Callable:
        CASES.append({"name": name, "function": function, "bullets": bullets,
                      "columns": columns, "budget": seconds})
        return function
    return register


RAW_ID = re.compile(r"\b(head|instance|project|run)_[0-9a-f]{8,}")
STATE_PATH = re.compile(r"/state/v2/|/\.hydra-worktrees/")


def strip_frame(line: str) -> str:
    return line.strip().strip("|│").strip()


def pane_text(screen: str, anchor: str) -> str:
    """Text of the pane that shows `anchor`, with its wrapped rows rejoined.
    The pane's column span is the frame-free run around the anchor's row."""
    lines = screen.splitlines()
    for line in lines:
        start = line.find(anchor)
        if start < 0:
            continue
        left = max(line.rfind("|", 0, start), line.rfind("│", 0, start)) + 1
        right_candidates = [i for i in (line.find("|", start), line.find("│", start)) if i >= 0]
        right = min(right_candidates) if right_candidates else len(line)
        pieces = [row[left:right].strip() for row in lines]
        return " ".join(" ".join(pieces).split())
    return ""


class Case(usability.Journey):
    def __init__(self, source: Path, prefix: Path, output: Path, name: str, columns: int) -> None:
        super().__init__(source, prefix, output, name, columns)
        # The native observer strips colour from its ASCII snapshots but the
        # raw bytes keep it when the TUI is allowed to colour (styled snapshots).
        self.env["HYDRA_TEST_COLOR"] = "1"
        self.name = f"{name}-{columns}"
        self.results: list[dict] = []
        self.evidence_serial = 0

    # Recording --------------------------------------------------------------
    def record(self, bullet: str, name: str, passed: bool, detail: str = "",
               evidence: list[str] | None = None) -> bool:
        links = list(evidence or [])
        if self.observer and self.observer.records:
            last = next((r for r in reversed(self.observer.records) if r.get("event") == "snapshot"), None)
            if last:
                links.append(f"{self.observer.directory.name}/snapshot-{last['id']}.txt")
        self.results.append({"bullet": bullet, "check": name, "passed": bool(passed), "detail": detail,
                             "evidence": links, "at_ns": time.monotonic_ns()})
        self.proof.append({"acceptance": name, "bullet": bullet, "passed": bool(passed), "detail": detail})
        return bool(passed)

    def note(self, text: str) -> None:
        self.proof.append({"note": text, "at_ns": time.monotonic_ns()})

    def keep(self, name: str, text: str) -> str:
        """Retain text evidence under the case directory; returns its relative path."""
        self.evidence_serial += 1
        path = self.output / f"evidence-{self.evidence_serial:02d}-{name}"
        path.write_text(text if text.endswith("\n") or not text else text + "\n")
        return path.name

    def keep_json(self, name: str, value) -> str:
        return self.keep(name + ".json", json.dumps(value, indent=2, default=str))

    # Opening the control centre ---------------------------------------------
    def open(self, argv_env: dict[str, str] | None = None) -> None:
        """Open the public entry (`hydra` in the repository) on a fresh observer;
        a second call after close() retains a second terminal directory."""
        self.opens = getattr(self, "opens", 0) + 1
        directory = self.output / ("terminal" if self.opens == 1 else f"terminal-{self.opens}")
        self.observer = usability.Observer(
            self.source / "build/test-tui-pty", self.prefix / "libexec/hydra/hydra-tui", self.hydra,
            directory, self.repo, {**self.env, **(argv_env or {})}, self.columns,
            40 if self.columns > 80 else 24)
        self.observer.wait("raw-ready", timeout=15)
        self.see("HYDRA /")

    def quit(self) -> dict:
        """Leave the control centre with q and require a clean exit."""
        assert self.observer
        cleanup = self.observer.close(attached=self.attached)
        self.attached = False
        if not cleanup.get("reaped") or cleanup.get("observer_exit"):
            raise RuntimeError(f"UI did not exit cleanly: {cleanup}")
        self.observer = None
        return cleanup

    # Screens ----------------------------------------------------------------
    def rows(self) -> list[str]:
        return [strip_frame(line) for line in self.screen().splitlines()]

    def tab_bar(self, screen: str | None = None) -> str:
        lines = (screen or self.screen()).splitlines()
        return lines[1] if len(lines) > 1 else ""

    def hint(self, screen: str | None = None) -> str:
        lines = (screen or self.screen()).rstrip("\n").splitlines()
        return lines[-1].strip() if lines else ""

    def styled(self, tag: str) -> vt.Screen:
        """Re-derive the latest snapshot with colours from the retained raw bytes."""
        assert self.observer
        last = next(r for r in reversed(self.observer.records) if r.get("event") == "snapshot")
        directory = self.observer.directory
        end = 0
        for line in (directory / "terminal.index").read_text().splitlines():
            sequence, _, offset, length = map(int, line.split())
            if sequence <= last["sequence"]:
                end = max(end, offset + length)
        screen = vt.Screen(self.observer.columns, self.observer.rows)
        with (directory / "terminal.raw").open("rb") as raw:
            screen.feed(raw.read(end))
        (self.output / f"styled-{tag}.html").write_text(screen.html(tag))
        return screen

    def selected_row(self, screen: str | None = None) -> str:
        """The row marked as selected in a list (leading '>' inside the frame,
        which may be nested as in Overview's panels)."""
        for line in (screen or self.screen()).splitlines():
            body = re.sub(r"^[|│ ]+", "", line).rstrip("|│ ")
            if body.startswith("> ") or body.startswith(">* "):
                return body
        return ""

    def watch(self, seconds: float, interval: float = 0.1) -> list[str]:
        """Snapshots taken continuously for a bounded time (transient screens)."""
        screens, deadline = [], time.monotonic() + seconds
        while time.monotonic() < deadline:
            screens.append(self.screen())
            time.sleep(interval)
        return screens

    # Processes and terminals ------------------------------------------------
    def tmux(self, *argv: str) -> str:
        result = subprocess.run(["tmux", *argv], env=self.env, cwd=self.repo, capture_output=True,
                                text=True, timeout=10, check=False)
        return result.stdout.rstrip("\n") if result.returncode == 0 else ""

    def pane_pids(self, session: str | None = None) -> list[int]:
        target = ["-t", session] if session else ["-a"]
        listing = self.tmux("list-panes", *target, "-F", "#{pane_pid}")
        return [int(p) for p in listing.split() if p.isdigit()]

    @staticmethod
    def environ(pid: int) -> dict[str, str]:
        data = Path(f"/proc/{pid}/environ").read_bytes()
        pairs = (chunk.partition(b"=") for chunk in data.split(b"\0") if chunk)
        return {k.decode(errors="replace"): v.decode(errors="replace") for k, _, v in pairs}

    @staticmethod
    def children(pid: int) -> list[int]:
        found = []
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                stat = (entry / "stat").read_text()
            except OSError:
                continue
            fields = stat.rsplit(")", 1)[1].split()
            if fields and int(fields[1]) == pid:
                found.append(int(entry.name))
        return found

    @staticmethod
    def command_line(pid: int) -> str:
        try:
            return Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ").decode(errors="replace").strip()
        except OSError:
            return ""

    def pty(self, tag: str, argv: list[str], columns: int, rows: int, env: dict[str, str] | None = None,
            cwd: Path | None = None) -> PtyProcess:
        return PtyProcess(argv, cwd or self.repo, env or self.env, self.output / f"pty-{tag}", columns, rows)

    def wait_file(self, path: Path, timeout: float = 5) -> bool:
        deadline = time.monotonic() + timeout
        while not path.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        return path.exists()

    def history_files(self) -> dict[str, str]:
        home = Path(self.env["HOME"])
        return {p.name: p.read_text(errors="replace") for p in home.glob(".*history*") if p.is_file()}

    def make_repo_dirty(self) -> None:
        """Tracked modification, untracked file, ignore rules and an ignored file."""
        (self.repo / ".gitignore").write_text("*.log\nbuild/\n")
        self.command("git", "add", ".gitignore")
        self.command("git", "-c", "commit.gpgSign=false", "commit", "-qm", "ignore rules")
        (self.repo / "README.txt").write_text("Disposable usability fixture, edited.\n")
        (self.repo / "notes.txt").write_text("untracked draft\n")
        (self.repo / "debug.log").write_text("ignored output\n")
        (self.repo / "build").mkdir()
        (self.repo / "build" / "out.bin").write_bytes(b"\0\1")
        exclude = self.repo / ".git" / "info" / "exclude"
        exclude.write_text(exclude.read_text() + "private-notes.txt\n")
        (self.repo / "private-notes.txt").write_text("excluded locally\n")

    def repo_state(self) -> dict:
        return {"tree": usability.tree(self.repo), "status": self.command("git", "status", "--porcelain"),
                "exclude": (self.repo / ".git" / "info" / "exclude").read_text()}


def select_head(j: Case, name: str, attempts: int = 12) -> str:
    """Move the Work list selection to the named head with k/j."""
    j.keys(b"k" * attempts)
    time.sleep(0.3)
    for _ in range(attempts):
        row = j.selected_row()
        if row.split()[1:2] == [name]:
            return row
        j.keys(b"j")
        time.sleep(0.2)
    raise AssertionError(f"head {name!r} not found in the list; last selected {j.selected_row()!r}")


def make_stopped(j: Case, branch: str) -> None:
    """A head whose agent reported its exit through the public adapter
    contract and whose terminal is gone: shown as 'stopped'."""
    lifecycle = json.loads(j.cli("lifecycle", branch, "--json"))["data"]
    event = json.dumps({"schema_version": 1, "instance_id": lifecycle["instance_id"],
                        "kind": "observed", "status": "exited"})
    result = subprocess.run([str(j.hydra), "adapter", "ingest", branch], cwd=j.repo, env=j.env, input=event,
                            capture_output=True, text=True, timeout=30, check=False)
    if result.returncode:
        raise RuntimeError(f"adapter ingest failed: {result.stderr}")
    j.proof.append({"command": ("hydra", "adapter", "ingest", branch), "input": event, "output": result.stdout})
    j.tmux("kill-session", "-t", branch)


def profile_fixture(j: Case, name: str = "codex", script: Path | None = None) -> Path:
    """Install the deterministic provider as `name` on a private PATH."""
    tools = j.base / "fixture-bin"
    tools.mkdir(exist_ok=True)
    target = script or (j.source / "tests/fixtures/usability/provider.py")
    (tools / name).symlink_to(target)
    j.env["PATH"] = os.pathsep.join([str(tools), str(j.prefix / "bin"), j.env["PATH"]])
    return tools
