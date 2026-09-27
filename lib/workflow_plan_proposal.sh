#!/bin/sh
# Head-associated drafts. Proposal publication never grants execution approval.

# Usage: workflow_plan_local_profile <head-dir>
# Prints the head's recorded agent profile when a plan can name it as a tool.
workflow_plan_local_profile() {
    _wplp_profile="$(sed -n '1p' "$1/profile" 2>/dev/null || true)"
    case "$_wplp_profile" in
        ''|-|none|[!a-z]*|*[!a-z0-9_-]*|*[-_]|*[-_][-_]*) return 0 ;;
    esac
    [ "${#_wplp_profile}" -le 64 ] || return 0
    printf '%s\n' "$_wplp_profile"
}

# Usage: workflow_plan_local_policy [profile]
# The guided local policy: one worker, writes only inside heads the plan itself
# spawns (@spawned:*), and the head's own agent profile when it has one.
workflow_plan_local_policy() {
    _wplpol_tools='"sh","git","make"'
    [ -z "${1:-}" ] || _wplpol_tools="$_wplpol_tools,\"profile:$1\""
    printf '{"schema_version":1,"envelope":{"hosts":["local"],"tools":[%s],"effects":["execute","worktree"],"writes":["@spawned:*"],"parallelism":1,"timeout_seconds":3600,"artifact_bytes":1048576,"max_heads":4,"disk_mb":1024,"retry_budget":0,"repair_budget":0}}' "$_wplpol_tools"
}

# Usage: workflow_plan_draft_digest <file>
workflow_plan_draft_digest() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
    else shasum -a 256 "$1" | awk '{print $1}'; fi
}

# Usage: workflow_plan_returned <planning-dir>
# Succeeds while the current draft is the exact revision the user sent back.
workflow_plan_returned() {
    [ -f "$1/returned" ] && [ -f "$1/draft.json" ] || return 1
    [ "$(sed -n '1p' "$1/returned")" = "$(workflow_plan_draft_digest "$1/draft.json")" ]
}

# Records that the user requested changes to the current draft. The marker binds
# that exact draft digest, so neither review nor launch can use it until the
# agent republishes; the feedback is kept beside it for the agent to read.
workflow_plan_return() {
    [ -f "$1/draft.json" ] || { echo 'No proposal to return: the agent has not published a draft for this head.' >&2; return 1; }
    [ "${#2}" -le 4096 ] || { echo 'Feedback is limited to 4096 bytes.' >&2; return 1; }
    _wpr_digest="$(workflow_plan_draft_digest "$1/draft.json")" || return 1
    state_v2_write_text "$1/feedback" "$2
" || return 1
    workflow_atomic_scalar "$1/returned" "$_wpr_digest" || return 1
    printf 'Proposal %.12s for %s returned for changes. It cannot be validated or executed until the agent publishes a revision with hydra workflow plan propose.\n' "$_wpr_digest" "$3"
}

workflow_plan_proposal() (
    _wpp_action="$1"
    shift
    _wpp_policy=false
    _wpp_return=""
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
            elif [ $# -eq 2 ] && [ "$1" = --return ] && [ -n "$2" ]; then _wpp_return="$2"
            elif [ $# -ne 0 ]; then exit 2; fi
            ;;
        *) exit 2 ;;
    esac
    parallel_head_load "$_wpp_branch" || { echo 'Select an existing head before proposing a plan.' >&2; exit 1; }
    _wpp_dir="$PARALLEL_HEAD_DIR/planning"
    case "$_wpp_dir" in *'	'*|*'
'*) echo 'Planning paths cannot contain tabs or newlines.' >&2; exit 1 ;; esac
    umask 077
    if [ "$_wpp_action" = propose ] || [ "$_wpp_policy" = true ] || [ -n "$_wpp_return" ]; then
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
            # A republished draft answers any earlier request for changes.
            rm -f "$_wpp_dir/returned" "$_wpp_dir/feedback"
            printf 'Proposal saved for %s. In Hydra press B, then P to review it. No validation or execution has occurred.\n' "$_wpp_branch"
            exit 0
        fi
        if [ -n "$_wpp_return" ]; then
            workflow_plan_return "$_wpp_dir" "$_wpp_return" "$_wpp_branch"
            exit $?
        fi
        workflow_plan_returned "$_wpp_dir" && {
            echo 'This proposal was returned for changes. Wait for the agent to publish a revision with hydra workflow plan propose.' >&2
            exit 1
        }
        _wpp_local_profile="$(workflow_plan_local_profile "$PARALLEL_HEAD_DIR")"
        workflow_atomic_scalar "$_wpp_dir/policy.json" "$(workflow_plan_local_policy "$_wpp_local_profile")" || exit 1
        # The versioned projection below is parsed exactly; notes go only to a terminal.
        if [ -z "$_wpp_local_profile" ] && [ -t 2 ]; then
            printf 'Local policy for %s authorizes no agent profile: the head records none. Plans can run sh, git and make only.\n' "$_wpp_branch" >&2
        fi
    fi
    [ -f "$_wpp_dir/draft.json" ] || {
        echo 'No proposal from this agent yet. Ask it to discuss the objective, use hydra workflow plan schema, and publish with hydra workflow plan propose <draft.json>.' >&2
        exit 1
    }
    if workflow_plan_returned "$_wpp_dir"; then
        echo 'This proposal was returned for changes. Wait for the agent to publish a revision with hydra workflow plan propose.' >&2
        exit 1
    fi
    [ -f "$_wpp_dir/policy.json" ] || { echo 'Choose a local execution policy in Hydra before validating this proposal.' >&2; exit 1; }
    printf 'HYDRA_PLAN_PROPOSAL\t1\nP\t%s\t%s\n' "$_wpp_dir/draft.json" "$_wpp_dir/policy.json"
)
