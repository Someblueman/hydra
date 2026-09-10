#!/usr/bin/env python3
import json, os, pathlib

root = pathlib.Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"])
out = pathlib.Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / "report.json"
f = json.loads((root / "finding").read_text())
members = []
for x in f["selected_ids"]:
    members.append(json.loads((root / ("member-" + x)).read_text()))
out.write_text(
    json.dumps(
        {"schema_version": 1, "selected_ids": f["selected_ids"], "members": members},
        separators=(",", ":"),
    )
    + "\n"
)
