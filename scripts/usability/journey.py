"""One installed-product session: private HOME, Hydra state, tmux server and
Git repository; the native PTY observer drives the control centre and every
action, screen and independent check is retained as proof."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from bench_i1_pty import Observer

# Stable product text of the four-screen control centre. The top bar names
# the screens by their keys; the brand before them differs by character set.
TOP_BAR = ("1 Work", "4 Stats")
# The Conversation rule while keys go to the agent ("typing to codex ·
# Ctrl-] leave"), and the status line once Ctrl-] gave them back.
TYPING = "typing to "
LEFT_INPUT = "Keys go to Hydra"
RIGHT = b"\x1b[C"


def run(argv: list[str], cwd: Path, env: dict[str, str]) -> str:
    result = subprocess.run(
        argv, cwd=cwd, env=env, text=True, capture_output=True, timeout=90, check=False
    )
    if result.returncode:
        raise RuntimeError(
            f"{argv!r}: exit {result.returncode}\n{result.stdout}\n{result.stderr}"
        )
    return result.stdout.strip()


def tree(repo: Path) -> dict[str, str]:
    return {
        str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in repo.rglob("*")
        if p.is_file() and ".git" not in p.relative_to(repo).parts
    }


class Journey:
    def __init__(
        self, source: Path, prefix: Path, output: Path, name: str, columns: int
    ):
        self.output = output / f"{name}-{columns}"
        self.output.mkdir()
        self.base = Path(tempfile.mkdtemp(prefix="hydra-ux.", dir="/tmp"))
        self.repo = self.base / "repo"
        self.repo.mkdir()
        self.hydra = prefix / "bin/hydra"
        self.env = {
            k: v
            for k, v in os.environ.items()
            if not k.startswith(("HYDRA_", "GIT_", "TMUX"))
        }
        self.env.update(
            HOME=str(self.base / "home"),
            HYDRA_HOME=str(self.base / "state"),
            TMUX_TMPDIR=str(self.base / "tmux"),
            TMPDIR=str(self.base),
            HYDRA_NONINTERACTIVE="1",
            HYDRA_TEST_PUBLIC_ENTRY="1",
            LC_ALL="C",
            LANG="C",
            TERM="xterm-256color",
        )
        # Private like a real user's home and Hydra state: under a
        # group-writable umask (Ubuntu's 002) Hydra refuses shared state.
        for key in ("HOME", "HYDRA_HOME", "TMUX_TMPDIR"):
            Path(self.env[key]).mkdir(mode=0o700)
        self.proof: list[dict] = []
        self.failures: list[str] = []
        self.observer: Observer | None = None
        self.attached = False
        self.source, self.prefix, self.columns = source, prefix, columns

    def initialize(self) -> None:
        for args in (
            ["init", "-q"],
            ["config", "user.name", "Usability fixture"],
            ["config", "user.email", "fixture@example.invalid"],
        ):
            self.command("git", *args)
        (self.repo / "README.txt").write_text("Disposable usability fixture.\n")
        self.command("git", "add", "README.txt")
        self.command("git", "-c", "commit.gpgSign=false", "commit", "-qm", "fixture")

    def command(self, *argv: str) -> str:
        value = run(list(argv), self.repo, self.env)
        self.proof.append(
            {"command": argv, "output": value, "at_ns": time.monotonic_ns()}
        )
        return value

    def cli(self, *argv: str) -> str:
        return self.command(str(self.hydra), *argv)

    def open(self) -> None:
        self.observer = Observer(
            self.source / "build/test-tui-pty",
            self.prefix / "libexec/hydra/hydra-tui",
            self.hydra,
            self.output / "terminal",
            self.repo,
            self.env,
            self.columns,
            40 if self.columns > 80 else 24,
        )
        self.observer.wait("raw-ready", timeout=15)
        self.see_all(*TOP_BAR)

    def see(self, text: str) -> str:
        assert self.observer
        screen, receipt = self.observer.visible(text)
        self.proof.append(
            {"expect": text, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()}
        )
        return screen

    def see_all(self, *texts: str, timeout: float = 15) -> str:
        """Wait for one applied screen that shows every text at once."""
        assert self.observer
        screen, receipt = self.observer._visible(
            lambda screen, record: record["parser_complete"]
            and all(text in screen for text in texts),
            time.monotonic_ns() + int(timeout * 1e9),
            " and ".join(map(repr, texts)),
        )
        self.proof.append(
            {"expect_all": texts, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()}
        )
        return screen

    def see_prose(self, text: str, timeout: float = 15) -> str:
        """Wait for text that may be word-wrapped across rows of the screen."""
        assert self.observer
        screen, receipt = self.observer._visible(
            lambda screen, record: record["parser_complete"] and text in prose(screen),
            time.monotonic_ns() + int(timeout * 1e9),
            f"{text!r} (rows rejoined)",
        )
        self.proof.append(
            {"expect_prose": text, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()}
        )
        return screen

    def see_any(self, *texts: str, timeout: float = 15) -> str:
        """Wait for a screen showing at least one of several outcomes."""
        assert self.observer
        screen, receipt = self.observer._visible(
            lambda screen, record: record["parser_complete"] and any(t in screen for t in texts),
            time.monotonic_ns() + int(timeout * 1e9),
            " or ".join(map(repr, texts)),
        )
        self.proof.append({"expect_any": texts, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()})
        return screen

    def screen(self) -> str:
        assert self.observer
        screen, receipt = self.observer.snapshot()
        self.proof.append({"snapshot": receipt["id"], "at_ns": time.monotonic_ns()})
        return screen

    def check(self, condition: bool, message: str) -> None:
        """An independent outcome check, recorded whether it passes or not."""
        self.proof.append(
            {"check": message, "passed": bool(condition), "at_ns": time.monotonic_ns()}
        )
        assert condition, message

    def expect(self, condition: bool, message: str) -> bool:
        """A desired-behaviour check that does not block the rest of the
        journey: it is recorded, and any failure still fails the journey."""
        self.proof.append(
            {"expect_check": message, "passed": bool(condition), "at_ns": time.monotonic_ns()}
        )
        if not condition:
            self.failures.append(message)
        return bool(condition)

    def expect_see(self, text: str, message: str, timeout: float = 10) -> bool:
        assert self.observer
        try:
            _, receipt = self.observer.visible(text, timeout)
            self.proof.append({"expect": text, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()})
            return self.expect(True, message)
        except TimeoutError:
            return self.expect(False, f"{message} (not shown: {text!r})")

    def until(self, predicate, timeout: float, message: str):
        """Poll independent evidence (files, Git, CLI) within a deadline."""
        deadline = time.monotonic() + timeout
        while True:
            value = predicate()
            if value or time.monotonic() >= deadline:
                break
            time.sleep(0.1)
        self.check(bool(value), message)
        return value

    def keys(self, value: bytes) -> None:
        assert self.observer
        self.observer.input(f"action-{len(self.proof)}", value)
        self.proof.append({"keys_hex": value.hex(), "at_ns": time.monotonic_ns()})

    def agent_choices(self) -> list[str]:
        """The new-task form's agent choices in order: the project default,
        each agent `hydra agent list` reports available, then a plain shell."""
        names = ["project default"]
        for row in self.cli("agent", "list").splitlines():
            fields = row.split()
            if len(fields) >= 2 and fields[1] == "yes" and fields[0] != "none" and fields[0] not in names:
                names.append(fields[0])
        return [*names, "none"]

    def new_task(self, name: str, agent: str, objective: str = "", direct: bool = False) -> str:
        """Fill the new-task form from its visible fields: the task, the agent
        (moved to with Right, as the choice row lists them), the objective
        and how the agent starts; Enter submits."""
        self.keys(b"n")
        form = self.see_all("New task", "Agent", "Objective")
        choices = self.agent_choices()
        self.check(agent in choices and agent in form, f"The new-task form offers the agent {agent!r}")
        self.keys(name.encode())
        self.keys(b"\t" + RIGHT * choices.index(agent) + b"\t")
        if objective:
            self.keys(objective.encode())
        if direct:
            self.keys(b"\t" + RIGHT)
        self.keys(b"\r")
        return form

    def attach_agent(self, key: bytes = b"a") -> str:
        """Talk to the selected head: its Conversation takes the keys."""
        self.keys(key)
        screen = self.see(TYPING)
        self.attached = True
        return screen

    def leave_agent(self) -> str:
        self.keys(b"\x1d")
        self.attached = False
        return self.see(LEFT_INPUT)

    def escape(self, times: int = 1) -> None:
        """Esc on its own each time: an Esc followed at once by another key
        reads as that key with Alt held."""
        for _ in range(times):
            time.sleep(0.2)
            self.keys(b"\x1b")
            time.sleep(0.3)

    def seed(self) -> None:
        self.cli("init", "--no-agent")
        self.cli("spawn", "sample", "--no-agent")

    def settle(self, timeout: float = 30) -> list[str]:
        """Wait for plan launch owners to record their exit: an owner keeps
        retiring heads and writing notices after its run reaches a final state."""
        launches = Path(self.env["HYDRA_HOME"]) / "state/v2/projects"

        def state(directory: Path) -> str:
            try:
                return (directory / "state").read_text().strip()
            except OSError:
                return "absent"

        deadline = time.monotonic() + timeout
        while True:
            pending = [f"{d.name[:12]}={state(d)}" for d in launches.glob("*/workflows/launches/*")
                       if d.is_dir() and state(d) not in ("finished", "failed")]
            if not pending or time.monotonic() >= deadline:
                for d in launches.glob("*/workflows/launches/*"):
                    if (d / "owner.log").exists():  # retained as evidence
                        (self.output / f"launch-owner-{d.name[:12]}.log").write_text(
                            (d / "owner.log").read_text())
                return pending
            time.sleep(0.2)

    def close(self) -> None:
        cleanup = {}
        pending = self.settle()
        try:
            if self.observer:
                cleanup = self.observer.close(attached=self.attached)
                if not cleanup.get("reaped") or cleanup.get("observer_exit"):
                    raise RuntimeError(f"UI cleanup incomplete: {cleanup}")
            if pending:
                raise RuntimeError(f"plan launch owners still running: {pending}")
        finally:
            # This root owns the entire socket namespace; no global tmux cleanup.
            for socket in Path(self.env["TMUX_TMPDIR"]).glob("tmux-*/*"):
                subprocess.run(
                    ["tmux", "-S", str(socket), "kill-server"],
                    capture_output=True,
                    timeout=10,
                    check=False,
                )
            (self.output / "proof.json").write_text(
                json.dumps(self.proof, indent=2) + "\n"
            )
            (self.output / "cleanup.json").write_text(
                json.dumps(cleanup, indent=2) + "\n"
            )
            deadline = time.monotonic() + 10
            while True:
                try:
                    shutil.rmtree(self.base)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.2)



def prose(screen: str) -> str:
    """Screen text with frame borders removed and word-wrapped lines rejoined."""
    lines = [re.sub(r"^[|│ ]+|[|│ ]+$", "", line) for line in screen.splitlines()]
    return " ".join(" ".join(lines).split())


def sessions_of(j: Journey) -> list[str]:
    """Terminal sessions on this journey's private tmux server (none is not an error)."""
    listing = subprocess.run(["tmux", "list-sessions", "-F", "#{session_name}"],
                             env=j.env, capture_output=True, text=True, timeout=10, check=False)
    return listing.stdout.split() if listing.returncode == 0 else []


def footer(screen: str) -> str:
    rows = screen.rstrip("\n").splitlines()
    return rows[-1] if rows else ""


def see_footer(j: Journey, text: str, absent: str = "", timeout: float = 10) -> str:
    """Wait for a settled screen whose footer offers ``text`` (and not ``absent``)."""
    assert j.observer
    screen, receipt = j.observer._visible(
        lambda screen, record: record["parser_complete"] and text in footer(screen)
        and not (absent and absent in footer(screen)),
        time.monotonic_ns() + int(timeout * 1e9), f"footer {text!r}")
    j.proof.append({"expect_footer": text, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()})
    return screen


def page(j: Journey, key: bytes, *texts: str, presses: int = 5) -> str:
    """Press a page key ([ or ]) until a page showing every text is up."""
    for _ in range(presses):
        try:
            return j.see_all(*texts, timeout=1.5)
        except TimeoutError:
            j.keys(key)
    return j.see_all(*texts)


def selected(row: str) -> bool:
    """A list row with the selection bar in its first column ('>' without
    Unicode; a collapsed run's '>' sits further in)."""
    return row[1:2] in (">", "\u258c")


def select(j: Journey, text: str, presses: int = 10) -> str:
    """Move the focused list's selection to the row showing ``text``."""
    for key in (b"k", b"j"):
        for _ in range(presses):
            time.sleep(0.3)
            screen = j.screen()
            if any(text in row and selected(row) for row in screen.splitlines()):
                return screen
            j.keys(key)
    raise AssertionError(f"no list row showing {text!r} could be selected")


def list_column(screen: str) -> list[str]:
    """The Work list's rows: the columns left of the pane beside it, as its
    titled rule ("Heads ---- 3  ...") ends."""
    rows = screen.splitlines()
    for row in rows:
        match = re.match(r"\s*Heads[^-\u2500]*[-\u2500]+ \S+ ", row)
        if match:
            return [r[:match.end()] for r in rows]
    return rows


def column_prose(screen: str, anchor: str) -> str:
    """Text of the pane that shows ``anchor`` on its left edge, rows rejoined:
    every row from the anchor's column rightwards (panes have no borders)."""
    for row in screen.splitlines():
        x = row.find(anchor)
        if x >= 0:
            return " ".join(" ".join(r[x:] for r in screen.splitlines()).split())
    return ""


def see_column(j: Journey, anchor: str, *texts: str, timeout: float = 15) -> str:
    """Wait for the pane whose left edge shows ``anchor`` to say every text."""
    assert j.observer
    screen, receipt = j.observer._visible(
        lambda screen, record: record["parser_complete"]
        and all(text in column_prose(screen, anchor) for text in texts),
        time.monotonic_ns() + int(timeout * 1e9), f"{texts!r} beside {anchor!r}")
    j.proof.append({"expect_column": [anchor, *texts], "snapshot": receipt["id"], "at_ns": time.monotonic_ns()})
    return screen
