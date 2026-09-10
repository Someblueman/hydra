#!/bin/sh
set -eu
{
    cat "$HYDRA_WORKFLOW_INPUTS_DIR/draft"
    printf '\n\n## Sealed evidence\n\nWorkload SHA-256: `'
    cat "$HYDRA_WORKFLOW_INPUTS_DIR/workload_sha256"
    printf '`\n\nEnvironment:\n\n```\n'
    cat "$HYDRA_WORKFLOW_INPUTS_DIR/environment"
    printf '```\n\nRaw samples:\n\n```csv\n'
    cat "$HYDRA_WORKFLOW_INPUTS_DIR/raw"
    printf '```\n'
} > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.md"
