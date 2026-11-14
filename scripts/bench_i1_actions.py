"""View actions scheduled from a fixed clock, with declared semantic predicates."""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any

from bench_i1_pty import Observer

# The control centre's top bar ("# hydra" in ASCII; the benchmark runs in the
# C locale) and the screens' texts at fixed cells, (column, row, length, text).
IDENTITY = (1, 0, len("# hydra"), "# hydra")
INBOX = (1, 2, len("Needs you"), "Needs you")
# The Work conversation's rule note once no terminal client is attached, at
# 120 columns (the note is right-aligned in the conversation pane).
WORK_PREVIEW = (48, 2, len("latest output / enter to talk"), "latest output / enter to talk")


def wait_until(target_ns: int) -> None:
    while (remaining := target_ns - time.monotonic_ns()) > 0:
        time.sleep(min(0.02, remaining / 1e9))


def scheduled_status(deadline_ns: int, observed: list[dict[str, Any]]) -> str:
    if not observed:
        return "miss"
    return (
        "observed"
        if observed[0]["observed_ns"] < deadline_ns
        else "scheduled-deadline-miss"
    )


def late_dispatch_control(client: Observer) -> dict[str, Any]:
    scheduled = time.monotonic_ns()
    deadline = scheduled + 4_000_000_000
    wait_until(deadline + 100_000_000)
    client.expect("late-dispatch", IDENTITY, INBOX)
    client.input("late-dispatch", b"2")
    observed = client.wait("observed", "late-dispatch")
    status = scheduled_status(deadline, [observed])
    if status != "scheduled-deadline-miss":
        raise RuntimeError(
            "late controller dispatch extended its scheduled response budget"
        )
    return {
        "scheduled_ns": scheduled,
        "scheduled_deadline_ns": deadline,
        "receipt": observed,
        "status": status,
    }


def view_actions(
    client: Observer, start_ns: int, output: Path, from_attachment: bool = False
) -> list[dict[str, Any]]:
    """Start on Work at 120x40; deadlines remain four seconds per input.

    navigation: Inbox (2), or from an attached agent: close its client and
    leave (the conversation shows the latest output again); search: back to
    Work (1) and filter the list, echoed in the status line; resize to 100x32
    moves that status line up; cancel: Esc clears the filter and the live
    status chip returns."""
    identity = IDENTITY
    tag = "I10SEARCH104729"
    plan = (
        (
            0,
            "navigation",
            b"\x1cx\x1b" if from_attachment else b"2",
            WORK_PREVIEW if from_attachment else INBOX,
        ),
        (
            4,
            "search",
            f"1/{tag}\n".encode(),
            (1, 38, len(f"Filter: {tag}"), f"Filter: {tag}"),
        ),
        (8, "resize", b"", (1, 30, len(f"Filter: {tag}"), f"Filter: {tag}")),
        (12, "cancel", b"\x1b", (2, 30, len("* live"), "* live")),
    )
    actions: list[dict[str, Any]] = []
    for offset, identifier, keys, response in plan:
        target = start_ns + offset * 1_000_000_000
        wait_until(target)
        client.expect(identifier, identity, response)
        sent = (
            client.resize(identifier, 100, 32)
            if identifier == "resize"
            else client.input(identifier, keys)
        )
        actions.append(
            {
                "id": identifier,
                "scheduled_ns": target,
                "scheduled_deadline_ns": target + 4_000_000_000,
                "controller_sent_ns": sent,
                "identity": identity,
                "response": response,
                "deadline_ms": 4000,
                "meaning": {
                    "navigation": "close terminal client and return to heads"
                    if from_attachment
                    else "Details tab applied",
                    "search": "accepted query echoed in status line; result completeness is unmeasured",
                    "resize": "new-width top border applied",
                    "cancel": "query cleared and the live status line restored; worker cancellation is unmeasured",
                }[identifier],
            }
        )
    wait_until(start_ns + 16_100_000_000)
    for action in actions:
        identifier = action["id"]
        relevant = [
            record for record in client.records if record.get("id") == identifier
        ]
        action["receipts"] = relevant
        observed = [record for record in relevant if record["event"] == "observed"]
        action["status"] = scheduled_status(action["scheduled_deadline_ns"], observed)
        action["scheduled_to_observed_ns"] = (
            observed[0]["observed_ns"] - action["scheduled_ns"] if observed else None
        )
        action["input_to_observed_ns"] = (
            observed[0]["observed_ns"] - observed[0]["input_ns"] if observed else None
        )
        if any(record["event"] == "invalid-expectation" for record in relevant):
            action["status"] = "invalid-predicate"
        if not any(record["event"] in ("input", "resize") for record in relevant):
            action["status"] = "injection-unconfirmed"
    output.write_text(json.dumps(actions, indent=2) + "\n")
    return actions
