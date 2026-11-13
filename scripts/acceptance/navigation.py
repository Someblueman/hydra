"""U8: displayed hints match what Tab/Shift-Tab, arrows, Enter and Esc do."""

from __future__ import annotations

import re
import time
from pathlib import Path

from acceptance.harness import Case, case

DOWN, UP, SHIFT_TAB, ESC, ENTER, TAB = b"\x1b[B", b"\x1b[A", b"\x1b[Z", b"\x1b", b"\r", b"\t"


def active_tab(screen: str) -> str:
    match = re.search(r"\[([A-Za-z]+)\]", screen.splitlines()[1] if screen.count("\n") > 1 else "")
    return match.group(1) if match else ""


def pane_titles(screen: str) -> list[str]:
    """Pane titles on the workspace's first frame row, left to right."""
    row = screen.splitlines()[2] if screen.count("\n") > 2 else ""
    return re.findall(r"\+- ([A-Z][A-Z /]+?) -", row)


def focused_pane(j: Case, tag: str) -> str:
    """The pane whose title is drawn in the selected (reverse-video) tone in
    the styled snapshot re-derived from the raw bytes. The tree pane also
    marks its selected row with '>' only while focused. '' when none is."""
    for attempt in range(4):
        rows = j.screen().splitlines()
        styled = j.styled(f"{tag}-{attempt}")
        focused = []
        for y, row in enumerate(rows[2:], start=2):
            for match in re.finditer(r"\+- ([A-Za-z][A-Za-z /]+?) -", row):
                if styled.row_styles(y)[match.start(1)][2]:
                    focused.append(match.group(1).split(" / ")[0])
        if focused:
            return "|".join(focused)
        if any(re.match(r"^[|│]>  > ", row) for row in rows):
            return "PROJECT"
        time.sleep(0.3)
    return ""


@case("u8-hints", ("U8.a", "U8.b", "U8.c"), columns=(80, 140))
def hints(j: Case) -> None:
    j.cli("init", "--no-agent")
    j.cli("spawn", "alpha", "--no-agent")
    j.cli("spawn", "beta", "--no-agent")
    j.open()
    j.keys(b"1")
    screen = j.settled(j.see_all("[Work]", "alpha", "beta"))
    hint = j.hint(screen)
    first = j.selected_row(screen)
    j.keys(DOWN)
    time.sleep(0.3)
    second = j.selected_row()
    j.keys(UP)
    time.sleep(0.3)
    back = j.selected_row()
    j.record("U8.a", f"Work hint {hint!r}: Down and Up move the selection and back",
             "j/k select" in hint and first and second and first != second and back == first,
             detail=f"{first[:30]!r} -> {second[:30]!r} -> {back[:30]!r}")
    j.keys(ENTER)
    screen = j.settled(j.see("[Details]"))
    j.record("U8.a", "Work hint 'Enter details': Enter opens the selected head's details",
             "Enter details" in hint and first.split()[1] in screen.splitlines()[2])
    hint = j.hint(screen)
    j.keys(ESC)
    screen = j.see("[Work]")
    j.record("U8.a", f"Details hint {hint!r}: Esc returns to Work with the same selection",
             "Esc back" in hint and j.selected_row(screen) == first)
    j.keys(b"?")
    help_screen = j.see("KEYBOARD HELP")
    j.keys(ESC)
    j.see("[Work]")
    j.keys(TAB)
    after_tab = active_tab(j.see("[Details]"))
    j.keys(SHIFT_TAB)
    after_back = active_tab(j.see("[Work]"))
    j.record("U8.a", "Help says Tab / Shift-Tab move to the next / previous tab: they do",
             "Tab / Shift-Tab" in help_screen and after_tab == "Details" and after_back == "Work")
    # Overview: arrows select, Enter opens details, Tab goes to the next tab.
    j.keys(b"3")
    screen = j.settled(j.see("[Overview]"))
    hint = j.hint(screen)
    before = j.selected_row(screen)
    j.keys(DOWN)
    time.sleep(0.3)
    moved = j.selected_row()
    j.keys(ENTER)
    details = j.see("[Details]")
    j.record("U8.a", f"Overview hint {hint!r}: Down moves the selection and Enter opens its details",
             "j/k select" in hint and "Enter details" in hint and before != moved
             and moved.split()[1] in details.splitlines()[2], detail=f"{before[:30]!r} -> {moved[:30]!r}")
    j.keys(b"3")
    j.see("[Overview]")
    j.keys(TAB)
    j.record("U8.a", "Overview hint 'Tab next tab': Tab moves to Attention",
             "Tab next tab" in hint and active_tab(j.see("[Attention]")) == "Attention")
    # Coordination: Esc goes back to Details.
    j.keys(b"2")
    j.see("[Details]")
    j.keys(b"c")
    screen = j.settled(j.see_any("[Coordination]", "[Coord]"))
    hint = j.hint(screen)
    j.keys(ESC)
    j.record("U8.a", f"Coordination hint {hint!r}: Esc goes back",
             "Esc back" in hint and active_tab(j.see_any("[Details]", "[Work]")) in ("Details", "Work"))
    # Dialog: Esc cancels.
    j.keys(b"1")
    j.see("[Work]")
    j.keys(b"x")
    screen = j.see("y/N:")
    j.record("U8.a", "the confirmation dialog states Enter submit / Esc cancel", "Esc cancel" in screen)
    j.keys(ESC)
    screen = j.see("Removal cancelled")
    j.record("U8.a", "Esc cancels the dialog and both heads remain",
             "alpha" in screen and "beta" in screen and len(j.cli("list").splitlines()) >= 4)
    workspace_focus(j)
    agent_focus(j)


def workspace_focus(j: Case) -> None:
    j.keys(b"W")
    screen = j.settled(j.see("[Workspace]"))
    hint = j.hint(screen)
    titles = pane_titles(screen)
    j.record("U8.b", f"Workspace hint {hint!r} offers Tab for panes; panes visible: {', '.join(titles)}",
             "Tab pane" in hint and len(titles) >= 2)
    sequence = [focused_pane(j, "focus-0")]
    for i in range(1, len(titles) + 3):
        j.keys(TAB)
        time.sleep(0.4)
        screen = j.screen()
        if "Typing goes to the agent" in j.hint(screen):
            j.keys(b"\x02\t")
            time.sleep(0.4)
        sequence.append(focused_pane(j, f"focus-{i}"))
    changes = sum(1 for a, b in zip(sequence, sequence[1:]) if a != b)
    j.record("U8.b", "each Tab moves the visible focus to another pane and the cycle returns to the start",
             changes >= 2 and sequence[0] in sequence[1:] and all(s for s in sequence),
             detail=" -> ".join(sequence))
    before = focused_pane(j, "focus-shift-before")
    j.keys(SHIFT_TAB)
    time.sleep(0.4)
    reverse = focused_pane(j, "focus-shift-after")
    j.keys(TAB)
    time.sleep(0.4)
    forward = focused_pane(j, "focus-shift-forward")
    j.record("U8.b", "Shift-Tab reverses the pane cycle (Tab after it returns to the same pane)",
             reverse != before and forward == before, detail=f"{before} <- {reverse} -> {forward}")


def agent_focus(j: Case) -> None:
    j.keys(b"\x1b\x1b1")
    j.see("[Work]")
    j.keys(UP * 3)
    time.sleep(0.3)
    branch = j.selected_row().split()[1]
    j.keys(b"a")
    screen = j.see("Typing goes to the agent")
    j.attached = True
    hint = j.hint(screen)
    j.record("U8.c", f"attached hint {hint!r} says typing goes to the agent and names Ctrl-B Tab to leave",
             "Ctrl-B Tab leave" in hint and "INPUT TO AGENT" in screen)
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
    j.keys(b"\x02\t")
    j.attached = False
    screen = j.see("Keys go to Hydra")
    j.record("U8.c", "Ctrl-B Tab leaves agent input: the status says keys go to Hydra", True)
    j.record("U8.c", "after leaving input the screen says how to return to the agent (pane label or hint)",
             "Tab here to talk to the agent" in screen or "ATTACHED /" in screen or "Tab pane" in j.hint(screen)
             or " a agent" in j.hint(screen) or "a talk to agent" in j.hint(screen) or "a reconnect" in screen,
             detail=f"hint: {j.hint(screen)!r}")
    returned = False
    for _ in range(8):
        j.keys(TAB)
        time.sleep(0.4)
        if "Typing goes to the agent" in j.hint():
            returned = True
            break
    j.attached = returned
    j.record("U8.c", "Tab cycles the panes and brings focus back to the agent pane", returned)
    if returned:
        j.keys(b"printf back > back-proof\r")
        j.record("U8.c", "typing after returning reaches the agent again", j.wait_file(worktree / "back-proof", 5))
        j.keys(b"\x02\t")
        j.attached = False
        j.see("Keys go to Hydra")
