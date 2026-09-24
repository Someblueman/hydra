#!/usr/bin/env python3
"""Deterministic interactive provider fixture; never claims live-model coverage."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

print("DETERMINISTIC PROVIDER FIXTURE - ready to discuss", flush=True)
for line in sys.stdin:
    request = line.strip()
    if request not in ("draft", "revise"):
        print(
            "Discuss the plan; type draft or revise. Hydra owns execution approval.",
            flush=True,
        )
        continue
    plan = json.loads(Path(os.environ["UX_PLAN_TEMPLATE"]).read_text())
    if request == "revise":
        plan["objective"] += " (revised after discussion)"
    with tempfile.TemporaryDirectory(prefix="provider-proposal-") as directory:
        draft = Path(directory) / "draft.json"
        draft.write_text(json.dumps(plan))
        result = subprocess.run(
            ["hydra", "workflow", "plan", "propose", str(draft)],
            capture_output=True,
            text=True,
            check=False,
            timeout=15,
        )
        print(result.stdout or result.stderr, flush=True)
    print(
        "PROPOSAL REVISION READY" if request == "revise" else "PROPOSAL READY",
        flush=True,
    )
