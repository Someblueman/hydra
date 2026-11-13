"""A deterministic screen-only evaluator for goal-driven trials.

It is given a goal in plain words and the visible screen text, never a feature
name, a key sequence or Hydra source. It reads the controls the screen itself
offers (key hints in the footer, inline "x  label" pairs, "Press n to ..."
sentences, the tab bar once the screen or help has said how tabs are reached,
and open prompts) and prefers the control whose label shares vocabulary with
the goal. No model is involved; the same screen and goal always give the same
choice, so a trial is a reproducible discoverability measurement.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

KEY_NAMES = {
    "Enter": b"\r", "Esc": b"\x1b", "Tab": b"\t", "Shift-Tab": b"\x1b[Z", "Space": b" ",
    "Up": b"\x1b[A", "Down": b"\x1b[B", "Left": b"\x1b[D", "Right": b"\x1b[C",
    "PgUp": b"\x1b[5~", "PgDn": b"\x1b[6~",
}
SINGLE = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ?:/[]")
DIGITS = set("0123456789")


def key_bytes(key: str) -> bytes | None:
    if key.startswith("Ctrl-B "):
        rest = key_bytes(key[len("Ctrl-B "):])
        return None if rest is None else b"\x02" + rest
    if key in KEY_NAMES:
        return KEY_NAMES[key]
    if len(key) == 1 and (key in SINGLE or key in DIGITS):
        return key.encode()
    return None


def key_group(token: str) -> list[str]:
    """Keys named by one hint token: "j/k", "[/]", "h/l/J/K", "Enter", "x"."""
    token = token.strip(",;")
    if token in DIGITS:
        return []  # a count such as "1 heads", never a control in a hint
    if key_bytes(token) is not None:
        return [token]
    if token == "[/]":
        return ["[", "]"]
    parts = token.split("/")
    if len(parts) > 1 and all(key_bytes(p) is not None for p in parts):
        return parts
    return []


@dataclass
class Affordance:
    keys: list[str]
    label: str
    source: str
    row: int

    @property
    def key(self) -> str:
        return self.keys[0]


def strip_frame(line: str) -> str:
    return line.strip().strip("|").strip()


def chunk_affordances(line: str, row: int, source: str) -> list[Affordance]:
    """Pairs of key and label from a hint line, read pane by pane (panes are
    separated by their borders). A cell offers controls when it has at least
    two key-led chunks, or a key followed by two spaces and a label; a sentence
    whose first word happens to be a letter does not."""
    found: list[Affordance] = []
    for cell in re.split(r"\|+", line):
        found += cell_affordances(cell, row, source)
    return found


def cell_affordances(line: str, row: int, source: str) -> list[Affordance]:
    text = strip_frame(line)
    if not text:
        return []
    chunks = [c for c in re.split(r"\s{2,}", text) if c]
    listed = len(chunks) == 1 and text.count(" / ") >= 1
    if listed:
        chunks = [c.strip() for c in text.split(" / ") if c.strip()]
    found: list[Affordance] = []
    pending: list[str] = []
    key_led = 0
    for chunk in chunks:
        tokens = chunk.split()
        keys = key_group(tokens[0]) if tokens else []
        if tokens and tokens[0] == "Ctrl-B" and len(tokens) > 1 and key_bytes("Ctrl-B " + tokens[1]):
            keys, tokens = ["Ctrl-B " + tokens[1]], tokens[1:]
        # "Tab / Shift-Tab" or "Up / Down, j / k": every token is a key or "/".
        if keys and all(t == "/" or key_group(t) for t in tokens[1:]):
            group = [k for t in tokens if t != "/" for k in key_group(t)]
            pending.extend(group)
            key_led += 1
            continue
        if keys and len(tokens) > 1:
            found.append(Affordance(keys, " ".join(tokens[1:]), source, row))
            key_led += 1
            pending = []
        elif pending:
            found.append(Affordance(pending, chunk, source, row))
            pending = []
    if key_led >= 2 or source == "footer" or listed:
        return found
    # One pair: accept only "x  label" (two spaces after the key).
    return [a for a in found if re.search(rf"(^|\s){re.escape(a.keys[-1])}\s{{2,}}", text)]


PRESS = re.compile(r"[Pp]ress (\S+)(?: or (\S+))? (?:to|for) ([^.;:]+)")
CTRL = re.compile(r"Ctrl-B (\S+) ([a-z][\w ]*?)(?=[,.;(]|$)")


def sentence_affordances(line: str, row: int) -> list[Affordance]:
    text = strip_frame(line)
    found = []
    for m in PRESS.finditer(text):
        keys = [k for k in (m.group(1), m.group(2)) if k and key_bytes(k)]
        if keys:
            found.append(Affordance(keys, m.group(3).strip(), "sentence", row))
    for m in CTRL.finditer(text):
        if key_bytes("Ctrl-B " + m.group(1)):
            found.append(Affordance(["Ctrl-B " + m.group(1)], m.group(2).strip(), "sentence", row))
    return found


def tabs(screen: str) -> tuple[list[str], int]:
    """Tab labels on the tab row and the active tab's index (-1 when none)."""
    rows = screen.splitlines()
    if len(rows) < 2:
        return [], -1
    labels = rows[1].split()
    active = next((i for i, t in enumerate(labels) if t.startswith("[") and t.endswith("]")), -1)
    return [t.strip("[]") for t in labels], active


@dataclass
class Reading:
    affordances: list[Affordance] = field(default_factory=list)
    tab_labels: list[str] = field(default_factory=list)
    active_tab: int = -1
    prompt: str = ""
    prompt_kind: str = ""  # "text", "yes-no", "choice"


def read_screen(screen: str, help_open: bool) -> Reading:
    rows = screen.splitlines()
    r = Reading()
    r.tab_labels, r.active_tab = tabs(screen)
    body = rows[2:-1] if len(rows) > 3 else rows
    for index, line in enumerate(body, start=2):
        source = "help" if help_open else "panel"
        r.affordances += chunk_affordances(line, index, source)
        r.affordances += sentence_affordances(line, index)
    if rows:
        r.affordances += chunk_affordances(rows[-1], len(rows) - 1, "footer")
    for index, line in enumerate(rows):
        if line.startswith("INPUT TO HYDRA / text field"):
            cursor = next((i for i in range(index + 1, len(rows)) if rows[i].strip() == "_"), len(rows))
            r.prompt = " ".join(" ".join(rows[index + 1:cursor]).split())
            r.prompt_kind = "yes-no" if r.prompt.rstrip().endswith("y/N:") else "text"
        elif line.startswith("INPUT TO HYDRA / "):
            r.prompt = " ".join(" ".join(rows[index:]).split())
            r.prompt_kind = "choice"
    return r


def words(label: str) -> list[str]:
    return [w for w in re.split(r"[^a-z0-9-]+", label.lower()) if w]


def score(label: str, want: tuple[str, ...], avoid: tuple[str, ...]) -> int:
    seen = words(label)
    hits = {stem for stem in want if any(w.startswith(stem) for w in seen)}
    misses = {stem for stem in avoid if any(w.startswith(stem) for w in seen)}
    return len(hits) - 2 * len(misses)


@dataclass
class Choice:
    keys: list[str]
    label: str
    source: str
    score: int
    fits: bool


def tab_choices(reading: Reading, method: str | None, want, avoid) -> list[Choice]:
    if not method or reading.active_tab < 0:
        return []
    choices = []
    for index, label in enumerate(reading.tab_labels):
        s = score(label, want, avoid)
        if s <= 0 or index == reading.active_tab:
            continue
        distance = index - reading.active_tab
        if method == "Tab":
            keys = ["Tab"] * distance if distance > 0 else ["Shift-Tab"] * -distance
        elif method == "arrows":
            keys = ["Right"] * distance if distance > 0 else ["Left"] * -distance
        else:
            keys = [str(index + 1)]
        choices.append(Choice(keys, f"tab {label}", "tabs", s, True))
    return choices


def rank(reading: Reading, want: tuple[str, ...], avoid: tuple[str, ...], tab_method: str | None) -> list[Choice]:
    """Controls in order of preference: best vocabulary match first, screen
    order within a score; only positive matches count as fitting the goal."""
    choices = []
    for a in reading.affordances:
        s = score(a.label, want, avoid)
        if s > 0:
            choices.append(Choice([a.key], a.label, a.source, s, True))
    choices += tab_choices(reading, tab_method, want, avoid)
    choices.sort(key=lambda c: -c.score)
    return choices


def learned_tab_method(reading: Reading, screen: str) -> str | None:
    """How the screen says tabs are reached: a footer "Tab next tab", help's
    "1-9 jump to a tab" or "Left / Right ... tab"."""
    for a in reading.affordances:
        if a.source == "footer" and "Tab" in a.keys and "tab" in a.label.lower():
            return "Tab"
    if re.search(r"1-9\s+jump to a tab", screen):
        return "digits"
    for a in reading.affordances:
        if "Right" in a.keys and "tab" in a.label.lower():
            return "arrows"
    return None
