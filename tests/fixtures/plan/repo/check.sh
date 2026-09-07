#!/bin/sh
set -eu
cmp "$HYDRA_WORKFLOW_INPUTS_DIR/subject" "$HYDRA_WORKFLOW_INPUTS_DIR/expected"
hash=$(shasum -a 256 "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)
printf '{"schema_version":1,"verdict":"pass","subject_sha256":"%s","requirements":["content"],"evidence":"Compared exact expected text"}\n' "$hash" > "$HYDRA_WORKFLOW_OUTPUTS_DIR/check.json"
