#!/bin/sh
# Plan asset run on the verifier head: the worker's summary must name both files.
set -eu
printf 'Checking the worker summary\n'
for file in greeting.txt lib/greet.sh; do
    grep -Fq "$file" "$HYDRA_WORKFLOW_INPUTS_DIR/subject" || { printf '[FAIL] summary names %s\n' "$file"; exit 1; }
    printf '[PASS] summary names %s\n' "$file"
done
hash=$(shasum -a 256 "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)
printf '{"schema_version":1,"verdict":"pass","subject_sha256":"%s","requirements":["summary-names-files"],"evidence":"The summary names both changed files"}\n' \
    "$hash" > "$HYDRA_WORKFLOW_OUTPUTS_DIR/check.json"
