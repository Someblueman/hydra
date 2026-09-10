#!/usr/bin/env python3
"""Analyze every admitted pair; failed or missing measurements stay invalid."""
import csv
import json
import math
import os
from pathlib import Path
import random
import statistics

LIMITS = ["One synthetic line-count workload on one host; no Hydra-wide performance claim.",
          "Fresh-process elapsed time includes process startup and OS effects.",
          "Ten pairs give limited precision; p95 is the sample maximum, not a tail guarantee.",
          "Paired bootstrap assumes independent trial pairs and may understate drift or correlated noise."]


def percentile(values, probability):
    return sorted(values)[math.ceil(probability * len(values)) - 1]


def analyze(manifest, rows):
    result = {"schema_version": 2, "manifest": manifest, "raw_summary": {
              "rows": len(rows), "failures": [r for r in rows if r.get("status") != "ok"],
              "invalid_reason": None}, "baseline": None, "candidate": None,
              "paired_uncertainty": None, "outcome": "invalid/insufficient measurement", "limits": LIMITS}
    try:
        columns = {"sample_id", "implementation", "trial", "order", "elapsed_ns", "status", "count", "returncode"}
        if len(rows) != 20 or any(set(row) != columns for row in rows):
            raise ValueError("expected exactly twenty complete raw rows")
        values = {"baseline": [], "candidate": []}
        for index, row in enumerate(rows):
            trial, position = index // 2 + 1, index % 2
            name = ("baseline", "candidate")[(position + (trial % 2 == 0)) % 2]
            if (row["sample_id"], row["implementation"], row["trial"], row["order"]) != (f"{trial:02d}-{name}", name, str(trial), str(position)):
                raise ValueError("pair identity or alternating order mismatch")
            elapsed = int(row["elapsed_ns"])
            if elapsed <= 0 or row["status"] != "ok" or row["count"] != "4003" or row["returncode"] != "0":
                raise ValueError("invalid, failed or wrong-result trial")
            values[name].append(elapsed)
        warmups = manifest["warmups"]
        if len(warmups) != 4 or manifest["failures"]:
            raise ValueError("missing warmups or recorded failures")
        for index, row in enumerate(warmups):
            if (row["implementation"], row["warmup"], row["status"], row["count"], row["returncode"]) != (("baseline", "candidate")[index % 2], index // 2 + 1, "ok", "4003", 0):
                raise ValueError("invalid warmup")
        for name, samples in values.items():
            result[name] = {"n": len(samples), "median_ns": statistics.median(samples),
                            "p95_ns": percentile(samples, .95)}
        ratios = [candidate / baseline - 1 for baseline, candidate in zip(values["baseline"], values["candidate"])]
        rng = random.Random(1729)
        bootstraps = [statistics.median(rng.choices(ratios, k=10)) for _ in range(10000)]
        result["paired_uncertainty"] = {"method": "paired bootstrap median relative change",
            "estimate": statistics.median(ratios), "seed": 1729, "resamples": 10000,
            "interval": "90% percentile; independent pair assumption",
            "lower": percentile(bootstraps, .05), "upper": percentile(bootstraps, .95)}
        # The target and interval use the same paired median-change estimator.
        result["outcome"] = "target established" if statistics.median(ratios) <= -.10 and percentile(bootstraps, .95) < 0 else "target not established"
    except (ValueError, TypeError, KeyError, ZeroDivisionError) as error:
        result["raw_summary"]["invalid_reason"] = str(error)
    return result


def main():
    inputs = Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"])
    manifest = json.loads((inputs / "manifest").read_text())
    with (inputs / "raw").open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    result = analyze(manifest, rows)
    (Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / "analysis.json").write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")


if __name__ == "__main__":
    main()
