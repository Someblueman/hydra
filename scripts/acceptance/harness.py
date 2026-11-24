"""Case harness: an installed-build scenario that records matrix checks.

A Case is a usability Journey (fresh HOME, HYDRA_HOME, tmux socket namespace
and disposable repository on the prefix-installed product) whose checks are
tagged with the acceptance bullet they establish, each with retained evidence.
"""

from __future__ import annotations

import json
import subprocess
import time
from collections.abc import Callable
from pathlib import Path

from bench_i1_pty import Observer
from usability import journey as usability

CASES: list[dict] = []


def case(name: str, bullets: tuple[str, ...], columns: tuple[int, ...] = (80,), seconds: int = 120):
    """Register a case: name, the bullets it reports on, the widths it runs at."""
    def register(function: Callable) -> Callable:
        CASES.append({"name": name, "function": function, "bullets": bullets,
                      "columns": columns, "budget": seconds})
        return function
    return register


class Case(usability.Journey):
    def __init__(self, source: Path, prefix: Path, output: Path, name: str, columns: int) -> None:
        super().__init__(source, prefix, output, name, columns)
        # The native observer strips colour from its ASCII snapshots but the
        # raw bytes keep it when the TUI is allowed to colour.
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
        """Open the public entry (`hydra` in the repository) on a fresh observer."""
        self.observer = Observer(
            self.source / "build/test-tui-pty", self.prefix / "libexec/hydra/hydra-tui", self.hydra,
            self.output / "terminal", self.repo, {**self.env, **(argv_env or {})}, self.columns,
            40 if self.columns > 80 else 24)
        self.observer.wait("raw-ready", timeout=15)
        self.see_all(*usability.TOP_BAR)

    # Screens ----------------------------------------------------------------
    def selected_row(self, screen: str | None = None) -> str:
        """The Work list's selected row, as "> name": the selection bar ('>'
        without Unicode) and the row's status glyph are dropped."""
        for line in usability.list_column(screen or self.screen()):
            if usability.selected(line):
                words = line[2:].split()
                if words and not words[0][:1].isalnum():
                    words = words[1:]  # the status glyph
                return "> " + " ".join(words)
        return ""

    # Processes and terminals ------------------------------------------------
    def tmux(self, *argv: str) -> str:
        result = subprocess.run(["tmux", *argv], env=self.env, cwd=self.repo, capture_output=True,
                                text=True, timeout=10, check=False)
        return result.stdout.rstrip("\n") if result.returncode == 0 else ""
