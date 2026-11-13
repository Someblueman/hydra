"""A bounded VT emulator for acceptance evidence.

It applies the escape sequences Hydra's presenter and tmux emit, keeps text
attributes so snapshots retain colour, and counts the events the flicker
check needs: full-screen clears, cells rewritten with an identical glyph and
attributes, cursor placements that paint nothing, and glyphs that disappear
and reappear within a few frames. The native termviz terminal remains the
oracle for ASCII snapshots; `text()` is compared against it in the harness.
"""

from __future__ import annotations

import html
import unicodedata

DEC_GRAPHICS = {"j": "┘", "k": "┐", "l": "┌", "m": "└", "n": "┼", "q": "─", "t": "├",
                "u": "┤", "v": "┴", "w": "┬", "x": "│", "a": "▒", "`": "◆"}
BLINK_NS = 100_000_000
COLOURS16 = ["#000", "#a00", "#0a0", "#a50", "#00a", "#a0a", "#0aa", "#aaa",
             "#555", "#f55", "#5f5", "#ff5", "#55f", "#f5f", "#5ff", "#fff"]


def colour_css(value: int | None) -> str:
    if value is None:
        return ""
    if value < 16:
        return COLOURS16[value]
    if value < 232:
        value -= 16
        r, g, b = value // 36, value // 6 % 6, value % 6
        return "#%02x%02x%02x" % tuple(0 if c == 0 else 55 + c * 40 for c in (r, g, b))
    grey = 8 + (value - 232) * 10
    return "#%02x%02x%02x" % (grey, grey, grey)


class Cell:
    __slots__ = ("glyph", "fg", "bg", "bold", "dim", "reverse", "underline", "width")

    def __init__(self) -> None:
        self.glyph, self.fg, self.bg, self.width = " ", None, None, 1
        self.bold = self.dim = self.reverse = self.underline = False

    def same(self, other: "Cell") -> bool:
        return (self.glyph, self.fg, self.bg, self.bold, self.dim, self.reverse, self.underline) == (
            other.glyph, other.fg, other.bg, other.bold, other.dim, other.reverse, other.underline)

    def copy_from(self, other: "Cell") -> None:
        for name in self.__slots__:
            setattr(self, name, getattr(other, name))


class Screen:
    """Primary and alternate buffers, DEC private modes and SGR attributes."""

    def __init__(self, cols: int, rows: int) -> None:
        self.cols, self.rows = cols, rows
        self.primary = [[Cell() for _ in range(cols)] for _ in range(rows)]
        self.alternate = [[Cell() for _ in range(cols)] for _ in range(rows)]
        self.alt_active = False
        self.x = self.y = 0
        self.saved = (0, 0)
        self.top, self.bottom = 0, rows - 1
        self.pending_wrap = False
        self.cursor_visible = True
        self.attr = Cell()
        self.g0_graphics = self.shift_graphics = False
        self.state = "ground"
        self.params = ""
        self.intermediate = ""
        self.osc = bytearray()
        self.utf8 = bytearray()
        self.frame = 0
        self.now_ns = 0  # set by a replaying analyser before each feed
        self.metrics = {"full_clears": 0, "writes": 0, "identical_rewrites": 0,
                        "cursor_moves": 0, "idle_cursor_moves": 0, "blinks": 0, "scrolls": 0}
        self._moved_without_write = False
        self._erased: dict[tuple[int, int], tuple[str, int]] = {}

    # Buffers and resize -----------------------------------------------------
    @property
    def cells(self) -> list[list[Cell]]:
        return self.alternate if self.alt_active else self.primary

    def resize(self, cols: int, rows: int) -> None:
        for name in ("primary", "alternate"):
            old = getattr(self, name)
            new = [[Cell() for _ in range(cols)] for _ in range(rows)]
            for y in range(min(rows, len(old))):
                for x in range(min(cols, len(old[y]))):
                    new[y][x].copy_from(old[y][x])
            setattr(self, name, new)
        self.cols, self.rows = cols, rows
        self.top, self.bottom = 0, rows - 1
        self.x, self.y = min(self.x, cols - 1), min(self.y, rows - 1)

    def new_frame(self) -> None:
        self._flush_idle_move()
        self.frame += 1

    # Output -----------------------------------------------------------------
    def text(self) -> str:
        """One character per cell, ASCII only, like the native observer."""
        lines = []
        for row in self.cells:
            line = []
            for cell in row:
                if cell.width == 0:
                    line.append(" ")
                else:
                    line.append(cell.glyph if 32 <= ord(cell.glyph[0]) <= 126 else "?")
            lines.append("".join(line))
        return "\n".join(lines) + "\n"

    def html(self, title: str = "") -> str:
        out = ['<!doctype html><meta charset="utf-8"><title>', html.escape(title), "</title>",
               "<style>body{background:#111;color:#ddd;font:13px/1.25 monospace}"
               "pre{white-space:pre}.b{font-weight:bold}.d{opacity:.6}.u{text-decoration:underline}</style>",
               "<pre>"]
        for row in self.cells:
            for cell in row:
                if cell.width == 0:
                    continue
                fg, bg = cell.fg, cell.bg
                if cell.reverse:
                    fg, bg = (bg if bg is not None else 0), (fg if fg is not None else 7)
                style = ""
                if fg is not None:
                    style += f"color:{colour_css(fg)};"
                if bg is not None:
                    style += f"background:{colour_css(bg)};"
                classes = " ".join(c for c, on in (("b", cell.bold), ("d", cell.dim), ("u", cell.underline)) if on)
                opening = f'<span style="{style}" class="{classes}">' if style or classes else ""
                out.append(opening + html.escape(cell.glyph) + ("</span>" if opening else ""))
            out.append("\n")
        out.append("</pre>")
        return "".join(out)

    def row_styles(self, y: int) -> list[tuple[int | None, int | None, bool]]:
        """(fg, bg, reverse) per column of one row, for focus-colour checks."""
        return [(c.fg, c.bg, c.reverse) for c in self.cells[y]]

    # Feeding ----------------------------------------------------------------
    def feed(self, data: bytes) -> None:
        for byte in data:
            self._byte(byte)

    def _byte(self, b: int) -> None:
        if self.state == "ground":
            self._ground(b)
        elif self.state == "esc":
            self._escape(b)
        elif self.state == "csi":
            self._csi(b)
        elif self.state == "osc":
            if b == 7:
                self.state = "ground"
            elif b == 0x1B:
                self.state = "osc_esc"
            else:
                self.osc.append(b)
        elif self.state == "osc_esc":
            self.state = "ground" if b == ord("\\") else "osc"
        elif self.state == "string":
            if b == 0x1B:
                self.state = "string_esc"
        elif self.state == "string_esc":
            self.state = "ground" if b == ord("\\") else "string"
        elif self.state == "charset":
            self.g0_graphics = b == ord("0")
            self.state = "ground"
        elif self.state == "charset1":
            self.state = "ground"

    def _ground(self, b: int) -> None:
        if self.utf8:
            if 0x80 <= b < 0xC0:
                self.utf8.append(b)
                need = 2 if self.utf8[0] < 0xE0 else 3 if self.utf8[0] < 0xF0 else 4
                if len(self.utf8) == need:
                    try:
                        self._print(self.utf8.decode("utf-8"))
                    except UnicodeDecodeError:
                        self._print("?")
                    self.utf8.clear()
                return
            self.utf8.clear()
            self._print("?")
        if b == 0x1B:
            self.state = "esc"
        elif b == 0x0E:
            self.shift_graphics = True
        elif b == 0x0F:
            self.shift_graphics = False
        elif b == 0x0A or b == 0x0B or b == 0x0C:
            self._linefeed()
        elif b == 0x0D:
            self.x, self.pending_wrap = 0, False
        elif b == 0x08:
            self.x, self.pending_wrap = max(0, self.x - 1), False
        elif b == 0x09:
            self.x = min(self.cols - 1, (self.x // 8 + 1) * 8)
        elif b == 0x07 or b < 0x20 or b == 0x7F:
            return
        elif b >= 0xC0:
            self.utf8.append(b)
        elif b < 0x80:
            self._print(chr(b))

    def _escape(self, b: int) -> None:
        c = chr(b)
        self.state = "ground"
        if c == "[":
            self.state, self.params, self.intermediate = "csi", "", ""
        elif c == "]":
            self.state, self.osc = "osc", bytearray()
        elif c in "PX^_":
            self.state = "string"
        elif c == "(":
            self.state = "charset"
        elif c == ")":
            self.state = "charset1"
        elif c == "7":
            self.saved = (self.x, self.y)
        elif c == "8":
            self.x, self.y = self.saved
            self.pending_wrap = False
        elif c == "M":
            if self.y == self.top:
                self._scroll(-1)
            else:
                self.y -= 1
        elif c == "D":
            self._linefeed()
        elif c == "E":
            self.x = 0
            self._linefeed()
        elif c == "c":
            self.__init__(self.cols, self.rows)  # RIS: full reset
            self.metrics["full_clears"] += 1
        elif c == "=" or c == ">":
            return

    def _csi(self, b: int) -> None:
        c = chr(b)
        if 0x30 <= b <= 0x3F:
            self.params += c
            return
        if 0x20 <= b <= 0x2F:
            self.intermediate += c
            return
        self.state = "ground"
        if b < 0x40 or b > 0x7E:
            return
        private = self.params[:1] in ("?", ">", "=", "<")
        raw = self.params[1:] if private else self.params
        values = [int(p) if p.isdigit() else 0 for p in raw.split(";")] if raw else []
        n = values[0] if values and values[0] else 1
        if private:
            if c in "hl":
                self._private_mode(values, c == "h")
            return
        if c in "Hf":
            row = values[0] if values else 1
            col = values[1] if len(values) > 1 else 1
            self._move(max(0, min(self.cols - 1, (col or 1) - 1)), max(0, min(self.rows - 1, (row or 1) - 1)))
        elif c == "A":
            self._move(self.x, max(self.top, self.y - n))
        elif c == "B":
            self._move(self.x, min(self.bottom, self.y + n))
        elif c == "C":
            self._move(min(self.cols - 1, self.x + n), self.y)
        elif c == "D":
            self._move(max(0, self.x - n), self.y)
        elif c == "E":
            self._move(0, min(self.bottom, self.y + n))
        elif c == "F":
            self._move(0, max(self.top, self.y - n))
        elif c == "G":
            self._move(max(0, min(self.cols - 1, n - 1)), self.y)
        elif c == "d":
            self._move(self.x, max(0, min(self.rows - 1, n - 1)))
        elif c == "J":
            self._erase_display(values[0] if values else 0)
        elif c == "K":
            self._erase_line(values[0] if values else 0)
        elif c == "m":
            self._sgr(values if values else [0])
        elif c == "r":
            top = (values[0] if values else 1) or 1
            bottom = (values[1] if len(values) > 1 else self.rows) or self.rows
            if top < bottom <= self.rows:
                self.top, self.bottom = top - 1, bottom - 1
            self._move(0, 0)
        elif c == "L":
            self._insert_lines(n)
        elif c == "M":
            self._delete_lines(n)
        elif c == "S":
            for _ in range(n):
                self._scroll(1)
        elif c == "T":
            for _ in range(n):
                self._scroll(-1)
        elif c == "X":
            for x in range(self.x, min(self.cols, self.x + n)):
                self._write_cell(x, self.y, " ", 1)
        elif c == "P":
            row = self.cells[self.y]
            del row[self.x:self.x + n]
            row.extend(Cell() for _ in range(self.cols - len(row)))
        elif c == "@":
            row = self.cells[self.y]
            for _ in range(n):
                row.insert(self.x, Cell())
            del row[self.cols:]
        elif c == "s":
            self.saved = (self.x, self.y)
        elif c == "u":
            self.x, self.y = self.saved
        elif c == "b":
            return

    def _private_mode(self, values: list[int], on: bool) -> None:
        for v in values:
            if v in (1049, 1047, 47):
                if on and not self.alt_active:
                    self.alt_active = True
                    if v == 1049:
                        self.saved = (self.x, self.y)
                    self._erase_display(2)
                elif not on and self.alt_active:
                    self.alt_active = False
                    if v == 1049:
                        self.x, self.y = self.saved
            elif v == 25:
                self.cursor_visible = on

    # Painting ---------------------------------------------------------------
    def _move(self, x: int, y: int) -> None:
        self._flush_idle_move()
        self.x, self.y, self.pending_wrap = x, y, False
        self.metrics["cursor_moves"] += 1
        self._moved_without_write = True

    def _flush_idle_move(self) -> None:
        if self._moved_without_write:
            self.metrics["idle_cursor_moves"] += 1
        self._moved_without_write = False

    def _linefeed(self) -> None:
        self.pending_wrap = False
        if self.y == self.bottom:
            self._scroll(1)
        elif self.y < self.rows - 1:
            self.y += 1

    def _scroll(self, direction: int) -> None:
        self.metrics["scrolls"] += 1
        cells = self.cells
        region = cells[self.top:self.bottom + 1]
        if direction > 0:
            region = region[1:] + [[Cell() for _ in range(self.cols)]]
        else:
            region = [[Cell() for _ in range(self.cols)]] + region[:-1]
        cells[self.top:self.bottom + 1] = region

    def _insert_lines(self, n: int) -> None:
        cells = self.cells
        for _ in range(n):
            del cells[self.bottom]
            cells.insert(self.y, [Cell() for _ in range(self.cols)])

    def _delete_lines(self, n: int) -> None:
        cells = self.cells
        for _ in range(n):
            del cells[self.y]
            cells.insert(self.bottom, [Cell() for _ in range(self.cols)])

    def _erase_display(self, mode: int) -> None:
        if mode in (2, 3):
            self.metrics["full_clears"] += 1
            for y in range(self.rows):
                for x in range(self.cols):
                    self._write_cell(x, y, " ", 1, erase=True, full=True)
        elif mode == 0:
            self._erase_line(0)
            for y in range(self.y + 1, self.rows):
                for x in range(self.cols):
                    self._write_cell(x, y, " ", 1, erase=True)
        elif mode == 1:
            self._erase_line(1)
            for y in range(0, self.y):
                for x in range(self.cols):
                    self._write_cell(x, y, " ", 1, erase=True)

    def _erase_line(self, mode: int) -> None:
        start, end = (self.x, self.cols) if mode == 0 else (0, self.x + 1) if mode == 1 else (0, self.cols)
        for x in range(start, end):
            self._write_cell(x, self.y, " ", 1, erase=True)

    def _print(self, glyph: str) -> None:
        width = 2 if unicodedata.east_asian_width(glyph) in ("W", "F") else 1
        if unicodedata.combining(glyph):
            return
        if (self.g0_graphics or self.shift_graphics) and glyph in DEC_GRAPHICS:
            glyph = DEC_GRAPHICS[glyph]
        if self.pending_wrap or self.x + width > self.cols:
            self.x = 0
            self._linefeed()
        self._write_cell(self.x, self.y, glyph, width)
        if width == 2 and self.x + 1 < self.cols:
            self._write_cell(self.x + 1, self.y, "", 0)
        self.x += width
        if self.x >= self.cols:
            self.x = self.cols - 1
            self.pending_wrap = True

    def _write_cell(self, x: int, y: int, glyph: str, width: int, erase: bool = False,
                    full: bool = False) -> None:
        """A blink is content removed by an erase sequence (EL, partial ED) and
        painted back identically within BLINK_NS; a full clear is counted on
        its own, so the one repaint a resize needs is not a blink."""
        if not (0 <= x < self.cols and 0 <= y < self.rows):
            return
        self._moved_without_write = False
        cell = self.cells[y][x]
        new = Cell()
        new.copy_from(self.attr)
        new.glyph, new.width = glyph, width
        if erase:
            new.bold = new.dim = new.reverse = new.underline = False
            new.fg = None
        self.metrics["writes"] += 1
        key = (x, y)
        if cell.same(new) and cell.width == width:
            self.metrics["identical_rewrites"] += 1
        elif erase and not full and cell.glyph not in (" ", ""):
            self._erased[key] = (cell.glyph, self.now_ns)
        elif glyph != " " and key in self._erased:
            before, when = self._erased.pop(key)
            if before == glyph and self.now_ns - when <= BLINK_NS:
                self.metrics["blinks"] += 1
        cell.copy_from(new)

    def _sgr(self, values: list[int]) -> None:
        i = 0
        while i < len(values):
            v = values[i]
            if v == 0:
                self.attr = Cell()
            elif v == 1:
                self.attr.bold = True
            elif v == 2:
                self.attr.dim = True
            elif v == 4:
                self.attr.underline = True
            elif v == 7:
                self.attr.reverse = True
            elif v == 22:
                self.attr.bold = self.attr.dim = False
            elif v == 24:
                self.attr.underline = False
            elif v == 27:
                self.attr.reverse = False
            elif 30 <= v <= 37:
                self.attr.fg = v - 30
            elif 90 <= v <= 97:
                self.attr.fg = v - 90 + 8
            elif 40 <= v <= 47:
                self.attr.bg = v - 40
            elif 100 <= v <= 107:
                self.attr.bg = v - 100 + 8
            elif v == 39:
                self.attr.fg = None
            elif v == 49:
                self.attr.bg = None
            elif v in (38, 48) and i + 1 < len(values):
                if values[i + 1] == 5 and i + 2 < len(values):
                    setattr(self.attr, "fg" if v == 38 else "bg", values[i + 2])
                    i += 2
                elif values[i + 1] == 2 and i + 4 < len(values):
                    r, g, b = values[i + 2:i + 5]
                    setattr(self.attr, "fg" if v == 38 else "bg", 16 + 36 * (r // 51) + 6 * (g // 51) + b // 51)
                    i += 4
            i += 1
