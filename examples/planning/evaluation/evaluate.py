#!/usr/bin/env python3
"""Independent oracle runner for the frozen 9E planner cases."""

import argparse
import csv
import contextlib
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent
HELDOUT = ROOT / "planner-evaluation-heldout-v2.json"
CONTRACTS = ROOT / "planner-visible-contracts.json"
MAX_ARTIFACT_BYTES = 65536
CASE_CONTRACTS = {
    "fc-01-normalize-record": "feature.normalize_records.v1",
    "fc-02-reject-duplicate-config": "feature.reject_duplicate_config.v1",
    "fc-03-two-member-report": "feature.compose_two_members.v1",
    "fr-01-schedule-metrics": "research.schedule_metrics.v1",
    "fr-02-threshold-negative": "research.threshold_claim.v1",
    "fr-03-preserve-uncertainty": "research.preserve_unknowns.v1",
    "mm-01-exact-members": "manifest.exact_members.v1",
    "mm-02-hash-bound-members": "manifest.hash_bound_members.v1",
    "mm-03-selected-and-skipped": "manifest.selected_skipped.v1",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def normalize_record(value: str) -> str:
    return value.strip(" \t\r\n").translate(str.maketrans(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz"))


def fc01(inputs):
    records = inputs.get("records")
    if not isinstance(records, list) or not records or not all(isinstance(item, str) for item in records):
        raise ValueError("records must be a nonempty string array")
    return "".join(normalize_record(item) + "\n" for item in records)


def fc02(inputs):
    raw = inputs.get("config_bytes")
    if not isinstance(raw, str):
        raise ValueError("config_bytes must be text")
    config = {}
    for line in raw.splitlines():
        if "=" not in line:
            raise ValueError("config line has no separator")
        key, value = line.split("=", 1)
        if key in config:
            return {"error": f"duplicate key: {key}", "verdict": "fail"}
        config[key] = value
    return {"config": config, "verdict": "pass"}


def fc03(inputs):
    first, second = inputs.get("member_a"), inputs.get("member_b")
    if not isinstance(first, str) or not isinstance(second, str):
        raise ValueError("both members must be text")
    return first + second


def parse_jobs(raw: str):
    rows = list(csv.DictReader(raw.splitlines()))
    if not rows or any(set(row) != {"job", "duration"} for row in rows):
        raise ValueError("jobs CSV must contain job,duration")
    jobs = []
    for index, row in enumerate(rows):
        duration = int(row["duration"])
        if not row["job"] or duration <= 0:
            raise ValueError("invalid job row")
        jobs.append((row["job"], duration, index))
    return jobs


def schedule_metrics(jobs):
    waits = []
    elapsed = 0
    for _, duration, _ in jobs:
        waits.append(elapsed)
        elapsed += duration
    return {"mean_wait": round(sum(waits) / len(waits), 6), "max_wait": max(waits)}


def fr01(inputs):
    jobs = parse_jobs(inputs.get("jobs_csv"))
    fcfs = jobs
    sjf = sorted(jobs, key=lambda item: (item[1], item[2]))
    return {"fcfs": schedule_metrics(fcfs), "sjf": schedule_metrics(sjf)}


def fr02(inputs):
    metrics, thresholds = inputs.get("metrics"), inputs.get("thresholds")
    if not isinstance(metrics, dict) or not isinstance(thresholds, dict):
        raise ValueError("metrics and thresholds must be objects")
    statuses = {}
    passing = []
    for policy, values in metrics.items():
        p95_fail = values["p95_turnaround"] > thresholds["p95_turnaround_max"]
        wait_fail = values["max_wait"] > thresholds["max_wait_max"]
        if not p95_fail and not wait_fail:
            statuses[policy] = "pass"
            passing.append(policy)
        elif p95_fail and wait_fail:
            statuses[policy] = "fail_both"
        elif p95_fail:
            statuses[policy] = "fail_p95_turnaround"
        else:
            statuses[policy] = "fail_max_wait"
    claim = "no_policy_satisfies_both_thresholds" if not passing else "policies_satisfy_both_thresholds"
    return {"claim": claim, **statuses}


def fr03(inputs):
    observations = inputs.get("observations")
    if not isinstance(observations, list):
        raise ValueError("observations must be a list")
    values = {}
    for item in observations:
        if not isinstance(item, str) or "=" not in item:
            raise ValueError("malformed observation")
        key, value = item.split("=", 1)
        if value == "unknown":
            values[key] = None
        elif key == "completed":
            values[key] = int(value)
        elif key == "rework_probability":
            values[key] = float(value)
        elif key == "transfer_bytes":
            values[key] = int(value)
        else:
            raise ValueError("unknown observation")
    if set(values) != {"completed", "rework_probability", "transfer_bytes"}:
        raise ValueError("incomplete observations")
    values["claim"] = "finite_observation_only"
    return values


def mm01(inputs):
    declared = inputs.get("declared_members")
    manifest = inputs.get("manifest_members")
    if not isinstance(declared, list) or not isinstance(manifest, list):
        raise ValueError("member lists required")
    missing = [item for item in declared if item not in manifest]
    extra = [item for item in manifest if item not in declared]
    duplicate = len(manifest) != len(set(manifest))
    return {"members": [item for item in declared if item in manifest], "missing": missing, "extra": extra,
            "verdict": "pass" if not missing and not extra and not duplicate else "fail"}


def mm02(inputs):
    manifest = inputs.get("manifest")
    files = inputs.get("files")
    if not isinstance(manifest, dict) or not isinstance(files, dict):
        raise ValueError("manifest required")
    checked = sorted(manifest)
    mismatches = []
    for member in checked:
        value = files.get(member)
        digest = hashlib.sha256(value.encode("utf-8")).hexdigest() if isinstance(value, str) else None
        if digest != manifest[member]:
            mismatches.append(member)
    return {"checked": checked, "mismatches": mismatches,
            "verdict": "pass" if not mismatches and set(files) == set(manifest) else "fail"}


def mm03(inputs):
    declared = inputs.get("declared_members")
    selection = inputs.get("selection")
    if not isinstance(declared, list) or not isinstance(selection, dict):
        raise ValueError("declared_members and selection required")
    selected = [item for item in declared if selection.get(item) is True]
    skipped = [{"id": item, "reason": "selection=false"} for item in declared if selection.get(item) is False]
    records = set(selected) | {item["id"] for item in skipped}
    missing = [item for item in declared if item not in records]
    verdict = "pass" if not missing and len(records) == len(declared) else "fail"
    return {"selected": selected, "skipped": skipped, "missing_records": missing, "verdict": verdict}


SOLVERS = {
    "fc-01-normalize-record": fc01,
    "fc-02-reject-duplicate-config": fc02,
    "fc-03-two-member-report": fc03,
    "fr-01-schedule-metrics": fr01,
    "fr-02-threshold-negative": fr02,
    "fr-03-preserve-uncertainty": fr03,
    "mm-01-exact-members": mm01,
    "mm-02-hash-bound-members": mm02,
    "mm-03-selected-and-skipped": mm03,
}


def artifact_value(artifact):
    if "bytes" in artifact:
        return artifact["bytes"]
    if "json" in artifact:
        return artifact["json"]
    raise ValueError("artifact has no bytes or json value")


def strict_equal(actual, expected):
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return (set(actual) == set(expected) and
                all(strict_equal(actual[key], expected[key]) for key in expected))
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(strict_equal(a, e) for a, e in zip(actual, expected))
    return actual == expected


def artifact_size(value):
    if isinstance(value, str):
        return len(value.encode("utf-8"))
    try:
        encoded = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    except (TypeError, ValueError):
        return None
    return len(encoded)


def equal_artifact(actual, expected):
    if isinstance(expected, dict) and isinstance(actual, str):
        try:
            actual = json.loads(actual, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"invalid JSON constant {value}")))
        except (TypeError, ValueError, json.JSONDecodeError):
            return False
    size = artifact_size(actual)
    return size is not None and size <= MAX_ARTIFACT_BYTES and strict_equal(actual, expected)


def public_inputs(case_id, hidden):
    if case_id == "fc-01-normalize-record":
        return {"records": hidden["records"]}
    if case_id == "fc-02-reject-duplicate-config":
        return hidden["config_bytes"]
    if case_id == "fc-03-two-member-report":
        return {"member_a": hidden["member_a"], "member_b": hidden["member_b"]}
    if case_id == "fr-01-schedule-metrics":
        return hidden["jobs_csv"]
    if case_id == "fr-02-threshold-negative":
        return {"metrics": hidden["metrics"], "thresholds": hidden["thresholds"]}
    if case_id == "fr-03-preserve-uncertainty":
        observations = {}
        for item in hidden["observations"]:
            key, value = item.split("=", 1)
            observations[key] = value if value == "unknown" else (int(value) if key == "completed" or key == "transfer_bytes" else float(value))
        return {"observations": observations}
    return hidden


def import_candidate(path: Path):
    if not path.is_file():
        raise ValueError("candidate must be a regular Python file")
    spec = importlib.util.spec_from_file_location("planner_candidate", path)
    if spec is None or spec.loader is None:
        raise ValueError("candidate cannot be imported")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    solve = getattr(module, "solve", None)
    if not callable(solve):
        raise ValueError("candidate must export solve(task_type, inputs)")
    return solve


def candidate_worker(candidate_path: Path, contract_id: str, inputs_json: str):
    started = time.process_time()
    try:
        inputs = json.loads(inputs_json, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"invalid JSON constant {value}")))
        with open(os.devnull, "w", encoding="utf-8") as sink, contextlib.redirect_stdout(sink), contextlib.redirect_stderr(sink):
            solve = import_candidate(candidate_path)
            result = solve(contract_id, inputs)
        encoded = json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
        response = {"ok": True, "result": json.loads(encoded), "cpu_seconds": time.process_time() - started}
    except Exception as exc:
        response = {"ok": False, "error": f"candidate_error:{type(exc).__name__}:{exc}",
                    "cpu_seconds": time.process_time() - started}
    sys.stdout.write(json.dumps(response, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n")


def run_candidate(candidate_path: Path, contract_id: str, inputs, timeout_seconds=5):
    payload = json.dumps(inputs, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
    command = [sys.executable, str(Path(__file__).resolve()), "--worker", str(candidate_path), contract_id, payload]
    started = time.monotonic()
    try:
        completed = subprocess.run(command, input="", capture_output=True, text=True, timeout=timeout_seconds, check=False)
    except subprocess.TimeoutExpired:
        return None, None, time.monotonic() - started, "candidate_timeout"
    wall = time.monotonic() - started
    if completed.returncode:
        return None, None, wall, f"candidate_worker_exit:{completed.returncode}"
    try:
        response = json.loads(completed.stdout, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"invalid JSON constant {value}")))
        if not isinstance(response, dict) or not response.get("ok"):
            return None, response.get("cpu_seconds") if isinstance(response, dict) else None, wall, response.get("error", "candidate_worker_error") if isinstance(response, dict) else "candidate_worker_error"
        return response.get("result"), response.get("cpu_seconds"), wall, None
    except (TypeError, ValueError, json.JSONDecodeError) as exc:
        return None, None, wall, f"malformed_candidate_response:{exc}"


def evaluate(candidate_path: Path):
    with HELDOUT.open(encoding="utf-8") as stream:
        heldout = json.load(stream)
    with CONTRACTS.open(encoding="utf-8") as stream:
        contracts = json.load(stream)
    cases = []
    accepted = 0
    control_total = 0
    control_rejections = 0
    false_accepts = 0
    total_wall = 0.0
    total_cpu = 0.0
    for case in heldout["cases"]:
        case_id = case["id"]
        solver = SOLVERS.get(case_id)
        contract_id = CASE_CONTRACTS.get(case_id)
        if solver is None or contract_id is None:
            raise ValueError(f"no independent oracle for {case_id}")
        hidden_inputs = case["inputs"]
        expected = solver(hidden_inputs)
        if "exact_json" in case["acceptance"]:
            declared = case["acceptance"]["exact_json"]
        else:
            declared = case["acceptance"]["exact_bytes"]
            if isinstance(expected, dict):
                declared = json.loads(declared)
        if not equal_artifact(expected, declared):
            raise ValueError(f"frozen acceptance disagrees with oracle for {case_id}")
        actual, cpu, wall, error = run_candidate(candidate_path, contract_id, public_inputs(case_id, hidden_inputs))
        cpu = cpu if isinstance(cpu, (int, float)) and not isinstance(cpu, bool) else 0.0
        total_wall += wall
        total_cpu += cpu
        case_ok = error is None and equal_artifact(actual, expected)
        if case_ok:
            accepted += 1
        controls = []
        for control in case.get("incorrect_artifacts", []):
            control_total += 1
            value = artifact_value(control)
            rejected = not equal_artifact(value, expected)
            if rejected:
                control_rejections += 1
            else:
                false_accepts += 1
            controls.append({"id": control["id"], "rejected": rejected,
                             "reason": control["reason"] if rejected else "oracle accepted declared incorrect artifact"})
        cases.append({"id": case_id, "contract_id": contract_id, "class": case["class"],
                      "expected_format": "exact_json" if "exact_json" in case["acceptance"] else "exact_bytes",
                      "accepted": case_ok,
                      "reason": "accepted" if case_ok else (error or "output_mismatch"),
                      "wall_seconds": wall, "cpu_seconds": cpu, "negative_controls": controls})
    return {
        "schema_version": 1,
        "candidate": {"path": str(candidate_path), "sha256": sha256(candidate_path)},
        "frozen_sources": {"heldout_sha256": sha256(HELDOUT), "contracts_sha256": sha256(CONTRACTS),
                            "oracle_sha256": sha256(Path(__file__).resolve())},
        "cases": cases,
        "aggregate": {"case_total": len(cases), "accepted_correct": accepted,
                       "correct_case_failures": len(cases) - accepted,
                       "false_acceptances": false_accepts, "negative_control_total": control_total,
                       "negative_controls_rejected": control_rejections, "wall_seconds": total_wall,
                       "cpu_seconds": total_cpu},
        "status": "pass" if accepted == len(cases) and false_accepts == 0 and control_rejections == control_total else "fail",
    }


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--worker":
        candidate_worker(Path(sys.argv[2]), sys.argv[3], sys.argv[4])
        return 0
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = evaluate(args.candidate.resolve())
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    except Exception as exc:
        result = {"schema_version": 1, "status": "fail", "reason": f"oracle_error:{type(exc).__name__}:{exc}"}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        return 1
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
