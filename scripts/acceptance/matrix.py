"""The acceptance matrix: every acceptance bullet of the open roadmap items.

Bullet ids are stable keys that checks and the report share. The sentences
paraphrase docs/ROADMAP.md; the roadmap text remains the specification.
"""

from __future__ import annotations

ITEMS = {
    "U1": "Keep ordinary onboarding out of the source tree",
    "U2": "Clean terminal entry and human-readable context",
    "U3": "Start the workspace before creating work",
    "U6": "Stable rendering without flicker",
    "U7": "Readable details and actionable recovery",
    "U8": "Consistent, discoverable navigation",
    "U9": "Useful overview and coordination",
    "U12": "Keep action confirmation and results inside the control centre",
    "U13": "Shared navigation and meaningful statistics",
    "U15": "Clear purposes for heads, overview and coordination",
    "U17": "Conversation-to-plan integration without manual JSON",
}

BULLETS = {
    "U1.a": "Opening, configuring and reopening Hydra in a clean existing repo leaves its files and git status unchanged until project work is requested.",
    "U1.b": "Existing dirty work and ignore rules are preserved.",
    "U1.c": "Shared repository configuration is written only after an explicit, previewed opt-in.",
    "U1.d": "Repository commands still require explicit trust before execution; fewer setup steps do not imply blanket trust.",
    "U2.a": "A fresh spawn opens with usable input and clean output at ordinary terminal widths: no bootstrap exports in the visible transcript, scrollback or shell history; concise context.",
    "U2.b": "Agents and subprocesses still receive the correct environment.",
    "U2.c": "Reconnecting with an actual terminal keeps the transcript clean and the prompt usable.",
    "U3.a": "From an uninitialized repository, reach a working conversation from the UI without running init or spawn, exporting variables or constructing a plan file.",
    "U3.b": "An interactive CLI task launch enters or stays in the control centre with the selected agent visible inside it.",
    "U3.c": "Missing providers and missing authentication have actionable in-app states.",
    "U3.d": "Entering the UI alone authorizes no agent work and no repository commands.",
    "U6.a": "Idle: no full-screen clears, bounded repaint of unchanged content, selection and cursor preserved across refreshes.",
    "U6.b": "Streaming agent output: no full-screen clears, bounded repaint of unchanged content, no content disappearing and reappearing.",
    "U6.c": "Polling local state changes: no full-screen clears, bounded repaint of unchanged content.",
    "U6.d": "Switching panes: no full-screen clears, bounded repaint of unchanged content.",
    "U6.e": "Resizing: at most one full repaint per resize, bounded repaint afterwards.",
    "U6.f": "A terminal recording of each phase is retained (bounded size) for the perceptual review that remains a human gate.",
    "U7.a": "Details for a running head, a stopped head and a head whose terminal is gone present objective, agent, progress, checks, changes and pending decisions in grouped, wrapped sections.",
    "U7.b": "Raw IDs, source paths and confidence metadata appear only in the optional technical or diagnostics group.",
    "U7.c": "A stopped terminal is explained in plain language with what is known about retained work; no dead-session, try-hydra or hydra doctor wording; no inferred loss or failure.",
    "U7.d": "Destructive actions stay separate from restoring access to work.",
    "U8.a": "Tab/Shift-Tab, arrows, Enter and Esc behave as the displayed hints say in Work, Details, Overview, Coordination and dialogs.",
    "U8.b": "Inside the Workspace, Tab and Shift-Tab move pane focus as hinted and the focus is visible.",
    "U8.c": "Tab and Esc reach an attached agent; the hinted Ctrl-B Tab leaves input and Tab brings focus back to the agent.",
    "U9.a": "With no work, Overview and Coordination explain the state and offer a next action instead of an empty dashboard.",
    "U9.b": "With a single agent, Overview answers what is running, needs attention, changed and finished with routes; Coordination explains why there is nothing to coordinate.",
    "U9.c": "With multi-agent work and a blocked dependency, Overview shows the pending decision with a route and Coordination explains assignments, dependencies and blockers.",
    "U9.d": "Unknown values are distinguished from zero and the reason is identified.",
    "U12.a": "Removing a stopped head is confirmed and completed in-app with an accurate result; no shell detour, no return-to-UI keypress; focus moves to surviving work.",
    "U12.b": "Removing an active head is confirmed and completed in-app with an accurate result.",
    "U12.c": "Cancelling the confirmation changes nothing.",
    "U12.d": "Dirty work is refused with an accurate explanation; the head and its files remain.",
    "U12.e": "A failed or partially successful bulk action reports each outcome and leaves surviving work selected.",
    "U13.a": "Statistics reached during standalone agent work with empty history keeps the tab bar and selection and explains what is covered.",
    "U13.b": "Statistics reached during a recorded workflow with populated history keeps the tab bar, lists the run and returns to the previous context.",
    "U15.a": "A committed two-file change shows the task's diff rather than an ambiguous zero-change summary.",
    "U15.b": "Heads, Overview and Coordination each answer a distinct question with current evidence; empty coordination explains why there is nothing to coordinate.",
    "U17.a": "An executable plan is created and revised from a conversation without asking the user to write JSON, locate draft files or translate agent prose.",
    "U17.b": "The exact revision and its digest are shown before execution approval; a changed plan invalidates the old approval.",
    "U17.c": "A proposal is not presented as a validated workflow before validation.",
}


def item_of(bullet: str) -> str:
    return bullet.split(".")[0]
