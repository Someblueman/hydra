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

workflow_plan_proposal() (
    _wpp_action="$1"
    shift
    _wpp_policy=false
    case "$_wpp_action" in
        propose)
            _wpp_usage='Usage: hydra workflow plan propose <draft.json> [--branch <head>] [--asset NAME=FILE]...'
            [ $# -ge 1 ] || { echo "$_wpp_usage" >&2; exit 2; }
            _wpp_input="$1"
            shift
            _wpp_branch="" _wpp_assets="" _wpp_asset_count=0
            while [ $# -gt 0 ]; do
                [ $# -ge 2 ] || { echo "$_wpp_usage" >&2; exit 2; }
                case "$1" in
                    --branch) [ -z "$_wpp_branch" ] || { echo "$_wpp_usage" >&2; exit 2; }; _wpp_branch="$2" ;;
                    --asset)
                        case "$2" in
                            ?*=?*) ;;
                            *) echo 'Each --asset takes NAME=FILE, where NAME is a plan ID.' >&2; exit 2 ;;
                        esac
                        case "$2" in *'
'*) echo 'Asset paths cannot contain newlines.' >&2; exit 2 ;; esac
                        _wpp_asset_count=$((_wpp_asset_count + 1))
                        [ "$_wpp_asset_count" -le 16 ] || { echo 'A proposal carries at most 16 assets.' >&2; exit 2; }
                        _wpp_assets="$_wpp_assets$2
"
                        ;;
                    *) echo "$_wpp_usage" >&2; exit 2 ;;
                esac
                shift 2
            done
            [ -n "$_wpp_branch" ] || _wpp_branch="$(git symbolic-ref --quiet --short HEAD)" || exit 1
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
            mkdir "$_wpp_tmp/assets" || exit 1
            # Assets are private copies beside the draft. The source checkout
            # is never read or written; a republish replaces the whole set.
            while IFS= read -r _wpp_asset; do
                [ -n "$_wpp_asset" ] || continue
                workflow_plan_tool proposal-asset "${_wpp_asset%%=*}" "${_wpp_asset#*=}" "$_wpp_tmp/assets" > "$_wpp_tmp/result.json" || {
                    printf 'Asset %s refused: ' "${_wpp_asset%%=*}" >&2
                    cat "$_wpp_tmp/result.json" >&2
                    exit 1
                }
            done <<EOF
$_wpp_assets
EOF
            workflow_plan_tool proposal-copy "$_wpp_input" "$_wpp_tmp/draft.json" "$_wpp_tmp/assets" > "$_wpp_tmp/result.json" || {
                cat "$_wpp_tmp/result.json" >&2
                exit 1
            }
            rm -rf "$_wpp_dir/.assets.previous" || exit 1
            if [ -d "$_wpp_dir/assets" ]; then mv "$_wpp_dir/assets" "$_wpp_dir/.assets.previous" || exit 1; fi
            mv "$_wpp_tmp/assets" "$_wpp_dir/assets" || exit 1
            mv "$_wpp_tmp/draft.json" "$_wpp_dir/draft.json" || exit 1
            rm -rf "$_wpp_dir/.assets.previous"
            _wpp_saved="$_wpp_branch"
            [ "$_wpp_asset_count" -eq 0 ] || _wpp_saved="$_wpp_branch with $_wpp_asset_count asset(s)"
            printf 'Proposal saved for %s. In Hydra press B, then P to review it. No validation or execution has occurred.\n' "$_wpp_saved"
            exit 0
        fi
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
    [ -f "$_wpp_dir/policy.json" ] || { echo 'Choose a local execution policy in Hydra before validating this proposal.' >&2; exit 1; }
    printf 'HYDRA_PLAN_PROPOSAL\t1\nP\t%s\t%s\n' "$_wpp_dir/draft.json" "$_wpp_dir/policy.json"
)
