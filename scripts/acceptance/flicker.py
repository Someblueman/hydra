"""U6: measured flicker. The raw PTY byte stream of the installed TUI is
recorded while idle, while an agent streams output, while local state is
polled, on pane switches and on resize; each phase is replayed through the
bounded emulator and counted per refresh.

A refresh (frame) is one burst of presenter output: reads of the PTY that
arrive within FRAME_GAP_NS of each other. Thresholds were set from the
current build's idle behaviour (see docs/ACCEPTANCE.md) and are asserted as
desired behaviour, never relaxed to the measurement.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

from acceptance import vt
from acceptance.harness import Case, case

FRAME_GAP_NS = 5_000_000
RECORDING_LIMIT = 2 * 1024 * 1024  # bytes retained per phase as evidence
IDLE_SECONDS = 30
# Bounds for repainting unchanged content: the presenter bridges gaps of up
# to 16 unchanged cells inside a changed run, so a changed row may rewrite a
# few unchanged cells; a refresh must never rewrite most of the screen
# unchanged. Idle refreshes (clock, status) are tiny.
IDENTICAL_FRACTION_LIMIT = 0.25   # identical rewrites / screen cells, per refresh, worst case
IDENTICAL_MEAN_LIMIT = 0.05       # identical rewrites / screen cells, averaged over refreshes


class Recording:
    """Phase boundaries by observer read sequence, replayed from terminal.raw."""

    def __init__(self, j: Case) -> None:
        self.j = j
        self.marks: list[tuple[str, int, int]] = []  # (label, sequence, ns)

    def mark(self, label: str) -> None:
        assert self.j.observer
        _, record = self.j.observer.snapshot()
        self.marks.append((label, record["sequence"], time.monotonic_ns()))

    def reads(self) -> list[tuple[int, int, int, int]]:
        assert self.j.observer
        lines = (self.j.observer.directory / "terminal.index").read_text().splitlines()
        return [tuple(map(int, line.split())) for line in lines if len(line.split()) == 4]

    def analyse(self, phases: list[tuple[str, str, str]], cols: int, rows: int) -> dict:
        assert self.j.observer
        raw = (self.j.observer.directory / "terminal.raw").read_bytes()
        reads = self.reads()
        marks = {label: sequence for label, sequence, _ in self.marks}
        # Replay everything in order so each phase starts from the true screen.
        screen = vt.Screen(cols, rows)
        results = {}
        position = 0
        for name, start, end in phases:
            first, last = marks[start], marks[end]
            # Reads before this phase that were not yet replayed.
            while position < len(reads) and reads[position][0] <= first:
                _, ns, offset, length = reads[position]
                screen.now_ns = ns
                screen.feed(raw[offset:offset + length])
                position += 1
            frames, slice_start = [], position
            previous_ns = None
            frame_metrics = dict(screen.metrics)
            cursor_positions = []
            while position < len(reads) and reads[position][0] <= last:
                _, ns, offset, length = reads[position]
                if previous_ns is not None and ns - previous_ns > FRAME_GAP_NS:
                    frames.append(self._frame_delta(screen, frame_metrics))
                    cursor_positions.append((screen.x, screen.y, screen.cursor_visible))
                    frame_metrics = dict(screen.metrics)
                    screen.new_frame()
                screen.now_ns = ns
                screen.feed(raw[offset:offset + length])
                previous_ns = ns
                position += 1
            if previous_ns is not None:
                frames.append(self._frame_delta(screen, frame_metrics))
                cursor_positions.append((screen.x, screen.y, screen.cursor_visible))
                screen.new_frame()
            phase_bytes = b"".join(raw[r[2]:r[2] + r[3]] for r in reads[slice_start:position])
            (self.j.output / f"recording-{name}.raw").write_bytes(phase_bytes[:RECORDING_LIMIT])
            (self.j.output / f"recording-{name}-end.html").write_text(screen.html(f"{name} end"))
            cells = cols * rows
            results[name] = {
                "frames": len(frames),
                "bytes": len(phase_bytes),
                "retained_bytes": min(len(phase_bytes), RECORDING_LIMIT),
                "full_clears": sum(f["full_clears"] for f in frames),
                "writes": sum(f["writes"] for f in frames),
                "identical_rewrites": sum(f["identical_rewrites"] for f in frames),
                "worst_identical_fraction": max((f["identical_rewrites"] / cells for f in frames), default=0.0),
                "mean_identical_fraction": (sum(f["identical_rewrites"] for f in frames) / cells / len(frames)) if frames else 0.0,
                "blinks": sum(f["blinks"] for f in frames),
                "idle_cursor_moves": sum(f["idle_cursor_moves"] for f in frames),
                "scrolls": sum(f["scrolls"] for f in frames),
                "cursor_positions": sorted(set(cursor_positions))[:8],
                "cursor_hidden_throughout": all(not c[2] for c in cursor_positions) if cursor_positions else None,
            }
        return results

    @staticmethod
    def _frame_delta(screen: vt.Screen, before: dict) -> dict:
        return {k: screen.metrics[k] - before[k] for k in before}


def resize_window(j: Case, label: str, cols: int, rows: int) -> None:
    assert j.observer
    j.observer.resize(label, cols, rows)
    time.sleep(1.5)


@case("u6-flicker", ("U6.a", "U6.b", "U6.c", "U6.d", "U6.e", "U6.f"), columns=(80, 140), seconds=120)
def flicker(j: Case) -> None:
    j.cli("init", "--no-agent")
    j.cli("spawn", "alpha", "--no-agent")
    j.cli("spawn", "beta", "--no-agent")
    rows = 40 if j.columns > 80 else 24
    j.open()
    j.keys(b"1")
    j.see_all("[Work]", "alpha", "beta")
    time.sleep(1.0)
    recording = Recording(j)
    # Idle on the Work list for 30 s: only clock/status may change.
    selected_before = j.selected_row()
    recording.mark("idle-start")
    time.sleep(IDLE_SECONDS)
    recording.mark("idle-end")
    selected_after = j.selected_row()
    # Streaming: attach to alpha and let its shell print 300 lines.
    j.keys(b"a")
    j.see("Typing goes to the agent")
    j.attached = True
    time.sleep(1.0)
    recording.mark("stream-start")
    j.tmux("send-keys", "-t", "alpha", "for i in $(seq 1 300); do echo streamed line $i; sleep 0.02; done", "Enter")
    time.sleep(9)
    recording.mark("stream-end")
    j.keys(b"\x02\t")
    j.attached = False
    j.see("Keys go to Hydra")
    j.keys(b"\x02x")  # close the agent view so later pane cycling stays in Hydra
    time.sleep(0.5)
    j.keys(b"\x1b\x1b1")
    j.see("[Work]")
    # Polling: local state changes made by the CLI while the UI refreshes.
    time.sleep(1.0)
    recording.mark("poll-start")
    j.cli("spawn", "gamma", "--no-agent")
    time.sleep(3)
    j.cli("outcome", "beta", "done", "--summary", "finished")
    time.sleep(3)
    recording.mark("poll-end")
    j.see("gamma")
    # Pane switches inside the Workspace and across views.
    j.keys(b"W")
    j.see("[Workspace]")
    time.sleep(0.5)
    recording.mark("switch-start")
    for _ in range(6):
        j.keys(b"\t")
        time.sleep(0.4)
    for key in (b"3", b"4", b"2", b"1"):
        j.keys(key)
        time.sleep(0.5)
    recording.mark("switch-end")
    # Resize twice (80x24 <-> 140x40) and settle.
    recording.mark("resize-start")
    resize_window(j, "resize-1", 140 if j.columns == 80 else 80, 40 if j.columns == 80 else 24)
    resize_window(j, "resize-2", j.columns, rows)
    recording.mark("resize-end")
    j.see("[Work]")

    stats = recording.analyse([("idle", "idle-start", "idle-end"), ("stream", "stream-start", "stream-end"),
                               ("poll", "poll-start", "poll-end"), ("switch", "switch-start", "switch-end"),
                               ("resize", "resize-start", "resize-end")], j.columns, rows)
    evidence = j.keep_json("flicker-metrics", {"columns": j.columns, "rows": rows, "frame_gap_ns": FRAME_GAP_NS,
                                               "thresholds": {"identical_fraction_limit": IDENTICAL_FRACTION_LIMIT,
                                                              "identical_mean_limit": IDENTICAL_MEAN_LIMIT},
                                               "phases": stats})
    idle = stats["idle"]
    j.record("U6.a", f"idle {IDLE_SECONDS}s: zero full-screen clears over {idle['frames']} refreshes",
             idle["full_clears"] == 0, detail=json.dumps(idle), evidence=[evidence, "recording-idle.raw"])
    j.record("U6.a", "idle: unchanged content is not repainted beyond the bound",
             idle["worst_identical_fraction"] <= IDENTICAL_FRACTION_LIMIT
             and idle["mean_identical_fraction"] <= IDENTICAL_MEAN_LIMIT,
             detail=f"worst {idle['worst_identical_fraction']:.3f} mean {idle['mean_identical_fraction']:.4f} of screen cells")
    j.record("U6.a", "idle: nothing is erased and painted back; the selection is preserved; the cursor stays hidden",
             idle["blinks"] == 0 and idle["idle_cursor_moves"] == 0 and selected_before == selected_after
             and selected_before != "" and idle["cursor_hidden_throughout"] in (True, None),
             detail=f"selected {selected_before!r} -> {selected_after!r}; cursor positions {idle['cursor_positions']}")
    for name, bullet, label in (("stream", "U6.b", "streaming agent output"), ("poll", "U6.c", "polling state"),
                                ("switch", "U6.d", "pane and view switches")):
        phase = stats[name]
        j.record(bullet, f"{label}: zero full-screen clears over {phase['frames']} refreshes",
                 phase["full_clears"] == 0, detail=json.dumps(phase), evidence=[evidence, f"recording-{name}.raw"])
        j.record(bullet, f"{label}: no content is erased and painted back (blink) and no cursor jump paints nothing",
                 phase["blinks"] == 0 and phase["idle_cursor_moves"] == 0,
                 detail=f"blinks {phase['blinks']}, idle cursor moves {phase['idle_cursor_moves']}")
        if name != "switch":
            j.record(bullet, f"{label}: unchanged content is not repainted beyond the bound",
                     phase["worst_identical_fraction"] <= IDENTICAL_FRACTION_LIMIT
                     and phase["mean_identical_fraction"] <= IDENTICAL_MEAN_LIMIT,
                     detail=f"worst {phase['worst_identical_fraction']:.3f} mean {phase['mean_identical_fraction']:.4f}")
        else:
            j.note(f"switch phase repaints {phase['mean_identical_fraction']:.3f} of cells unchanged on average; "
                   "a view change legitimately repaints whole panels, so only clears and blinks are asserted")
    resize = stats["resize"]
    j.record("U6.e", "resize: at most one full repaint per resize (two resizes)",
             resize["full_clears"] <= 2, detail=json.dumps(resize), evidence=[evidence, "recording-resize.raw"])
    j.record("U6.e", "resize: beyond the full repaint nothing is erased and painted back",
             resize["blinks"] == 0, detail=f"blinks {resize['blinks']}")
    retained = [f"recording-{name}.raw" for name in stats]
    j.record("U6.f", "the raw terminal recording of every phase is retained (bounded to 2 MiB each) with per-refresh metrics",
             all((j.output / name).exists() for name in retained)
             and all((j.output / name).stat().st_size <= RECORDING_LIMIT for name in retained),
             detail=", ".join(f"{name}: {stats[name.split('-')[1].split('.')[0]]['retained_bytes']} bytes" for name in retained),
             evidence=[evidence, *retained, *[f"recording-{name}-end.html" for name in stats]])
