"""Comprehension trials (U19): for each concept the trial reaches its entry
point or empty state in the installed control centre and checks that the
visible explanation states the purpose, when it helps, a concrete example and
the next action in plain language, inside the visible area at 80x24 and
140x40, and that command names it mentions exist in ``hydra help``."""

from __future__ import annotations

import re
import time
from dataclasses import dataclass, field
from typing import Callable

from usability.journey import Journey, prose

JARGON = ("sha256", "json", "schema", "envelope", "dag", "argv", "stdin", "instance_", "head_", "project_", "fifo")
COMMAND = re.compile(r"\bhydra ([a-z][a-z-]*)(?: ([a-z][a-z-]*))?")


@dataclass
class Concept:
    name: str
    entry: str
    reach: Callable[[Journey], str]
    facets: dict[str, list[str]]
    session: str
    leave: Callable[[Journey], None] = lambda j: None
    plain: bool = True
    notes: list[str] = field(default_factory=list)


def help_index(j: Journey) -> dict[str, str]:
    """Top-level commands from ``hydra help`` with the text of each block."""
    index: dict[str, list[str]] = {}
    current = None
    for line in j.cli("help").splitlines():
        m = re.match(r"^  ([a-z][a-z-]*)\b", line)
        if m:
            current = m.group(1)
            index.setdefault(current, [])
        if current:
            index[current].append(line)
    return {k: "\n".join(v) for k, v in index.items()}


def check_commands(j: Journey, text: str, index: dict[str, str]) -> None:
    for command, sub in COMMAND.findall(text):
        j.expect(command in index, f"Mentioned command exists in hydra help: hydra {command}")
        if sub and command in index and sub not in ("and", "or", "to", "with", "for", "the", "appear", "here"):
            block = index[command]
            # A command that takes a positional argument (`provenance <head>`)
            # may be shown with a real head name after it.
            positional = re.search(rf"^  {re.escape(command)}\s+<", block, re.M) is not None
            j.expect(positional or re.search(rf"\b{re.escape(sub)}\b", block) is not None,
                     f"Mentioned subcommand exists in hydra help: hydra {command} {sub}")


def settled(j: Journey, screen: str) -> str:
    """The screen once two consecutive snapshots agree (a panel's text can
    land a frame after its title)."""
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        time.sleep(0.3)
        current = j.screen()
        if current == screen:
            return current
        screen = current
    return screen


def evaluate(j: Journey, concept: Concept, index: dict[str, str]) -> dict:
    screen = concept.reach(j)
    if "shell TUI" not in concept.name:
        screen = settled(j, screen)
    j.proof.append({"comprehension": concept.name, "entry": concept.entry, "at_ns": time.monotonic_ns()})
    text = prose(screen)
    lower = text.lower()
    result = {"concept": concept.name, "entry": concept.entry, "facets": {}, "columns": j.columns}
    for facet, phrases in concept.facets.items():
        ok = any(p.lower() in lower for p in phrases)
        result["facets"][facet] = ok
        j.expect(ok, f"{concept.name}: the {concept.entry} explains {facet} "
                     f"(within {j.columns}x{40 if j.columns > 80 else 24}): one of {phrases!r}")
    if concept.plain:
        found = [w for w in JARGON if w in lower]
        result["plain_language"] = not found
        j.expect(not found, f"{concept.name}: the explanation avoids internal terms {found!r}")
    check_commands(j, text, index)
    concept.leave(j)
    return result


# ---- reaching entry points (scripted: these trials measure copy, not discovery) ----

def view(key: bytes, marker: str):
    def reach(j: Journey) -> str:
        j.keys(key)
        return j.see(marker)
    return reach


def back(j: Journey) -> None:
    j.keys(b"\x1b")
    time.sleep(0.2)


def reach_work_empty(j: Journey) -> str:
    j.keys(b"1")
    return j.see("Heads in this project")


def reach_shell_tui(j: Journey) -> str:
    """The maintained shell TUI's empty state, rendered in the journey's private tmux."""
    rows = 40 if j.columns > 80 else 24
    j.command("tmux", "new-session", "-d", "-s", "shell-tui", "-x", str(j.columns), "-y", str(rows),
              f"{j.hydra} tui --basic")
    deadline = time.monotonic() + 8
    screen = ""
    while time.monotonic() < deadline:
        time.sleep(0.4)
        screen = j.command("tmux", "capture-pane", "-p", "-t", "shell-tui")
        if "Press" in screen:
            break
    (j.output / "shell-tui.txt").write_text(screen)
    j.command("tmux", "kill-session", "-t", "shell-tui")
    return screen


def reach_details(j: Journey) -> str:
    j.keys(b"1")
    j.see("Heads in this project")
    j.keys(b"\r")
    return j.see("Details: sample")


def reach_coordination(j: Journey) -> str:
    reach_details(j)
    j.keys(b"c")
    return j.see("COORDINATION  sample")


def reach_run_panel(j: Journey) -> str:
    j.keys(b"\x1b\x1b3")
    return j.see("RUN / no run yet")


def reach_plan_pane(j: Journey) -> str:
    j.keys(b"\x1b\x1b1")
    j.see("Heads in this project")
    j.keys(b"8")
    j.see("[Workspace]")
    j.keys(b"B")
    return j.see("No proposal loaded")


def reach_attached(j: Journey) -> str:
    j.keys(b"\x1b\x1b1")
    j.see("Heads in this project")
    j.keys(b"a")
    screen = j.see("Typing goes to the agent")
    j.attached = True
    return screen


def leave_attached(j: Journey) -> None:
    j.keys(b"\x02\t")
    j.see("Keys go to Hydra")
    j.attached = False
    j.keys(b"\x1b\x1b")


def reach_removal_prompt(j: Journey) -> str:
    j.keys(b"1")
    j.see("Heads in this project")
    j.keys(b"x")
    return j.see("y/N:")


def leave_removal_prompt(j: Journey) -> None:
    j.keys(b"\x1b")
    j.see("Removal cancelled")


CONCEPTS = [
    Concept("head", "Work empty state", reach_work_empty, {
        "purpose": ["A head is one piece of work: its own branch and worktree, a terminal, and an agent working inside it"],
        "when it helps": ["follow several heads here at once"],
        "example": ["for example"],
        "next action": ["Press n to start one"],
    }, "empty", back),
    Concept("head (shell TUI)", "basic TUI empty list", reach_shell_tui, {
        "purpose": ["one piece of work"],
        "when it helps": ["several"],
        "example": ["for example"],
        "next action": ["Press n", "Press 'n'"],
    }, "empty"),
    Concept("workflow", "Workflows empty state", view(b"6", "No workflow runs recorded"), {
        "purpose": ["A workflow is a saved sequence of steps Hydra runs and tracks for a task"],
        "when it helps": ["A single agent conversation does not need one"],
        "example": ["for example implement a change, run the tests, then wait for your review"],
        "next action": ["hydra workflow run"],
        "plan path": ["approve"],
    }, "empty", back),
    Concept("Fleet / host", "Hosts empty state", view(b"9", "REMOTE HOSTS"), {
        "purpose": ["Hydra can run agents on other machines over SSH"],
        "when it helps": ["when a task needs", "when you want"],
        "example": ["for example"],
        "next action": ["Press Enter or A to add a host"],
        "consequences before acting": ["Nothing changes on the remote until you approve it"],
    }, "empty", back),
    Concept("statistics", "Statistics empty state", view(b"7", "No workflow statistics yet"), {
        "purpose": ["Statistics cover workflows that Hydra ran and recorded"],
        "when it helps": ["A standalone agent conversation is not a recorded run"],
        "example": ["for example"],
        "next action": ["Workflows explains what a workflow is"],
    }, "empty", back),
    Concept("gate / check", "Attention empty state", view(b"4", "Nothing needs your attention"), {
        "purpose": ["what needs a decision"],
        "when it helps": ["approval request", "failed check"],
        "example": ["for example"],
        "next action": ["Enter"],
    }, "head", back),
    Concept("plan vs. approved workflow", "plan pane without a proposal", reach_plan_pane, {
        "purpose": ["agent's suggested plan", "A proposal is"],
        "when it helps": ["runs nothing until"],
        "example": ["for example"],
        "next action": ["P reviews the agent's proposal"],
        "approval path": ["validates", "approve"],
    }, "head"),
    Concept("workflow run", "Overview run panel", reach_run_panel, {
        "purpose": ["A run appears here when you execute an approved plan"],
        "when it helps": ["what needs you next"],
        "example": ["each step, the head it ran on"],
        "next action": ["hydra workflow run", "E in the plan review"],
    }, "head"),
    Concept("coordination view", "Coordination of one head", reach_coordination, {
        "purpose": ["Coordination between heads matters", "claims (files a head intends to change)"],
        "when it helps": ["when several heads", "A single agent session"],
        "example": ["for example"],
        "next action": [": claims", "more actions"],
    }, "head", lambda j: j.keys(b"\x1b\x1b")),
    Concept("live session vs. completed work", "Details of a running head", reach_details, {
        "purpose": ["(the live terminal)"],
        "when it helps": ["declared when done"],
        "example": ["done or blocked"],
        "next action": ["talk to the agent"],
    }, "head", back, plain=False),
    Concept("interruption", "attached agent input", reach_attached, {
        "purpose": ["Typing goes to the agent"],
        "consequences before acting": ["keeps it running", "keeps running"],
        "leave": ["leave"],
        "close": ["close"],
    }, "head", leave_attached, plain=False),
    Concept("removal", "removal confirmation", reach_removal_prompt, {
        "consequences before acting": ["closes the terminal and deletes the worktree"],
        "what is kept": ["branch is kept"],
        "protection": ["uncommitted changes are refused"],
        "next action": ["y/N"],
    }, "head", leave_removal_prompt, plain=False),
]


def run_session(j: Journey, session: str) -> list[dict]:
    if session == "head":
        j.seed()
    j.open()
    index = help_index(j)
    results = []
    for concept in CONCEPTS:
        if concept.session != session:
            continue
        try:
            results.append(evaluate(j, concept, index))
        except TimeoutError as error:
            j.expect(False, f"{concept.name}: entry point not reached ({error})")
            results.append({"concept": concept.name, "entry": concept.entry, "blocked": str(error)})
    return results


SESSIONS = ("empty", "head")
