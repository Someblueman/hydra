#!/bin/sh
# Acceptance fixture step: seal a small output for the handoff workflow.
set -eu
printf 'draft result\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/answer.txt"
