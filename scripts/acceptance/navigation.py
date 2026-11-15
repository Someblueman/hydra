"""U8: displayed hints match what Tab/Shift-Tab, arrows, Enter and Esc do."""

from __future__ import annotations

import time
from pathlib import Path

from acceptance.harness import Case, case, says
from usability.journey import LEFT_INPUT, TYPING, footer, see_footer

DOWN, UP, SHIFT_TAB, ESC, ENTER, TAB = b"\x1b[B", b"\x1b[A", b"\x1b[Z", b"\x1b", b"\r", b"\t"

# The footer is generated from the focused pane's keys, so it says which
# pane has the focus: the list offers removal, the Conversation talking and
# scrolling, a Details page scrolling only.
PANES = (("list", "x remove"), ("conversation", "enter talk"), ("details", "scroll"))


def focused_pane(j: Case) -> str:
    """The pane the footer is for ('' when it names none)."""
    hint = j.hint(j.settled(j.screen()))
    return next((name for name, mark in PANES if mark in hint), "")


@case("u8-hints", ("U8.a", "U8.b", "U8.c"), columns=(80, 140))
def hints(j: Case) -> None:
    j.cli("init", "--no-agent")
    j.cli("spawn", "alpha", "--no-agent")
    j.cli("spawn", "beta", "--no-agent")
    j.open()
    j.keys(b"1")
    screen = j.settled(j.see_all("alpha", "beta", "2 heads"))
    hint = j.hint(screen)
    first = j.selected_row(screen)
    j.keys(DOWN)
    time.sleep(0.3)
    second = j.selected_row()
    j.keys(UP)
    time.sleep(0.3)
    back = j.selected_row()
    j.record("U8.a", f"Work hint {hint!r}: Up and Down move the selection and back",
             "select" in hint and first and second and first != second and back == first,
             detail=f"{first[:30]!r} -> {second[:30]!r} -> {back[:30]!r}")
    # Enter talks to the selected head's agent; Ctrl-] gives the keys back
    # with the selection where it was.
    j.keys(ENTER)
    screen = j.see(TYPING)
    j.attached = True
    j.record("U8.a", "Work hint 'enter/a talk': Enter attaches the selected head's agent and gives it the keys",
             "enter/a talk" in hint and first.split()[1] in "\n".join(screen.splitlines()[:3]))
    j.leave_agent()
    j.keys(b"\x1cx")  # close the conversation: the agent keeps running, Tab no longer types to it
    j.escape()
    screen = see_footer(j, "x remove")
    j.record("U8.a", "after Ctrl-] and Esc the Work list has the focus and the same selection",
             j.selected_row(screen) == first, detail=j.selected_row(screen)[:40])
    j.keys(b"?")
    help_screen = j.see("KEYBOARD HELP")
    j.escape()
    see_footer(j, "x remove")
    j.keys(TAB)
    after_tab = focused_pane(j)
    j.keys(SHIFT_TAB)
    after_back = focused_pane(j)
    j.record("U8.a", "Help says tab/shift+tab move between panes: they do (the footer follows the focus)",
             "tab/shift+tab" in help_screen and "pane" in help_screen and after_tab not in ("", "list")
             and after_back == "list", detail=f"{after_tab} <- {after_back}")
    # Inbox: arrows select, Enter opens the item, Esc goes back to the list.
    j.keys(b"2")
    screen = j.settled(j.see("Needs you"))
    hint = j.hint(screen)
    j.record("U8.a", f"Inbox hint {hint!r}: the hints match what the screen offers (nothing waits, so no item keys)",
             "Nothing needs you" in screen and "approve" not in hint and "? help" in hint)
    # Dialog: Esc cancels.
    j.keys(b"1")
    see_footer(j, "x remove")
    j.keys(b"x")
    screen = j.see_prose("Remove ")
    j.record("U8.a", "the confirmation dialog states its keys: y removes, esc cancels", "esc cancel" in screen and "y remove" in screen)
    j.escape()
    screen = j.see("Removal cancelled")
    j.record("U8.a", "Esc cancels the dialog and both heads remain",
             "alpha" in screen and "beta" in screen and len(j.cli("list").splitlines()) >= 4)
    pane_focus(j)
    agent_focus(j)


def pane_focus(j: Case) -> None:
    see_footer(j, "x remove")
    hint = j.hint()
    sequence = [focused_pane(j)]
    for _ in range(4):
        j.keys(TAB)
        time.sleep(0.4)
        if TYPING in j.screen():
            j.leave_agent()
        sequence.append(focused_pane(j))
    changes = sum(1 for a, b in zip(sequence, sequence[1:]) if a != b)
    j.record("U8.b", f"Work hint {hint!r} offers tab/shift+tab for panes",
             "tab/shift+tab pane" in hint or j.columns < 100, detail="below 100 columns the footer drops the pane hint; help names it")
    j.record("U8.b", "each Tab moves the visible focus to another pane and the cycle returns to the start",
             changes >= 2 and sequence[0] in sequence[1:] and all(sequence), detail=" -> ".join(sequence))
    before = focused_pane(j)
    j.keys(SHIFT_TAB)
    time.sleep(0.4)
    reverse = focused_pane(j)
    j.keys(TAB)
    time.sleep(0.4)
    forward = focused_pane(j)
    j.record("U8.b", "Shift-Tab reverses the pane cycle (Tab after it returns to the same pane)",
             reverse != before and forward == before, detail=f"{before} <- {reverse} -> {forward}")


def agent_focus(j: Case) -> None:
    j.escape(2)
    j.keys(b"1")
    see_footer(j, "x remove")
    j.keys(UP * 3)
    time.sleep(0.3)
    branch = j.selected_row().split()[1]
    screen = j.attach_agent()
    hint = footer(screen)
    j.record("U8.c", f"attached hint {hint!r} names ctrl+] to leave; the pane says keys go to the agent",
             "ctrl+] leave" in hint and TYPING in screen)
    worktree = Path(j.cli("path", branch))
    j.keys(b"cat > key-proof\r")
    time.sleep(0.3)
    j.keys(b"x\ty")
    time.sleep(0.3)
    j.keys(ESC)
    time.sleep(0.3)
    j.keys(b"z\r")
    time.sleep(0.3)
    j.keys(b"\x04")
    proof = worktree / "key-proof"
    deadline = time.monotonic() + 5
    content = b""
    while not content and time.monotonic() < deadline:
        content = proof.read_bytes() if proof.exists() else b""
        time.sleep(0.1)
    j.record("U8.c", "Tab and Esc typed while attached reach the agent's input (verified in its file)",
             content == b"x\tyz\n" or content == b"x\ty\x1bz\n", detail=f"agent received {content!r}")
    j.record("U8.c", "the Esc key itself is delivered to the agent, as the help promises",
             content == b"x\ty\x1bz\n", detail=f"agent received {content!r}")
    screen = j.leave_agent()
    j.record("U8.c", "Ctrl-] leaves agent input: the status says keys go to Hydra", LEFT_INPUT in screen)
    j.record("U8.c", "after leaving input the screen says how to return to the agent (pane rule or hint)",
             says(screen, "tab here to type") or "talk" in j.hint(screen), detail=f"hint: {j.hint(screen)!r}")
    returned = False
    for _ in range(4):
        j.keys(TAB)
        time.sleep(0.4)
        if TYPING in j.screen():
            returned = True
            break
    j.attached = returned
    j.record("U8.c", "Tab cycles the panes and brings focus back to the agent pane", returned)
    if returned:
        j.keys(b"printf back > back-proof\r")
        j.record("U8.c", "typing after returning reaches the agent again", j.wait_file(worktree / "back-proof", 5))
        j.leave_agent()
