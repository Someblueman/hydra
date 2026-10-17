"""U7: readable details and actionable recovery for running, stopped and
terminal-gone heads."""

from __future__ import annotations

import re

from acceptance.harness import RAW_ID, STATE_PATH, Case, case, make_stopped, profile_fixture, select_head, strip_frame

SECTIONS = ("CHANGES", "CHECKS", "NEXT")
LOSS_WORDS = ("lost", "failed", "crashed", "deleted", "corrupt")
INTERNAL_WORDS = ("dead-session", "try-hydra", "hydra doctor", "stale-lock", "orphan-worktree", "malformed-state")
RAW_KINDS = re.compile(r"\b(dead-session|stale-lock|orphan-worktree(-dirty)?|teardown-failure|malformed-state)\b")


def body(screen: str) -> list[str]:
    return [strip_frame(line) for line in screen.splitlines()[3:-2] if strip_frame(line)]


def no_raw(lines: list[str]) -> bool:
    return not any(RAW_ID.search(l) or STATE_PATH.search(l) or "confidence" in l.lower() for l in lines)


def details_checks(j: Case, name: str, state: str) -> str:
    select_head(j, name)
    j.keys(b"\r")
    screen = j.see(f"Details: {name}")
    lines = body(screen)
    grouped = all(any(l == s for l in lines) for s in SECTIONS)
    j.record("U7.a", f"{state} head: Details group identity (Agent, Session, Reported), CHANGES, CHECKS and NEXT",
             grouped and any(l.startswith("Agent") for l in lines) and any("Reported" in l for l in lines)
             and any(l.startswith("Session") or "Session" in l for l in lines))
    j.record("U7.a", f"{state} head: pending decisions are a labelled line under CHECKS (approval requests)",
             any(l.startswith("Approval requests") for l in lines))
    j.record("U7.a", f"{state} head: every row fits the width (wrapped, not clipped)",
             all(len(line.rstrip()) <= j.columns for line in screen.splitlines()))
    j.record("U7.b", f"{state} head: no raw head/instance/project id, state path or confidence word in Details",
             no_raw(lines), detail="; ".join(l for l in lines if RAW_ID.search(l) or STATE_PATH.search(l))[:200])
    j.keys(b"d")
    technical = j.see("Technical details")
    j.record("U7.b", f"{state} head: the technical group (d) is where the raw identities and sources live",
             bool(RAW_ID.search(technical)) and "IDENTITY" in technical and "SOURCES" in technical)
    j.keys(b"d")
    j.see(f"Details: {name}")
    return screen


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
    j.see_all("[Work]", "running", "stopped", "gone")
    running = details_checks(j, "running", "running")
    j.record("U7.a", "running head: the objective given at launch is shown as the task's reported objective or prompt",
             "Add a greeting" in running, detail="desired behaviour: Details do not show the launch objective")
    j.keys(b"\x1b")
    stopped = details_checks(j, "stopped", "stopped")
    plain = " ".join(body(stopped)).lower()
    j.record("U7.c", "stopped head: the session is labelled stopped in plain words and the restore action says files are kept",
             "stopped" in plain and "files are kept" in plain and not any(w in plain for w in INTERNAL_WORDS))
    j.record("U7.c", "stopped head: Details carry a plain-language account of what stopped and what remains",
             any(("exited" in l.lower() or "stopped" in l.lower()) and ("kept" in l.lower() or "work" in l.lower())
                 for l in body(stopped) if not l.startswith(("a ", "x "))),
             detail="desired behaviour: only the Session label changes; no sentence explains the stop")
    j.keys(b"\x1b")
    gone = details_checks(j, "gone", "terminal-gone")
    text = " ".join(body(gone))
    explanation = [l for l in body(gone) if l.startswith("The terminal is gone")]
    j.record("U7.c", "terminal-gone head: Details say the terminal is gone and the files are kept, without inferring loss or failure",
             bool(explanation) and "kept" in text and not any(w in text.lower() for w in LOSS_WORDS)
             and not any(w in text for w in INTERNAL_WORDS))
    next_lines = [l for l in body(gone) if l.startswith(("a ", "x ", "c ", ": ", "p "))]
    restore = [l for l in next_lines if l.startswith("a ") and "restart" in l]
    destructive = [l for l in next_lines if "x  remove" in l or l.startswith("x ")]
    j.record("U7.d", "terminal-gone head: restoring access (a restart) and removal (x) are separate actions on separate rows",
             bool(restore) and bool(destructive) and not any("remove" in l for l in restore))
    # Recovery: the finding, its explanation and its diagnostics.
    j.keys(b"5")
    listing = j.see_all("[Recovery]", "Terminal stopped: gone")
    rows = [strip_frame(l) for l in listing.splitlines() if "Terminal stopped" in l]
    j.record("U7.c", "Recovery lists the finding in plain language without its raw kind",
             bool(rows) and not any(RAW_KINDS.search(r) for r in rows), detail="; ".join(rows)[:160])
    j.keys(b"d")
    detail = j.see("The terminal session for gone is no longer running")
    lines = body(detail)
    heading = next((i for i, l in enumerate(lines) if l in ("DIAGNOSTICS", "Check: hydra doctor") or l.startswith("Check:")), len(lines))
    account = " ".join(lines[:heading])
    diagnostics = lines[heading:]
    j.record("U7.c", "the finding's account says what stopped, that files are kept, and offers an in-app way back (Open it from Work)",
             "no longer running" in account and "files are kept" in account and "Open it from Work" in account
             and not any(w in account.lower() for w in LOSS_WORDS) and not any(w in account for w in INTERNAL_WORDS))
    j.record("U7.b", "raw kind, source path and confidence appear only under a diagnostics heading after the account",
             heading < len(lines) and lines[heading] == "DIAGNOSTICS" and no_raw(lines[:heading])
             and any(STATE_PATH.search(l) for l in diagnostics),
             detail=f"first diagnostics line: {lines[heading] if heading < len(lines) else 'none'!r}")
    j.record("U7.d", "the finding's default action (Enter) runs a check; removal is not offered as the default",
             "Enter runs the check" in detail and "x removes" not in account
             and "Enter run the check" in j.hint(detail))
    j.keys(b"d")
    j.see("1 of 1")
