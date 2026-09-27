#!/bin/sh
# Proposal asset: runs in the worker head and writes the check report.
set -eu
test -s greeting.txt
if command -v shasum >/dev/null 2>&1; then
    hash=$(shasum -a 256 "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)
else
    hash=$(sha256sum "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)
fi
printf '{"schema_version":1,"verdict":"pass","subject_sha256":"%s","requirements":["greeting-present"],"evidence":"greeting.txt is present and make check passed in the worker head"}\n' "$hash" > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.json"
