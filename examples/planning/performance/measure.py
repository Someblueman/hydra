#!/usr/bin/env python3
"""Fixed, alternating fresh-process trials; never search for a favorable run."""
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sample(command, identity):
    started = time.perf_counter_ns()
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        elapsed = time.perf_counter_ns() - started
        count, code, error = result.stdout.strip(), result.returncode, result.stderr
    except subprocess.TimeoutExpired:
        elapsed, count, code, error = time.perf_counter_ns() - started, "", 124, "sample timeout"
    ok = code == 0 and count == "4003"
    return {**identity, "elapsed_ns": elapsed, "status": "ok" if ok else "fail",
            "count": count[:128], "returncode": code}, error[:1024]


def main():
    if os.environ.get("HYDRA_PERFORMANCE_EXCLUSIVE") != "1":
        raise SystemExit("A coordinated measurement window requires HYDRA_PERFORMANCE_EXCLUSIVE=1")
    root = Path(os.environ.get("HYDRA_WORKFLOW_REPO_ROOT", os.getcwd()))
    records = Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"]) / "records"
    output = Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"])
    tools = {"shell": shutil.which("sh"), "awk": shutil.which("awk"), "python": sys.executable}
    commands = {name: [tools["shell"], str(root / f"{name}.sh"), str(records)]
                for name in ("baseline", "candidate")}
    environment = {"python": sys.version, "platform": platform.platform(),
                   "machine": platform.machine(), "cwd": str(root),
                   "source_git": {name: subprocess.check_output(["git", "-C", str(root), "rev-parse", ref], text=True).strip()
                                  for name, ref in (("commit", "HEAD"), ("tree", "HEAD^{tree}"))},
                   "timer": "time.perf_counter_ns", "units": "nanoseconds",
                   "tools": {name: {"path": path, "sha256": digest(path)}
                             for name, path in tools.items()},
                   "load_before": list(os.getloadavg())}
    warmups, rows, failures = [], [], []
    for trial in range(1, 3):
        for name in commands:
            row, error = sample(commands[name], {"implementation": name, "warmup": trial})
            warmups.append(row)
            if row["status"] != "ok":
                failures.append({"phase": "warmup", **row, "stderr": error})
    for trial in range(1, 11):
        order = ("baseline", "candidate") if trial % 2 else ("candidate", "baseline")
        for position, name in enumerate(order):
            row, error = sample(commands[name], {"sample_id": f"{trial:02d}-{name}",
                                "implementation": name, "trial": trial, "order": position})
            rows.append(row)
            if row["status"] != "ok":
                failures.append({"phase": "trial", **row, "stderr": error})
    environment["load_after"] = list(os.getloadavg())
    with (output / "raw.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    protocol = {"warmups": 2, "trials": 10, "order": "alternating_AB_BA",
                "stopping_rule": "exactly_10_pairs_no_exclusions_or_retries",
                "sample_timeout_seconds": 10, "expected_count": "4003",
                "exclusions": [], "exclusive_hydra_measurement": True,
                "exclusivity_scope": "coordinated Hydra jobs; other system activity is observed, not excluded"}
    manifest = {"schema_version": 1, "workload": {"path": "records.txt", "sha256": digest(records),
                "bytes": records.stat().st_size}, "commands": commands,
                "sources": {name: {"path": f"{name}.sh", "sha256": digest(root / f"{name}.sh"),
                                    "bytes": (root / f"{name}.sh").stat().st_size} for name in commands},
                "environment": environment, "warmups": warmups, "failures": failures, "protocol": protocol}
    (output / "manifest.json").write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n")
    (output / "environment.txt").write_text(json.dumps(environment, sort_keys=True, indent=2) + "\n")


if __name__ == "__main__":
    main()
