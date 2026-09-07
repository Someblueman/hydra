#!/bin/sh
set -eu
printf 'Delivered report\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.txt"
