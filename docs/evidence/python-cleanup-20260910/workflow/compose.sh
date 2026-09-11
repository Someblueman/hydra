#!/bin/sh
# Seal exactly the supplied cleanup patch against this bound source snapshot.
set -eu
git apply --check "$HYDRA_WORKFLOW_INPUTS_DIR/patch"
cp "$HYDRA_WORKFLOW_INPUTS_DIR/patch" "$HYDRA_WORKFLOW_OUTPUTS_DIR/cleanup.patch"
