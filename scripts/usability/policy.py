"""A deterministic screen-only evaluator for goal-driven trials.

It is given a goal in plain words and the visible screen text, never a feature
name, a key sequence or Hydra source. It reads the controls the screen itself
offers (key hints in the footer, inline "x  label" pairs, "Press n to ..."
sentences, the top bar's numbered screens, a pane's page strip once the
footer names the page keys, and open dialogs) and prefers the control whose
label shares vocabulary with the goal. No model is involved; the same screen and goal always give the same
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


# Key names as the generated footers and help write them, in ASCII and in
# Unicode, mapped to the names above.
ALIASES = {
    "enter": "Enter", "esc": "Esc", "tab": "Tab", "shift+tab": "Shift-Tab", "space": "Space",
    "pgup": "PgUp", "pgdn": "PgDn", "ctrl+]": "Ctrl-]",
}
# Tokens that name a pair of keys in one glyph pair.
PAIRS = {"^/v": ["Up", "Down"], "\u2191/\u2193": ["Up", "Down"], "</>": ["Left", "Right"],
         "\u2190/\u2192": ["Left", "Right"], "[/]": ["[", "]"]}

# The agent-layer chords the control centre names: Ctrl-] alone, and
# Ctrl-\ followed by one key (keys files can rebind them; these are the
# defaults the screens show).
LEADERS = {"Ctrl-\\": b"\x1c", "ctrl+\\": b"\x1c"}


def key_bytes(key: str) -> bytes | None:
    key = ALIASES.get(key, key)
    if key == "Ctrl-]":
        return b"\x1d"
    for leader, byte in LEADERS.items():
        if key.startswith(leader + " "):
            rest = key_bytes(key[len(leader) + 1:])
            return None if rest is None else byte + rest
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
    if token in PAIRS:
        return list(PAIRS[token])
    if key_bytes(token) is not None:
        return [token]
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
    for cell in re.split(r"\|+|\s\u2022\s", line):
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
        if listed and tokens and tokens[0] in DIGITS and len(tokens) > 1:
            keys = [tokens[0]]  # "3 add a remote host" among listed controls
        if tokens and tokens[0] in LEADERS and len(tokens) > 1 and key_bytes(tokens[0] + " " + tokens[1]):
            keys, tokens = [tokens[0] + " " + tokens[1]], tokens[1:]
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
CTRL = re.compile(r"(Ctrl-\] |Ctrl-\\ \S+ )([a-z][\w ]*?)(?=[,.;(]|$)")


def sentence_affordances(line: str, row: int) -> list[Affordance]:
    text = strip_frame(line)
    found = []
    for m in PRESS.finditer(text):
        keys = [k for k in (m.group(1), m.group(2)) if k and key_bytes(k)]
        if keys:
            found.append(Affordance(keys, m.group(3).strip(), "sentence", row))
    for m in CTRL.finditer(text):
        if key_bytes(m.group(1).strip()):
            found.append(Affordance([m.group(1).strip()], m.group(2).strip(), "sentence", row))
    return found


# What each screen shows of its own: the top bar marks the active screen
# with colour only, so the screen is told by its content.
SCREEN_MARKS = {
    "Work": ("Heads ", "No agent work in this project yet"),
    "Inbox": ("Needs you",),
    "Hosts": ("+ Add a host",),
    "Stats": ("Sessions  Runs", "Workflow runs"),
}


def tabs(screen: str) -> tuple[list[str], int]:
    """The screens the top bar numbers ("1 Work  2 Inbox 3 ..."), in key
    order, and the index of the one shown (-1 when it cannot be told)."""
    rows = screen.splitlines()
    if not rows:
        return [], -1
    named = re.findall(r"(?:^|\s)([1-9]) ([A-Z][a-z]+)\b", rows[0])
    labels = [label for _, label in sorted(named, key=lambda pair: int(pair[0]))]
    body = "\n".join(rows[1:])
    active = next((i for i, label in enumerate(labels)
                   if any(mark in body for mark in SCREEN_MARKS.get(label, ()))), -1)
    return labels, active


PAGE_STRIP = re.compile(r"((?:[A-Z][a-z]+  )+[A-Z][a-z]+)  ?[-\u2500]{3}")


def page_strips(screen: str) -> list[str]:
    """Page names in the panes' titled rules ("Conversation  Details  Plan --")."""
    names: list[str] = []
    for row in screen.splitlines()[1:4]:
        for match in PAGE_STRIP.finditer(row):
            names += [n for n in match.group(1).split("  ") if n not in names]
    return names


@dataclass
class Reading:
    affordances: list[Affordance] = field(default_factory=list)
    tab_labels: list[str] = field(default_factory=list)
    active_tab: int = -1
    pages: list[str] = field(default_factory=list)
    prompt: str = ""
    prompt_kind: str = ""  # "form", "yes-no", "palette", "viewer"
    dialog: list[str] = field(default_factory=list)


DIALOG_TOP = re.compile(r"^(\s*)[+\u256d][-\u2500] (.+?) [-\u2500]+[+\u256e]")
DIALOG_BOTTOM = re.compile(r"[+\u2570][-\u2500]+ (.+?) [-\u2500][+\u256f]")


def dialog(rows: list[str]) -> tuple[str, list[str], str] | None:
    """The open dialog: its title, its inside rows and the key hints in its
    bottom edge."""
    for top, line in enumerate(rows):
        match = DIALOG_TOP.match(line)
        if not match:
            continue
        left = len(match.group(1))
        right = match.end() - 1  # the top right corner; the screen may go on beyond the dialog
        for bottom in range(top + 1, len(rows)):
            hints = DIALOG_BOTTOM.search(rows[bottom][left:right + 1])
            if hints:
                inside = [r[left + 1:right].strip() for r in rows[top + 1:bottom]]
                return match.group(2), inside, hints.group(1)
    return None


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
    if any("[" in a.keys and "]" in a.keys and "page" in a.label for a in r.affordances):
        r.pages = page_strips(screen)
    found = dialog(rows)
    if found and "KEYBOARD HELP" not in found[1][:2]:
        title, inside, hints = found
        r.dialog = inside
        r.prompt = " ".join(" ".join([title, *inside]).split())
        if "submit" in hints:
            r.prompt_kind = "form"
        elif re.search(r"(?:^|\s)y [a-z]", hints):
            r.prompt_kind = "yes-no"
        elif "run" in hints:
            r.prompt_kind = "palette"
        else:
            r.prompt_kind = "viewer"
    return r


def form_fields(inside: list[str]) -> list[tuple[str, str]]:
    """A form's fields in order: each label and its input row (the row under
    it that starts with the field's bar)."""
    fields = []
    for index in range(1, len(inside)):
        row = inside[index]
        if row[:1] in ("|", ">", "\u258c", "\u2502") and inside[index - 1] and inside[index - 1][:1] not in ("|", ">"):
            fields.append((inside[index - 1], row[1:].strip()))
    return fields


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


def page_choices(reading: Reading, want, avoid) -> list[Choice]:
    """A page named in a pane's strip, reached with ] once the footer names
    the page keys ("[/] page")."""
    return [Choice(["]"], f"page {name}", "pages", score(name, want, avoid), True)
            for name in reading.pages if score(name, want, avoid) > 0]


def tab_choices(reading: Reading, method: str | None, want, avoid) -> list[Choice]:
    if not method or (reading.active_tab < 0 and method != "digits"):
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
    choices += page_choices(reading, want, avoid)
    choices.sort(key=lambda c: -c.score)
    return choices


def learned_tab_method(reading: Reading, screen: str) -> str | None:
    """How the screen says tabs are reached: the top bar's numbered screens
    ("1 Work  2 Inbox"), a footer "Tab next tab", help's "1-9 jump to a tab"
    or "Left / Right ... tab"."""
    if len(reading.tab_labels) >= 2:
        return "digits"
    for a in reading.affordances:
        if a.source == "footer" and "Tab" in a.keys and "tab" in a.label.lower():
            return "Tab"
    if re.search(r"1-9\s+jump to a tab", screen):
        return "digits"
    for a in reading.affordances:
        if "Right" in a.keys and "tab" in a.label.lower():
            return "arrows"
    return None
