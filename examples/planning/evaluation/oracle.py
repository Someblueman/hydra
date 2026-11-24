#!/usr/bin/env python3
"""Independent oracle for the 9E planning evaluation.

Loads the archived held-out cases and the held-out additions, computes every
expected artifact from the case inputs and the frozen visible contracts (never
from a candidate), and compares artifacts strictly with JSON types and a size
bound.
"""

import csv
import hashlib
import json
from pathlib import Path

MAX_ARTIFACT_BYTES = 65536
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
