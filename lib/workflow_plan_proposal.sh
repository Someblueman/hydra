#!/bin/sh
# Head-associated drafts. Proposal publication never grants execution approval.
workflow_plan_proposal() (
    _wpp_action="$1"
    shift
    _wpp_policy=false
    case "$_wpp_action" in
        propose)
            [ $# -ge 1 ] || { echo 'Usage: hydra workflow plan propose <draft.json> [--branch <head>]' >&2; exit 2; }
            _wpp_input="$1"
            shift
            if [ $# -eq 0 ]; then _wpp_branch="$(git symbolic-ref --quiet --short HEAD)" || exit 1
            elif [ $# -eq 2 ] && [ "$1" = --branch ]; then _wpp_branch="$2"
            else exit 2; fi
            ;;
        proposal)
            [ $# -ge 1 ] || exit 2
            _wpp_branch="$1"
            shift
            if [ $# -eq 1 ] && [ "$1" = --local-policy ]; then _wpp_policy=true
            elif [ $# -ne 0 ]; then exit 2; fi
            ;;
        *) exit 2 ;;
    esac
    parallel_head_load "$_wpp_branch" || { echo 'Select an existing head before proposing a plan.' >&2; exit 1; }
    _wpp_dir="$PARALLEL_HEAD_DIR/planning"
    case "$_wpp_dir" in *'	'*|*'
'*) echo 'Planning paths cannot contain tabs or newlines.' >&2; exit 1 ;; esac
    umask 077
    if [ "$_wpp_action" = propose ] || [ "$_wpp_policy" = true ]; then
        mkdir -p "$_wpp_dir" || exit 1
        _wpp_lock="head_${PARALLEL_HEAD_ID}"
        acquire_lock "$_wpp_lock" 'publish planning proposal' "$PARALLEL_HEAD_ID" || exit 1
        _wpp_tmp=""
        trap '[ -z "$_wpp_tmp" ] || rm -rf "$_wpp_tmp"; release_lock "$_wpp_lock"' EXIT
        trap 'exit 130' INT
        trap 'exit 143' TERM
        trap 'exit 129' HUP
        if [ "$_wpp_action" = propose ]; then
            # Re-read under the head lock so a resumed instance cannot be
            # overwritten by a stale agent from the preceding conversation.
            parallel_head_load "$_wpp_branch" || exit 1
            if { [ -n "${HYDRA_HEAD_ID:-}" ] && [ "$HYDRA_HEAD_ID" != "$PARALLEL_HEAD_ID" ]; } ||
               { [ -n "${HYDRA_INSTANCE_ID:-}" ] && [ "$HYDRA_INSTANCE_ID" != "$LIFECYCLE_INSTANCE_ID" ]; }; then
                echo 'Proposal refused: this agent no longer owns the selected head instance.' >&2
                exit 1
            fi
            _wpp_tmp="$(mktemp -d "$_wpp_dir/.proposal.XXXXXX")" || exit 1
            workflow_plan_tool proposal-copy "$_wpp_input" "$_wpp_tmp/draft.json" >/dev/null || exit 1
            mv "$_wpp_tmp/draft.json" "$_wpp_dir/draft.json" || exit 1
            printf 'Proposal saved for %s. In Hydra press B, then P to review it. No validation or execution has occurred.\n' "$_wpp_branch"
            exit 0
        fi
        workflow_atomic_scalar "$_wpp_dir/policy.json" '{"schema_version":1,"envelope":{"hosts":["local"],"tools":["sh","git"],"effects":["execute","worktree"],"writes":[],"parallelism":1,"timeout_seconds":300,"artifact_bytes":1048576,"max_heads":4,"disk_mb":1,"retry_budget":0,"repair_budget":0}}' || exit 1
    fi
    [ -f "$_wpp_dir/draft.json" ] || {
        echo 'No proposal from this agent yet. Ask it to discuss the objective, use hydra workflow plan schema, and publish with hydra workflow plan propose <draft.json>.' >&2
        exit 1
    }
    [ -f "$_wpp_dir/policy.json" ] || { echo 'Choose a local execution policy in Hydra before validating this proposal.' >&2; exit 1; }
    printf 'HYDRA_PLAN_PROPOSAL\t1\nP\t%s\t%s\n' "$_wpp_dir/draft.json" "$_wpp_dir/policy.json"
)
