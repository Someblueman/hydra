"""Render a termviz-cells screen (exact colours and attributes) to HTML and SVG."""

from __future__ import annotations

import html

# Defaults for cells the program left on the terminal's own colours: the same
# values tests/termviz/screen_support.c assumes for an unthemed terminal.
DEFAULT_FG = "dddddd"
DEFAULT_BG = "161616"
ATTR_BOLD, ATTR_DIM, ATTR_ITALIC, ATTR_UNDERLINE, ATTR_REVERSE = 1, 2, 4, 8, 16

CELL_W = 8.4
CELL_H = 17.0


def resolve(cell: list, no_colour: bool) -> tuple[str, str, int]:
    """Effective foreground, background and attributes of one cell."""
    _, fg, bg, attrs, _ = cell
    fg = fg or DEFAULT_FG
    bg = bg or DEFAULT_BG
    if no_colour:
        fg, bg = DEFAULT_FG, DEFAULT_BG
    if attrs & ATTR_REVERSE:
        fg, bg = bg, fg
    return fg, bg, attrs


def style_of(fg: str, bg: str, attrs: int) -> str:
    parts = [f"color:#{fg}", f"background:#{bg}"]
    if attrs & ATTR_BOLD:
        parts.append("font-weight:bold")
    if attrs & ATTR_DIM:
        parts.append("opacity:.7")
    if attrs & ATTR_ITALIC:
        parts.append("font-style:italic")
    if attrs & ATTR_UNDERLINE:
        parts.append("text-decoration:underline")
    return ";".join(parts)


def runs(row: list, no_colour: bool):
    """Consecutive cells sharing one style, as (text, fg, bg, attrs)."""
    current = None
    text = ""
    for cell in row:
        if cell[4] == 0:
            continue  # wide-glyph continuation
        key = resolve(cell, no_colour)
        if current is None or key != current:
            if current is not None:
                yield text, *current
            current, text = key, ""
        text += cell[0]
    if current is not None:
        yield text, *current


def to_html(screen: dict, no_colour: bool = False, cursor: bool = True) -> str:
    """A <pre> block: one <span> per style run, exact colours as CSS."""
    rows = []
    cx, cy = screen["cursor"]["x"], screen["cursor"]["y"]
    for y, row in enumerate(screen["cells"]):
        spans = []
        column = 0
        for text, fg, bg, attrs in runs(row, no_colour):
            if cursor and screen["cursor"]["visible"] and y == cy and column <= cx < column + len(text):
                at = cx - column
                before, under, after = text[:at], text[at], text[at + 1 :]
                if before:
                    spans.append(f'<span style="{style_of(fg, bg, attrs)}">{html.escape(before)}</span>')
                spans.append(f'<span class="cursor" style="{style_of(bg, fg, attrs)}">{html.escape(under)}</span>')
                if after:
                    spans.append(f'<span style="{style_of(fg, bg, attrs)}">{html.escape(after)}</span>')
            else:
                spans.append(f'<span style="{style_of(fg, bg, attrs)}">{html.escape(text)}</span>')
            column += len(text)
        rows.append("".join(spans))
    background = screen["cells"][0][0][2] if screen["cells"] and screen["cells"][0] else None
    bg = DEFAULT_BG if no_colour or not background else background
    return (
        f'<pre class="screen" style="background:#{bg};color:#{DEFAULT_FG}" '
        f'data-columns="{screen["columns"]}" data-rows="{screen["rows"]}">'
        + "\n".join(rows)
        + "</pre>"
    )


def to_svg(screen: dict, no_colour: bool = False) -> str:
    """A self-contained SVG: background rectangles per run and monospace text."""
    width = screen["columns"] * CELL_W
    height = screen["rows"] * CELL_H
    rects, texts = [], []
    for y, row in enumerate(screen["cells"]):
        column = 0
        for text, fg, bg, attrs in runs(row, no_colour):
            x = column * CELL_W
            rects.append(
                f'<rect x="{x:.1f}" y="{y * CELL_H:.1f}" width="{len(text) * CELL_W:.1f}" '
                f'height="{CELL_H:.1f}" fill="#{bg}"/>'
            )
            if text.strip():
                weight = ' font-weight="bold"' if attrs & ATTR_BOLD else ""
                opacity = ' opacity="0.7"' if attrs & ATTR_DIM else ""
                texts.append(
                    f'<text x="{x:.1f}" y="{y * CELL_H + CELL_H - 4:.1f}" fill="#{fg}"{weight}{opacity} '
                    f'xml:space="preserve">{html.escape(text)}</text>'
                )
            column += len(text)
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:.0f}" height="{height:.0f}" '
        f'viewBox="0 0 {width:.0f} {height:.0f}" font-family="DejaVu Sans Mono, Menlo, monospace" font-size="14">'
        + "".join(rects)
        + "".join(texts)
        + "</svg>"
    )


def to_text(screen: dict) -> str:
    lines = []
    for row in screen["cells"]:
        lines.append("".join(cell[0] for cell in row if cell[4] != 0))
    return "\n".join(lines) + "\n"
