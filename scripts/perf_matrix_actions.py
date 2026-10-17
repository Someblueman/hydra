"""Scheduled interaction stimuli and display-update polling for one client."""

from __future__ import annotations

import json
import re
import threading
import time
from pathlib import Path
from typing import Any

from bench_i1_actions import scheduled_status, wait_until
from bench_i1_pty import Observer

IDENTITY = (1, 0, len("HYDRA /"), "HYDRA /")
DEADLINE_NS = 4_000_000_000
CHANGED = re.compile(r"/ (\d+) changed files?(?!\w)")


class ClientState:
    """What the Work view looks like now, so each stimulus has a fresh predicate."""

    def __init__(self, attached: bool = False) -> None:
        self.view = "work"
        self.columns, self.rows = 120, 40
        self.attached = attached


def _step(state: ClientState, kind: str, tag: str) -> tuple[bytes | tuple[int, int], tuple[int, int, int, str], str]:
    if kind == "navigation":
        if state.attached:
            state.attached, state.view = False, "work"
            return b"\x02x\x1b", (1, 1, 6, "[Work]"), "close terminal client and return to heads"
        if state.view == "work":
            state.view = "details"
            return b"\n", (7, 1, 9, "[Details]"), "Details tab applied"
        state.view = "work"
        return b"1", (1, 1, 6, "[Work]"), "Work tab applied"
    if kind == "search":
        text = f"Search: {tag}"
        return f"/{tag}\n".encode(), (1, state.rows - 2, len(text), text), (
            "accepted query echoed in status line; result completeness is unmeasured")
    if kind == "resize":
        if (state.columns, state.rows) == (120, 40):
            state.columns, state.rows = 100, 32
        else:
            state.columns, state.rows = 120, 40
        return (state.columns, state.rows), (state.columns - 2, 2, 1, "+"), "new-width top border applied"
    text = "Live"
    state.view = "work"  # Escape leaves the Details tab as well as the query.
    return b"\x1b", (1, state.rows - 2, len(text), text), (
        "query cleared and the live status line restored; worker cancellation is unmeasured")


def run_actions(client: Observer, state: ClientState, start_ns: int, gap_ns: int,
                tag: str, output: Path) -> list[dict[str, Any]]:
    """Four stimuli on an absolute schedule; responses never move the schedule."""
    actions: list[dict[str, Any]] = []
    for index, kind in enumerate(("navigation", "search", "resize", "cancel")):
        target = start_ns + index * gap_ns
        keys, response, meaning = _step(state, kind, tag)
        identifier = f"{tag}-{kind}"
        wait_until(target)
        client.expect(identifier, IDENTITY, response)
        sent = (client.resize(identifier, *keys) if isinstance(keys, tuple)
                else client.input(identifier, keys))
        actions.append({"id": identifier, "kind": kind, "scheduled_ns": target,
                        "scheduled_deadline_ns": target + DEADLINE_NS, "controller_sent_ns": sent,
                        "response": response, "deadline_ms": DEADLINE_NS // 1_000_000,
                        "meaning": meaning, "boundary": "PTY frame completion of the matching applied region; not input-to-pixel"})
    wait_until(start_ns + 3 * gap_ns + DEADLINE_NS + 100_000_000)
    for action in actions:
        relevant = [record for record in client.records if record.get("id") == action["id"]]
        observed = [record for record in relevant if record["event"] == "observed"]
        action["receipts"] = relevant
        action["status"] = scheduled_status(action["scheduled_deadline_ns"], observed)
        action["scheduled_to_observed_ns"] = observed[0]["observed_ns"] - action["scheduled_ns"] if observed else None
        action["input_to_observed_ns"] = observed[0]["observed_ns"] - observed[0]["input_ns"] if observed else None
        action["controller_lateness_ns"] = action["controller_sent_ns"] - action["scheduled_ns"]
        if any(record["event"] == "invalid-expectation" for record in relevant):
            action["status"] = "invalid-predicate"
        if not any(record["event"] in ("input", "resize") for record in relevant):
            action["status"] = "injection-unconfirmed"
        if action["status"] != "observed":
            try:
                screen, _ = client.snapshot()
                action["screen_after_miss"] = screen
            except (OSError, RuntimeError, TimeoutError) as error:
                action["screen_after_miss"] = repr(error)
    output.write_text(json.dumps(actions, indent=2) + "\n")
    return actions


def restore_work_view(client: Observer, state: ClientState, timeout: float = 8) -> None:
    """Untimed: back to the Work view at 120x40 so the next case starts equal."""
    if state.attached:
        client.input("restore-detach", b"\x02x\x1b")
        state.attached = False
    screen, _ = client.snapshot()
    if "[Work]" not in screen:
        client.input("restore-view", b"1")
    client.visible("[Work]", timeout=timeout)
    if (state.columns, state.rows) != (120, 40):
        client.resize("restore-size", 120, 40)
    state.view, state.columns, state.rows = "work", 120, 40
    client.visible("Live", timeout=timeout)


class ChangePoller:
    """Poll applied screens; map each visible change-count step to its change event."""

    def __init__(self, client: Observer, interval_ns: int = 100_000_000) -> None:
        self.client, self.interval_ns = client, interval_ns
        self.frames: list[dict[str, Any]] = []
        self.error: str | None = None
        self.thread: threading.Thread | None = None
        self.stop_ns = 0

    def _poll(self) -> None:
        try:
            target = time.monotonic_ns()
            while time.monotonic_ns() < self.stop_ns:
                wait_until(target)
                screen, record = self.client.snapshot()
                match = CHANGED.search(screen)
                self.frames.append({"polled_ns": target, "observed_ns": record["observed_ns"],
                                    "sequence": record["sequence"],
                                    "changed_files": int(match.group(1)) if match else None})
                (self.client.directory / f"snapshot-{record['id']}.txt").unlink(missing_ok=True)
                target += self.interval_ns
        except (OSError, RuntimeError, TimeoutError, ValueError) as error:
            self.error = repr(error)

    def start(self, stop_ns: int) -> None:
        self.stop_ns = stop_ns
        self.thread = threading.Thread(target=self._poll, daemon=True)
        self.thread.start()

    def finish(self, changes: list[dict[str, Any]]) -> dict[str, Any]:
        if self.thread:
            self.thread.join(timeout=10)
        steps, previous = [], None
        for frame in self.frames:
            if frame["changed_files"] is not None and frame["changed_files"] != previous:
                if previous is not None:
                    steps.append(frame)
                previous = frame["changed_files"]
        latencies, matched = [], []
        for step in steps:
            candidates = [row for row in changes if row["monotonic_ns"] <= step["observed_ns"]
                          and row["version"] == step["changed_files"]]
            if not candidates:
                matched.append({"frame": step, "change": None, "reason": "no change event with that count before the frame"})
                continue
            change = candidates[-1]
            earlier = [f for f in self.frames if f["observed_ns"] < step["observed_ns"]]
            lower = max(change["monotonic_ns"], earlier[-1]["observed_ns"]) if earlier else change["monotonic_ns"]
            latency = step["observed_ns"] - change["monotonic_ns"]
            exact = bool(earlier) and step["sequence"] == earlier[-1]["sequence"] + 1
            latencies.append(latency)
            matched.append({"frame": step, "change": change, "latency_ns": latency,
                            "latency_lower_bound_ns": latency if exact else lower - change["monotonic_ns"],
                            "single_read_between_polls": exact})
        observed_versions = {row["change"]["version"] for row in matched if row.get("change")}
        unseen = [row for row in changes if row["version"] not in observed_versions]
        return {
            "frames_polled": len(self.frames),
            "frames": [[f["observed_ns"], f["sequence"], f["changed_files"]] for f in self.frames],
            "poll_interval_ns": self.interval_ns,
            "poll_error": self.error,
            "display_region": "selected head detail line: 'N changed files'",
            "boundary": "worker write receipt to the first polled applied frame showing the new count; the frame may have been applied up to one poll interval earlier (lower bound given); detection/collection/model/render split: unknown (the TUI exposes no timestamps)",
            "steps": matched,
            "latency_ns": latencies,
            "changes_declared": len(changes),
            "changes_not_seen_in_window": len(unseen),
            "unseen_note": "changes near the window end may be shown after polling stopped; the 2 s refresh period bounds staleness only if the adapter completes within it",
        }
