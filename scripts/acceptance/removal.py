"""U12: confirmation and results of head removal stay inside the control centre."""

from __future__ import annotations

import time
from pathlib import Path

from acceptance.harness import Case, case, heads, make_stopped, says, select_head
from usability.journey import see_footer

RETURN_PROMPT = "Press Enter to return"


def in_app_throughout(j: Case, bullet: str, label: str, seconds: float = 3.0) -> list[str]:
    """Every screen during the action keeps the control centre's tab bar and
    never asks for a return-to-UI keypress."""
    screens = j.watch(seconds)
    detours = [i for i, s in enumerate(screens) if "Work" not in j.tab_bar(s) or RETURN_PROMPT in s]
    j.record(bullet, f"{label}: no shell detour and no '{RETURN_PROMPT}' during {len(screens)} continuous snapshots",
             not detours, detail=f"snapshots with a detour: {detours}" if detours else "")
    return screens


def remove(j: Case, bullet: str, name: str, answer: bytes = b"y") -> str:
    select_head(j, name)
    j.keys(b"x")
    dialog = j.see_prose(f"Remove {name}?")
    j.record(bullet, f"{name}: the confirmation dialog names the exact target and the consequences",
             says(dialog, "deletes its worktree") and says(dialog, f"The branch {name} is kept")
             and "y remove" in dialog)
    j.keys(answer)
    return dialog


def close_output(j: Case) -> None:
    """Close a command's output (the removal's messages) when one is open."""
    if "REMOVAL OUTPUT" in j.screen():
        j.escape()
    see_footer(j, "x remove")


@case("u12-removal", ("U12.a", "U12.b", "U12.c", "U12.d", "U12.e"))
def removal(j: Case) -> None:
    j.cli("init", "--no-agent")
    for name in ("stopped", "active", "survivor", "dirty", "clean"):
        j.cli("spawn", name, "--no-agent")
    make_stopped(j, "stopped")
    j.tmux("send-keys", "-t", "active", "sleep 600", "Enter")
    worktrees = {name: Path(j.cli("path", name)) for name in ("stopped", "active", "survivor", "dirty", "clean")}
    (worktrees["dirty"] / "draft.txt").write_text("uncommitted draft\n")
    j.open()
    j.keys(b"1")
    j.see_all("stopped", "active", "survivor")
    # A stopped head.
    remove(j, "U12.a", "stopped")
    in_app_throughout(j, "U12.a", "removing a stopped head")
    screen = j.see("Removed stopped")
    j.record("U12.a", "the outcome text is accurate: the head is gone from the list, its worktree removed, its branch kept",
             "stopped" not in heads(j) and not worktrees["stopped"].exists()
             and bool(j.command("git", "rev-parse", "--verify", "stopped")))
    selected = j.selected_row(screen)
    j.record("U12.a", "focus moves to surviving work", selected.split()[1:2] != ["stopped"] and selected != "",
             detail=selected[:40])
    # An active head (a process is running in its terminal).
    active_pane = j.pane_pids("active")
    remove(j, "U12.b", "active")
    in_app_throughout(j, "U12.b", "removing an active head")
    screen = j.see("Removed active")
    j.record("U12.b", "the active head's terminal process is stopped and its worktree removed; the branch is kept",
             "active" not in heads(j) and not worktrees["active"].exists()
             and not any(Path(f"/proc/{p}").exists() for p in active_pane)
             and bool(j.command("git", "rev-parse", "--verify", "active")))
    j.record("U12.b", "focus moves to surviving work", j.selected_row(screen).split()[1:2] != ["active"])
    # Cancel.
    remove(j, "U12.c", "survivor", answer=b"n")
    screen = j.see("Removal cancelled; nothing changed")
    j.record("U12.c", "cancelling keeps the head, its worktree and the selection",
             "survivor" in heads(j) and worktrees["survivor"].exists()
             and j.selected_row(screen).split()[1:2] == ["survivor"])
    # Dirty work.
    remove(j, "U12.d", "dirty")
    screens = in_app_throughout(j, "U12.d", "a refused removal")
    outcome = j.screen()
    j.record("U12.d", "the refusal explains that uncommitted changes protect the head",
             any(("uncommitted" in s.lower() or "untracked" in s.lower()) and "dirty" in s for s in [*screens, outcome]),
             detail=next((r.strip() for r in outcome.splitlines() if "untracked" in r or "uncommitted" in r), "")[:120])
    close_output(j)
    j.record("U12.d", "the dirty head, its worktree and its draft file remain",
             "dirty" in heads(j) and (worktrees["dirty"] / "draft.txt").read_text() == "uncommitted draft\n")
    # Partial bulk action: one protected head, one removable head.
    select_head(j, "dirty")
    j.keys(b" ")
    select_head(j, "clean")
    j.keys(b" ")
    screen = j.see("2 marked")
    j.keys(b"x")
    dialog = j.see_prose("Remove 2 heads")
    j.record("U12.e", "the bulk confirmation names both targets",
             "dirty" in dialog and "clean" in dialog)
    j.keys(b"y")
    screens = in_app_throughout(j, "U12.e", "a partially successful bulk removal", 4.0)
    j.until(lambda: "clean" not in heads(j), 20, "the removable head of the bulk action was removed")
    outcome = j.screen()
    text = "\n".join([*screens, outcome])
    j.record("U12.e", "the result reports both outcomes: clean removed, dirty refused for uncommitted changes",
             "clean" in text and "dirty" in text and ("uncommitted" in text.lower() or "refused" in text.lower()),
             detail=outcome.splitlines()[-2].strip()[:160])
    close_output(j)
    j.record("U12.e", "the protected head survives with its draft and surviving work is selected",
             "dirty" in heads(j) and (worktrees["dirty"] / "draft.txt").exists()
             and j.selected_row().split()[1:2] in (["dirty"], ["survivor"]), detail=j.selected_row()[:40])
    j.record("U12.e", "no return-to-UI keypress was needed after the bulk action",
             not any(RETURN_PROMPT in s for s in screens) and RETURN_PROMPT not in outcome)
    time.sleep(0.2)
