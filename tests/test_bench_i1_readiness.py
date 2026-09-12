#!/usr/bin/env python3
"""Reject a real native snapshot that completes after a future readiness deadline."""

from __future__ import annotations

import json
import os
import signal
import sys
import threading
import time
from pathlib import Path

from bench_i1_pty import Observer, owned_heads_visible


def retained_screen_controls(root: Path, output: Path) -> None:
    screens = {}
    for count in (1, 10, 50):
        screen = (root / f"tests/fixtures/tui/item10-ready-{count}.txt").read_text()
        branches = {f"i10-h{index:02d}" for index in range(count)}
        assert owned_heads_visible(screen, True, branches), count
        screens[count] = screen
    screen = screens[50]
    branches = {f"i10-h{index:02d}" for index in range(50)}
    assert "i10-h00" not in screen
    negatives = {
        "wrong-count": screen.replace("50 heads", "49 heads", 1),
        "unknown-row": screen.replace("i10-h09", "not-owned", 1),
        "duplicate-row": screen.replace("i10-h09", "i10-h29", 1),
        "selected-unknown": "not-owned".join(screen.rsplit("i10-h29", 1)),
    }
    for name, changed in negatives.items():
        assert not owned_heads_visible(changed, True, branches), name
    assert not owned_heads_visible(screen, False, branches)
    (output / "owned-head-predicate.json").write_text(
        json.dumps(
            {
                "positive_counts": [1, 10, 50],
                "worker0_offscreen_accepted": True,
                "rejected": [*negatives, "parser-incomplete"],
            },
            indent=2,
        )
        + "\n"
    )


def main() -> None:
    root, build, output, source = (Path(value).resolve() for value in sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    retained_screen_controls(root, output)
    client = Observer(
        build / "test-tui-pty",
        build / "hydra-tui",
        source / "tests/fixtures/tui/fake-hydra.sh",
        output / "late-readiness",
        root,
        {**os.environ, "HYDRA_HOME": str(output / "fake-home")},
    )
    resume: threading.Timer | None = None
    evidence: dict[str, object] = {}
    fake_branches = {"feature-live", "feature-stale", "feature-unavailable"}
    try:
        client.ready(fake_branches)
        client.process.send_signal(signal.SIGSTOP)
        stopped_pid, stopped_status = os.waitpid(client.process.pid, os.WUNTRACED)
        assert stopped_pid == client.process.pid and os.WIFSTOPPED(stopped_status)
        # The nested snapshot starts while the budget is still positive. Its
        # native response becomes possible only after the deadline has passed.
        timeout = (time.monotonic_ns() - client.started_ns) / 1e9 + 0.15
        deadline_ns = client.started_ns + int(timeout * 1e9)
        identifier = f"screen-{client.serial + 1}"
        resume = threading.Timer(0.35, client.process.send_signal, [signal.SIGCONT])
        resume.start()
        entered_ns = time.monotonic_ns()
        try:
            client.ready(fake_branches, timeout=timeout)
        except TimeoutError as error:
            rejected_ns = time.monotonic_ns()
            evidence["rejection"] = str(error)
        else:
            raise AssertionError("late nested snapshot was accepted as ready")
        resume.join(timeout=2)
        late = client.wait("snapshot", identifier, timeout=2)
        sent = next(
            json.loads(line)["sent_ns"]
            for line in (client.directory / "controller.jsonl").read_text().splitlines()
            if json.loads(line)["command"] == f"S {identifier}"
        )
        assert entered_ns <= sent < deadline_ns <= rejected_ns
        assert rejected_ns < late["controller_received_ns"], late
        # Resuming the observer can land midway through a redraw. Retain that
        # first late response, then require an actually matching screen too.
        _, matching = client.ready(
            fake_branches,
            timeout=(time.monotonic_ns() - client.started_ns) / 1e9 + 2,
        )
        assert matching["controller_received_ns"] > deadline_ns
        try:
            client.wait("snapshot", matching["id"], deadline_ns=deadline_ns)
        except TimeoutError:
            evidence["cached_late_receipt_rejected"] = True
        else:
            raise AssertionError("cached late snapshot bypassed the deadline")
        evidence.update(
            entered_ns=entered_ns,
            snapshot_sent_ns=sent,
            deadline_ns=deadline_ns,
            rejected_ns=rejected_ns,
            late_snapshot=late,
            matching_late_snapshot=matching,
        )
    finally:
        if resume is not None:
            resume.cancel()
            resume.join(timeout=2)
        if client.process.poll() is None:
            client.process.send_signal(signal.SIGCONT)
        cleanup = client.close()
        evidence["cleanup"] = cleanup
        (output / "readiness-deadline.json").write_text(
            json.dumps(evidence, indent=2) + "\n"
        )
    assert cleanup.get("terminal_restored") and cleanup.get("reaped"), cleanup
    assert not cleanup.get("forced") and cleanup["observer_exit"] == 0, cleanup
    print(
        "PASS future readiness deadline: native snapshot arrived late and was rejected"
    )


if __name__ == "__main__":
    main()
