"""U7: readable details and actionable recovery for running, stopped and
terminal-gone heads."""

from __future__ import annotations

import re

from acceptance.harness import RAW_ID, STATE_PATH, Case, case, column_rows, make_stopped, profile_fixture, select_head, strip_frame
from usability.journey import page, prose, select

SECTIONS = ("CHANGES", "CHECKS")
LOSS_WORDS = ("lost", "failed", "crashed", "deleted", "corrupt")
INTERNAL_WORDS = ("dead-session", "try-hydra", "hydra doctor", "stale-lock", "orphan-worktree", "malformed-state")
RAW_KINDS = re.compile(r"\b(dead-session|stale-lock|orphan-worktree(-dirty)?|teardown-failure|malformed-state)\b")


def body(screen: str) -> list[str]:
    return [strip_frame(line) for line in screen.splitlines()[3:-2] if strip_frame(line)]


def details_lines(screen: str) -> list[str]:
    """The Details page's rows: from its label column rightwards (the page
    sits right of the list, and of the Conversation from 120 columns)."""
    return [line for line in column_rows(screen, "Agent")[1:-2] if line]


def no_raw(lines: list[str]) -> bool:
    return not any(RAW_ID.search(l) or STATE_PATH.search(l) or "confidence" in l.lower() for l in lines)


def details_checks(j: Case, name: str, state: str) -> list[str]:
    select_head(j, name)
    screen = page(j, b"]", "Session", "CHANGES")
    lines = details_lines(screen)
    grouped = all(any(l == s for l in lines) for s in SECTIONS)
    j.record("U7.a", f"{state} head: Details group identity (Agent, Session, Reported), CHANGES and CHECKS",
             grouped and any(l.startswith("Agent") for l in lines) and any(l.startswith("Reported") for l in lines)
             and any(l.startswith("Session") for l in lines), detail=" / ".join(lines)[:300])
    j.record("U7.a", f"{state} head: pending decisions are a labelled line under CHECKS (approval requests)",
             any(l.startswith("Approvals") for l in lines))
    j.record("U7.a", f"{state} head: every row fits the width (wrapped, not clipped)",
             all(len(line.rstrip()) <= j.columns for line in screen.splitlines()))
    j.record("U7.b", f"{state} head: no raw head/instance/project id, state path or confidence word in Details",
             no_raw(lines), detail="; ".join(l for l in lines if RAW_ID.search(l) or STATE_PATH.search(l))[:200])
    j.keys(b"d")
    technical = j.see_all("IDENTITY", "LIFECYCLE")
    j.record("U7.b", f"{state} head: the technical group (d) is where the raw identities and sources live",
             bool(RAW_ID.search(technical)) and "IDENTITY" in technical)
    j.keys(b"d")
    j.see_all("Session", "CHANGES")
    return lines


@case("u7-details-recovery", ("U7.a", "U7.b", "U7.c", "U7.d"))
def details_recovery(j: Case) -> None:
    profile_fixture(j)
    j.cli("init", "--no-agent")
    j.cli("spawn", "running", "--profile", "codex", "--prompt", "Add a greeting to the project")
    j.cli("spawn", "stopped", "--no-agent")
    j.cli("spawn", "gone", "--no-agent")
    make_stopped(j, "stopped")
    j.tmux("kill-session", "-t", "gone")
    j.open()
    j.keys(b"1")
    j.see_all("running", "stopped", "gone")
    running = details_checks(j, "running", "running")
    j.record("U7.a", "running head: the objective given at launch is shown as the task's reported objective or prompt",
             "Add a greeting" in " ".join(running), detail="desired behaviour: Details do not show the launch objective")
    stopped = details_checks(j, "stopped", "stopped")
    plain = " ".join(stopped).lower()
    j.record("U7.c", "stopped head: the session is labelled stopped in plain words and the restore action says files are kept",
             "stopped" in plain and "are kept" in plain and not any(w in plain for w in INTERNAL_WORDS),
             detail=plain[:300])
    j.record("U7.c", "stopped head: Details carry a plain-language account of what stopped and what remains",
             "the agent reported that it exited" in plain and "files in the worktree are kept" in plain
             and "restarts the terminal" in plain,
             detail=plain[:300])
    gone = details_checks(j, "gone", "terminal-gone")
    text = " ".join(gone)
    j.record("U7.c", "terminal-gone head: Details say the terminal is gone and the files are kept, without inferring loss or failure",
             "The terminal is gone" in text and "kept" in text and not any(w in text.lower() for w in LOSS_WORDS)
             and not any(w in text for w in INTERNAL_WORDS), detail=text[:300])
    hint = j.hint()
    j.record("U7.d", "terminal-gone head: restoring access (a restarts the terminal) and removal (x) are separate actions",
             "a restarts the terminal" in text and "talk" in hint and "x remove" in hint
             and not any("remove" in part for part in hint.split("|") if "talk" in part), detail=hint)
    # Inbox: the stopped terminal, its account and its diagnostics.
    j.keys(b"2")
    listing = j.see("Terminal stopped: gone")
    rows = [strip_frame(l) for l in listing.splitlines() if "Terminal stopped" in l]
    j.record("U7.c", "the Inbox lists the stopped terminal in plain language without its raw kind",
             bool(rows) and not any(RAW_KINDS.search(r) for r in rows), detail="; ".join(rows)[:160])
    select(j, "Terminal stopped: gone")
    j.keys(b"\r")
    detail = j.see_prose("The terminal session for gone is no longer running")
    account = prose(detail).split("The terminal session for gone", 1)[1]
    j.record("U7.c", "the item's account says what stopped, that files are kept, and offers an in-app way back (o opens it in Work)",
             "no longer running" in account and "files are kept" in account and "opens the head in Work" in account
             and not any(w in account.lower() for w in LOSS_WORDS) and not any(w in account for w in INTERNAL_WORDS),
             detail=account[:300])
    actions = next((l for l in detail.splitlines() if "Restart terminal" in l), "")
    j.record("U7.d", "the item's actions restore access (restart, run its check, open in Work); removal is not offered",
             "Restart terminal" in actions and "Open in Work" in actions and "remove" not in actions.lower()
             and "x remove" not in j.hint(detail), detail=actions.strip())
    technical = page(j, b"[", "DIAGNOSTICS")
    lines = body(technical)
    heading = next((i for i, l in enumerate(lines) if l == "DIAGNOSTICS"), len(lines))
    j.record("U7.b", "raw kind, source path and confidence appear only on the Technical page, under DIAGNOSTICS",
             heading < len(lines) and any(RAW_KINDS.search(l) for l in lines[heading:])
             and any("Confidence" in l for l in lines[heading:]) and no_raw(body(detail)[:6]),
             detail=" / ".join(lines[heading:heading + 5]))
