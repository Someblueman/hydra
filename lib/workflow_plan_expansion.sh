#!/bin/sh
# Head-associated plan expansions (roadmap 9F): a proposal that adds new
# nodes, dependencies on sealed deliverables, new checks and the frozen joins
# they feed to a finished plan run. Admission compiles the proposal with the
# one compiler under the base run's own policy and records version N+1 under
# the head's private planning state. Admitting never approves or executes:
# each round runs through the ordinary exact-digest `workflow plan run`.

# Usage: workflow_plan_expansion_dir <head> <run-id>
# Loads the head and sets WORKFLOW_PLAN_EXPANSIONS to the chain directory.
workflow_plan_expansion_dir() {
    parallel_head_load "$1" || { echo 'Select an existing head before admitting or reading plan expansions.' >&2; return 1; }
    case "$PARALLEL_HEAD_DIR" in *'	'*|*'
'*) echo 'Planning paths cannot contain tabs or newlines.' >&2; return 1 ;; esac
    WORKFLOW_PLAN_EXPANSIONS="$PARALLEL_HEAD_DIR/planning/expansions/$2"
}

# Usage: workflow_plan_expansion <head> --run <run-id> [--proposal <expansion.json>] [--json]
workflow_plan_expansion() (
    _wpx_usage='Usage: hydra workflow plan expand <head> --run <run-id> [--proposal <expansion.json>] [--json]'
    [ $# -ge 1 ] || { echo "$_wpx_usage" >&2; exit 2; }
    _wpx_head="$1"
    shift
    _wpx_run="" _wpx_proposal="" _wpx_json=0 _wpx_tmp=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --run)
                if [ $# -lt 2 ] || [ -n "$_wpx_run" ]; then echo "$_wpx_usage" >&2; exit 2; fi
                _wpx_run="$2"; shift 2 ;;
            --proposal)
                if [ $# -lt 2 ] || [ -n "$_wpx_proposal" ]; then echo "$_wpx_usage" >&2; exit 2; fi
                _wpx_proposal="$2"; shift 2 ;;
            --json) _wpx_json=1; shift ;;
            *) echo "$_wpx_usage" >&2; exit 2 ;;
        esac
    done
    hydra_valid_id "$_wpx_run" || { cli_error 'workflow plan expand' invalid_arguments '--run takes one workflow run ID' 'use a run ID reported by hydra workflow plan run'; exit 2; }
    _wpx_runs="$(workflow_runs_dir)" || exit 1
    [ -d "$_wpx_runs/$_wpx_run" ] || { cli_error 'workflow plan expand' run_not_found "workflow run not found: $_wpx_run" 'expand a run created by hydra workflow plan run'; exit 1; }
    workflow_plan_expansion_dir "$_wpx_head" "$_wpx_run" || exit 1
    _wpx_dir="$WORKFLOW_PLAN_EXPANSIONS"
    if [ -z "$_wpx_proposal" ]; then
        if [ "$_wpx_json" -eq 1 ]; then workflow_plan_tool expansion-show "$_wpx_dir" "$_wpx_runs/$_wpx_run" "$_wpx_runs"
        else workflow_plan_tool expansion-view "$_wpx_dir" "$_wpx_runs/$_wpx_run" "$_wpx_runs"; fi
        exit $?
    fi
    [ -f "$_wpx_proposal" ] || { cli_error 'workflow plan expand' invalid_arguments "proposal file not found: $_wpx_proposal" 'write the proposal as hydra-plan-expansion-1 JSON'; exit 1; }
    _wpx_root="$(workflow_repo_root)" || {
        cli_error 'workflow plan expand' invalid_source "source $(pwd) is not a Git repository" "run the admission from the project's main checkout at the base run's commit"
        exit 1
    }
    umask 077
    hydra_private_mkdir "$_wpx_dir" || exit 1
    _wpx_lock="head_${PARALLEL_HEAD_ID}"
    acquire_lock "$_wpx_lock" 'admit plan expansion' "$PARALLEL_HEAD_ID" || exit 1
    trap '[ -z "$_wpx_tmp" ] || rm -rf "$_wpx_tmp"; release_lock "$_wpx_lock"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    # An interrupted admission leaves only a scratch directory; it never
    # becomes a round, so a later admission simply starts over.
    _wpx_tmp="$(mktemp -d "$_wpx_dir/.round.XXXXXX")" || exit 1
    _wpx_result="$(workflow_plan_tool expansion-admit "$_wpx_tmp" "$_wpx_dir" "$_wpx_runs/$_wpx_run" "$_wpx_runs" "$_wpx_proposal" "$_wpx_root" "$(date +%s)")" || {
        if [ "$_wpx_json" -eq 1 ]; then printf '%s\n' "$_wpx_result"; else printf '%s\n' "$_wpx_result" >&2; fi
        exit 1
    }
    _wpx_round="$(printf '%s\n' "$_wpx_result" | sed -n 's/^.*"data":{"round":\([0-9][0-9]*\),.*$/\1/p')"
    case "$_wpx_round" in ''|*[!0-9]*) exit 1 ;; esac
    if [ -e "$_wpx_dir/round-$_wpx_round" ]; then
        echo "Round $_wpx_round already exists for run $_wpx_run; the admission was not published." >&2
        exit 1
    fi
    mv "$_wpx_tmp" "$_wpx_dir/round-$_wpx_round" || exit 1
    _wpx_tmp=""
    if [ "$_wpx_json" -eq 1 ]; then printf '%s\n' "$_wpx_result"
    else
        printf 'Expansion round %s admitted for run %s. Nothing is approved or running: review hydra workflow plan show on its compiled.json and execute it with the exact digest below.\n' "$_wpx_round" "$_wpx_run"
        workflow_plan_tool expansion-view "$_wpx_dir" "$_wpx_runs/$_wpx_run" "$_wpx_runs"
    fi
)

# Usage: workflow_plan_expansion_bind <compiled.json> <accepted-sha256> <stage-dir>
# Sets _workflow_plan_expansion to the round directory when the artifact is an
# admitted round; refuses a changed round, a changed dependency or a round that
# already executed. Called inside cmd_workflow_plan's run subshell.
workflow_plan_expansion_bind() {
    _wpxb_round="$(dirname "$1")"
    [ -f "$_wpxb_round/expansion.json" ] || return 0
    _wpxb_runs="$(workflow_runs_dir)" || return 1
    workflow_plan_tool expansion-bind "$_wpxb_round" "$2" "$_wpxb_runs" > "$3/expansion-bind.json" || {
        cat "$3/expansion-bind.json" >&2
        return 1
    }
    # A link to a run that was never published is cleared: no work started.
    [ ! -f "$_wpxb_round/run-id" ] || rm -f "$_wpxb_round/run-id" || return 1
    _workflow_plan_expansion="$_wpxb_round"
}

# Usage: workflow_plan_expansion_link <run-dir>
# Records the round in the run and the run in the round. The link is created
# exclusively, so a round never gets two runs.
workflow_plan_expansion_link() {
    [ -n "${_workflow_plan_expansion:-}" ] || return 0
    cp "$_workflow_plan_expansion/expansion.json" "$1/plan-expansion.json" || return 1
    _wpxl_run="$(awk -F '\t' '$1 == "run_id" { print $2; exit }' "$1/manifest.tsv")"
    hydra_valid_id "$_wpxl_run" || return 1
    (umask 077; set -C; printf '%s\n' "$_wpxl_run" > "$_workflow_plan_expansion/run-id") 2>/dev/null || {
        echo 'Error: this expansion round already has a run; a version executes once' >&2
        return 1
    }
}
