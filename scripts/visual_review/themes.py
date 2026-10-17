"""The native TUI's declared palettes, mirrored from src/tui/render.c.

``check_source`` fails when the C table no longer matches this mirror, so the
lint can never silently judge screens against a stale palette.
"""

from __future__ import annotations

import re
from pathlib import Path

TONES = ("base", "border", "title", "selected", "warning", "strong", "success", "muted", "focus")

# emit_style palettes: one escape per tone, in enum tone order.
PALETTES = {
    "terminal": [
        "\033[0m", "\033[0;36m", "\033[0;1;36m", "\033[0;1;7m", "\033[0;33m", "\033[0;1m",
        "\033[0;32m", "\033[0;2m", "\033[0;1;36m",
    ],
    "dark": [
        "\033[0;38;5;252;48;5;234m", "\033[0;38;5;66;48;5;234m", "\033[0;1;38;5;117;48;5;234m",
        "\033[0;38;5;16;48;5;117m", "\033[0;38;5;214;48;5;234m", "\033[0;1;38;5;252;48;5;234m",
        "\033[0;38;5;114;48;5;234m", "\033[0;38;5;245;48;5;234m", "\033[0;1;38;5;117;48;5;234m",
    ],
    "light": [
        "\033[0;38;5;16;48;5;255m", "\033[0;38;5;67;48;5;255m", "\033[0;1;38;5;24;48;5;255m",
        "\033[0;1;38;5;231;48;5;24m", "\033[0;38;5;130;48;5;255m", "\033[0;1;38;5;16;48;5;255m",
        "\033[0;38;5;28;48;5;255m", "\033[0;38;5;245;48;5;255m", "\033[0;1;38;5;24;48;5;255m",
    ],
}

ANSI16 = [
    0x000000, 0xCD3131, 0x0DBC79, 0xE5E510, 0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5,
    0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, 0x3B8EEA, 0xD670D6, 0x29B8DB, 0xFFFFFF,
]
LEVELS = [0, 95, 135, 175, 215, 255]


def indexed(value: int) -> int:
    """The termviz terminal's 256-colour mapping (src/termviz/terminal_sgr.c)."""
    if value < 16:
        return ANSI16[value]
    if value < 232:
        v = value - 16
        return (LEVELS[v // 36] << 16) | (LEVELS[(v // 6) % 6] << 8) | LEVELS[v % 6]
    grey = 8 + (value - 232) * 10
    return (grey << 16) | (grey << 8) | grey


def parse_sgr(escape: str) -> dict:
    """Foreground/background (as ``rrggbb`` or None) and attributes of one escape."""
    params = [int(p) for p in escape[2:-1].split(";") if p]
    fg = bg = None
    bold = dim = reverse = False
    i = 0
    while i < len(params):
        p = params[i]
        if p == 0:
            fg = bg = None
            bold = dim = reverse = False
        elif p == 1:
            bold = True
        elif p == 2:
            dim = True
        elif p == 7:
            reverse = True
        elif 30 <= p <= 37:
            fg = f"{indexed(p - 30):06x}"
        elif 40 <= p <= 47:
            bg = f"{indexed(p - 40):06x}"
        elif p in (38, 48) and i + 2 < len(params) and params[i + 1] == 5:
            colour = f"{indexed(params[i + 2]):06x}"
            if p == 38:
                fg = colour
            else:
                bg = colour
            i += 2
        i += 1
    return {"fg": fg, "bg": bg, "bold": bold, "dim": dim, "reverse": reverse}


def tones(theme: str) -> dict[str, dict]:
    return {name: parse_sgr(escape) for name, escape in zip(TONES, PALETTES[theme])}


def declared_colours(theme: str) -> set[str | None]:
    colours: set[str | None] = set()
    for tone in tones(theme).values():
        colours.add(tone["fg"])
        colours.add(tone["bg"])
    return colours


def check_source(render_c: Path) -> list[str]:
    """Escapes in src/tui/render.c's palettes that differ from this mirror."""
    text = render_c.read_text()
    start = text.index("static const char *palettes[3][9]")
    end = text.index("};", start)
    found = [
        s.encode().decode("unicode_escape")
        for s in re.findall(r'"((?:\\033\[[0-9;]*m))"', text[start:end])
    ]
    expected = PALETTES["terminal"] + PALETTES["dark"] + PALETTES["light"]
    if found == expected:
        return []
    return [f"render.c palette entry {i}: {f!r} != {e!r}" for i, (f, e) in enumerate(zip(found, expected)) if f != e] or [
        f"render.c declares {len(found)} palette entries, this mirror {len(expected)}"
    ]
