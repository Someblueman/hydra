#!/usr/bin/env python3
"""Drive and render the README quick tour; record.sh prepares the world.

``capture`` runs the checkout's native control centre over a pseudo-terminal
(the visual review's driver), walks the tour below with real key presses and
keeps every byte the program wrote with the time it arrived. ``render``
replays those bytes through kettle's own terminal model (kettle-cells) at the
moments the screen changed, draws each distinct screen with its exact cell
colours, and encodes the frames with their real timing into a GIF (ffmpeg
palettegen/paletteuse) and an H.264 MP4.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "scripts/visual_review"))

from driver import Driver  # noqa: E402
import render as cells_render  # noqa: E402
import themes  # noqa: E402

COLUMNS, ROWS = 120, 32
ESC = "\x1b"
FPS = 10  # screen samples per second; the TUI repaints at most this often anyway
FINAL_HOLD = 2.5  # seconds the last frame stays before the GIF loops


# --------------------------------------------------------------------------
# capture


class Recorder(Driver):
    """The visual review's driver, keeping when each chunk of output arrived."""

    def __init__(self, *args, **kwargs) -> None:
        self.started = time.monotonic()
        self.stamps: list[tuple[float, int]] = []  # (seconds, bytes so far)
        self.events: list[tuple[float, str, str]] = []  # (seconds, kind, text)
        super().__init__(*args, **kwargs)

    def now(self) -> float:
        return time.monotonic() - self.started

    def pump(self, seconds: float) -> int:
        # Driver.pump, with a timestamp per chunk.
        deadline = time.monotonic() + seconds
        got = 0
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return got
            ready, _, _ = select.select([self.master], [], [], min(remaining, 0.02))
            if not ready:
                if self.process.poll() is not None:
                    return got
                continue
            try:
                chunk = os.read(self.master, 65536)
            except OSError:
                return got
            if not chunk:
                return got
            self.raw += chunk
            self.last_read = time.monotonic()
            self.stamps.append((self.now(), len(self.raw)))
            got += len(chunk)


class Tour:
    def __init__(self, driver: Recorder) -> None:
        self.d = driver

    def caption(self, text: str) -> None:
        self.d.events.append((self.d.now(), "caption", text))

    def hold(self, seconds: float) -> None:
        self.d.pump(seconds)

    def wait(self, *markers: str, timeout: float = 20.0) -> None:
        self.d.wait_for(*markers, timeout=timeout)
        self.d.settle(0.2)

    def press(self, key: str, label: str) -> None:
        self.d.events.append((self.d.now(), "key", label))
        self.d.send(key)

    def escape(self) -> None:
        # An Esc followed at once by another key reads as Alt+key.
        self.d.pump(0.2)
        self.press(ESC, "Esc")
        self.d.pump(0.3)

    def type(self, text: str, pace: float = 0.085) -> None:
        for character in text:
            self.d.send(character)
            self.d.pump(pace)

    def selected(self, text: str) -> bool:
        return any(line.lstrip().startswith("▌") and text in line
                   for line in self.d.text().splitlines())

    def select(self, text: str, pace: float = 0.3, record: bool = True) -> None:
        """Move the list's selection bar to the row showing ``text``, one
        key press at a time, in the direction of that row."""
        for _ in range(16):
            self.d.settle(0.15)
            lines = self.d.text().splitlines()
            bar = [i for i, line in enumerate(lines) if line.lstrip().startswith("\u258c")]
            target = [i for i, line in enumerate(lines) if text in line]
            if not bar or not target:
                break
            if any(text in lines[i] for i in bar):
                return
            key, label = ("j", "\u2193") if target[0] > bar[0] else ("k", "\u2191")
            if record:
                self.press(key, label)
            else:
                self.d.send(key)
            self.d.pump(pace)
        raise TimeoutError(f"no row showing {text!r} could be selected:\n{self.d.text()}")


def tour(t: Tour) -> None:
    # Off camera: the first refresh, then the selection on fix-tests.
    t.wait("1 Work", "fix-tests", "run ci-check", "build-box", timeout=30)
    t.select("fix-tests", pace=0.1, record=False)
    t.wait("42 passed", "Remote live")
    t.hold(1.0)
    t.d.events.append((t.d.now(), "start", ""))

    t.caption("1 Work: heads beside the selected agent's live conversation")
    t.hold(4.0)
    t.caption("Enter talks to the agent: keys now go to it")
    t.press("\r", "Enter")
    t.wait("keys go to the agent")
    t.hold(1.2)
    t.type("why was it flaky?")
    t.hold(0.4)
    t.press("\r", "Enter")
    t.wait("by path makes it stable")
    t.hold(3.0)
    t.caption("ctrl+] gives the keys back to Hydra; the agent keeps running")
    t.press("\x1d", "ctrl+]")
    t.wait("Keys go to Hydra")
    t.hold(2.2)

    t.caption("A planning agent: the plan it published waits on the Plan page")
    t.select("search-index")
    t.wait("Waiting for your approval", "Plan ready · published")
    t.hold(3.0)
    t.caption("] pages the side pane: Details, Plan and, for a run, Run")
    t.press("]", "]")
    t.wait("Agent ", "CHANGES")
    t.hold(2.5)
    t.press("]", "]")
    t.wait("Plan ready · published")
    t.hold(1.5)
    t.caption("A workflow run: its graph, steps and evidence on the Run page")
    t.select("run ci-check")
    t.wait("succeeded · run ci-check", "STEP")
    t.hold(4.0)

    t.caption("2 Inbox: everything that waits for you")
    t.press("2", "2")
    t.wait("Approve publish: release-notes", "Terminal stopped: docs-refresh")
    t.hold(3.0)
    t.select("Step failed: docs-check")
    t.wait("What failed", "r Review")
    t.hold(2.5)
    t.select("Approve publish: release-notes")
    t.press("\r", "Enter")
    t.wait("What is waiting", "Y Approve")
    t.hold(1.5)
    t.caption("Every action asks first, naming exactly what it changes")
    t.press("Y", "Y")
    t.wait("Approve publish?", "esc cancel")
    t.hold(3.5)
    t.escape()
    t.wait("Nothing was decided")
    t.hold(2.0)

    t.caption("3 Hosts: this machine and the remote hosts Hydra observes")
    t.press("3", "3")
    t.wait("This machine", "build-box", "on PATH")
    t.hold(2.5)
    t.select("build-box")
    t.wait("Connection: reachable")
    t.hold(2.5)

    t.caption("4 Stats: recorded workflow runs and head sessions")
    t.press("4", "4")
    t.wait("Workflow runs", "ci-check", "Retries")
    t.hold(3.5)
    t.press("]", "]")
    t.wait("Head sessions", "fix-tests")
    t.hold(3.5)

    t.caption("? lists every key of the place you are in")
    t.press("?", "?")
    t.wait("KEYBOARD HELP")
    t.hold(3.5)
    t.press("?", "?")
    t.wait("Head sessions")
    t.hold(1.0)
    t.caption("q quits; the agents keep running in tmux")
    t.press("q", "q")
    # The tour ends on the last frame of the control centre, held by render.
    t.d.events.append((t.d.now(), "end", ""))
    t.hold(1.0)


def capture(args: argparse.Namespace) -> int:
    env = dict(os.environ)
    for name in ("NO_COLOR", "TMUX", "TMUX_PANE", "COLUMNS", "LINES"):
        env.pop(name, None)
    env.update(TERM="xterm-256color", HYDRA_TUI_THEME="dark", LANG="C.UTF-8", LC_ALL="C.UTF-8")
    driver = Recorder([str(args.tui), "--hydra", str(args.hydra), "--store"], args.cells, args.cwd, env,
                      COLUMNS, ROWS)
    try:
        tour(Tour(driver))
    finally:
        driver.close(b"\x1b\x1bq")
    out = args.out
    (out / "output.raw").write_bytes(bytes(driver.raw))
    (out / "timing.json").write_text(json.dumps({
        "columns": COLUMNS, "rows": ROWS, "stamps": driver.stamps, "events": driver.events,
    }) + "\n")
    return 0


# --------------------------------------------------------------------------
# render

CELL_W, CELL_H, FONT_SIZE = 9, 19, 15
# The recording's terminal: its own default colours are the dark theme's text
# and surface, so cells an agent leaves on the defaults match the frame.
DEFAULT_FG = themes.colour("dark", "text")
DEFAULT_BG = themes.colour("dark", "surface")


def resolve(cell: list) -> tuple[str, str, int]:
    """Effective foreground, background and attributes of one cell."""
    _, fg, bg, attrs, _ = cell
    fg, bg = fg or DEFAULT_FG, bg or DEFAULT_BG
    if attrs & cells_render.ATTR_REVERSE:
        fg, bg = bg, fg
    return fg, bg, attrs
PAD = 14
BAR_H = 34


def font_file(pattern: str) -> str | None:
    try:
        path = subprocess.run(["fc-match", "-f", "%{file}", pattern], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    return path or None


class Painter:
    """Draws kettle-cells screens with Pillow: exact colours per cell, box
    drawing and block elements drawn as geometry, other glyphs from a
    monospace font (DejaVu Sans Mono unless HYDRA_DEMO_FONT names one)."""

    def __init__(self) -> None:
        from PIL import ImageFont

        family = os.environ.get("HYDRA_DEMO_FONT", "DejaVu Sans Mono")
        regular = font_file(family)
        bold = font_file(f"{family}:bold")
        if not regular:
            raise SystemExit(f"record.py: no font file for {family!r} (fc-match)")
        self.fonts = {False: ImageFont.truetype(regular, FONT_SIZE),
                      True: ImageFont.truetype(bold or regular, FONT_SIZE)}
        # Glyphs the main font lacks come from these, in order.
        self.fallbacks = [ImageFont.truetype(p, FONT_SIZE) for p in
                          filter(None, (font_file("DejaVu Sans"), font_file("Noto Sans Symbols2"),
                                        font_file("FreeMono"), font_file("FreeSans")))]
        self.caption_font = ImageFont.truetype(font_file("DejaVu Sans") or regular, 15)
        ascent, descent = self.fonts[False].getmetrics()
        self.baseline = (CELL_H - (ascent + descent)) // 2 + ascent
        self.rows: dict[str, object] = {}
        self.missing_cache: dict[tuple[int, str], bool] = {}

    def has_glyph(self, font, character: str) -> bool:
        key = (id(font), character)
        if key not in self.missing_cache:
            # A missing glyph renders as the font's .notdef box.
            self.missing_cache[key] = self.ink(font, character) != self.ink(font, "\U000F0000")
        return self.missing_cache[key]

    @staticmethod
    def ink(font, character: str) -> bytes:
        from PIL import Image, ImageDraw

        image = Image.new("L", (3 * CELL_W, 2 * CELL_H))
        ImageDraw.Draw(image).text((CELL_W, CELL_H), character, font=font, fill=255, anchor="ls")
        return image.tobytes()

    def glyph_font(self, character: str, bold: bool):
        font = self.fonts[bold]
        if self.has_glyph(font, character):
            return font
        for fallback in self.fallbacks:
            if self.has_glyph(fallback, character):
                return fallback
        return font

    # Box drawing: (left, right, up, down) arms per character, 1 light, 2 heavy.
    BOX = {
        "─": (1, 1, 0, 0), "━": (2, 2, 0, 0), "│": (0, 0, 1, 1), "┃": (0, 0, 2, 2),
        "┌": (0, 1, 0, 1), "┐": (1, 0, 0, 1), "└": (0, 1, 1, 0), "┘": (1, 0, 1, 0),
        "├": (0, 1, 1, 1), "┤": (1, 0, 1, 1), "┬": (1, 1, 0, 1), "┴": (1, 1, 1, 0), "┼": (1, 1, 1, 1),
        "╴": (1, 0, 0, 0), "╶": (0, 1, 0, 0), "╵": (0, 0, 1, 0), "╷": (0, 0, 0, 1),
    }
    ROUND = {"╭": (0, 1, 0, 1), "╮": (1, 0, 0, 1), "╰": (0, 1, 1, 0), "╯": (1, 0, 1, 0)}

    def draw_special(self, draw, x: int, y: int, character: str, fg: tuple) -> bool:
        cx, cy = x + CELL_W // 2, y + CELL_H // 2
        if character in self.BOX:
            left, right, up, down = self.BOX[character]
            for arm, box in ((left, (x, cy, cx, cy)), (right, (cx, cy, x + CELL_W - 1, cy)),
                             (up, (cx, y, cx, cy)), (down, (cx, cy, cx, y + CELL_H - 1))):
                if arm:
                    x0, y0, x1, y1 = box
                    if arm == 2:
                        x0, x1 = (x0, x1) if y0 != y1 else (x0 - 1, x1 + 1)
                        y0, y1 = (y0 - 1, y1 + 1) if y0 == y1 else (y0, y1)
                    draw.rectangle((x0, y0, x1, y1), fill=fg)
            return True
        if character in self.ROUND:
            left, right, up, down = self.ROUND[character]
            r = min(CELL_W, CELL_H) // 2 - 1
            # Straight arms up to the arc, then a quarter circle joining them.
            if left:
                draw.rectangle((x, cy, cx - r, cy), fill=fg)
            if right:
                draw.rectangle((cx + r, cy, x + CELL_W - 1, cy), fill=fg)
            if up:
                draw.rectangle((cx, y, cx, cy - r), fill=fg)
            if down:
                draw.rectangle((cx, cy + r, cx, y + CELL_H - 1), fill=fg)
            ox = cx - 2 * r if left else cx
            oy = cy - 2 * r if up else cy
            quadrant = {"╭": 180, "╮": 270, "╰": 90, "╯": 0}[character]
            draw.arc((ox, oy, ox + 2 * r, oy + 2 * r), quadrant, quadrant + 90, fill=fg, width=1)
            return True
        code = ord(character)
        if 0x2581 <= code <= 0x2588:  # lower eighths and the full block
            height = round(CELL_H * (code - 0x2580) / 8)
            draw.rectangle((x, y + CELL_H - height, x + CELL_W - 1, y + CELL_H - 1), fill=fg)
            return True
        if 0x2589 <= code <= 0x258F:  # left eighths
            width = round(CELL_W * (0x2590 - code) / 8)
            draw.rectangle((x, y, x + width - 1, y + CELL_H - 1), fill=fg)
            return True
        if character == "▀":
            draw.rectangle((x, y, x + CELL_W - 1, y + CELL_H // 2 - 1), fill=fg)
            return True
        if character == "▐":
            draw.rectangle((x + CELL_W // 2, y, x + CELL_W - 1, y + CELL_H - 1), fill=fg)
            return True
        return False

    @staticmethod
    def rgb(hex_colour: str) -> tuple[int, int, int]:
        return tuple(int(hex_colour[i:i + 2], 16) for i in (0, 2, 4))

    def row_image(self, row: list, cursor_x: int | None):
        from PIL import Image, ImageDraw

        key = json.dumps([row, cursor_x], separators=(",", ":"))
        cached = self.rows.get(key)
        if cached is not None:
            return cached
        image = Image.new("RGB", (len(row) * CELL_W, CELL_H))
        draw = ImageDraw.Draw(image)
        x = 0
        for column, cell in enumerate(row):
            character, _, _, attrs, width = cell
            if width == 0:
                continue  # covered by the wide glyph before it
            fg, bg, attrs = resolve(cell)
            fg, bg = self.rgb(fg), self.rgb(bg)
            if attrs & cells_render.ATTR_DIM:
                fg = tuple(round(f * 0.7 + b * 0.3) for f, b in zip(fg, bg))
            if column == cursor_x:
                fg, bg = bg, fg
            span = CELL_W * max(width, 1)
            x = column * CELL_W
            draw.rectangle((x, 0, x + span - 1, CELL_H - 1), fill=bg)
            if character and character != " " and not self.draw_special(draw, x, 0, character, fg):
                font = self.glyph_font(character, bool(attrs & cells_render.ATTR_BOLD))
                advance = font.getlength(character)
                draw.text((x + (span - advance) / 2, self.baseline), character, font=font, fill=fg, anchor="ls")
            if attrs & cells_render.ATTR_UNDERLINE:
                draw.line((x, CELL_H - 2, x + span - 1, CELL_H - 2), fill=fg)
        self.rows[key] = image
        return image

    def frame(self, screen: dict, caption: str, key: str):
        from PIL import Image, ImageDraw

        cells = screen["cells"]
        background = self.rgb(resolve(cells[0][0])[1])
        width = COLUMNS * CELL_W + 2 * PAD
        height = ROWS * CELL_H + 2 * PAD + BAR_H
        image = Image.new("RGB", (width, height), background)
        cursor = screen["cursor"]
        for y, row in enumerate(cells):
            cursor_x = cursor["x"] if cursor["visible"] and cursor["y"] == y else None
            image.paste(self.row_image(row, cursor_x), (PAD, PAD + y * CELL_H))
        # The caption bar: what this part of the tour shows, and the key just pressed.
        draw = ImageDraw.Draw(image)
        top = ROWS * CELL_H + 2 * PAD
        bar = tuple(max(0, c - 10) for c in background)
        draw.rectangle((0, top, width, height), fill=bar)
        draw.line((0, top, width, top), fill=self.rgb("4e7a7a"))
        middle = top + BAR_H // 2
        draw.text((PAD, middle), caption, font=self.caption_font, fill=self.rgb("d0d0d0"), anchor="lm")
        if key:
            label = self.caption_font.getlength(key)
            right = width - PAD
            draw.rounded_rectangle((right - label - 20, middle - 11, right, middle + 11), radius=5,
                                   fill=self.rgb("87d7ff"))
            draw.text((right - 10, middle), key, font=self.caption_font, fill=self.rgb("000000"), anchor="rm")
        return image


def cells_at(tool: Path, raw: bytes, offset: int) -> dict:
    with tempfile.NamedTemporaryFile(prefix="tour-raw.", delete=False) as handle:
        handle.write(raw[:offset])
        path = handle.name
    try:
        out = subprocess.run([str(tool), "--size", f"{COLUMNS}x{ROWS}", path], capture_output=True,
                             text=True, check=True, timeout=60).stdout
        return json.loads(out)
    finally:
        os.unlink(path)


def timeline(timing: dict) -> tuple[list[tuple[float, int]], float, float]:
    """The byte offsets to show and when: the last offset of every 1/FPS
    slot in which output arrived, from the tour's start to its end."""
    events = timing["events"]
    start = next(t for t, kind, _ in events if kind == "start")
    end = next(t for t, kind, _ in events if kind == "end")
    stamps = timing["stamps"]
    first = max((o for t, o in stamps if t <= start), default=0)
    points = [(0.0, first)]
    slots: dict[int, int] = {}
    for t, offset in stamps:
        if start < t <= end:
            slots[int((t - start) * FPS) + 1] = offset
    for slot in sorted(slots):
        points.append((slot / FPS, slots[slot]))
    return points, start, end


def overlays(events: list, start: float) -> list[tuple[float, str, str]]:
    """(time, caption, key) changes: a key label shows for 1.2 seconds."""
    changes = []
    caption, key_until, key = "", -1.0, ""
    marks = sorted({max(0.0, t - start) for t, kind, _ in events if kind in ("caption", "key")} |
                   {max(0.0, t - start) + 1.2 for t, kind, _ in events if kind == "key"} | {0.0})
    for mark in marks:
        for t, kind, text in events:
            moment = max(0.0, t - start)
            if moment <= mark + 1e-9:
                if kind == "caption":
                    caption = text
                elif kind == "key":
                    key, key_until = text, moment + 1.2
        changes.append((mark, caption, key if mark < key_until - 1e-9 else ""))
    return changes


def encode(frames: list[tuple[Path, float]], work: Path, output: Path) -> None:
    listing = work / "frames.ffconcat"
    lines = ["ffconcat version 1.0"]
    for path, duration in frames:
        lines += [f"file '{path.name}'", f"duration {duration:.3f}"]
    lines.append(f"file '{frames[-1][0].name}'")  # the demuxer needs the last file twice
    listing.write_text("\n".join(lines) + "\n")
    gif, mp4 = output / "quick-tour.gif", output / "quick-tour.mp4"
    common = ["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(listing)]
    subprocess.run(common + [
        "-vf", "split[a][b];[a]palettegen=max_colors=128:stats_mode=full:reserve_transparent=0[p];"
               "[b][p]paletteuse=dither=none:diff_mode=rectangle",
        "-fps_mode", "passthrough", "-loop", "0", str(gif)], check=True)
    subprocess.run(common + [
        "-vf", "fps=20,format=yuv420p", "-c:v", "libx264", "-preset", "slow", "-tune", "animation",
        "-crf", "20", "-movflags", "+faststart", str(mp4)], check=True)


def render(args: argparse.Namespace) -> int:
    timing = json.loads((args.capture / "timing.json").read_text())
    raw = (args.capture / "output.raw").read_bytes()
    points, start, end = timeline(timing)
    offsets = sorted({o for _, o in points})
    with ThreadPoolExecutor(max_workers=3) as pool:
        screens = dict(zip(offsets, pool.map(lambda o: cells_at(args.cells, raw, o), offsets)))
    changes = overlays(timing["events"], start)
    # Merge the screen and overlay timelines into (time, offset, caption, key).
    # On a 50 ms grid, so no frame is shorter than a GIF can show.
    moments = sorted({round(t * 20) / 20 for t, _ in points} | {round(t * 20) / 20 for t, _, _ in changes})
    duration = end - start
    sequence = []
    for moment in moments:
        if moment >= duration:
            continue
        offset = max((o for t, o in points if t <= moment + 1e-9), default=points[0][1])
        _, caption, key = max((c for c in changes if round(c[0] * 20) / 20 <= moment + 1e-9), key=lambda c: c[0])
        digest = hashlib.sha1(json.dumps([screens[offset]["cells"], screens[offset]["cursor"], caption, key])
                              .encode()).hexdigest()
        if sequence and sequence[-1][1] == digest:
            continue
        sequence.append((moment, digest, offset, caption, key))
    painter = Painter()
    work = Path(tempfile.mkdtemp(prefix="tour-frames."))
    try:
        frames = []
        rendered: dict[str, Path] = {}
        for index, (moment, digest, offset, caption, key) in enumerate(sequence):
            following = sequence[index + 1][0] if index + 1 < len(sequence) else duration + FINAL_HOLD
            if digest not in rendered:
                path = work / f"frame-{len(rendered):04d}.png"
                painter.frame(screens[offset], caption, key).save(path)
                rendered[digest] = path
            frames.append((rendered[digest], following - moment))
        encode(frames, work, args.output)
        if args.frames:
            args.frames.mkdir(parents=True, exist_ok=True)
            for index, (path, _) in enumerate(frames):
                moment = sequence[index][0]
                shutil.copy(path, args.frames / f"t{moment:06.2f}.png")
        print(f"{len(frames)} frames ({len(rendered)} distinct), {duration + FINAL_HOLD:.1f}s:")
        for name in ("quick-tour.gif", "quick-tour.mp4"):
            print(f"  {args.output / name}  {(args.output / name).stat().st_size / 1e6:.2f} MB")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    c = commands.add_parser("capture", help="drive the tour and keep the timed output")
    c.add_argument("--tui", type=Path, required=True)
    c.add_argument("--hydra", type=Path, required=True)
    c.add_argument("--cells", type=Path, required=True)
    c.add_argument("--cwd", type=Path, required=True)
    c.add_argument("--out", type=Path, required=True)
    r = commands.add_parser("render", help="replay, draw and encode a capture")
    r.add_argument("--cells", type=Path, required=True)
    r.add_argument("--capture", type=Path, required=True)
    r.add_argument("--output", type=Path, required=True)
    r.add_argument("--frames", type=Path, help="also keep every frame here as PNG")
    args = parser.parse_args()
    return capture(args) if args.command == "capture" else render(args)


if __name__ == "__main__":
    raise SystemExit(main())
