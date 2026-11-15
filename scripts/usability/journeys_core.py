"""Entry, navigation, attachment, removal and stopped-terminal journeys."""

from __future__ import annotations

import time
from pathlib import Path

from usability.journey import TYPING, Journey, page, prose, see_footer, select, sessions_of, tree

# What each screen shows of its own, and the key the top bar gives it.
SCREENS = (
    (b"2", "Inbox", ("Needs you",)),
    (b"3", "Hosts", ("This machine", "+ Add a host")),
    (b"4", "Stats", ("Head sessions",)),
)
# The footer is generated from the focused pane's keys: the Work list
# offers removal, a Conversation or Details pane scrolls instead.
LIST_KEYS = "x remove"
PANE_KEYS = "scroll"


def typed(j: Journey, worktree: Path, name: str, text: str = "delivered", timeout: float = 5) -> bool:
    """Type a command to the attached agent's shell and check its effect on disk."""
    j.keys(f"printf {text} > {name}\r".encode())
    deadline = time.monotonic() + timeout
    while not (worktree / name).exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    return (worktree / name).exists() and (worktree / name).read_text() == text


def clean_entry(j: Journey) -> None:
    before = tree(j.repo)
    status = j.command("git", "status", "--porcelain")
    j.open()
    j.see("n new task")
    assert tree(j.repo) == before, "Entering Hydra changed repository files"
    assert j.command("git", "status", "--porcelain") == status
    assert not j.cli("list", "--json").count('"branch"'), (
        "Entry created execution resources"
    )
    j.new_task("first-task", "none")
    j.see(TYPING)
    j.attached = True
    worktree = Path(j.cli("path", "first-task"))
    assert worktree.is_dir()
    assert typed(j, worktree, "input-proof"), "Typed input did not reach the new head"
    (worktree / "input-proof").unlink()


def navigation(j: Journey) -> None:
    """Every screen by its number key; Tab and Shift-Tab move the focus
    between Work's panes (the footer follows the focus); Esc returns it."""
    j.seed()
    j.open()
    j.see("sample")
    j.keys(b"1")
    see_footer(j, LIST_KEYS)
    for key, _, markers in SCREENS:
        j.keys(key)
        j.see_all(*markers)
        j.keys(b"1")
        j.see_all("sample", "1 head")
        see_footer(j, LIST_KEYS)
    j.keys(b"\t")
    see_footer(j, PANE_KEYS, absent=LIST_KEYS)
    j.keys(b"\x1b[Z")
    see_footer(j, LIST_KEYS)
    j.keys(b"\t")
    see_footer(j, PANE_KEYS, absent=LIST_KEYS)
    j.escape()
    see_footer(j, LIST_KEYS)
    assert Path(j.cli("path", "sample")).is_dir()


def attachment(j: Journey) -> None:
    j.seed()
    j.open()
    j.see("sample")
    j.keys(b"1")
    see_footer(j, LIST_KEYS)
    for cycle in range(2):
        j.attach_agent()
        # Verify input independently on disk, not by an echoed command marker.
        worktree = Path(j.cli("path", "sample"))
        assert typed(j, worktree, f"input-{cycle}"), "Typed input did not reach the agent"
        (worktree / f"input-{cycle}").unlink()
        assert j.observer
        for width, height in (
            (80, 24),
            (140, 40),
            (j.columns, 40 if j.columns > 80 else 24),
        ):
            j.observer.resize(f"resize-{cycle}-{width}", width, height)
            j.see("Ctrl-] leave")
        j.leave_agent()
        j.keys(b"\x1cx")
        j.escape(2)
        see_footer(j, LIST_KEYS)
        assert j.command("tmux", "list-panes", "-a", "-F", "#{pane_pid}")


def removal(j: Journey) -> None:
    j.seed()
    worktree = Path(j.cli("path", "sample"))
    dirty = worktree / "keep.txt"
    dirty.write_text("must survive\n")
    j.open()
    j.see("sample")
    j.keys(b"1")
    see_footer(j, LIST_KEYS)
    j.keys(b"x")
    j.see_prose("Remove sample?")
    j.keys(b"y")
    j.see("exit 1")
    assert dirty.read_text() == "must survive\n"
    j.escape()
    dirty.unlink()
    j.keys(b"x")
    j.see_prose("Remove sample?")
    j.keys(b"y")
    j.see("Removed sample")
    assert not worktree.exists(), "Removal did not remove the worktree"
    # The control centre carries on with what is left: here, nothing.
    j.see("No agent work in this project yet")
    assert j.command("git", "rev-parse", "--verify", "sample"), (
        "Removal deleted the branch"
    )


LOSS_WORDS = ("lost", "failed", "crashed", "deleted", "corrupt")
INTERNAL_WORDS = ("dead-session", "try-hydra", "hydra doctor")


def recovery_terminal(j: Journey) -> None:
    # U7: an interactive head whose terminal is gone keeps its work; the view
    # must say what stopped without inferring failure or loss, and offer a way
    # back to the work.
    j.seed()
    worktree = Path(j.cli("path", "sample"))
    (worktree / "committed.txt").write_text("committed work\n")
    j.command("git", "-C", str(worktree), "add", "committed.txt")
    j.command("git", "-C", str(worktree), "-c", "commit.gpgSign=false", "commit", "-qm", "work")
    (worktree / "draft.txt").write_text("uncommitted work\n")
    sessions = j.command("tmux", "list-sessions", "-F", "#{session_name}").splitlines()
    j.check(len(sessions) == 1, "The head has exactly one terminal session")
    j.command("tmux", "kill-session", "-t", sessions[0])
    j.open()
    j.see("sample")
    j.keys(b"1")
    # The Details page beside the list (one of the shared pane's pages below 120 columns).
    details = prose(page(j, b"]", "The terminal is gone", "CHANGES"))
    j.expect(not any(word in details.lower() for word in LOSS_WORDS),
             "Details do not infer failure or lost work from a missing terminal")
    j.expect("kept" in details, "Details say the head's files are kept")
    j.expect(not any(word in details for word in INTERNAL_WORDS),
             "Details explain the stopped terminal without internal status names")
    # Inbox lists the stopped terminal with its plain-language account.
    j.keys(b"2")
    j.see("Terminal stopped: sample")
    select(j, "Terminal stopped: sample")
    j.keys(b"\r")
    account = j.see_prose("The terminal session for sample is no longer running")
    explanation = prose(account).split("The terminal session for sample", 1)[1].split("Head ", 1)[0]
    j.expect("files are kept" in explanation, "Inbox says the worktree and files are kept")
    j.expect(not any(word in explanation.lower() for word in LOSS_WORDS),
             "Inbox does not infer failure or lost work from a missing terminal")
    j.expect("opens the head in Work" in explanation, "Inbox offers a way back to the work")
    j.check((worktree / "draft.txt").read_text() == "uncommitted work\n",
            "Uncommitted work is untouched")
    # The offered action: open the head in Work, then talk to it, which
    # restores its terminal.
    j.keys(b"o")
    see_footer(j, "talk")
    j.keys(b"a")
    # Either an attached pane or its disconnection notice: both are outcomes
    # of the offered action, judged below by whether a terminal came back.
    screen = j.see_any(TYPING, "disconnected")
    j.attached = True
    j.expect(TYPING in screen, "Opening the head attaches a live terminal")
    deadline = time.monotonic() + 15
    while not sessions_of(j) and time.monotonic() < deadline:
        time.sleep(0.2)
    if j.expect(bool(sessions_of(j)), "Opening the head from Work restored its terminal"):
        j.expect(typed(j, worktree, "restored-proof", "restored", timeout=10),
                 "Input reaches the restored terminal in the head's worktree")
    j.check((worktree / "draft.txt").read_text() == "uncommitted work\n",
            "Opening the head kept uncommitted work")
    j.check("committed.txt" in j.command("git", "-C", str(worktree), "ls-files"),
            "Opening the head kept committed work")
