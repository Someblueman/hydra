#!/usr/bin/env python3
"""Independent deterministic implementation of the visible 9E contracts."""
import csv
import hashlib
import json
import sys
from decimal import Decimal, ROUND_HALF_UP


def artifact(name, value):
    return {name: json.dumps(value, ensure_ascii=False, separators=(",", ":"))}


def contract_name(identifier):
    pieces = identifier.split(".")
    return pieces[-2] if pieces[-1].startswith("v") else pieces[-1]


def normalize(data):
    lower = str.maketrans({chr(n): chr(n + 32) for n in range(65, 91)})
    body = "".join(item.strip(" \t\r\n").translate(lower) + "\n"
                   for item in data["records"])
    return {"report.txt": body}


def config(data):
    source = data.get("config.txt", data.get("config", ""))
    parsed = {}
    for line in source.splitlines():
        key, value = line.split("=", 1)
        if key in parsed:
            return artifact("config-result.json", {"error": "duplicate key: " + key,
                                                     "verdict": "fail"})
        parsed[key] = value
    return artifact("config-result.json", {"config": parsed, "verdict": "pass"})


def compose(data):
    files = data.get("files", data)
    return {"subject": files["member_a"] + files["member_b"]}


def schedule(data):
    source = data.get("jobs.csv", data.get("jobs", ""))
    rows = list(csv.DictReader(source.splitlines()))
    original = [(row["job"], int(row["duration"]), position)
                for position, row in enumerate(rows)]

    def waits(order):
        result, elapsed = [], 0
        for _job, duration, _position in order:
            result.append(elapsed)
            elapsed += duration
        return result

    def summarize(values):
        mean = (Decimal(sum(values)) / Decimal(len(values))).quantize(
            Decimal("0.000001"), rounding=ROUND_HALF_UP)
        return {"mean_wait": float(mean), "max_wait": max(values)}

    shortest = sorted(original, key=lambda row: (row[1], row[2]))
    return artifact("research.json", {"fcfs": summarize(waits(original)),
                                       "sjf": summarize(waits(shortest))})


def threshold(data):
    limits = data["thresholds"]
    statuses, passing = {}, []
    for policy, observed in data["metrics"].items():
        bad_p = observed["p95_turnaround"] > limits["p95_turnaround_max"]
        bad_w = observed["max_wait"] > limits["max_wait_max"]
        if not bad_p and not bad_w:
            statuses[policy] = "pass"
            passing.append(policy)
        elif bad_p and bad_w:
            statuses[policy] = "fail_both"
        elif bad_p:
            statuses[policy] = "fail_p95_turnaround"
        else:
            statuses[policy] = "fail_max_wait"
    claim = ("no_policy_satisfies_both_thresholds" if not passing else
             "policies_satisfying_both_thresholds:" + ",".join(passing))
    return artifact("claim.json", {"claim": claim, **statuses})


def preserve(data):
    observations = data["observations"]
    result = {"completed": observations["completed"],
              "rework_probability": observations["rework_probability"],
              "transfer_bytes": observations["transfer_bytes"],
              "claim": "finite_observation_only"}
    for field in ("rework_probability", "transfer_bytes"):
        if result[field] == "unknown":
            result[field] = None
    return artifact("limits.json", result)


def exact_members(data):
    declared = data["declared_members"]
    present = data["manifest_members"]
    counts = {member: present.count(member) for member in set(present)}
    missing = [member for member in declared if member not in present]
    extra = [member for member in present if member not in declared]
    duplicate = any(number > 1 for number in counts.values())
    return artifact("manifest-result.json", {
        "members": [member for member in declared if member in present],
        "missing": missing, "extra": extra,
        "verdict": "pass" if not missing and not extra and not duplicate else "fail"
    })


def bound_hashes(data):
    declarations, files = data["manifest"], data.get("files", {})
    checked, mismatches = sorted(declarations), []
    for member in checked:
        if member not in files:
            mismatches.append(member)
            continue
        raw = files[member]
        if isinstance(raw, str):
            raw = raw.encode("utf-8")
        if hashlib.sha256(raw).hexdigest() != declarations[member]:
            mismatches.append(member)
    return artifact("manifest-result.json", {
        "checked": checked, "mismatches": mismatches,
        "verdict": "pass" if not mismatches else "fail"
    })


def selection(data):
    declared, choices = data["declared_members"], data["selection"]
    selected = [member for member in declared if choices.get(member) is True]
    skipped = [{"id": member, "reason": "selection=false"}
               for member in declared if choices.get(member) is False]
    missing = [member for member in declared if member not in choices]
    valid = (not missing and set(choices) == set(declared) and
             all(isinstance(choices[member], bool) for member in declared))
    return artifact("selection.json", {
        "selected": selected, "skipped": skipped,
        "missing_records": missing, "verdict": "pass" if valid else "fail"
    })


def solve(task_type, inputs):
    operations = {"normalize_records": normalize,
                  "reject_duplicate_config": config,
                  "compose_two_members": compose,
                  "schedule_metrics": schedule,
                  "threshold_claim": threshold,
                  "preserve_unknowns": preserve,
                  "exact_members": exact_members,
                  "hash_bound_members": bound_hashes,
                  "selected_skipped": selection}
    name = contract_name(task_type)
    if name not in operations:
        raise ValueError("unknown task_type: " + task_type)
    return operations[name](inputs)


if __name__ == "__main__":
    request = json.load(sys.stdin)
    json.dump(solve(request["task_type"], request.get("inputs", {})),
              sys.stdout, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.write("\n")
