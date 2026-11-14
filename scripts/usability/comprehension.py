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

from usability.journey import TYPING, Journey, page, prose

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
    screen = settled(j, concept.reach(j))
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
    j.keys(b"1")
    time.sleep(0.3)


def reach_work_empty(j: Journey) -> str:
    j.keys(b"1")
    return j.see("No agent work in this project yet")


def reach_runs_empty(j: Journey) -> str:
    """Stats' Runs page: what a recorded run is, before there is one."""
    j.keys(b"4")
    j.see("Sessions  Runs")
    return page(j, b"]", "No workflow runs recorded yet")


def reach_sessions_empty(j: Journey) -> str:
    """Stats' Sessions page before any head was started."""
    j.keys(b"4")
    j.see("Sessions  Runs")
    return page(j, b"[", "No sessions recorded yet")


def reach_details(j: Journey) -> str:
    """The selected head's Details page beside the list (a page of the shared
    pane below 120 columns), scrolled to its end, where the coordination
    explanation is."""
    j.keys(b"1")
    j.see("sample")
    return page(j, b"]", "Session", "CHANGES")


def reach_coordination(j: Journey) -> str:
    reach_details(j)
    return j.see_prose("Claims are files a head means to change")


def reach_plan_choice(j: Journey) -> str:
    """The new-task form's Start field: planning first or doing it now."""
    j.keys(b"1")
    j.see("sample")
    j.keys(b"n")
    return j.see_all("New task", "Plan first")


def leave_form(j: Journey) -> None:
    j.escape()
    j.see("sample")


def reach_attached(j: Journey) -> str:
    j.keys(b"1")
    j.see("sample")
    screen = j.attach_agent()
    return j.see_all(TYPING, "keeps it running") or screen


def leave_attached(j: Journey) -> None:
    j.leave_agent()
    j.keys(b"\x1cx")
    j.escape()


def reach_removal_prompt(j: Journey) -> str:
    j.keys(b"1")
    j.see("sample")
    j.keys(b"x")
    return j.see_prose("Remove sample?")


def leave_removal_prompt(j: Journey) -> None:
    j.escape()
    j.see("Removal cancelled")


CONCEPTS = [
    Concept("head", "Work empty state", reach_work_empty, {
        "purpose": ["A head is one task: its own branch, worktree, terminal and agent"],
        "what Hydra does": ["Hydra creates them; you talk to each agent here and approve what it does"],
        "next action": ["n start a task"],
    }, "empty", back),
    Concept("workflow", "Stats Runs page without runs", reach_runs_empty, {
        "purpose": ["A run is recorded each time a plan is executed"],
        "example": ["its steps, timing, outcomes, retries and tokens"],
    }, "empty", back),
    Concept("Fleet / host", "Hosts screen", view(b"3", "This machine"), {
        "purpose": ["Hydra can run agents on other machines over SSH"],
        "when it helps": ["when a task needs more than this machine offers"],
        "example": ["for example a build box"],
        "next action": ["Press Enter or A to add a host"],
        "consequences before acting": ["Nothing changes on the remote until you approve it"],
    }, "empty", back),
    Concept("statistics", "Stats empty state", reach_sessions_empty, {
        "purpose": ["A session is recorded each time a head is started or resumed"],
        "when it helps": ["records when the head's terminal is active or quiet"],
        "next action": ["Start work from the Work tab"],
    }, "empty", back),
    Concept("gate / check", "Inbox empty state", view(b"2", "Nothing needs you"), {
        "purpose": ["appear here, with what you can do about each"],
        "when it helps": ["Approvals, failed checks"],
        "what appears": ["finished results, stopped terminals and repairs"],
    }, "head", back),
    Concept("plan vs. approved work", "new-task form, Start field", reach_plan_choice, {
        "purpose": ["Plan first"],
        "approval path": ["nothing is implemented until you approve a plan"],
    }, "head", leave_form),
    Concept("coordination", "Details of one head (Coordinate row)", reach_coordination, {
        "purpose": ["Claims are files a head means to change"],
        "when it helps": ["Hydra flags overlap"],
        "example": ["0 claims"],
    }, "head", back),
    Concept("live session vs. completed work", "Details of a running head", reach_details, {
        "purpose": ["(the live terminal)"],
        "when it helps": ["declared when done"],
        "example": ["done or blocked"],
    }, "head", back, plain=False),
    Concept("interruption", "attached agent input", reach_attached, {
        "purpose": ["keys go to the agent"],
        "consequences before acting": ["keeps it running"],
        "leave": ["leave"],
        "close": ["close"],
    }, "head", leave_attached, plain=False),
    Concept("removal", "removal confirmation", reach_removal_prompt, {
        "consequences before acting": ["Closes its terminal and deletes its worktree"],
        "what is kept": ["is kept"],
        "protection": ["uncommitted or untracked changes is refused"],
        "next action": ["y remove"],
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
