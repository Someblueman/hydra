#!/bin/sh
set -eu
digest="$(sha256sum "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | awk '{print $1}')"
{
    cat assessor.txt
    printf '\nsubject_sha256: %s\n\n<submitted_report>\n' "$digest"
    cat "$HYDRA_WORKFLOW_INPUTS_DIR/subject"
    printf '\n</submitted_report>\n'
} > "$HYDRA_WORKFLOW_OUTPUTS_DIR/prompt"
