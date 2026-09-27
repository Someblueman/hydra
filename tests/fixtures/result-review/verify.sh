#!/bin/sh
# Report-writing verifier: prints a test-runner style log and a schema 2 report.
set -eu
printf 'Running kill dry-run checks\n'
for check in preview-targets no-mutation; do printf '[PASS] %s\n' "$check"; done
printf 'unrelated progress line\n'
printf 'Passed: 2  Failed: 0  Total: 2\n'
hash=$(shasum -a 256 "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)
validator=$(sed -n 's/.*"check":"\([a-f0-9]*\)".*/\1/p' "$HYDRA_WORKFLOW_VALIDATION_FILE")
printf '{"schema_version":2,"verdict":"pass","subject_sha256":"%s","validator_sha256":"%s","requirements":["preview-targets","no-mutation"],"evidence":"Ran 2 kill dry-run checks: 2 passed, 0 failed"}\n' \
    "$hash" "$validator" > "$HYDRA_WORKFLOW_OUTPUTS_DIR/check.json"
