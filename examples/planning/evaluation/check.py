#!/usr/bin/env python3
"""Independent fixed-case semantic checker with Hydra's actual validator binding."""

import hashlib
import json
import os
from pathlib import Path
import sys
from evaluate import SOLVERS, artifact_value, equal_artifact


def canonical(value):
    return (
        json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        .replace("/", "\\/")
        .encode()
    )


def digest(value):
    return hashlib.sha256(value).hexdigest()


def main():
    inputs = Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"])
    output = Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / "check.json"
    raw_subject = (inputs / "subject").read_bytes()
    subject = json.loads(raw_subject)
    cases = json.loads((inputs / "oracle").read_text())["cases"]
    results = subject.get("results", [])
    complete = subject.get("schema_version") == 1 and [
        result.get("id") for result in results
    ] == [case["id"] for case in cases]
    observations = []
    for index, case in enumerate(cases):
        expected = SOLVERS[case["id"]](case["inputs"])
        result = results[index] if complete else {}
        correct = (
            complete
            and result.get("error") is None
            and equal_artifact(result.get("value"), expected)
        )
        rejected = all(
            not equal_artifact(artifact_value(control), expected)
            for control in case["incorrect_artifacts"]
        )
        raw = {
            "actual": "pass" if correct and rejected else "fail",
            "expected": expected,
            "observed": result.get("value"),
            "error": result.get("error"),
            "corruptions_rejected": rejected,
            "class": case["class"],
        }
        observations.append(
            {"id": case["id"], "raw": raw, "raw_sha256": digest(canonical(raw))}
        )
    failed = sum(observation["raw"]["actual"] != "pass" for observation in observations)
    passed = complete and failed == 0
    binding = json.loads(
        Path(os.environ["HYDRA_WORKFLOW_VALIDATION_FILE"]).read_text()
    )["data"]
    record = {
        "obligation_id": "semantic-output",
        "subject_manifest_sha256": digest(raw_subject),
        "validator_identity": "fixed-independent-oracle-v2",
        "validator_recipe_sha256": binding["check-recipe"],
        "invocation": {
            "argv": ["python3", "check.py"],
            "exit_code": 0 if passed else 1,
        },
        "environment": {"host": "local", "toolchain": sys.version},
        "case_inventory": [case["id"] for case in cases],
        "observations": observations,
        "raw_evidence_sha256": digest(canonical(observations)),
        "counts": {"executed": len(cases), "failed": failed, "skipped": 0},
        "limitations": [
            "Nine finite cases; interface failures do not establish semantic planning improvement."
        ],
    }
    report = {
        "schema_version": 3,
        "execution_status": "completed",
        "evidence_status": "valid",
        "domain_verdict": "pass" if passed else "fail",
        "verdict": "pass" if passed else "fail",
        "subject_sha256": record["subject_manifest_sha256"],
        "validator_sha256": binding["check"],
        "requirements": ["verified-cases"],
        "evidence": "Recomputed every fixed case and rejected all declared corruptions.",
        "limitations": record["limitations"],
        "evidence_records": [record],
    }
    output.write_text(json.dumps(report, separators=(",", ":")) + "\n")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
