#!/usr/bin/env python3
"""Independent finding and joined-report checks for the two staged boundaries."""
import hashlib
import json
import os
from pathlib import Path
import sys
from unique_json import loads


def canonical(value, stage1=False):
    text = json.dumps(value, sort_keys=True, separators=(",", ":"))
    return (text.replace("/", "\\/") if stage1 else text).encode()


def digest(value):
    return hashlib.sha256(value).hexdigest()


def check_finding(manifest_bytes, finding, expected):
    selected = [item["id"] for item in expected]
    fields = {"schema_version", "status", "source_path", "source_sha256",
              "selected_ids", "result_sha256", "results", "limitations"}
    passed = (isinstance(finding, dict) and set(finding) == fields
              and type(finding.get("schema_version")) is int and finding["schema_version"] == 3
              and finding.get("status") == "pass" and finding.get("source_path") == "manifest.json"
              and finding.get("selected_ids") == selected
              and finding.get("source_sha256") == digest(manifest_bytes)
              and canonical(finding.get("results")) == canonical(expected)
              and finding.get("result_sha256") == digest(canonical(expected)))
    return passed, {"selected_ids": finding.get("selected_ids") if isinstance(finding, dict) else None,
                    "expected": selected}


def check_join(root, report, expected):
    finding = loads((root / "finding").read_bytes())
    selected = [item["id"] for item in expected]
    joined = {"schema_version": 1, "selected_ids": finding["selected_ids"], "members": expected}
    passed = canonical(report) == canonical(joined) and finding["selected_ids"] == selected
    return passed, {"expected_members": expected, "observed_members": report}


def main():
    if sys.argv[1:] not in ([], ["stage1"]):
        raise ValueError("usage: check.py [stage1]")
    stage1 = sys.argv[1:] == ["stage1"]
    stage = "stage1" if stage1 else "stage2"
    case = "stage1-finding" if stage1 else "stage2-membership"
    root = Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"])
    manifest_bytes = (root / "manifest").read_bytes()
    manifest = loads(manifest_bytes)
    content = (root / "subject").read_bytes()
    subject = loads(content)
    expected = [{"id": item["id"], "value": item["value"], "square": item["value"] ** 2}
                for item in manifest["items"] if item["enabled"]]
    passed, raw = (check_finding(manifest_bytes, subject, expected) if stage1
                   else check_join(root, subject, expected))
    verdict = "pass" if passed else "fail"
    raw["actual"] = verdict
    bindings = loads(Path(os.environ["HYDRA_WORKFLOW_VALIDATION_FILE"]).read_bytes())["data"]
    observations = [{"id": case, "raw": raw, "raw_sha256": digest(canonical(raw, stage1))}]
    record = {
        "obligation_id": stage + "-check", "subject_manifest_sha256": digest(content),
        "validator_identity": "staged-stage1-check-v1" if stage1 else "staged-check-v1",
        "validator_recipe_sha256": bindings["check-recipe"],
        "invocation": {"argv": ["python3", "check.py"] + (["stage1"] if stage1 else []),
                       "exit_code": 0 if passed else 1},
        "environment": {"host": "local", "toolchain": sys.version},
        "case_inventory": [case], "observations": observations,
        "raw_evidence_sha256": digest(canonical(observations, stage1)),
        "counts": {"executed": 1, "failed": 0 if passed else 1, "skipped": 0},
        "limitations": ["Finite staged fixture."],
    }
    result = {
        "schema_version": 3, "execution_status": "completed", "evidence_status": "valid",
        "domain_verdict": verdict, "verdict": verdict,
        "subject_sha256": record["subject_manifest_sha256"], "validator_sha256": bindings["check"],
        "requirements": [stage], "evidence": (
            "Validated stage-1 finding source binding and selected membership." if stage1
            else "Verified exact frozen stage-1 membership and arithmetic."),
        "limitations": record["limitations"], "evidence_records": [record],
    }
    (Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / "check.json").write_text(
        json.dumps(result, separators=(",", ":")) + "\n")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
