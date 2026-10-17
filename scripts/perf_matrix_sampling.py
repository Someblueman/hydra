"""Scheduled process-tree sampling with per-role attribution and honest tails."""

from __future__ import annotations

import os
import statistics
import time
from pathlib import Path
from typing import Any

from bench_i1_actions import wait_until
from bench_i1_process import ProcessCounters, cpu_seconds

ROLE_FAMILIES = {
    "native-tui": "hydra_tui",
    "pty-observer": "observer",
    "tmux-server": "tmux",
    "pane-shell": "tmux",
    "agent": "agents",
    "execution-owner": "hydra_headless_owner",
    "controller": "observer",
}


def distribution(values: list[float]) -> dict[str, Any]:
    """Median always; p95 from 20 samples, p99 from 100; never fabricate tails."""
    clean = [value for value in values if value is not None]
    if not clean:
        return {"n": 0, "min": None, "median": None, "p95": None, "p99": None, "max": None,
                "tail_reason": "no observations"}
    ordered = sorted(clean)

    def percentile(fraction: float) -> float:
        index = max(0, min(len(ordered) - 1, round(fraction * (len(ordered) - 1))))
        return ordered[index]

    return {
        "n": len(ordered),
        "min": ordered[0],
        "median": statistics.median(ordered),
        "p95": percentile(0.95) if len(ordered) >= 20 else None,
        "p99": percentile(0.99) if len(ordered) >= 100 else None,
        "max": ordered[-1],
        "tail_reason": None if len(ordered) >= 100 else
        "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound",
    }


def _system() -> dict[str, Any]:
    forks = switches = None
    try:
        for line in Path("/proc/stat").read_text().splitlines():
            if line.startswith("processes "):
                forks = int(line.split()[1])
            elif line.startswith("ctxt "):
                switches = int(line.split()[1])
    except OSError:
        pass
    return {"forks_since_boot": forks, "context_switches_since_boot": switches,
            "loadavg": os.getloadavg()}


class WindowSampler:
    """Fixed-schedule samples; the schedule never waits for anything observed."""

    def __init__(self, roots: dict[int, str], interval_ns: int = 500_000_000) -> None:
        self.counters = ProcessCounters()
        self.roots = roots
        self.interval_ns = interval_ns

    def sample(self, scheduled_ns: int) -> dict[str, Any]:
        return {"scheduled_ns": scheduled_ns, "sampled_ns": time.monotonic_ns(),
                "processes": self.counters.tree(self.roots), "system": _system(),
                "sample_end_ns": time.monotonic_ns()}

    def run(self, start_ns: int, end_ns: int) -> list[dict[str, Any]]:
        samples = []
        target = start_ns
        while target <= end_ns:
            wait_until(target)
            samples.append(self.sample(target))
            target += self.interval_ns
        return samples


def role_map(samples: list[dict[str, Any]]) -> dict[int, str]:
    roles: dict[int, str] = {}
    for sample in samples:
        for pid, row in sample["processes"].items():
            roles.setdefault(int(pid), row["role"])
    return roles


def _delta(first: dict[str, Any], last: dict[str, Any], name: str) -> int | None:
    before, after = first.get(name), last.get(name)
    if before is None or after is None or after < before:
        return None
    return after - before


def summarize_window(samples: list[dict[str, Any]]) -> dict[str, Any]:
    """Per process episode and per role: CPU, memory, descriptors, wakeups."""
    if len(samples) < 2:
        return {"error": "fewer than two samples", "roles": {}, "processes": []}
    span_s = (samples[-1]["sampled_ns"] - samples[0]["sampled_ns"]) / 1e9
    episodes: dict[str, list[dict[str, Any]]] = {}
    for sample in samples:
        for row in sample["processes"].values():
            if row.get("available"):
                episodes.setdefault(f"{row['pid']}:{row['identity_start']}", []).append(row)
    processes = []
    for rows in episodes.values():
        first, last = rows[0], rows[-1]
        seconds = cpu_seconds(first, last) if len(rows) > 1 else None
        tick_ns = last["cpu_tick_ns_numer"] / last["cpu_tick_ns_denom"]
        reaped = _delta(first, last, "reaped_children_cpu_ticks")
        processes.append({
            "pid": first["pid"], "role": first["role"], "samples": len(rows),
            "present_whole_window": len(rows) == len(samples),
            "cpu_seconds": seconds,
            "reaped_children_cpu_seconds": reaped * tick_ns / 1e9 if reaped is not None else None,
            "max_resident_bytes": max(row["resident_bytes"] for row in rows),
            "peak_rss_bytes": last.get("peak_rss_bytes"),
            "max_fds": max((row["fds"] for row in rows if row.get("fds") is not None), default=None),
            "threads": last.get("threads"),
            "voluntary_switches": _delta(first, last, "voluntary_ctxt_switches"),
            "nonvoluntary_switches": _delta(first, last, "nonvoluntary_ctxt_switches"),
        })
    roles: dict[str, dict[str, Any]] = {}
    for row in processes:
        family = row["role"].split("/")[0].split("#")[0]
        group = roles.setdefault(family, {
            "processes": 0, "cpu_seconds": 0.0, "cpu_unknown_processes": 0,
            "reaped_children_cpu_seconds": 0.0, "voluntary_switches": 0,
            "nonvoluntary_switches": 0, "wakeups_unknown_processes": 0,
            "max_fds": 0, "peak_rss_bytes": 0,
        })
        group["processes"] += 1
        if row["cpu_seconds"] is None:
            group["cpu_unknown_processes"] += 1
        else:
            group["cpu_seconds"] += row["cpu_seconds"]
        if row["reaped_children_cpu_seconds"] is not None:
            group["reaped_children_cpu_seconds"] += row["reaped_children_cpu_seconds"]
        if row["voluntary_switches"] is None:
            group["wakeups_unknown_processes"] += 1
        else:
            group["voluntary_switches"] += row["voluntary_switches"]
            group["nonvoluntary_switches"] += row["nonvoluntary_switches"] or 0
        group["max_fds"] = max(group["max_fds"], row["max_fds"] or 0)
        group["peak_rss_bytes"] = max(group["peak_rss_bytes"], row["peak_rss_bytes"] or 0)
    # Resident memory summed per tick, so the maximum of the sum is reported.
    for family, group in roles.items():
        sums = []
        for sample in samples:
            sums.append(sum(row["resident_bytes"] for row in sample["processes"].values()
                            if row.get("available") and row["role"].split("/")[0].split("#")[0] == family))
        group["max_summed_resident_bytes"] = max(sums)
        group["cpu_ms_per_second"] = 1000 * group["cpu_seconds"] / span_s if span_s else None
        group["cpu_percent_one_core"] = 100 * group["cpu_seconds"] / span_s if span_s else None
        group["cpu_percent_machine"] = (100 * group["cpu_seconds"] / span_s / (os.cpu_count() or 1)
                                        if span_s else None)
        group["wakeups_per_second"] = (group["voluntary_switches"] / span_s
                                       if span_s and not group["wakeups_unknown_processes"] else None)
    first_sys, last_sys = samples[0]["system"], samples[-1]["system"]
    forks = _delta(first_sys, last_sys, "forks_since_boot")
    return {
        "span_seconds": span_s,
        "samples": len(samples),
        "logical_cpus": os.cpu_count(),
        "normalization": "cpu_percent_one_core: 100 = one busy logical CPU; cpu_percent_machine divides by logical CPUs",
        "wakeups_note": "voluntary context switches per second from /proc/<pid>/status; a sleep-then-wake proxy, not a hardware wakeup counter",
        "roles": roles,
        "processes": processes,
        "process_count_max": max(len([r for r in s["processes"].values() if r.get("available")]) for s in samples),
        "system_forks": forks,
        "system_forks_per_second": forks / span_s if forks is not None and span_s else None,
        "system_forks_scope": "whole machine (shared host), an upper bound on fixture launches",
        "loadavg_start": first_sys["loadavg"],
        "loadavg_end": last_sys["loadavg"],
        "sampling_cost_ns": distribution([s["sample_end_ns"] - s["sampled_ns"] for s in samples]),
        "schedule_lateness_ns": distribution([s["sampled_ns"] - s["scheduled_ns"] for s in samples]),
    }


def family_totals(summary: dict[str, Any]) -> dict[str, Any]:
    """Collapse roles into the attribution families named by the roadmap."""
    totals: dict[str, dict[str, float | None]] = {}
    for role, group in summary.get("roles", {}).items():
        family = ROLE_FAMILIES.get(role, "other")
        entry = totals.setdefault(family, {"cpu_seconds": 0.0, "max_summed_resident_bytes": 0.0,
                                           "reaped_children_cpu_seconds": 0.0})
        entry["cpu_seconds"] += group["cpu_seconds"]
        entry["max_summed_resident_bytes"] += group["max_summed_resident_bytes"]
        entry["reaped_children_cpu_seconds"] += group["reaped_children_cpu_seconds"]
    span = summary.get("span_seconds") or None
    for entry in totals.values():
        entry["cpu_percent_one_core"] = 100 * entry["cpu_seconds"] / span if span else None
    return {
        "families": totals,
        "helpers_note": "helper CPU (hydra CLI adapter, git, tmux client calls) appears as the launcher's "
        "reaped_children_cpu_seconds; unreaped or orphaned helpers are unknown",
    }
