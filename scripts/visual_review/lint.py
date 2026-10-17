#!/usr/bin/env python3
"""Bounded, deterministic layout lint over visual-review captures.

Reads OUTPUT/captures/*.json (termviz-cells screens) and OUTPUT/manifest.json
and reports, per screen: text clipped at the terminal's edge or cut at the
end of its area, overlapping panels or text drawn over a border, a missing
focus indicator, colours outside the theme's declared palette (or a status
word painted in a contradicting tone), raw identifiers in headings or too
many outside detail/diagnostic screens, repeated keyboard hints, and text
contrast below the threshold in the dark and light themes.

Findings listed in lint-known.tsv (rule, capture glob, message substring,
reason) are reported as known and do not fail the lint; everything else does.
See docs/VISUAL-REVIEW.md.
"""

from __future__ import annotations

import fnmatch
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import themes  # noqa: E402

CONTRAST_NORMAL = 4.5
CONTRAST_MUTED = 3.0
IDENTIFIER_LIMIT = 3
BORDER_VERTICAL = set("│|┃║")
BORDER_HORIZONTAL = set("─-━═")
TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT = set("╭┌+"), set("╮┐+"), set("╰└+"), set("╯┘+")
IDENTIFIER = re.compile(r"\b(?:run|head|task|instance|project)_[0-9a-f]{4,}\b|\b[0-9a-f]{32,64}\b")
HINT = re.compile(r"(?:^|(?<=\s\s)|(?<=[·│|] ))((?:Enter|Esc|Tab|Space|[a-zA-Z?/:]|Ctrl-B [a-z])  ?[a-z][a-z-]+(?: [a-z-]+)?)(?=\s{2,}|\s*[·|│]|\s*$)")
NEGATIVE_WORDS = ("failed", "blocked", "terminal gone", "stale", "need attention", "needs you", "outcome unknown", "cancelled")
POSITIVE_WORDS = ("succeeded", "running", "done", "ready", "PASS", "verified")
ALNUM = re.compile(r"[A-Za-z0-9]")


def luminance(colour: str) -> float:
    def channel(value: int) -> float:
        c = value / 255
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (int(colour[i : i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def contrast(fg: str, bg: str) -> float:
    a, b = luminance(fg), luminance(bg)
    if a < b:
        a, b = b, a
    return (a + 0.05) / (b + 0.05)


class Screen:
    def __init__(self, entry: dict, data: dict) -> None:
        self.entry = entry
        self.id = entry["id"]
        self.theme = entry["theme"]
        self.group = entry.get("group", "")
        self.cells = data["cells"]
        self.columns, self.rows = data["columns"], data["rows"]
        self.cursor = data["cursor"]
        self.text = ["".join(c[0] if c[4] else "" for c in row) for row in self.cells]
        self.tones = themes.tones("dark" if self.theme == "nocolor" else self.theme)

    def glyph(self, x: int, y: int) -> str:
        if 0 <= y < self.rows and 0 <= x < self.columns:
            return self.cells[y][x][0] or ""
        return ""

    def colours(self, x: int, y: int) -> tuple[str | None, str | None, int]:
        cell = self.cells[y][x]
        fg, bg, attrs = cell[1], cell[2], cell[3]
        if attrs & 16:
            fg, bg = bg, fg
        return fg, bg, attrs


class Lint:
    def __init__(self, screen: Screen, finding) -> None:
        self.s = screen
        self.finding = finding

    def run(self) -> None:
        self.clipping()
        self.overlap()
        self.focus()
        self.palette()
        self.identifiers()
        self.hints()
        self.contrast()

    # -- text clipped at the terminal edge or cut at the end of its area --
    def clipping(self) -> None:
        s = self.s
        for y, line in enumerate(s.text):
            if line[-1:].strip():
                self.finding("clip-edge", y, f"text reaches the terminal's last column: {line.strip()[-24:]!r}")
            if y < 2:
                continue  # the title and tab rows are right-aligned by design
            for x in range(1, s.columns - 1):
                here, right = s.glyph(x, y), s.glyph(x + 1, y)
                if not ALNUM.match(here) or right != " " or not ALNUM.match(s.glyph(x - 1, y)):
                    continue
                after = s.glyph(x + 2, y)
                if (after in BORDER_VERTICAL or x + 2 >= s.columns - 2) and self.cut(y, x):
                    self.finding("clip-area", y, f"a word is cut at the end of its area: {line[max(0, x - 30):x + 1].strip()!r}")
        footer = s.text[-1].rstrip()
        if re.search(r"(?:^|\s)[?A-Za-z/:]\s[a-z]{1,2}$", footer):
            self.finding("clip-area", s.rows - 1, f"the footer hints are cut at the edge: {footer[-24:]!r}")

    def cut(self, y: int, x: int) -> bool:
        """Whether the word ending at column x was cut rather than wrapped."""
        s = self.s
        line = s.text[y]
        fragment = line[: x + 1].split()[-1]
        inner = line[: x + 1].rstrip()
        start = len(inner) - len(inner.lstrip(" │|"))
        following = s.text[y + 1] if y + 1 < s.rows else ""
        next_text = following.lstrip(" │|")
        next_start = len(following) - len(next_text)
        tabular = re.search(r"\S {3,}\S", inner.strip(" │|")) is not None
        if fragment.count("(") > fragment.count(")"):
            return True
        if re.fullmatch(r"[0-9a-f]{32,}", fragment) or len(re.sub(r"[^0-9A-Za-z]", "", fragment)) >= 16:
            return not re.match(r"[0-9A-Za-z]", next_text)
        if next_text.strip() and next_start >= start and not tabular:
            return False  # prose wrapped onto the next row
        word = re.sub(r"[^A-Za-z]", "", fragment)
        return len(word) >= 3 and not fragment.endswith((")", ".", ":", "…")) and word.lower() not in COMPLETE_WORDS

    # -- panels that intersect, or text drawn over a panel border --
    def rectangles(self) -> list[tuple[int, int, int, int]]:
        s = self.s
        found = []
        for y in range(s.rows):
            for x in range(s.columns):
                if s.glyph(x, y) not in TOP_LEFT:
                    continue
                for x2 in range(x + 2, s.columns):
                    if s.glyph(x2, y) in TOP_RIGHT and all(
                        s.glyph(i, y) in BORDER_HORIZONTAL or s.glyph(i, y) not in (TOP_LEFT | TOP_RIGHT) for i in range(x + 1, x2)
                    ):
                        for y2 in range(y + 2, s.rows):
                            if s.glyph(x, y2) in BOTTOM_LEFT and s.glyph(x2, y2) in BOTTOM_RIGHT:
                                found.append((x, y, x2, y2))
                                break
                        break
        return found

    def overlap(self) -> None:
        s = self.s
        rects = self.rectangles()
        for a in rects:
            for b in rects:
                if a >= b:
                    continue
                inter = a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
                contains = (a[0] <= b[0] and a[1] <= b[1] and a[2] >= b[2] and a[3] >= b[3]) or (
                    b[0] <= a[0] and b[1] <= a[1] and b[2] >= a[2] and b[3] >= a[3])
                if inter and not contains:
                    self.finding("overlap", a[1], f"panels {a} and {b} intersect")
        for x1, y1, x2, y2 in rects:
            for y in range(y1 + 1, y2):
                for x in (x1, x2):
                    glyph = s.glyph(x, y)
                    if glyph not in BORDER_VERTICAL and glyph not in (TOP_LEFT | TOP_RIGHT | BOTTOM_LEFT | BOTTOM_RIGHT) and glyph.strip():
                        self.finding("overlap", y, f"text {glyph!r} drawn over a panel border at column {x}")

    # -- a selection, cursor or focused control must be visible --
    def focus(self) -> None:
        s = self.s
        selected = s.tones["selected"]
        for y in range(s.rows):
            for x in range(s.columns):
                fg, bg, attrs = s.colours(x, y)
                if s.theme == "nocolor":
                    break
                if attrs & 16 and s.theme == "terminal":
                    return
                if bg and bg == selected["bg"] and fg == selected["fg"]:
                    return
        if s.cursor["visible"] and s.cursor["x"] < s.columns - 1:
            return
        if s.theme == "nocolor" and len(s.text) > 1 and re.search(r"\[[A-Za-z]+\]", s.text[1]):
            return  # the active tab is the textual focus indicator without colour
        if any(re.match(r"^[│|]?\s{0,3}>\s", line) or " > " in line[:6] for line in s.text):
            return
        if any(re.search(r"\b(y approve|Type yes|Enter (continues|checks|signs|hands|reconciles|tries)|Press Enter)\b", line) for line in s.text):
            return
        self.finding("focus", 0, "no selection marker, focused field, cursor or action prompt is visible")

    # -- only the theme's declared colours; status words in fitting tones --
    def palette(self) -> None:
        s = self.s
        if s.theme == "nocolor":
            foreign = sorted({c for row in s.cells for c in (row[0][1], row[0][2]) if c})
            for y, row in enumerate(s.cells):
                if any(c[1] or c[2] for c in row):
                    self.finding("palette", y, f"colour escapes in no-colour mode: {sorted({c[1] or c[2] for c in row if c[1] or c[2]})[:3]}")
                    break
            return
        declared = themes.declared_colours(s.theme)
        reported: set[str] = set()
        for y, row in enumerate(s.cells):
            for cell in row:
                for colour in (cell[1], cell[2]):
                    if colour and colour not in declared and colour not in reported:
                        reported.add(colour)
                        self.finding("palette", y, f"colour #{colour} is not in the {s.theme} theme's declared palette")
        success, warning = s.tones["success"]["fg"], s.tones["warning"]["fg"]
        for y, line in enumerate(s.text):
            for word in NEGATIVE_WORDS:
                for match in re.finditer(re.escape(word), line):
                    fg = s.colours(match.start(), y)[0]
                    if fg == success:
                        self.finding("status-tone", y, f"{word!r} is painted in the success tone")
            for word in POSITIVE_WORDS:
                for match in re.finditer(r"\b" + re.escape(word) + r"\b", line):
                    fg = s.colours(match.start(), y)[0]
                    if fg == warning:
                        self.finding("status-tone", y, f"{word!r} is painted in the warning tone")

    # -- raw identifiers --
    def identifiers(self) -> None:
        s = self.s
        title = s.tones["title"]
        count = 0
        for y, line in enumerate(s.text):
            for match in IDENTIFIER.finditer(line):
                fg, _, attrs = s.colours(match.start(), y)
                heading = y < 2 or (attrs & 1 and fg == title["fg"]) or (
                    y < s.rows and s.glyph(match.start() - 2, y) in BORDER_HORIZONTAL)
                if heading:
                    self.finding("identifier", y, f"raw identifier in a heading: {match.group(0)[:20]}")
                elif s.group not in ("detail", "diagnostic"):
                    count += 1
        if count > IDENTIFIER_LIMIT:
            self.finding("identifier", 0, f"{count} raw identifiers outside a detail or diagnostic screen (limit {IDENTIFIER_LIMIT})")

    # -- the same keyboard hint twice on one screen --
    def hints(self) -> None:
        seen: dict[str, int] = {}
        for y, line in enumerate(self.s.text):
            for match in HINT.finditer(line):
                hint = re.sub(r"\s+", " ", match.group(1))
                seen.setdefault(hint, y)
                if seen[hint] != y:
                    self.finding("hint-repeat", y, f"keyboard hint {hint!r} is already shown on row {seen[hint]}")

    # -- contrast of text against its background --
    def contrast(self) -> None:
        s = self.s
        if s.theme not in ("dark", "light"):
            return
        muted = s.tones["muted"]["fg"]
        low: dict[tuple[str, str], tuple[int, str]] = {}
        for y, row in enumerate(s.cells):
            for x, cell in enumerate(row):
                if not cell[0].strip() or not cell[4]:
                    continue
                fg, bg, _ = s.colours(x, y)
                if not fg or not bg:
                    continue
                graphic = cell[0] in BORDER_VERTICAL or cell[0] in BORDER_HORIZONTAL or cell[0] in "+-|" or "\u2500" <= cell[0] <= "\u257f"
                threshold = CONTRAST_MUTED if fg == muted or graphic else CONTRAST_NORMAL
                ratio = contrast(fg, bg)
                if ratio < threshold and (fg, bg) not in low:
                    low[(fg, bg)] = (y, f"#{fg} on #{bg} has contrast {ratio:.2f} (< {threshold}) at {s.text[y].strip()[:32]!r}")
        for (fg, bg), (y, message) in sorted(low.items()):
            self.finding("contrast", y, message)


COMPLETE_WORDS = set("""
yet now run runs head heads step steps task tasks plan plans agent agents host hosts work done none here there seen
new old all one two unit units file files line lines kept gone live stale fresh ready queued retired pending running
failed succeeded blocked unknown approved waiting stopped remote local project branch worktree terminal session
monitor overview details review attention recovery workflows statistics workspace help quit back next previous
select open close expand collapse search more actions technical coordination output remove agent approve decline
yes no cancel continue repair attempts attempt scope range mode theme light dark cyan amber green seconds minutes
hours days ago left right up down enter tab space esc home end page key keys the and or of in on at to for with
from by is are was were be been it its this that these those you your a an not off up down over under into onto
setup result output again worker branch review dismiss land diff refresh later evidence scroll approve decline
host hosts machine remote changed nothing continue identity log transcript section refs ids
""".split())


def load_known(path: Path) -> list[tuple[str, str, str, str]]:
    rows = []
    if not path.exists():
        return rows
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#") or line.startswith("rule\t"):
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            raise ValueError(f"{path}: malformed row {line!r}")
        rows.append(tuple(parts[:4]))
    return rows


def lint_directory(output: Path) -> dict:
    manifest = json.loads((output / "manifest.json").read_text())
    known = load_known(HERE / "lint-known.tsv")
    palette_drift = themes.check_source(HERE.parent.parent / "src/tui/render.c")
    findings: list[dict] = []
    for entry in sorted(manifest["captures"], key=lambda e: e["id"]):
        data = json.loads((output / "captures" / f"{entry['id']}.json").read_text())
        screen = Screen(entry, data)

        def finding(rule: str, row: int, message: str, entry=entry) -> None:
            match = next((k for k in known if k[0] == rule and fnmatch.fnmatch(entry["id"], k[1]) and k[2] in message), None)
            findings.append({"capture": entry["id"], "rule": rule, "row": row, "message": message,
                             "known": bool(match), "reason": match[3] if match else ""})

        Lint(screen, finding).run()
    for drift in palette_drift:
        findings.append({"capture": "-", "rule": "palette-mirror", "row": 0, "message": drift, "known": False, "reason": ""})
    # One finding per (capture, rule, message) keeps the report bounded.
    unique = {(f["capture"], f["rule"], f["message"]): f for f in findings}
    findings = sorted(unique.values(), key=lambda f: (f["capture"], f["rule"], f["row"], f["message"]))
    return {"screens": len(manifest["captures"]), "findings": findings,
            "known": sum(1 for f in findings if f["known"]), "new": sum(1 for f in findings if not f["known"])}


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: lint.py OUTPUT_DIR", file=sys.stderr)
        return 2
    output = Path(sys.argv[1]).resolve()
    result = lint_directory(output)
    (output / "lint.json").write_text(json.dumps(result, indent=2) + "\n")
    for f in result["findings"]:
        mark = "known" if f["known"] else "FAIL"
        print(f"[{mark}] {f['capture']} row {f['row']} {f['rule']}: {f['message']}")
    print(f"visual lint: {result['screens']} screens, {len(result['findings'])} findings, {result['known']} known, {result['new']} new")
    return 1 if result["new"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
