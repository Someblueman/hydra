"""The acceptance bullets the installed-build run still establishes.

Bullet ids are stable keys that checks and the report share. The sentences
paraphrase docs/ROADMAP.md; the roadmap text remains the specification. The
other open items (U1 to U3, U7 to U9, U12, U13, U15, U17) are established by
the focused native and shell tests, not by an installed-build run.
"""

from __future__ import annotations

ITEMS = {
    "U6": "Stable rendering without flicker",
}

BULLETS = {
    "U6.a": "Idle: no full-screen clears, bounded repaint of unchanged content, selection and cursor preserved across refreshes.",
    "U6.b": "Streaming agent output: no full-screen clears, bounded repaint of unchanged content, no content disappearing and reappearing.",
    "U6.c": "Polling local state changes: no full-screen clears, bounded repaint of unchanged content.",
    "U6.d": "Switching panes: no full-screen clears, bounded repaint of unchanged content.",
    "U6.e": "Resizing: at most one full repaint per resize, bounded repaint afterwards.",
    "U6.f": "A terminal recording of each phase is retained (bounded size) for the perceptual review that remains a human gate.",
}


def item_of(bullet: str) -> str:
    return bullet.split(".")[0]
