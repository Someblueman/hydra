"""Entry, navigation, attachment, removal and stopped-terminal journeys."""

from __future__ import annotations

import time
from pathlib import Path

from usability.journey import Journey, prose, sessions_of, tree


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
    j.keys(b"n")
    j.see("Task name:")
    j.keys(b"first-task\r")
    j.see("Agent profile")
    j.keys(b"none\r")
    j.see("Objective")
    j.keys(b"\r")
    j.see("Typing goes to the agent")
    j.attached = True
    assert Path(j.cli("path", "first-task")).is_dir()
    j.keys(b"printf delivered > input-proof\r")
    marker = Path(j.cli("path", "first-task")) / "input-proof"
    deadline = time.monotonic() + 5
    while not marker.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    assert marker.read_text() == "delivered"
    marker.unlink()


def navigation(j: Journey) -> None:
    j.seed()
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    for key, label in (
        (b"2", "[Details]"),
        (b"3", "[Overview]"),
        (b"4", "[Attention]"),
        (b"5", "[Recovery]"),
        # Below 100 columns the tab bar uses short labels.
        (b"6", "[Workflows]" if j.columns >= 100 else "[Flows]"),
        (b"7", "[Statistics]" if j.columns >= 100 else "[Stats]"),
    ):
        j.keys(key)
        j.see(label)
        j.keys(b"\x1b")
        j.see("[Work]")
    j.keys(b"\t")
    j.see("[Details]")
    j.keys(b"\x1b[Z")
    j.see("[Work]")
    assert Path(j.cli("path", "sample")).is_dir()


def attachment(j: Journey) -> None:
    j.seed()
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    for cycle in range(2):
        j.keys(b"a")
        j.see("Typing goes to the agent")
        j.attached = True
        # Verify input independently on disk, not by an echoed command marker.
        marker = f"input-{cycle}"
        j.keys(f"printf delivered > {marker}\r".encode())
        worktree = Path(j.cli("path", "sample"))
        deadline = time.monotonic() + 5
        while not (worktree / marker).exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert (worktree / marker).read_text() == "delivered"
        (worktree / marker).unlink()
        assert j.observer
        for width, height in (
            (80, 24),
            (140, 40),
            (j.columns, 40 if j.columns > 80 else 24),
        ):
            j.observer.resize(f"resize-{cycle}-{width}", width, height)
            j.see("Ctrl-B")
        j.keys(b"\x02\t")
        j.attached = False
        j.see("HYDRA /")
        j.keys(b"\x02x")
        j.keys(b"\x1b\x1b")
        j.see("[Work]")
        assert j.command("tmux", "list-panes", "-a", "-F", "#{pane_pid}")


def removal(j: Journey) -> None:
    j.seed()
    worktree = Path(j.cli("path", "sample"))
    dirty = worktree / "keep.txt"
    dirty.write_text("must survive\n")
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    j.keys(b"x")
    j.see("y/N:")
    j.keys(b"y\r")
    j.see("exit 1")
    assert dirty.read_text() == "must survive\n"
    j.keys(b"\x1b")
    dirty.unlink()
    j.keys(b"x")
    j.see("y/N:")
    j.keys(b"y\r")
    j.see("Removed sample")
    assert not worktree.exists(), "Removal did not remove the worktree"
    j.keys(b"\t")
    j.see("[Details]")
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
    j.see("[Work]")
    j.keys(b"\t")
    details = j.see_all("Details: sample", "NEXT")
    j.expect(not any(word in details.lower() for word in LOSS_WORDS),
             "Details do not infer failure or lost work from a missing terminal")
    j.expect("kept" in details, "Details say the head's files are kept")
    j.expect(not any(word in details for word in INTERNAL_WORDS),
             "Details explain the stopped terminal without internal status names")
    j.keys(b"5")
    j.see_all("[Recovery]", "Terminal stopped: sample")
    # The finding's plain-language account; raw kinds stay in its diagnostics.
    j.keys(b"d")
    account = j.see("The terminal session for sample is no longer running")
    explanation = prose(account).split("Check:")[0]
    j.expect("files are kept" in explanation, "Recovery says the worktree and files are kept")
    j.expect(not any(word in explanation.lower() for word in LOSS_WORDS),
             "Recovery does not infer failure or lost work from a missing terminal")
    j.expect("Open it from Work" in explanation, "Recovery offers a way back to the work")
    j.keys(b"d")
    j.check((worktree / "draft.txt").read_text() == "uncommitted work\n",
            "Uncommitted work is untouched")
    # The offered action: open the head from Work, which restores a terminal.
    j.keys(b"\x1b1")
    j.see("[Work]")
    j.keys(b"a")
    # Either an attached pane or its disconnection notice: both are outcomes
    # of the offered action, judged below by whether a terminal came back.
    screen = j.see_any("Typing goes to the agent", "CLIENT DISCONNECTED")
    j.attached = True
    j.expect("CLIENT DISCONNECTED" not in screen, "Opening the head attaches a live terminal")
    deadline = time.monotonic() + 15
    while not sessions_of(j) and time.monotonic() < deadline:
        time.sleep(0.2)
    if j.expect(bool(sessions_of(j)), "Opening the head from Work restored its terminal"):
        marker = worktree / "restored-proof"
        j.keys(b"printf restored > restored-proof\r")
        deadline = time.monotonic() + 10
        while not marker.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        j.expect(marker.exists(), "Input reaches the restored terminal in the head's worktree")
    j.check((worktree / "draft.txt").read_text() == "uncommitted work\n",
            "Opening the head kept uncommitted work")
    j.check("committed.txt" in j.command("git", "-C", str(worktree), "ls-files"),
            "Opening the head kept committed work")


