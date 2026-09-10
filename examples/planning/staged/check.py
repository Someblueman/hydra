#!/usr/bin/env python3
import hashlib, json, os, pathlib, sys
from unique_json import loads


def d(x):
    return hashlib.sha256(x).hexdigest()


def main():
    root = pathlib.Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"])
    out = pathlib.Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / "check.json"
    m = loads((root / "manifest").read_bytes())
    f = loads((root / "finding").read_bytes())
    r = loads((root / "subject").read_bytes())
    expected = [
        {"id": x["id"], "value": x["value"], "square": x["value"] ** 2}
        for x in m["items"]
        if x["enabled"]
    ]
    expected_report = {
        "schema_version": 1,
        "selected_ids": f["selected_ids"],
        "members": expected,
    }
    passed = json.dumps(r, sort_keys=True, separators=(",", ":")) == json.dumps(
        expected_report, sort_keys=True, separators=(",", ":")
    ) and f["selected_ids"] == [x["id"] for x in m["items"] if x["enabled"]]
    raw = {
        "actual": "pass" if passed else "fail",
        "expected_members": expected,
        "observed_members": r,
    }
    b = loads(pathlib.Path(os.environ["HYDRA_WORKFLOW_VALIDATION_FILE"]).read_bytes())[
        "data"
    ]
    obs = [
        {
            "id": "stage2-membership",
            "raw": raw,
            "raw_sha256": d(
                json.dumps(raw, sort_keys=True, separators=(",", ":")).encode()
            ),
        }
    ]
    rec = {
        "obligation_id": "stage2-check",
        "subject_manifest_sha256": d((root / "subject").read_bytes()),
        "validator_identity": "staged-check-v1",
        "validator_recipe_sha256": b["check-recipe"],
        "invocation": {
            "argv": ["python3", "check.py"],
            "exit_code": 0 if passed else 1,
        },
        "environment": {"host": "local", "toolchain": sys.version},
        "case_inventory": ["stage2-membership"],
        "observations": obs,
        "raw_evidence_sha256": d(
            json.dumps(obs, sort_keys=True, separators=(",", ":")).encode()
        ),
        "counts": {"executed": 1, "failed": 0 if passed else 1, "skipped": 0},
        "limitations": ["Finite staged fixture."],
    }
    result = {
        "schema_version": 3,
        "execution_status": "completed",
        "evidence_status": "valid",
        "domain_verdict": "pass" if passed else "fail",
        "verdict": "pass" if passed else "fail",
        "subject_sha256": rec["subject_manifest_sha256"],
        "validator_sha256": b["check"],
        "requirements": ["stage2"],
        "evidence": "Verified exact frozen stage-1 membership and arithmetic.",
        "limitations": rec["limitations"],
        "evidence_records": [rec],
    }
    out.write_text(json.dumps(result, separators=(",", ":")) + "\n")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
