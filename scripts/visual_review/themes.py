"""The native TUI's declared themes, mirrored from src/tui/render.c.

The control centre paints with kettle tokens; ``themes[3]`` in render.c says
which palette entry each token is in the terminal, dark and light themes.
``check_source`` fails when that table no longer matches this mirror, so the
lint can never silently judge screens against a stale palette.
"""

from __future__ import annotations

import re
from pathlib import Path

# Per theme: token -> 256-colour palette entry. A token a theme leaves out is
# the terminal's default colour (None in a capture).
THEMES = {
    "terminal": {"border": 6, "accent": 6, "on_accent": 0, "success": 2, "warning": 3},
    "dark": {"text": 252, "muted": 245, "surface": 234, "selection": 117, "on_selection": 16,
             "border": 66, "accent": 117, "on_accent": 16, "success": 114, "warning": 214},
    "light": {"text": 16, "muted": 245, "surface": 255, "selection": 24, "on_selection": 231,
              "border": 67, "accent": 24, "on_accent": 231, "success": 28, "warning": 130},
}

SIXTEEN = [
    0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
    0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
]
LEVELS = [0, 95, 135, 175, 215, 255]


def indexed(value: int) -> int:
    """The xterm palette as kettle prints it (kt_palette_rgb, src/kettle/color.c)."""
    if value < 16:
        return SIXTEEN[value]
    if value < 232:
        v = value - 16
        return (LEVELS[v // 36] << 16) | (LEVELS[(v // 6) % 6] << 8) | LEVELS[v % 6]
    grey = 8 + (value - 232) * 10
    return (grey << 16) | (grey << 8) | grey


def colour(theme: str, token: str) -> str | None:
    entry = THEMES[theme].get(token)
    return None if entry is None else f"{indexed(entry):06x}"


def tones(theme: str) -> dict[str, dict]:
    """The roles the lint judges by: foreground and background of each."""
    def tone(fg: str, bg: str = "surface") -> dict:
        return {"fg": colour(theme, fg), "bg": colour(theme, bg)}
    return {
        "base": tone("text"), "border": tone("border"), "title": tone("accent"),
        "selected": tone("on_selection", "selection"), "warning": tone("warning"),
        "strong": tone("text"), "success": tone("success"), "muted": tone("muted"),
        "focus": tone("accent"),
    }


def declared_colours(theme: str) -> set[str | None]:
    return {None, *(f"{indexed(entry):06x}" for entry in THEMES[theme].values())}


def check_source(render_c: Path) -> list[str]:
    """Tokens in src/tui/render.c's theme table that differ from this mirror."""
    text = render_c.read_text()
    try:
        start = text.index("static const struct kt_theme themes[3]")
    except ValueError:
        return ["render.c no longer declares `static const struct kt_theme themes[3]`"]
    end = text.index("};", start)
    found: dict[str, dict[str, int]] = {}
    for name, body in re.findall(r'\{"(\w+)", \{(.*?\})\}\}', text[start:end], re.S):
        found[name] = {}
        for token, fields in re.findall(r"\[KT_TOKEN_(\w+)\] = \{([^}]*)\}", body):
            first, second = [f.strip() for f in fields.split(",")][:2]
            # A 256-colour terminal is told the ansi256 entry when one is named.
            chosen = second if second != "0" else first
            index = re.fullmatch(r"KT_INDEX\((\d+)\)", chosen)
            if index:
                found[name][token.lower()] = int(index.group(1))
            elif chosen != "KT_DEFAULT":
                found[name][token.lower()] = -1  # a colour this mirror cannot express
    if found == THEMES:
        return []
    drift = []
    for theme in sorted(set(found) | set(THEMES)):
        ours, theirs = THEMES.get(theme, {}), found.get(theme, {})
        for token in sorted(set(ours) | set(theirs)):
            if ours.get(token) != theirs.get(token):
                drift.append(f"render.c {theme} {token}: {theirs.get(token)!r} != mirror {ours.get(token)!r}")
    return drift
