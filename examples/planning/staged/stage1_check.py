#!/usr/bin/env python3
import hashlib, json, os, pathlib, sys
from unique_json import loads


def canon(x):
    return (
        json.dumps(x, sort_keys=True, separators=(",", ":"))
        .replace("/", "\\/")
        .encode()
    )


def main():
    root = pathlib.Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"])
    out = pathlib.Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / "check.json"
    m = loads((root / "manifest").read_bytes())
    f = loads((root / "subject").read_bytes())
    b = loads(pathlib.Path(os.environ["HYDRA_WORKFLOW_VALIDATION_FILE"]).read_bytes())[
        "data"
    ]
    expected = [x["id"] for x in m["items"] if x["enabled"]]
    results = [
        {"id": x["id"], "value": x["value"], "square": x["value"] ** 2}
        for x in m["items"]
        if x["enabled"]
    ]
    expected_fields = {
        "schema_version",
        "status",
        "source_path",
        "source_sha256",
        "selected_ids",
        "result_sha256",
        "results",
        "limitations",
    }
    passed = (
        isinstance(f, dict)
        and set(f) == expected_fields
        and type(f.get("schema_version")) is int
        and f["schema_version"] == 3
        and f.get("status") == "pass"
        and f.get("source_path") == "manifest.json"
        and f.get("selected_ids") == expected
        and f.get("source_sha256")
        == hashlib.sha256((root / "manifest").read_bytes()).hexdigest()
        and canon(f.get("results")) == canon(results)
        and f.get("result_sha256")
        == hashlib.sha256(
            json.dumps(results, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()
    )
    raw = {
        "actual": "pass" if passed else "fail",
        "selected_ids": f.get("selected_ids"),
        "expected": expected,
    }
    obs = [
        {
            "id": "stage1-finding",
            "raw": raw,
            "raw_sha256": hashlib.sha256(canon(raw)).hexdigest(),
        }
    ]
    rec = {
        "obligation_id": "stage1-check",
        "subject_manifest_sha256": hashlib.sha256(
            (root / "subject").read_bytes()
        ).hexdigest(),
        "validator_identity": "staged-stage1-check-v1",
        "validator_recipe_sha256": b["check-recipe"],
        "invocation": {
            "argv": ["python3", "stage1_check.py"],
            "exit_code": 0 if passed else 1,
        },
        "environment": {"host": "local", "toolchain": sys.version},
        "case_inventory": ["stage1-finding"],
        "observations": obs,
        "raw_evidence_sha256": hashlib.sha256(canon(obs)).hexdigest(),
        "counts": {"executed": 1, "failed": 0 if passed else 1, "skipped": 0},
        "limitations": ["Finite staged fixture."],
    }
    out.write_text(
        json.dumps(
            {
                "schema_version": 3,
                "execution_status": "completed",
                "evidence_status": "valid",
                "domain_verdict": "pass" if passed else "fail",
                "verdict": "pass" if passed else "fail",
                "subject_sha256": rec["subject_manifest_sha256"],
                "validator_sha256": b["check"],
                "requirements": ["stage1"],
                "evidence": "Validated stage-1 finding source binding and selected membership.",
                "limitations": rec["limitations"],
                "evidence_records": [rec],
            },
            separators=(",", ":"),
        )
        + "\n"
    )
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
