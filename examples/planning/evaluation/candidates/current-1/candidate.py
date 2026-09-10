#!/usr/bin/env python3
"""Small, deterministic solver for the public 9E contract packet."""

import csv
import hashlib
import json
import sys
from decimal import Decimal, ROUND_HALF_UP


def _json_file(name, value):
    return {name: json.dumps(value, ensure_ascii=False, separators=(",", ":"))}


def _task_name(task_type):
    parts = task_type.split(".")
    return parts[-2] if parts[-1].startswith("v") else parts[-1]


def _records(inputs):
    records = inputs.get("records", [])
    lines = []
    for record in records:
        # The contract names exactly these ASCII whitespace characters.
        lines.append(record.strip(" \t\r\n").translate(
            str.maketrans({chr(c): chr(c + 32) for c in range(65, 91)})
        ))
    return {"report.txt": "".join(line + "\n" for line in lines)}


def _config(inputs):
    text = inputs.get("config.txt", inputs.get("config", ""))
    config = {}
    for line in text.splitlines():
        key, value = line.split("=", 1)
        if key in config:
            return _json_file("config-result.json", {
                "error": "duplicate key: " + key, "verdict": "fail"
            })
        config[key] = value
    return _json_file("config-result.json", {"config": config, "verdict": "pass"})


def _compose(inputs):
    members = inputs.get("files", inputs)
    return {"subject": members["member_a"] + members["member_b"]}


def _schedule(inputs):
    text = inputs.get("jobs.csv", inputs.get("jobs", ""))
    rows = list(csv.DictReader(text.splitlines()))
    jobs = [(row["job"], int(row["duration"]), index)
            for index, row in enumerate(rows)]

    def waits(ordered):
        elapsed = 0
        result = []
        for _job, duration, _index in ordered:
            result.append(elapsed)
            elapsed += duration
        return result

    fcfs = waits(jobs)
    sjf = waits(sorted(jobs, key=lambda item: (item[1], item[2])))

    def metrics(values):
        mean = (Decimal(sum(values)) / Decimal(len(values))).quantize(
            Decimal("0.000001"), rounding=ROUND_HALF_UP
        )
        return {"mean_wait": float(mean), "max_wait": max(values)}

    return _json_file("research.json", {"fcfs": metrics(fcfs), "sjf": metrics(sjf)})


def _threshold(inputs):
    metrics = inputs["metrics"]
    thresholds = inputs["thresholds"]
    p_limit = thresholds["p95_turnaround_max"]
    w_limit = thresholds["max_wait_max"]
    result = {}
    passing = []
    for policy, values in metrics.items():
        p_fail = values["p95_turnaround"] > p_limit
        w_fail = values["max_wait"] > w_limit
        if not p_fail and not w_fail:
            status = "pass"
            passing.append(policy)
        elif p_fail and w_fail:
            status = "fail_both"
        elif p_fail:
            status = "fail_p95_turnaround"
        else:
            status = "fail_max_wait"
        result[policy] = status
    result = {"claim": ("no_policy_satisfies_both_thresholds" if not passing
                         else "policies_satisfying_both_thresholds:" + ",".join(passing)), **result}
    return _json_file("claim.json", result)


def _unknowns(inputs):
    observations = inputs["observations"]
    return _json_file("limits.json", {
        "completed": observations["completed"],
        "rework_probability": (None if observations["rework_probability"] == "unknown"
                                else observations["rework_probability"]),
        "transfer_bytes": (None if observations["transfer_bytes"] == "unknown"
                            else observations["transfer_bytes"]),
        "claim": "finite_observation_only",
    })


def _members(inputs):
    declared = inputs["declared_members"]
    manifest = inputs["manifest_members"]
    counts = {member: manifest.count(member) for member in set(manifest)}
    members = [member for member in declared if member in manifest]
    missing = [member for member in declared if member not in manifest]
    extra = [member for member in manifest if member not in declared]
    duplicate = any(count > 1 for count in counts.values())
    verdict = "pass" if not missing and not extra and not duplicate else "fail"
    return _json_file("manifest-result.json", {
        "members": members, "missing": missing, "extra": extra, "verdict": verdict
    })


def _hash_members(inputs):
    manifest = inputs["manifest"]
    files = inputs.get("files", {})
    checked = sorted(manifest)
    mismatches = []
    for member in checked:
        if member not in files:
            mismatches.append(member)
            continue
        data = files[member]
        if isinstance(data, str):
            data = data.encode("utf-8")
        digest = hashlib.sha256(data).hexdigest()
        if digest != manifest[member]:
            mismatches.append(member)
    return _json_file("manifest-result.json", {
        "checked": checked, "mismatches": mismatches,
        "verdict": "pass" if not mismatches else "fail"
    })


def _selection(inputs):
    declared = inputs["declared_members"]
    selection = inputs["selection"]
    selected = [member for member in declared if selection.get(member) is True]
    skipped = [{"id": member, "reason": "selection=false"}
               for member in declared if selection.get(member) is False]
    missing = [member for member in declared if member not in selection]
    valid = (not missing and set(selection) == set(declared)
             and all(isinstance(selection[member], bool) for member in declared))
    return _json_file("selection.json", {
        "selected": selected, "skipped": skipped,
        "missing_records": missing, "verdict": "pass" if valid else "fail"
    })


def solve(task_type, inputs):
    """Return a mapping of required artifact filename to exact artifact value."""
    handlers = {
        "normalize_records": _records,
        "reject_duplicate_config": _config,
        "compose_two_members": _compose,
        "schedule_metrics": _schedule,
        "threshold_claim": _threshold,
        "preserve_unknowns": _unknowns,
        "exact_members": _members,
        "hash_bound_members": _hash_members,
        "selected_skipped": _selection,
    }
    name = _task_name(task_type)
    if name not in handlers:
        raise ValueError("unknown task_type: " + task_type)
    return handlers[name](inputs)


def main():
    request = json.load(sys.stdin)
    result = solve(request["task_type"], request.get("inputs", {}))
    json.dump(result, sys.stdout, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
