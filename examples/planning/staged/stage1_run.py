#!/usr/bin/env python3
import hashlib, json, os, pathlib
from unique_json import loads

root = pathlib.Path(os.environ["HYDRA_WORKFLOW_INPUTS_DIR"])
out = pathlib.Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / "finding.json"
raw = (root / "manifest").read_bytes()
m = loads(raw)
items = m.get("items", [])
if (
    not isinstance(m, dict)
    or set(m) != {"schema_version", "items"}
    or type(m.get("schema_version")) is not int
    or m["schema_version"] != 1
    or not isinstance(items, list)
    or len(items) > 8
    or any(
        not isinstance(x, dict)
        or type(x.get("enabled")) is not bool
        or type(x.get("value")) is not int
        or isinstance(x.get("value"), bool)
        or not -10 <= x["value"] <= 10
        for x in items
    )
):
    raise ValueError("manifest bounds")
seen = set()
for x in items:
    if (
        not isinstance(x, dict)
        or set(x) != {"id", "value", "enabled"}
        or not isinstance(x.get("id"), str)
        or not __import__("re").fullmatch(r"[a-z][a-z0-9-]{0,31}", x["id"])
        or x["id"] in seen
    ):
        raise ValueError("manifest members")
    seen.add(x["id"])
results = [
    {"id": x["id"], "value": x["value"], "square": x["value"] ** 2}
    for x in m["items"]
    if x["enabled"]
]
rb = json.dumps(results, sort_keys=True, separators=(",", ":")).encode()
out.write_text(
    json.dumps(
        {
            "schema_version": 3,
            "status": "pass",
            "source_path": "manifest.json",
            "source_sha256": hashlib.sha256(raw).hexdigest(),
            "selected_ids": [x["id"] for x in m["items"] if x["enabled"]],
            "result_sha256": hashlib.sha256(rb).hexdigest(),
            "results": results,
            "limitations": [
                "Finite local arithmetic selection; no runtime graph expansion."
            ],
        },
        sort_keys=True,
        separators=(",", ":"),
    )
    + "\n"
)
