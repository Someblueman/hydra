#!/usr/bin/env python3
"""Perf matrix unit checks: shim records, tails, window summaries, stimuli and schema."""

from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

from perf_matrix_actions import ClientState, _step
from perf_matrix_report import validate_bundle
from perf_matrix_sampling import distribution, summarize_window
from perf_matrix_shim import parse_records, summarize_launches


def shim_checks() -> None:
    raw = (b"10.00\tstart\tgit\t500\t400,300,1\t-C /wt/a status --short\n"
           b"10.01\tstart\thydra\t501\t300,1\ttui --data\n"
           b"garbage line\n"
           b"10.05\tend\thydra\t501\t300,1\t0\n"
           b"10.06\tstart\ttmux\t502\t300,1\t-S /s capture-pane -p -t sess-b\n"
           b"10.07\tstart\tgit\t503\t999,1\t-C /wt/b status --short\n")
    records = parse_records(raw, 5)
    assert sum("malformed" in row for row in records) == 1
    assert records[0]["monotonic_ns"] == 10_000_000_005 and records[0]["chain"] == [400, 300, 1]
    summary = summarize_launches(records, {300: "native-tui#0/child"}, {"/wt/a": "a", "/wt/b": "b"},
                                 {"sess-b": "b"}, 30.0)
    assert summary["by_helper"] == {"git": 2, "hydra": 1, "tmux": 1}, summary
    assert summary["by_launcher_role"]["native-tui#0"] == {"git": 1, "hydra": 1, "tmux": 1}
    assert summary["unattributed"] == 1 and summary["malformed_records"] == 1
    assert summary["per_worktree"] == {"a": {"git": 1, "tmux_capture": 0, "tmux_other": 0},
                                       "b": {"git": 1, "tmux_capture": 1, "tmux_other": 0}}
    assert summary["wrapped_durations_ns"]["hydra"]["n"] == 1 and summary["wrapped_durations_ns"]["hydra"]["max"] == 40_000_000
    assert summary["unfinished_wrapped"] == 0 and summary["per_minute"] == 8.0


def tail_checks() -> None:
    assert distribution([])["median"] is None
    small = distribution(list(range(19)))
    assert small["p95"] is None and small["p99"] is None and small["max"] == 18 and small["tail_reason"]
    assert distribution(list(range(20)))["p95"] == 18
    full = distribution(list(range(100)))
    assert full["p99"] == 98 and full["tail_reason"] is None


def _process(pid: int, role: str, ticks: int, rss: int, switches: int, reaped: int = 0) -> dict:
    return {"pid": pid, "available": True, "identity_start": 7, "role": role, "cpu_ticks": ticks,
            "cpu_tick_ns_numer": 1_000_000_000, "cpu_tick_ns_denom": 100, "resident_bytes": rss,
            "peak_rss_bytes": rss, "fds": 5, "threads": 1, "voluntary_ctxt_switches": switches,
            "nonvoluntary_ctxt_switches": 0, "reaped_children_cpu_ticks": reaped}


def window_checks() -> None:
    samples = []
    for tick in range(5):
        processes = {
            "1": _process(1, "native-tui#0", 10 * tick, 1000 + tick, 3 * tick, 20 * tick),
            "2": _process(2, "tmux-server", 0, 500, 0),
            "3": _process(3, "tmux-server/child", 2 * tick, 100, 0),
        }
        processes["3"]["voluntary_ctxt_switches"] = None
        samples.append({"scheduled_ns": tick * 10**9, "sampled_ns": tick * 10**9 + 1000, "sample_end_ns": tick * 10**9 + 2000,
                        "processes": processes,
                        "system": {"forks_since_boot": 100 + tick, "context_switches_since_boot": 0, "loadavg": (0, 0, 0)}})
    summary = summarize_window(samples)
    tui = summary["roles"]["native-tui"]
    assert abs(tui["cpu_seconds"] - 0.4) < 1e-9 and abs(tui["cpu_percent_one_core"] - 10.0) < 1e-6, tui
    assert abs(tui["reaped_children_cpu_seconds"] - 0.8) < 1e-9
    assert abs(tui["wakeups_per_second"] - 3.0) < 1e-6 and tui["max_summed_resident_bytes"] == 1004
    tmux = summary["roles"]["tmux-server"]
    assert tmux["processes"] == 2 and tmux["wakeups_per_second"] is None and tmux["wakeups_unknown_processes"] == 1
    assert tmux["max_summed_resident_bytes"] == 600
    assert summary["system_forks"] == 4 and summary["process_count_max"] == 3


def stimulus_checks() -> None:
    state = ClientState()
    keys, response, _ = _step(state, "navigation", "t")
    assert keys == b"\n" and response == (7, 1, 9, "[Details]") and state.view == "details"
    keys, response, _ = _step(state, "search", "t")
    assert response == (1, 38, len("Search: t"), "Search: t")
    keys, response, _ = _step(state, "resize", "t")
    assert keys == (100, 32) and response == (98, 2, 1, "+")
    keys, response, _ = _step(state, "cancel", "t")
    assert keys == b"\x1b" and response == (1, 30, 4, "Live") and state.view == "work"
    keys, response, _ = _step(state, "resize", "t")
    assert keys == (120, 40) and response == (118, 2, 1, "+")
    attached = ClientState(attached=True)
    keys, response, _ = _step(attached, "navigation", "t")
    assert keys == b"\x02x\x1b" and response == (1, 1, 6, "[Work]") and not attached.attached


def _window() -> dict:
    return {"processes": {"roles": {"native-tui": {"wakeups_per_second": 1.0, "wakeups_unknown_processes": 0},
                                    "agent": {"wakeups_per_second": None, "wakeups_unknown_processes": 1}},
                          "span_seconds": 3.0},
            "families": {}, "launches": {"per_minute": 20.0}}


def schema_checks() -> None:
    cell = {"status": "measured", "phase": "p1", "windows": {name: _window() for name in ("settle", "idle", "interaction")},
            "actions": [{"id": f"p1-{k}", "kind": k, "status": "observed", "input_to_observed_ns": 1000}
                        for k in ("navigation", "search", "resize", "cancel")],
            "unknowns": {"update_latency": "no repository changes in this case"},
            "pressure": {"coalesced_updates": None, "coalesced_updates_reason": "not exposed"},
            "locality": {"git_launches_per_unchanged_worktree": []}, "workload_proof": [{}]}
    bundle = {key: {} for key in ("platform", "declared", "durations", "attribution_boundaries")}
    bundle.update(schema="hydra-perf-matrix/1", generated_utc="now", variants=[], planned_cells=[],
                  unsupported_cells=[{"status": "unmeasured", "reason": "stated"}],
                  fixtures=[{"id": "f", "cleanup_ok": True, "shim_overhead": {}, "provisioning": {}, "errors": [], "cells": [cell]}])
    assert validate_bundle(bundle) == [], validate_bundle(bundle)
    broken = copy.deepcopy(bundle)
    broken["fixtures"][0]["cells"][0]["windows"]["idle"]["processes"]["roles"]["agent"]["wakeups_unknown_processes"] = 0
    broken["fixtures"][0]["cells"][0]["pressure"]["coalesced_updates_reason"] = ""
    broken["fixtures"][0]["cells"][0]["actions"][0]["status"] = "ok"
    del broken["fixtures"][0]["cells"][0]["unknowns"]["update_latency"]
    broken["unsupported_cells"].append({"status": "unsupported"})
    problems = validate_bundle(broken)
    assert len(problems) == 5, problems
    assert any("wakeups null" in p for p in problems) and any("coalesced" in p for p in problems)
    assert any("status ok" in p for p in problems) and any("update latency" in p for p in problems)


def main() -> None:
    output = Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)
    shim_checks()
    tail_checks()
    window_checks()
    stimulus_checks()
    schema_checks()
    (output / "units.json").write_text(json.dumps({"passed": ["shim", "tails", "window", "stimuli", "schema"]}) + "\n")
    print("PASS perf matrix units: shim attribution, guarded tails, window summary, stimulus plan, schema")


if __name__ == "__main__":
    main()
