#!/usr/bin/env python3
"""Run a frozen candidate against supplied task inputs; do not decide acceptance."""

import json
import os
from pathlib import Path
from evaluate import CASE_CONTRACTS, public_inputs, run_candidate

inputs = Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"])
output = Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / "subject.json"
requests = json.loads((inputs / "requests").read_text())
results = []
for case in requests["cases"]:
    value, cpu, wall, error = run_candidate(
        Path("candidate.py").resolve(),
        CASE_CONTRACTS[case["id"]],
        public_inputs(case["id"], case["inputs"]),
    )
    results.append(
        {
            "id": case["id"],
            "value": value,
            "error": error,
            "cpu_seconds": cpu,
            "wall_seconds": wall,
        }
    )
output.write_text(
    json.dumps(
        {"schema_version": 1, "results": results},
        allow_nan=False,
        separators=(",", ":"),
    )
    + "\n"
)
