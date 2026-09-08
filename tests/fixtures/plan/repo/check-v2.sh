#!/bin/sh
set -eu
cmp "$HYDRA_WORKFLOW_INPUTS_DIR/subject" "$HYDRA_WORKFLOW_INPUTS_DIR/expected"
hash=$(shasum -a 256 "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)
validator=$(sed -n 's/.*"check":"\([a-f0-9]*\)".*/\1/p' "$HYDRA_WORKFLOW_VALIDATION_FILE")
[ "${#validator}" -eq 64 ]
printf '{"schema_version":2,"verdict":"pass","subject_sha256":"%s","validator_sha256":"%s","requirements":["content"],"evidence":"Compared exact expected text"}\n' "$hash" "$validator" > "$HYDRA_WORKFLOW_OUTPUTS_DIR/check.json"
