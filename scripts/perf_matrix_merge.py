#!/usr/bin/env python3
"""Merge several perf-matrix run directories into one evidence bundle.

A baseline is often collected in more than one invocation (a fixture retried with a
different producer rate, the 50-head cells run on their own). Each run keeps its own
directory; this joins their bundles, re-validates, rewrites the summary and
proposed budgets over every measured cell, and publishes one compact bundle.
Nothing is re-measured and no cell is dropped: fixtures that failed stay in the
merged bundle with their errors.
"""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Any

from perf_matrix_report import publish, validate_bundle, write_summary

MERGED_KEYS = ("command", "generated_utc", "duration_seconds", "load_at_end", "validation",
               "raw_samples_published", "proposed_budgets")


def load_run(label: str, directory: Path) -> dict[str, Any]:
    bundle = json.loads((directory / "bundle.json").read_text())
    env = bundle["declared"].get("fixture_env") or {}
    for fixture in bundle["fixtures"]:
        fixture["id"] = f"{label}--{fixture['id']}"
        fixture["run"] = label
        for cell in fixture["cells"]:
            cell["fixture"] = fixture["id"]
            cell["run"] = label
            if env:
                cell["fixture_env"] = env  # flagged in the summary, excluded from proposed budgets
    for cell in bundle["planned_cells"]:
        cell["run"] = label
    return bundle


def merge(runs: list[tuple[str, dict[str, Any]]]) -> dict[str, Any]:
    first = runs[0][1]
    # A deep copy: per_run below holds the first run's declared policy beside the merged one.
    merged = copy.deepcopy({key: value for key, value in first.items() if key not in MERGED_KEYS})
    merged["generated_utc"] = first["generated_utc"]
    merged["runs"] = []
    merged["planned_cells"], merged["fixtures"], merged["unsupported_cells"] = [], [], []
    seen: set[str] = set()
    for label, bundle in runs:
        merged["runs"].append({
            "label": label, "command": bundle["command"], "generated_utc": bundle["generated_utc"],
            "duration_seconds": bundle["duration_seconds"], "load_note": bundle["platform"]["load_note"],
            "load_at_start": bundle["platform"]["load_at_start"], "load_at_end": bundle["load_at_end"],
            "declared": bundle["declared"], "durations": bundle["durations"],
            "identities_unchanged": [v.get("identities_unchanged") for v in bundle["variants"]],
            "fixtures": [f["id"] for f in bundle["fixtures"]],
        })
        merged["planned_cells"].extend(bundle["planned_cells"])
        merged["fixtures"].extend(bundle["fixtures"])
        for row in bundle["unsupported_cells"]:
            key = json.dumps(row, sort_keys=True)
            if key not in seen:
                seen.add(key)
                merged["unsupported_cells"].append(row)
        if bundle.get("attention") and not merged.get("attention"):
            merged["attention"] = bundle["attention"]
    merged["command"] = [run["command"] for run in merged["runs"]]
    merged["duration_seconds"] = sum(run["duration_seconds"] for run in merged["runs"])
    merged["load_at_end"] = merged["runs"][-1]["load_at_end"]
    merged["platform"]["load_note"] = "; ".join(f"{run['label']}: {run['load_note']}" for run in merged["runs"])
    merged["platform"]["load_at_start"] = merged["runs"][0]["load_at_start"]
    for variant in merged["variants"]:
        variant["identities_unchanged"] = all(
            flag for run in merged["runs"] for flag in run["identities_unchanged"])
    if any(run["declared"] != merged["declared"] for run in merged["runs"]):
        merged["declared"]["per_run"] = {run["label"]: run["declared"] for run in merged["runs"]}
    merged["merge_note"] = ("joined from separate runner invocations; fixture ids carry the run label, "
                            "load and producer intervals are recorded per run")
    return merged


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="append", required=True, metavar="LABEL=DIR",
                        help="a perf-matrix --output directory with its bundle.json; repeatable, in order")
    parser.add_argument("--output", type=Path, required=True, help="new directory for the merged bundle")
    parser.add_argument("--evidence-dir", type=Path, help="publish summary.md, bundle.json.gz and bounded raw samples here")
    parser.add_argument("--raw-budget-bytes", type=int, default=600_000)
    args = parser.parse_args()
    runs = []
    for spec in args.run:
        label, _, directory = spec.partition("=")
        if not label or not directory or "/" in label:
            parser.error(f"--run expects LABEL=DIR without a slash in the label: {spec}")
        runs.append((label, load_run(label, Path(directory).resolve())))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True)
    bundle = merge(runs)
    problems = validate_bundle(bundle)
    bundle["validation"] = {"ok": not problems, "problems": problems}
    write_summary(bundle, args.output / "summary.md")
    (args.output / "bundle.json").write_text(json.dumps(bundle, indent=1) + "\n")
    if args.evidence_dir:
        publish(bundle, args.output, args.evidence_dir.resolve(), args.raw_budget_bytes)
    cells = [c for f in bundle["fixtures"] for c in f["cells"]]
    measured = sum(1 for c in cells if c["status"] == "measured")
    print(f"runs merged: {len(runs)}; cells measured: {measured}/{len(bundle['planned_cells'])}; "
          f"fixtures with errors: {sum(1 for f in bundle['fixtures'] if f['errors'])}; validation ok: {not problems}")
    for problem in problems:
        print(f"  {problem}", file=sys.stderr)
    print(args.output / "summary.md")
    return 0 if not problems else 1


if __name__ == "__main__":
    raise SystemExit(main())
