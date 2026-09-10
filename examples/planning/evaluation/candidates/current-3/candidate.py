#!/usr/bin/env python3
"""Deterministic solver for the nine public planner contracts."""
import csv
import hashlib
import json
import sys
from decimal import Decimal, ROUND_HALF_UP


def jfile(filename, value):
    return {filename: json.dumps(value, ensure_ascii=False, separators=(",", ":"))}


def operation(identifier):
    fields = identifier.split(".")
    return fields[-2] if fields[-1].startswith("v") else fields[-1]


def normalize(arg):
    table = str.maketrans({chr(x): chr(x + 32) for x in range(65, 91)})
    return {"report.txt": "".join(
        value.strip(" \t\r\n").translate(table) + "\n" for value in arg["records"]
    )}


def reject_duplicates(arg):
    source = arg.get("config.txt", arg.get("config", ""))
    values = {}
    for row in source.splitlines():
        key, value = row.split("=", 1)
        if key in values:
            return jfile("config-result.json", {"error": "duplicate key: " + key,
                                                 "verdict": "fail"})
        values[key] = value
    return jfile("config-result.json", {"config": values, "verdict": "pass"})


def compose(arg):
    files = arg.get("files", arg)
    return {"subject": files["member_a"] + files["member_b"]}


def schedule(arg):
    source = arg.get("jobs.csv", arg.get("jobs", ""))
    rows = list(csv.DictReader(source.splitlines()))
    rows = [(row["job"], int(row["duration"]), index)
            for index, row in enumerate(rows)]

    def waits(order):
        total, output = 0, []
        for _name, duration, _index in order:
            output.append(total)
            total += duration
        return output

    def stats(values):
        average = (Decimal(sum(values)) / Decimal(len(values))).quantize(
            Decimal("0.000001"), rounding=ROUND_HALF_UP)
        return {"mean_wait": float(average), "max_wait": max(values)}

    sjf = sorted(rows, key=lambda row: (row[1], row[2]))
    return jfile("research.json", {"fcfs": stats(waits(rows)),
                                    "sjf": stats(waits(sjf))})


def claims(arg):
    limits = arg["thresholds"]
    statuses, good = {}, []
    for name, metrics in arg["metrics"].items():
        p_bad = metrics["p95_turnaround"] > limits["p95_turnaround_max"]
        w_bad = metrics["max_wait"] > limits["max_wait_max"]
        if not p_bad and not w_bad:
            status = "pass"
            good.append(name)
        elif p_bad and w_bad:
            status = "fail_both"
        elif p_bad:
            status = "fail_p95_turnaround"
        else:
            status = "fail_max_wait"
        statuses[name] = status
    heading = ("no_policy_satisfies_both_thresholds" if not good else
               "policies_satisfying_both_thresholds:" + ",".join(good))
    return jfile("claim.json", {"claim": heading, **statuses})


def limits(arg):
    source = arg["observations"]
    answer = {"completed": source["completed"],
              "rework_probability": source["rework_probability"],
              "transfer_bytes": source["transfer_bytes"],
              "claim": "finite_observation_only"}
    for field in ("rework_probability", "transfer_bytes"):
        if answer[field] == "unknown":
            answer[field] = None
    return jfile("limits.json", answer)


def members(arg):
    declared, actual = arg["declared_members"], arg["manifest_members"]
    counts = {item: actual.count(item) for item in set(actual)}
    missing = [item for item in declared if item not in actual]
    extra = [item for item in actual if item not in declared]
    valid = not missing and not extra and not any(n > 1 for n in counts.values())
    return jfile("manifest-result.json", {
        "members": [item for item in declared if item in actual],
        "missing": missing, "extra": extra, "verdict": "pass" if valid else "fail"
    })


def hashes(arg):
    declared, files = arg["manifest"], arg.get("files", {})
    checked, mismatches = sorted(declared), []
    for item in checked:
        if item not in files:
            mismatches.append(item)
            continue
        value = files[item]
        value = value.encode("utf-8") if isinstance(value, str) else value
        if hashlib.sha256(value).hexdigest() != declared[item]:
            mismatches.append(item)
    return jfile("manifest-result.json", {
        "checked": checked, "mismatches": mismatches,
        "verdict": "pass" if not mismatches else "fail"
    })


def selections(arg):
    declared, choices = arg["declared_members"], arg["selection"]
    chosen = [item for item in declared if choices.get(item) is True]
    skipped = [{"id": item, "reason": "selection=false"}
               for item in declared if choices.get(item) is False]
    missing = [item for item in declared if item not in choices]
    valid = (not missing and set(choices) == set(declared) and
             all(isinstance(choices[item], bool) for item in declared))
    return jfile("selection.json", {
        "selected": chosen, "skipped": skipped, "missing_records": missing,
        "verdict": "pass" if valid else "fail"
    })


def solve(task_type, inputs):
    handlers = {"normalize_records": normalize,
                "reject_duplicate_config": reject_duplicates,
                "compose_two_members": compose,
                "schedule_metrics": schedule,
                "threshold_claim": claims,
                "preserve_unknowns": limits,
                "exact_members": members,
                "hash_bound_members": hashes,
                "selected_skipped": selections}
    name = operation(task_type)
    if name not in handlers:
        raise ValueError("unknown task_type: " + task_type)
    return handlers[name](inputs)


if __name__ == "__main__":
    request = json.load(sys.stdin)
    result = solve(request["task_type"], request.get("inputs", {}))
    json.dump(result, sys.stdout, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.write("\n")
