#!/usr/bin/env python3
import json, os, pathlib, sys

ident, value = sys.argv[1], int(sys.argv[2])
out = pathlib.Path(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / f"result-{ident}.json"
out.write_text(
    json.dumps(
        {"id": ident, "value": value, "square": value * value}, separators=(",", ":")
    )
    + "\n"
)
