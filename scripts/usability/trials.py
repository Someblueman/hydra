"""Goal-driven screen-only trials: the evaluator in ``policy`` gets a goal in
plain words plus each screen, chooses from on-screen controls, and a separate
checker verifies the outcome against Git, tmux and Hydra state. The trial
records every screen, the controls it read, its choice and why, dead ends and
help reads, and derives a discoverability score per goal."""

from __future__ import annotations

import hashlib
import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from usability.journey import Journey, prose, sessions_of
from usability.journeys_plan import finished_run, planning_converse, planning_prepare, runs
from usability.policy import Reading, key_bytes, learned_tab_method, rank, read_screen

Predicate = Callable[[str], bool]


@dataclass
class Phase:
    goal: str
    want: tuple[str, ...]
    done: Predicate
    avoid: tuple[str, ...] = ("quit",)
    answers: dict[str, str] = field(default_factory=dict)
    confirm: bool = False


@dataclass
class Goal:
    name: str
    phases: list[Phase]
    setup: Callable[[Journey], None]
    verify: Callable[[Journey], None]
    observe: Callable[[Journey, str], None] | None = None
    max_steps: int = 14


class Trial:
    """One bounded attempt at a goal; every decision is written to proof."""

    def __init__(self, j: Journey, goal: Goal):
        self.j, self.goal = j, goal
        self.steps = 0
        self.dead_ends = 0
        self.help_opened = 0
        self.fits = 0
        self.hints_read = 0
        self.help_open = False
        self.tab_method: str | None = None
        self.exhausted: dict[str, set[str]] = {}
        self.history: list[tuple[str, str]] = []
        self.unresponsive: list[str] = []
        self.before = ""

    def changed(self, timeout: float = 8) -> bool:
        """Wait for the screen's content to react to the last action (the
        title and status rows may tick on their own); a screen that does not
        change within the deadline is reported as such in the proof."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if body(self.j.screen()) != body(self.before):
                return True
            time.sleep(0.1)
        self.j.proof.append({"trial_note": "screen unchanged after the action", "at_ns": time.monotonic_ns()})
        return False

    def press(self, keys: list[str], label: str, source: str, fits: bool) -> None:
        if self.help_open and keys != ["Esc"]:
            self.j.keys(key_bytes("Esc"))  # help only explains; close it, then act on what it said
            self.help_open = False
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and "KEYBOARD HELP" in self.j.screen():
                time.sleep(0.1)
            time.sleep(0.3)
            self.before = self.j.screen()  # judge the key against the screen it acts on
        for key in keys:
            value = key_bytes(key)
            if value:
                self.j.keys(value)
            time.sleep(0.15)
        self.steps += 1
        self.fits += int(fits)
        self.j.proof.append({"trial_choice": keys, "label": label, "source": source,
                             "fits_goal": fits, "at_ns": time.monotonic_ns()})
        if not self.changed():
            self.dead_ends += 1
            self.unresponsive.append(f"{' '.join(keys)} {label} ({source})")
            if fits and source in ("panel", "sentence"):
                # A hint shown next to the content promises an action here. Text
                # captures miss colour-only feedback (a focused pane's border),
                # so the control gets a second press before it is judged.
                for key in keys:
                    self.j.keys(key_bytes(key) or b"")
                    time.sleep(0.15)
                self.steps += 1
                self.j.proof.append({"trial_choice": keys, "label": label, "source": source,
                                     "fits_goal": fits, "retry": True, "at_ns": time.monotonic_ns()})
                if not self.changed(timeout=4):
                    self.j.expect(False, f"The visible control '{label}' ({' '.join(keys)}) responds when pressed")

    def answer(self, reading: Reading, phase: Phase) -> bool:
        """An open prompt: answer it from the goal's own words, confirm when the
        goal is to go ahead, otherwise cancel (a dead end)."""
        prompt = reading.prompt
        if reading.prompt_kind == "text":
            for pattern, value in phase.answers.items():
                if pattern.lower() in prompt.lower():
                    self.j.keys(value.encode() + b"\r")
                    self.steps += 1
                    self.fits += 1
                    self.j.proof.append({"trial_answer": pattern, "value": value, "at_ns": time.monotonic_ns()})
                    self.changed()
                    return True
        if reading.prompt_kind == "yes-no" and phase.confirm:
            self.press(["y", "Enter"], "y/N confirm", "prompt", True)
            return True
        if reading.prompt_kind in ("yes-no", "text"):
            # A question the goal gives no answer to: cancel it, a dead end.
            self.dead_ends += 1
            self.press(["Esc"], f"cancel the prompt: {prompt[:60]}", "prompt", False)
            return True
        return False

    def step(self, screen: str, phase: Phase) -> bool:
        self.before = screen
        reading = read_screen(screen, self.help_open)
        self.hints_read += len(reading.affordances)
        self.tab_method = learned_tab_method(reading, screen) or self.tab_method
        digest = hashlib.sha256(body(screen).encode()).hexdigest()[:16]
        if reading.prompt and self.answer(reading, phase):
            return True
        choices = rank(reading, phase.want, phase.avoid, self.tab_method)
        taken = self.exhausted.setdefault(digest, set())
        choices = [c for c in choices if c.label not in taken]
        self.j.proof.append({"trial_screen": digest, "goal": phase.goal, "help_open": self.help_open,
                             "controls_read": len(reading.affordances),
                             "candidates": [[c.keys, c.label, c.score] for c in choices[:6]],
                             "at_ns": time.monotonic_ns()})
        if not choices and not self.help_open and self.changed(timeout=3):
            return True  # the screen was still catching up with the last action
        if choices:
            choice = choices[0]
            taken.add(choice.label)
            if self.history and self.history[-1] == (digest, choice.label):
                self.dead_ends += 1
            self.history.append((digest, choice.label))
            self.press(choice.keys, choice.label, choice.source, True)
            return True
        if self.help_open:
            self.press(["Esc"], "close help", "fallback", False)
            self.help_open = False
            self.dead_ends += 1
            return True
        help_key = next((a for a in reading.affordances if "?" in a.keys and "help" in a.label.lower()), None)
        if help_key and self.help_opened < 2:
            self.help_opened += 1
            self.press(["?"], help_key.label, help_key.source, False)
            self.help_open = True
            return True
        back = next((a for a in reading.affordances if "Esc" in a.keys), None)
        if back and not any(h[0] == digest for h in self.history[:-1]):
            self.dead_ends += 1
            self.history.append((digest, "Esc"))
            self.press(["Esc"], back.label, back.source, False)
            return True
        return False

    def run(self) -> dict:
        j, goal = self.j, self.goal
        outcome: dict = {"goal": goal.name, "phases": []}
        for phase in goal.phases:
            j.proof.append({"trial_goal": phase.goal, "at_ns": time.monotonic_ns()})
            reached = False
            started = self.steps
            while self.steps < goal.max_steps:
                screen = settled_screen(j)
                if goal.observe:
                    goal.observe(j, screen)
                if phase.done(screen):
                    reached = True
                    break
                if not self.step(screen, phase):
                    break
            if not reached and self.steps < goal.max_steps:
                screen = settled_screen(j)
                reached = phase.done(screen)
            outcome["phases"].append({"goal": phase.goal, "reached": reached, "steps": self.steps - started})
            j.expect(reached, f"Goal reached from on-screen controls: {phase.goal}")
            if not reached:
                break
        outcome["score"] = {
            "steps": self.steps, "dead_ends": self.dead_ends, "help_opened": self.help_opened,
            "controls_read": self.hints_read, "fit": round(self.fits / self.steps, 2) if self.steps else 0.0,
            "completed": all(p["reached"] for p in outcome["phases"]) and len(outcome["phases"]) == len(goal.phases),
            "bound": goal.max_steps,
            "unresponsive": self.unresponsive,
        }
        j.proof.append({"discoverability": outcome["score"], "at_ns": time.monotonic_ns()})
        return outcome


def body(screen: str) -> str:
    """The rows a key can change: content and key hints, without the title
    row and the status row, which tick on their own."""
    rows = screen.splitlines()
    return "\n".join(rows[1:-2] + rows[-1:]) if len(rows) > 3 else screen


def settled_screen(j: Journey, quiet: float = 0.35) -> str:
    """The screen once two consecutive snapshots agree (the UI finished redrawing)."""
    previous = j.screen()
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        time.sleep(quiet)
        current = j.screen()
        if current == previous:
            return current
        previous = current
    return previous


# ---- goals ------------------------------------------------------------------

def typed_marker(j: Journey, worktree: Path, name: str) -> bool:
    j.keys(f"printf delivered > {name}\r".encode())
    deadline = time.monotonic() + 8
    while not (worktree / name).exists() and time.monotonic() < deadline:
        time.sleep(0.1)
    return (worktree / name).exists() and (worktree / name).read_text() == "delivered"


def start_task_setup(j: Journey) -> None:
    j.open()


def start_task_verify(j: Journey) -> None:
    j.attached = "Typing goes to the agent" in j.screen()
    listing = j.cli("list", "--json")
    j.check('"first-task"' in listing, "The head first-task exists after the trial")
    worktree = Path(j.cli("path", "first-task"))
    j.check(worktree.is_dir(), "The head's worktree exists")
    if j.attached:
        j.expect(typed_marker(j, worktree, "trial-input"), "Typed input reaches the new head's terminal")


def stopped_setup(j: Journey, dirty: bool = True) -> None:
    j.seed()
    worktree = Path(j.cli("path", "sample"))
    if dirty:
        (worktree / "draft.txt").write_text("uncommitted work\n")
    for session in sessions_of(j):
        j.command("tmux", "kill-session", "-t", session)
    j.open()


def stopped_explained(screen: str) -> bool:
    text = prose(screen).lower()
    return "sample" in text and "terminal" in text and "kept" in text and ("stopped" in text or "no longer running" in text or "gone" in text)


def why_stopped_verify(j: Journey) -> None:
    worktree = Path(j.cli("path", "sample"))
    j.check((worktree / "draft.txt").read_text() == "uncommitted work\n", "Uncommitted work is untouched")
    text = prose(j.screen()).lower()
    j.expect(not any(w in text for w in ("lost", "crashed", "corrupt")),
             "The explanation does not infer failure or lost work")


def remove_verify(j: Journey) -> None:
    j.until(lambda: not Path(j.base, ".hydra-worktrees").exists() or not any(
        p.is_dir() for p in Path(j.base, ".hydra-worktrees").rglob("*/head_*")), 15,
        "The worktree was removed")
    j.check(bool(j.command("git", "rev-parse", "--verify", "sample")), "Removal kept the branch")
    j.check('"sample"' not in j.cli("list", "--json"), "The head is no longer listed")


def removal_consequences(j: Journey, screen: str) -> None:
    text = prose(screen)
    if "Remove 1 head" in text and not getattr(j, "_removal_seen", False):
        j._removal_seen = True  # type: ignore[attr-defined]
        for phrase in ("closes the terminal", "deletes the worktree", "branch is kept"):
            j.expect(phrase in text, f"Removal asks before acting and explains: {phrase}")


def attention_setup(j: Journey) -> None:
    j.seed()
    j.open()


def attention_done(screen: str) -> bool:
    rows = screen.splitlines()
    return bool(rows) and "ATTENTION" in rows[0] and ("Nothing needs your attention" in screen or "need you" in screen)


def attention_verify(j: Journey) -> None:
    pending = j.cli("gate", "status", "sample")
    j.check("pending" not in pending.lower() or "0 pending" in pending.lower(),
            "No approval request is pending, as the view says")


def leave_setup(j: Journey) -> None:
    j.seed()
    j.open()
    j.see("sample")
    j.keys(b"a")
    j.see("Typing goes to the agent")
    j.attached = True


def leave_verify(j: Journey) -> None:
    j.attached = "Typing goes to the agent" in j.screen()
    j.check(bool(sessions_of(j)), "The agent's terminal session is still running")
    if j.attached:
        j.expect(typed_marker(j, Path(j.cli("path", "sample")), "trial-return"),
                 "Typed input reaches the agent after coming back")


def approve_setup(j: Journey) -> None:
    planning_prepare(j)
    planning_converse(j, "conversation")
    j._runs_before = set(runs(j))  # type: ignore[attr-defined]


def approve_done(screen: str) -> bool:
    return "RUN RECORDED" in screen or "LAUNCH REQUESTED" in screen


def approve_verify(j: Journey) -> None:
    run = finished_run(j, j._runs_before)  # type: ignore[attr-defined]
    j.check((run / "state").read_text().strip() == "succeeded", "The approved plan ran and succeeded")
    j.check(json.loads(j.cli("workflow", "plan", "result", run.name))["ok"],
            "Independent delivery verification passed")


def approval_consequences(j: Journey, screen: str) -> None:
    text = prose(screen)
    if "execution approval" in text and not getattr(j, "_approval_seen", False):
        j._approval_seen = True  # type: ignore[attr-defined]
        j.expect("Policy:" in text, "Approval shows the policy that bounds execution")
        j.expect("digest" in text and "revision" in text, "Approval names the exact revision and digest")
        j.expect("y executes exactly this validated revision" in text,
                 "Approval says what y does before anything runs")
        j.expect("head" in text.lower() or "spawn" in text.lower(),
                 "Approval says which heads the plan creates")


GOALS = {
    "start-task": Goal(
        "start-task",
        [Phase("Start work on a new task called first-task using a plain shell, no agent",
               ("start", "new", "task", "begin"),
               lambda s: "Typing goes to the agent" in s,
               answers={"Task name": "first-task", "Agent profile": "none", "Objective": ""})],
        start_task_setup, start_task_verify),
    "why-stopped": Goal(
        "why-stopped",
        [Phase("Find out why the task sample stopped and what happened to its files",
               ("why", "stopped", "explain", "detail", "recover", "repair", "problem", "check"),
               stopped_explained),
         Phase("Open the full details of the head sample",
               ("detail", "full"),
               lambda s: "Details: sample" in s and "kept" in prose(s))],
        stopped_setup, why_stopped_verify),
    "find-attention": Goal(
        "find-attention",
        [Phase("Find out whether anything needs a decision from you",
               ("attention", "decision", "needs"),
               attention_done)],
        attention_setup, attention_verify),
    "leave-return": Goal(
        "leave-return",
        [Phase("Stop typing to the agent and return to Hydra's own controls",
               ("leave", "return", "back", "hydra"),
               lambda s: "Keys go to Hydra" in s or ("Typing goes to the agent" not in s and "HYDRA /" in s)),
         Phase("Go back to typing to the agent",
               ("agent", "talk", "attach", "reconnect", "conversation"),
               lambda s: "Typing goes to the agent" in s)],
        leave_setup, leave_verify),
    "remove-stopped": Goal(
        "remove-stopped",
        [Phase("Remove the head called sample, whose terminal has stopped; keep its branch",
               ("remove", "delete"),
               lambda s: "Removed sample" in s, confirm=True)],
        lambda j: stopped_setup(j, dirty=False), remove_verify, observe=removal_consequences),
    "approve-plan": Goal(
        "approve-plan",
        [Phase("Approve the plan the agent proposed so Hydra runs it",
               ("approve", "execute", "run", "confirm", "go"),
               approve_done, confirm=True)],
        approve_setup, approve_verify, observe=approval_consequences),
}

PURPOSE = {
    "start-task": "Start work from a clean repository given only the goal; typed input must reach the new head",
    "why-stopped": "Explain a stopped terminal (files kept, no inferred loss) from on-screen controls",
    "find-attention": "Reach what needs a decision without being told the view's name or key",
    "leave-return": "Leave agent input and come back using only visible hints; the agent keeps running",
    "remove-stopped": "Remove a stopped head; consequences are shown before confirming; branch kept",
    "approve-plan": "Approve a validated proposal; policy, digest and effect shown before y; checked delivery",
}


def run_goal(j: Journey, goal: Goal) -> dict:
    goal.setup(j)
    trial = Trial(j, goal)
    outcome = trial.run()
    goal.verify(j)
    return outcome
