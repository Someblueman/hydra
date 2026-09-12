#!/bin/sh
# Read-only exact-attempt review. Native code owns identity, evidence and
# reference handling; shell only routes the public command.

workflow_review() (
    case "$#" in 5|6|13|14) ;; *)
        cli_error workflow review_invalid_arguments \
            'review requires PROJECT RUN STEP ATTEMPT REVISION or the exact 13-field selection, plus optional REFERENCES_JSON' \
            'supply the exact selected identity from workflow attention'
        exit 2
        ;;
    esac
    cmd_fleet_dispatch workflow-review "$@"
)
