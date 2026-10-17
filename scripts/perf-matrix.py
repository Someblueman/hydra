#!/usr/bin/env python3
"""Item 10 matrix runner over the I1 fixture: real heads, stand-in agents, exact evidence.

Cells are (heads, mode, clients, preview, case, variant, trial). Unsupported cells
are recorded, never silently skipped; missing counters stay null with a reason.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from bench_i1_provenance import identities, runtime_provenance
from perf_matrix_cell import CASES, Durations, FixtureRun
from perf_matrix_report import publish, validate_bundle, write_summary

ROOT = Path(__file__).resolve().parent.parent


def tool_versions() -> dict[str, str | None]:
    versions: dict[str, str | None] = {}
    for name, argv in (("git", ["git", "--version"]), ("tmux", ["tmux", "-V"]),
                       ("dash", ["sh", "-c", "echo ${0##*/}"]), ("cc", ["cc", "--version"])):
        try:
            versions[name] = subprocess.run(argv, capture_output=True, text=True, timeout=10,
                                            check=False).stdout.splitlines()[0]
        except (OSError, IndexError, subprocess.SubprocessError):
            versions[name] = None
    return versions


def declared_policy(source: Path) -> dict[str, Any]:
    refresh = None
    header = source / "src/tui/fleet_budget.h"
    if header.is_file():
        for line in header.read_text().splitlines():
            if "HYDRA_TUI_REFRESH_SECONDS" in line and "#define" in line:
                refresh = line.split()[2].rstrip("L")
    return {"tui_refresh_seconds": refresh, "change_interval_ns": 2_000_000_000,
            "output_line_bytes": 128, "default_output_interval_ns": 31_250_000, "workload_seed": 104729,
            "worker_phase_bound_ns": 30_000_000_000}


def plan_cells(args: argparse.Namespace) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Expand the requested lists; record combinations the harness cannot measure."""
    planned, unsupported = [], []
    for mode in args.modes:
        if mode == "remote-headless":
            unsupported.append({"mode": mode, "status": "unmeasured",
                                "reason": "no loopback receiver stand-in exists in this harness; a live verified fleet host is required"})
            continue
        for heads in args.heads:
            for clients in args.clients:
                for preview in args.preview:
                    for case in args.cases:
                        cell = {"heads": heads, "mode": mode, "clients": clients, "preview": preview, "case": case}
                        if mode == "headless" and preview == "open":
                            unsupported.append({**cell, "status": "unsupported",
                                                "reason": "headless workers have no terminal to attach"})
                        elif preview == "open" and clients != 1:
                            unsupported.append({**cell, "status": "not planned",
                                                "reason": "multi-client terminal attachment is outside this slice"})
                        elif heads == 50 and case not in args.cases_at_50:
                            unsupported.append({**cell, "status": "not planned", "reason": "50-head cells limited by --cases-at-50"})
                        elif preview == "open" and case not in ("quiescent", "sustained-output"):
                            unsupported.append({**cell, "status": "not planned",
                                                "reason": "preview-open cells cover quiescent and sustained-output only"})
                        else:
                            planned.append(cell)
    return planned, unsupported


def grouped(cells: list[dict[str, Any]]) -> dict[tuple[int, str], dict[tuple[int, str], list[str]]]:
    groups: dict[tuple[int, str], dict[tuple[int, str], list[str]]] = {}
    for cell in cells:
        fixture = groups.setdefault((cell["heads"], cell["mode"]), {})
        fixture.setdefault((cell["clients"], cell["preview"]), []).append(cell["case"])
    return groups


def run_variant(args: argparse.Namespace, variant: dict[str, Any], trial: int,
                groups: dict, durations: Durations, bundle: dict[str, Any]) -> None:
    for (heads, mode), client_groups in groups.items():
        label = f"{variant['name']}-t{trial}-h{heads}-{mode}"
        print(f"fixture {label}: provisioning {heads} {mode} heads", flush=True)
        run = FixtureRun(variant["source"], variant["build"], args.observer, args.output / label,
                         heads, mode, durations, label)
        run.record.update(variant=variant["name"], trial=trial, load_before=os.getloadavg())
        try:
            run.provision()
            for (clients, preview), cases in client_groups.items():
                observers, states = run.start_clients(clients, preview)
                try:
                    for case in cases:
                        meta = {"heads": heads, "mode": mode, "clients": clients, "preview": preview,
                                "variant": variant["name"], "trial": trial, "fixture": label}
                        print(f"  cell {heads}/{mode}/{clients}c/{preview}/{case}", flush=True)
                        run.run_case(observers, states, case, meta, args.output_interval_ns)
                finally:
                    run.close_clients(observers, states)
        except (OSError, RuntimeError, ValueError, TimeoutError, subprocess.SubprocessError) as error:
            run.record["errors"].append(repr(error))
            print(f"  fixture error: {error!r}", flush=True)
        finally:
            run.finish()
            run.record["load_after"] = os.getloadavg()
            bundle["fixtures"].append(run.record)
            (args.output / label / "fixture.json").write_text(json.dumps(run.record, indent=1) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--observer", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="new directory for raw evidence")
    parser.add_argument("--heads", type=int, nargs="+", choices=(1, 10, 50), default=[1, 10])
    parser.add_argument("--modes", nargs="+", choices=("interactive", "headless", "remote-headless"),
                        default=["interactive", "headless", "remote-headless"])
    parser.add_argument("--clients", type=int, nargs="+", choices=(1, 3), default=[1, 3])
    parser.add_argument("--preview", nargs="+", choices=("closed", "open"), default=["closed", "open"])
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    parser.add_argument("--cases-at-50", nargs="+", choices=CASES, default=["quiescent", "one-changing"])
    parser.add_argument("--settle", type=float, default=4.0, help="seconds sampled before each steady window")
    parser.add_argument("--idle", type=float, default=10.0, help="unstimulated steady seconds per case")
    parser.add_argument("--gap", type=float, default=2.0, help="seconds between the four scheduled stimuli")
    parser.add_argument("--poll-ms", type=int, default=100, help="display-update polling period")
    parser.add_argument("--output-interval-ns", type=int, help="override the 31.25 ms producer line interval")
    parser.add_argument("--trials", type=int, default=1)
    parser.add_argument("--candidate-source", type=Path, help="second runtime to alternate with the baseline")
    parser.add_argument("--candidate-build", type=Path)
    parser.add_argument("--attention", type=Path, help="existing attention qualification report.json to embed")
    parser.add_argument("--evidence-dir", type=Path, help="publish summary.md, bundle.json and bounded raw samples here")
    parser.add_argument("--raw-budget-bytes", type=int, default=1_500_000)
    parser.add_argument("--load-note", default="shared 4-core container; peers may run concurrently")
    args = parser.parse_args()
    args.source, args.build, args.output = args.source.resolve(), args.build.resolve(), args.output.resolve()
    args.observer = (args.observer or args.build / "test-tui-pty").resolve()
    if bool(args.candidate_source) != bool(args.candidate_build):
        parser.error("--candidate-source and --candidate-build go together")
    args.output.mkdir(parents=True)
    durations = Durations(args.settle, args.idle, args.gap, args.poll_ms)
    variants = [{"name": "baseline", "source": args.source, "build": args.build}]
    if args.candidate_source:
        variants.append({"name": "candidate", "source": args.candidate_source.resolve(),
                         "build": args.candidate_build.resolve()})
    planned, unsupported = plan_cells(args)
    started = time.monotonic_ns()
    bundle: dict[str, Any] = {
        "schema": "hydra-perf-matrix/1",
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "command": [sys.executable, *sys.argv],
        "platform": {"system": platform.system(), "release": platform.release(), "machine": platform.machine(),
                     "logical_cpus": os.cpu_count(), "python": sys.version.split()[0],
                     "clk_tck": os.sysconf("SC_CLK_TCK"), "page_size": os.sysconf("SC_PAGE_SIZE"),
                     "tools": tool_versions(), "load_note": args.load_note, "load_at_start": os.getloadavg()},
        "monotonic_clock": vars(time.get_clock_info("monotonic")),
        "variants": [{"name": v["name"], "source": str(v["source"]), "build": str(v["build"]),
                      "provenance": runtime_provenance(v["source"], None),
                      "identities": identities(v["source"], v["build"], args.observer, None)} for v in variants],
        "declared": declared_policy(args.source),
        "durations": {"settle_s": args.settle, "idle_s": args.idle, "stimulus_gap_s": args.gap,
                      "interaction_s": durations.interaction_ns / 1e9, "poll_ms": args.poll_ms,
                      "sample_interval_ms": 500, "stimulus_deadline_ms": 4000},
        "planned_cells": planned,
        "unsupported_cells": unsupported + [
            {"platform": "darwin", "status": "not measured", "reason": "this run executed on Linux only"}],
        "attribution_boundaries": {
            "hydra_tui": "native hydra-tui process per client (role native-tui#N)",
            "hydra_helpers": "hydra CLI adapter, hydra-core and hydra-fleet launches counted by PATH shims; their CPU is the launcher's reaped-children CPU",
            "tmux": "tmux server plus pane shells; tmux client calls are counted launches",
            "agents": "deterministic stand-in workers (role agent); headless owners are hydra exec processes",
            "observer": "PTY observer per client plus this controller (rusage); shim cost per launch is calibrated",
            "latency": "PTY frame completion of a matching applied region; never input-to-pixel",
        },
        "fixtures": [],
    }
    for trial in range(args.trials):
        order = variants if trial % 2 == 0 else list(reversed(variants))
        for variant in order:
            run_variant(args, variant, trial, grouped(planned), durations, bundle)
    bundle["attention"] = json.loads(args.attention.read_text()) if args.attention else None
    bundle["duration_seconds"] = (time.monotonic_ns() - started) / 1e9
    bundle["load_at_end"] = os.getloadavg()
    for variant, declared in zip(variants, bundle["variants"]):
        declared["identities_unchanged"] = identities(variant["source"], variant["build"], args.observer, None) == declared["identities"]
    problems = validate_bundle(bundle)
    bundle["validation"] = {"ok": not problems, "problems": problems}
    write_summary(bundle, args.output / "summary.md")
    (args.output / "bundle.json").write_text(json.dumps(bundle, indent=1) + "\n")
    if args.evidence_dir:
        publish(bundle, args.output, args.evidence_dir.resolve(), args.raw_budget_bytes)
    measured = sum(1 for f in bundle["fixtures"] for c in f["cells"] if c["status"] == "measured")
    clean = all(f.get("cleanup_ok") and not f["errors"] for f in bundle["fixtures"])
    print(f"cells measured: {measured}/{len(planned)}; cleanup ok: {clean}; validation ok: {not problems}")
    print(args.output / "summary.md")
    return 0 if clean and not problems and measured == len(planned) else 1


if __name__ == "__main__":
    raise SystemExit(main())
