#!/usr/bin/env python3
"""PRE-COMPILE stage 2 from one exact, fresh stage-1 finding."""

import hashlib, json, os, pathlib, re, subprocess, sys
from unique_json import loads


def sha(b):
    return hashlib.sha256(b).hexdigest()


def fail(s):
    raise ValueError(s)


def load_manifest(path):
    raw = path.read_bytes()
    if len(raw) > 4096:
        fail("manifest exceeds 4096 bytes")
    obj = loads(raw)
    if (
        not isinstance(obj, dict)
        or set(obj) != {"schema_version", "items"}
        or type(obj["schema_version"]) is not int
        or obj["schema_version"] != 1
    ):
        fail("manifest schema")
    seen = set()
    if not isinstance(obj["items"], list) or len(obj["items"]) > 8:
        fail("manifest cardinality")
    for x in obj["items"]:
        if (
            not isinstance(x, dict)
            or set(x) != {"id", "value", "enabled"}
            or not isinstance(x["id"], str)
            or not re.fullmatch(r"[a-z][a-z0-9-]{0,31}", x["id"])
            or x["id"] in seen
            or type(x["value"]) is not int
            or isinstance(x["value"], bool)
            or not -10 <= x["value"] <= 10
            or type(x["enabled"]) is not bool
        ):
            fail("manifest member")
        seen.add(x["id"])
    return obj


def main(argv):
    if len(argv) != 4:
        return 2
    manifest_path = pathlib.Path(argv[1])
    run_id = argv[2]
    output = pathlib.Path(argv[3])
    if not re.fullmatch(r"run_[a-z0-9]+", run_id):
        fail("invalid workflow run ID")
    if manifest_path.name != "manifest.json":
        fail("manifest path must be manifest.json")
    manifest = load_manifest(manifest_path)
    hydra = os.environ.get("HYDRA_BIN", "hydra")
    proc = subprocess.run(
        [hydra, "workflow", "plan", "result", run_id],
        cwd=manifest_path.parent,
        env=os.environ.copy(),
        capture_output=True,
        text=True,
        timeout=30,
    )
    if proc.returncode != 0:
        fail("public workflow result refused or unavailable")
    envelope = loads(proc.stdout.encode())
    result = envelope.get("data") if isinstance(envelope, dict) else None
    if not isinstance(result, dict):
        fail("public workflow result malformed")
    if (
        set(result)
        != {"schema_version", "plan_sha256", "verdict", "deliverables", "checks"}
        or result["schema_version"] != 1
        or result["verdict"] != "pass"
    ):
        fail("workflow result is not an accepted passing delivery")
    delivery = result["deliverables"].get("report")
    check = result["checks"].get("check")
    if (
        not isinstance(delivery, dict)
        or not isinstance(check, dict)
        or check.get("verdict") != "pass"
    ):
        fail("workflow result lacks passing stage-1 check")
    finding_path = pathlib.Path(delivery.get("path", ""))
    if not finding_path.is_file() or delivery.get("type") != "file":
        fail("workflow result finding path unavailable")
    if finding_path.stat().st_size > 8192:
        fail("finding exceeds 8192 bytes")
    finding_bytes = finding_path.read_bytes()
    if delivery.get("sha256") != sha(finding_bytes):
        fail("workflow result finding hash mismatch")
    local_finding = manifest_path.parent / "finding.json"
    if local_finding.read_bytes() != finding_bytes:
        fail("local finding does not match accepted stage-1 bytes")
    finding = loads(finding_bytes)
    raw = manifest_path.read_bytes()
    if set(finding) != {
        "limitations",
        "result_sha256",
        "results",
        "schema_version",
        "selected_ids",
        "source_path",
        "source_sha256",
        "status",
    }:
        fail("finding schema")
    if (
        type(finding["schema_version"]) is not int
        or finding["schema_version"] != 3
        or finding["status"] != "pass"
    ):
        fail("finding status/schema")
    if finding["source_path"] != "manifest.json" or finding["source_sha256"] != sha(
        raw
    ):
        fail("stale or changed finding source")
    expected = [x["id"] for x in manifest["items"] if x["enabled"]]
    if finding["selected_ids"] != expected:
        fail("finding selection mismatch")
    expected_results = [
        {"id": x["id"], "value": x["value"], "square": x["value"] ** 2}
        for x in manifest["items"]
        if x["enabled"]
    ]
    if json.dumps(finding["results"], sort_keys=True) != json.dumps(
        expected_results, sort_keys=True
    ):
        fail("finding results mismatch")
    result_raw = json.dumps(
        expected_results, sort_keys=True, separators=(",", ":")
    ).encode()
    if finding["result_sha256"] != sha(result_raw):
        fail("finding result hash mismatch")
    steps = []

    def add(i, r, k, n, a):
        steps.append(
            {"id": i, "role": r, "kind": k, "needs": n, "writes": [], "args": a}
        )

    for ident in expected:
        add(
            "spawn-item-" + ident,
            "work",
            "spawn",
            [],
            {"branch": "staged-item-" + ident, "terminal_mode": "headless"},
        )
    for x in manifest["items"]:
        if x["enabled"]:
            add(
                "work-item-" + x["id"],
                "work",
                "exec",
                ["spawn-item-" + x["id"]],
                {
                    "head": "staged-item-" + x["id"],
                    "argv": ["python3", "worker.py", x["id"], str(x["value"])],
                    "timeout": 30,
                },
            )
    add(
        "spawn-compose",
        "work",
        "spawn",
        [],
        {"branch": "staged-compose", "terminal_mode": "headless"},
    )
    add(
        "compose",
        "compose",
        "exec",
        ["spawn-compose"] + ["work-item-" + x for x in expected],
        {"head": "staged-compose", "argv": ["python3", "compose.py"], "timeout": 30},
    )
    add(
        "spawn-check",
        "work",
        "spawn",
        [],
        {"branch": "staged-check", "terminal_mode": "headless"},
    )
    add(
        "check",
        "verify",
        "exec",
        ["spawn-check", "compose"],
        {"head": "staged-check", "argv": ["python3", "check.py"], "timeout": 30},
    )
    ds = {}
    for x in expected:
        ds["work-item-" + x] = {
            "outputs": {
                "result": {
                    "type": "file",
                    "path": "result-" + x + ".json",
                    "max_bytes": 128,
                }
            }
        }
    ci = {"finding": {"input": "finding"}, "manifest": {"input": "manifest"}}
    for x in expected:
        ci["member-" + x] = {"step": "work-item-" + x, "output": "result"}
    ds["compose"] = {
        "inputs": ci,
        "outputs": {
            "report": {"type": "file", "path": "report.json", "max_bytes": 8192}
        },
    }
    ds["check"] = {
        "inputs": {
            "subject": {"step": "compose", "output": "report"},
            "manifest": {"input": "manifest"},
            "finding": {"input": "finding"},
        },
        "outputs": {
            "check": {"type": "object", "path": "check.json", "max_bytes": 8192}
        },
    }
    plan = {
        "schema_version": 1,
        "id": "staged-stage2",
        "objective": "Execute the exact bounded members selected by a fresh stage-1 finding and verify the fixed join.",
        "context": [],
        "assumptions": [
            "Stage 2 membership is frozen by the accepted stage-1 finding."
        ],
        "questions": [],
        "envelope": {
            "hosts": ["local"],
            "tools": ["python3"],
            "effects": ["worktree", "execute"],
            "writes": [],
            "parallelism": 4,
            "timeout_seconds": 300,
            "artifact_bytes": 65536,
            "max_heads": 10,
            "disk_mb": 1,
            "retry_budget": 0,
            "repair_budget": 0,
        },
        "steps": steps,
        "deliverables": [
            {
                "id": "report",
                "description": "Stage 2 selected-member report",
                "step": "compose",
                "output": "report",
                "destination": "run-artifact",
            }
        ],
        "checks": [
            {
                "id": "check",
                "method": "executable",
                "definition": json.dumps(
                    {
                        "predicate": "equals",
                        "cases": [{"id": "stage2-membership", "expected": "pass"}],
                    },
                    separators=(",", ":"),
                ),
                "step": "check",
                "input": "subject",
                "report": "check",
                "deliverable": "report",
            }
        ],
        "requirements": [
            {
                "id": "stage2",
                "criterion": "Only the fresh stage-1 selection is executed and every selected result is verified.",
                "deliverable": "report",
                "check": "check",
            }
        ],
        "data": {
            "schema_version": 1,
            "inputs": {
                "manifest": {
                    "path": "manifest.json",
                    "type": "file",
                    "max_bytes": 4096,
                },
                "finding": {"path": "finding.json", "type": "file", "max_bytes": 8192},
            },
            "steps": ds,
        },
        "obligations": [
            {
                "id": "stage2-check",
                "requirement": "stage2",
                "intent_ref": "objective",
                "subject": {
                    "deliverable": "report",
                    "step": "compose",
                    "output": "report",
                },
                "criterion": "Only the fresh stage-1 selection is executed and every selected result is verified.",
                "evaluation": {"method": "executable", "check": "check"},
                "required_evidence": ["subject_sha256", "verdict", "evidence"],
                "environment": {
                    "hosts": ["local"],
                    "tools": ["python3"],
                    "effects": ["execute"],
                },
                "completion_rule": "verdict=pass",
                "limitations": ["Finite staged fixture."],
            }
        ],
    }
    output.write_text(json.dumps(plan, indent=2) + "\n")


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv) or 0)
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as e:
        print("precompile error:", e, file=sys.stderr)
        sys.exit(1)
