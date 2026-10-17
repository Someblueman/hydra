#!/usr/bin/env python3
"""Independent oracle for the 9E planning evaluation.

Loads the archived held-out cases and the held-out additions, computes every
expected artifact from the case inputs and the frozen visible contracts (never
from a candidate), compares artifacts strictly, and runs a frozen candidate in
a separate interpreter with an empty stdin and a bounded timeout.

Worker mode: python3 -I oracle.py --worker <candidate.py> <contract_id> <inputs-json>
"""

import contextlib
import csv
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import time
from pathlib import Path

MAX_ARTIFACT_BYTES = 65536
CANDIDATE_TIMEOUT_SECONDS = 5
# The archived held-out cases carry no contract ID; this map is the archived one.
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


def strict_json_loads(text):
    def reject(value):
        raise ValueError(f"invalid JSON constant {value}")
    return json.loads(text, parse_constant=reject)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# --- expected results, written from the visible contract text only ---------

def normalize_records(hidden):
    lines = []
    for record in hidden["records"]:
        text = record.strip(" \t\r\n")
        lines.append("".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in text))
    return "".join(line + "\n" for line in lines)


def reject_duplicate_config(hidden):
    config = {}
    for line in hidden["config_bytes"].splitlines():
        key, value = line.split("=", 1)
        if key in config:
            return {"error": "duplicate key: " + key, "verdict": "fail"}
        config[key] = value
    return {"config": config, "verdict": "pass"}


def compose_two_members(hidden):
    return hidden["member_a"] + hidden["member_b"]


def schedule_metrics(hidden):
    rows = list(csv.DictReader(hidden["jobs_csv"].splitlines()))
    jobs = [(row["job"], int(row["duration"]), index) for index, row in enumerate(rows)]

    def metrics(order):
        waits, elapsed = [], 0
        for _job, duration, _index in order:
            waits.append(elapsed)
            elapsed += duration
        return {"mean_wait": round(sum(waits) / len(waits), 6), "max_wait": max(waits)}
    return {"fcfs": metrics(jobs), "sjf": metrics(sorted(jobs, key=lambda j: (j[1], j[2])))}


def threshold_claim(hidden):
    thresholds, out, passing = hidden["thresholds"], {}, []
    for policy, values in hidden["metrics"].items():
        p95_ok = values["p95_turnaround"] <= thresholds["p95_turnaround_max"]
        wait_ok = values["max_wait"] <= thresholds["max_wait_max"]
        if p95_ok and wait_ok:
            out[policy] = "pass"
            passing.append(policy)
        elif not p95_ok and not wait_ok:
            out[policy] = "fail_both"
        else:
            out[policy] = "fail_p95_turnaround" if not p95_ok else "fail_max_wait"
    if passing:
        raise ValueError("a passing policy needs a claim wording the visible contract leaves open; not admitted")
    return {"claim": "no_policy_satisfies_both_thresholds", **out}


def preserve_unknowns(hidden):
    out = {}
    for item in hidden["observations"]:
        key, value = item.split("=", 1)
        if value == "unknown":
            out[key] = None
        elif key == "rework_probability":
            out[key] = float(value)
        else:
            out[key] = int(value)
    out["claim"] = "finite_observation_only"
    return out


def exact_members(hidden):
    declared, manifest = hidden["declared_members"], hidden["manifest_members"]
    missing = [m for m in declared if m not in manifest]
    extra = [m for m in manifest if m not in declared]
    ok = not missing and not extra and len(manifest) == len(set(manifest))
    return {"members": [m for m in declared if m in manifest], "missing": missing,
            "extra": extra, "verdict": "pass" if ok else "fail"}


def hash_bound_members(hidden):
    manifest, files = hidden["manifest"], hidden["files"]
    checked = sorted(manifest)
    mismatches = [m for m in checked if m not in files or
                  hashlib.sha256(files[m].encode("utf-8")).hexdigest() != manifest[m]]
    ok = not mismatches and set(files) == set(manifest)
    return {"checked": checked, "mismatches": mismatches, "verdict": "pass" if ok else "fail"}


def selected_skipped(hidden):
    declared, selection = hidden["declared_members"], hidden["selection"]
    selected = [m for m in declared if selection.get(m) is True]
    skipped = [{"id": m, "reason": "selection=false"} for m in declared if selection.get(m) is False]
    missing = [m for m in declared if m not in selection]
    return {"selected": selected, "skipped": skipped, "missing_records": missing,
            "verdict": "pass" if not missing else "fail"}


SOLVERS = {
    "feature.normalize_records.v1": normalize_records,
    "feature.reject_duplicate_config.v1": reject_duplicate_config,
    "feature.compose_two_members.v1": compose_two_members,
    "research.schedule_metrics.v1": schedule_metrics,
    "research.threshold_claim.v1": threshold_claim,
    "research.preserve_unknowns.v1": preserve_unknowns,
    "manifest.exact_members.v1": exact_members,
    "manifest.hash_bound_members.v1": hash_bound_members,
    "manifest.selected_skipped.v1": selected_skipped,
}


def expected(contract_id, hidden):
    return SOLVERS[contract_id](hidden)


# --- candidate interfaces ----------------------------------------------------

def declared_inputs(contract_id, hidden):
    """The archived pilot's declared candidate interface."""
    if contract_id == "feature.reject_duplicate_config.v1":
        return hidden["config_bytes"]
    if contract_id == "research.schedule_metrics.v1":
        return hidden["jobs_csv"]
    if contract_id == "research.preserve_unknowns.v1":
        observations = {}
        for item in hidden["observations"]:
            key, value = item.split("=", 1)
            observations[key] = value if value == "unknown" else (
                float(value) if key == "rework_probability" else int(value))
        return {"observations": observations}
    return hidden


def adapted_inputs(contract_id, hidden, contract):
    """The archived post hoc diagnostic: file inputs become named-file maps."""
    inputs = declared_inputs(contract_id, hidden)
    if isinstance(inputs, str):
        return {contract["input"]["file"]: inputs}
    if contract_id == "feature.compose_two_members.v1":
        return {"files": inputs}
    return inputs


def adapt_output(value, contract):
    """Unwrap a single-entry output-file map named as the contract declares."""
    name = contract["output"].get("file")
    if isinstance(value, dict) and len(value) == 1 and name in value:
        return value[name]
    return value


# --- artifacts ---------------------------------------------------------------

def artifact_value(control):
    if "bytes" in control:
        return control["bytes"]
    if "json" in control:
        return control["json"]
    raise ValueError("incorrect artifact has no bytes or json value")


def strict_equal(actual, expect):
    if type(actual) is not type(expect):
        return False
    if isinstance(expect, dict):
        return set(actual) == set(expect) and all(strict_equal(actual[k], expect[k]) for k in expect)
    if isinstance(expect, list):
        return len(actual) == len(expect) and all(strict_equal(a, e) for a, e in zip(actual, expect))
    return actual == expect


def artifact_size(value):
    if isinstance(value, str):
        return len(value.encode("utf-8"))
    try:
        return len(json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8"))
    except (TypeError, ValueError):
        return None


def equal_artifact(actual, expect):
    if isinstance(expect, dict) and isinstance(actual, str):
        try:
            actual = strict_json_loads(actual)
        except (TypeError, ValueError):
            return False
    size = artifact_size(actual)
    return size is not None and size <= MAX_ARTIFACT_BYTES and strict_equal(actual, expect)


def declared_acceptance(case):
    acceptance = case["acceptance"]
    if "exact_json" in acceptance:
        return acceptance["exact_json"]
    return acceptance["exact_bytes"]


# --- cases -------------------------------------------------------------------

def load_cases(archived_heldout: Path, additions: Path):
    """Archived cases first, then additions; every expected value is checked
    against the independently declared acceptance before use."""
    cases = []
    for path, source in ((archived_heldout, "archived-v2"), (additions, "additions-v3")):
        document = strict_json_loads(path.read_text(encoding="utf-8"))
        for case in document["cases"]:
            contract_id = case.get("contract_id") or CASE_CONTRACTS[case["id"]]
            value = expected(contract_id, case["inputs"])
            declared = declared_acceptance(case)
            if isinstance(value, dict) and isinstance(declared, str):
                declared = strict_json_loads(declared)
            if not equal_artifact(value, declared):
                raise ValueError(f"declared acceptance disagrees with the oracle for {case['id']}")
            cases.append({"id": case["id"], "class": case["class"], "contract_id": contract_id,
                          "source": source, "inputs": case["inputs"], "expected": value,
                          "incorrect_artifacts": case.get("incorrect_artifacts", [])})
    return cases


# --- candidates --------------------------------------------------------------

def import_candidate(path: Path):
    if not path.is_file():
        raise ValueError("candidate must be a regular Python file")
    spec = importlib.util.spec_from_file_location("planner_candidate", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    solve = getattr(module, "solve", None)
    if not callable(solve):
        raise ValueError("candidate must export solve(task_type, inputs)")
    return solve


def worker(candidate: Path, contract_id: str, inputs_json: str):
    started = time.process_time()
    try:
        inputs = strict_json_loads(inputs_json)
        with open(os.devnull, "w", encoding="utf-8") as sink, contextlib.redirect_stdout(sink), contextlib.redirect_stderr(sink):
            result = import_candidate(candidate)(contract_id, inputs)
        encoded = json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
        response = {"ok": True, "result": strict_json_loads(encoded), "cpu_seconds": time.process_time() - started}
    except Exception as exc:  # the candidate is untrusted code; report every failure
        response = {"ok": False, "error": f"candidate_error:{type(exc).__name__}:{exc}", "cpu_seconds": time.process_time() - started}
    sys.stdout.write(json.dumps(response, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n")


def run_candidate(candidate: Path, contract_id: str, inputs):
    """Returns (value, cpu_seconds, wall_seconds, error)."""
    payload = json.dumps(inputs, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
    command = [sys.executable, "-I", str(Path(__file__).resolve()), "--worker", str(candidate), contract_id, payload]
    started = time.monotonic()
    try:
        completed = subprocess.run(command, input="", capture_output=True, text=True, timeout=CANDIDATE_TIMEOUT_SECONDS, check=False)
    except subprocess.TimeoutExpired:
        return None, None, time.monotonic() - started, "candidate_timeout"
    wall = time.monotonic() - started
    if completed.returncode:
        return None, None, wall, f"candidate_worker_exit:{completed.returncode}"
    try:
        response = strict_json_loads(completed.stdout)
    except (TypeError, ValueError) as exc:
        return None, None, wall, f"malformed_candidate_response:{exc}"
    if not isinstance(response, dict) or not response.get("ok"):
        error = response.get("error", "candidate_worker_error") if isinstance(response, dict) else "candidate_worker_error"
        return None, None, wall, error
    return response.get("result"), response.get("cpu_seconds"), wall, None


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "--worker":
        worker(Path(sys.argv[2]), sys.argv[3], sys.argv[4])
        sys.exit(0)
    sys.stderr.write(__doc__)
    sys.exit(2)
