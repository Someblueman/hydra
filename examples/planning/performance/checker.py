#!/usr/bin/env python3
"""Independent checker: reconstruct accepted measurements and bind their sources."""
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import shutil
import subprocess
import sys

LIMITS = ["One synthetic line-count workload on one host; no Hydra-wide performance claim.",
          "Fresh-process elapsed time includes process startup and OS effects.",
          "Ten pairs give limited precision; p95 is the sample maximum, not a tail guarantee.",
          "Paired bootstrap assumes independent trial pairs and may understate drift or correlated noise."]
COLUMNS = ["sample_id", "implementation", "trial", "order", "elapsed_ns", "status", "count", "returncode"]


def sha(value):
    return hashlib.sha256(value).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def median(values):
    ordered = sorted(values)
    middle = len(ordered) // 2
    return (ordered[middle - 1] + ordered[middle]) / 2 if len(ordered) % 2 == 0 else ordered[middle]


def check_manifest(manifest, root, records):
    require(set(manifest) == {"schema_version", "workload", "commands", "sources", "environment", "warmups", "failures", "protocol"}, "manifest fields")
    require(manifest["schema_version"] == 1, "manifest version")
    require(manifest["workload"] == {"path": "records.txt", "sha256": sha(records), "bytes": len(records)}, "workload binding")
    require(records.count(b"\n") == 4003 and records.endswith(b"\n"), "workload line-count contract")
    require(set(manifest["sources"]) == {"baseline", "candidate"}, "source set")
    for name in ("baseline", "candidate"):
        source = (root / f"{name}.sh").read_bytes()
        require(manifest["sources"][name] == {"path": f"{name}.sh", "sha256": sha(source), "bytes": len(source)}, "source binding")
    expected_protocol = {"warmups": 2, "trials": 10, "order": "alternating_AB_BA",
        "stopping_rule": "exactly_10_pairs_no_exclusions_or_retries", "sample_timeout_seconds": 10,
        "expected_count": "4003", "exclusions": [], "exclusive_hydra_measurement": True,
        "exclusivity_scope": "coordinated Hydra jobs; other system activity is observed, not excluded"}
    require(manifest["protocol"] == expected_protocol, "fixed protocol binding")
    environment = manifest["environment"]
    require(set(environment) == {"python", "platform", "machine", "cwd", "source_git", "timer", "units", "tools", "load_before", "load_after"}, "environment fields")
    for name, value in {"python": sys.version, "platform": platform.platform(), "machine": platform.machine(),
                        "timer": "time.perf_counter_ns", "units": "nanoseconds"}.items():
        require(environment[name] == value, f"environment {name}")
    require(isinstance(environment["cwd"], str) and Path(environment["cwd"]).is_absolute(), "measurement working directory")
    measured_root = Path(environment["cwd"])
    source_git = {name: subprocess.check_output(["git", "-C", str(root), "rev-parse", ref], text=True).strip()
                  for name, ref in (("commit", "HEAD"), ("tree", "HEAD^{tree}"))}
    require(environment["source_git"] == source_git, "cross-head source identity")
    paths = {"shell": shutil.which("sh"), "awk": shutil.which("awk"), "python": sys.executable}
    require(environment["tools"] == {name: {"path": path, "sha256": sha(Path(path).read_bytes())} for name, path in paths.items()}, "toolchain binding")
    for field in ("load_before", "load_after"):
        require(isinstance(environment[field], list) and len(environment[field]) == 3 and
                all(type(value) in (int, float) and math.isfinite(value) and value >= 0 for value in environment[field]), "load observations")
    require(set(manifest["commands"]) == {"baseline", "candidate"}, "command set")
    input_path = None
    for name, command in manifest["commands"].items():
        require(isinstance(command, list) and len(command) == 3 and
                command[:2] == [paths["shell"], str(measured_root / f"{name}.sh")] and
                isinstance(command[2], str) and Path(command[2]).is_absolute() and Path(command[2]).name == "records", "command binding")
        require(input_path is None or input_path == command[2], "matched workload command")
        input_path = command[2]
    warmups = manifest["warmups"]
    require(isinstance(warmups, list) and len(warmups) == 4, "warmup count")
    for index, warmup in enumerate(warmups):
        require(set(warmup) == {"implementation", "warmup", "elapsed_ns", "status", "count", "returncode"}, "warmup fields")
        require(warmup["implementation"] == ("baseline", "candidate")[index % 2] and
                type(warmup["warmup"]) is int and warmup["warmup"] == index // 2 + 1 and
                type(warmup["elapsed_ns"]) is int and warmup["elapsed_ns"] > 0 and
                warmup["status"] == "ok" and warmup["count"] == "4003" and
                type(warmup["returncode"]) is int and warmup["returncode"] == 0, "invalid or failed warmup")
    require(manifest["failures"] == [], "recorded measurement failures")


def check_report(inputs, root):
    subject = (inputs / "subject").read_bytes()
    report = json.loads(subject)
    manifest = json.loads((inputs / "manifest").read_text())
    require(set(report) == {"schema_version", "manifest", "raw_summary", "baseline", "candidate", "paired_uncertainty", "outcome", "limits", "raw_samples"}, "report fields")
    require(report["schema_version"] == 2 and report["manifest"] == manifest, "sealed manifest equality")
    check_manifest(manifest, root, (inputs / "records").read_bytes())
    with (inputs / "raw").open(newline="") as handle:
        reader = csv.DictReader(handle)
        require(reader.fieldnames == COLUMNS, "raw columns")
        rows = list(reader)
    require(len(rows) == 20, "paired trial count")
    samples = {"baseline": {}, "candidate": {}}
    for index, row in enumerate(rows):
        require(set(row) == set(COLUMNS) and None not in row.values(), "complete raw row")
        trial, position = divmod(index, 2)
        trial += 1
        expected = ("baseline", "candidate") if trial % 2 else ("candidate", "baseline")
        name = expected[position]
        require(row["sample_id"] == f"{trial:02d}-{name}" and row["implementation"] == name and
                row["trial"] == str(trial) and row["order"] == str(position), "pair identity or order")
        elapsed = int(row["elapsed_ns"])
        require(str(elapsed) == row["elapsed_ns"] and elapsed > 0 and row["status"] == "ok" and
                row["count"] == "4003" and row["returncode"] == "0", "invalid or failed trial")
        samples[name][trial] = elapsed
    require(report["raw_samples"] == (inputs / "raw").read_text().splitlines(), "sealed raw equality")
    require(report["raw_summary"] == {"rows": 20, "failures": [], "invalid_reason": None}, "raw summary")
    for name, values in samples.items():
        require(report[name] == {"n": 10, "median_ns": median(list(values.values())),
                                 "p95_ns": max(values.values())}, "sample summaries")
    effects = [samples["candidate"][trial] / samples["baseline"][trial] - 1 for trial in range(1, 11)]
    rng = random.Random(1729)
    # Explicit draw loop is independent of the analyzer's statistics/choices calls.
    draws = [median([effects[int(rng.random() * 10)] for _ in range(10)]) for _ in range(10000)]
    draws.sort()
    uncertainty = {"method": "paired bootstrap median relative change", "estimate": median(effects),
                   "seed": 1729, "resamples": 10000, "interval": "90% percentile; independent pair assumption",
                   "lower": draws[499], "upper": draws[9499]}
    require(report["paired_uncertainty"] == uncertainty, "paired uncertainty recomputation")
    expected = "target established" if median(effects) <= -.10 and draws[9499] < 0 else "target not established"
    require(report["outcome"] == expected, "outcome classification")
    require(report["limits"] == LIMITS, "measurement limitations")
    return {"outcome": expected, "baseline": report["baseline"], "candidate": report["candidate"],
            "paired_uncertainty": uncertainty, "measurement": 10, "unit": "paired_trials",
            "raw_csv_sha256": sha((inputs / "raw").read_bytes()),
            "manifest_sha256": sha((inputs / "manifest").read_bytes())}


def emit(inputs, output, valid, observation):
    context = json.loads(Path(os.environ["HYDRA_WORKFLOW_VALIDATION_FILE"]).read_text())["data"]
    subject = sha((inputs / "subject").read_bytes())
    verdict = "pass" if valid else "fail"
    raw = {"actual": verdict, "verdict": verdict, **observation}
    observations = [{"id": "performance-outcome", "raw": raw, "raw_sha256": sha(canonical(raw))}]
    records = []
    for identity in ("performance-outcome", "binding-check", "protocol-check", "analysis-check", "limits-check"):
        records.append({"obligation_id": identity, "subject_manifest_sha256": subject,
            "validator_identity": "independent-performance-checker-v3",
            "validator_recipe_sha256": context["performance-check-recipe"],
            "invocation": {"argv": ["python3", "checker.py"], "exit_code": 0 if valid else 1},
            "environment": {"host": platform.node() or "local", "toolchain": sys.version},
            "case_inventory": ["performance-outcome"], "observations": observations,
            "raw_evidence_sha256": sha(canonical(observations)),
            "counts": {"executed": 1, "failed": 0 if valid else 1, "skipped": 0}, "limitations": LIMITS})
    result = {"schema_version": 3, "execution_status": "completed", "evidence_status": "valid" if valid else "invalid",
              "domain_verdict": verdict, "verdict": verdict, "subject_sha256": subject,
              "validator_sha256": context["performance-check"], "evidence_records": records,
              "requirements": ["binding", "protocol", "analysis", "outcome", "limits"], "limitations": LIMITS,
              "evidence": "Independent source, protocol, raw sample and paired analysis checks."}
    (output / "assessment").write_text(json.dumps(result, separators=(",", ":")) + "\n")


def main():
    inputs = Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"])
    root = Path(os.environ.get("HYDRA_WORKFLOW_REPO_ROOT", os.getcwd()))
    try:
        observation = check_report(inputs, root)
        valid = True
    except (ValueError, TypeError, KeyError, OSError, ZeroDivisionError) as error:
        observation = {"measurement": 0, "unit": "validated_paired_trials", "error": f"{type(error).__name__}: {error}"}
        valid = False
    emit(inputs, Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]), valid, observation)
    return 0 if valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
