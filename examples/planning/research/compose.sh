#!/bin/sh
set -eu
{
    cat "$HYDRA_WORKFLOW_INPUTS_DIR/draft"
    printf '\n\n## Reproducibility appendix\n\nMetrics (time units from the input trace):\n\n```csv\n'
    cat "$HYDRA_WORKFLOW_INPUTS_DIR/metrics"
    # Markdown fences are literal output, not shell command substitutions.
    # shellcheck disable=SC2016
    printf '```\n\nComplete schedule:\n\n```csv\n'
    cat "$HYDRA_WORKFLOW_INPUTS_DIR/schedule"
    printf '```\n'
} > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.md"
