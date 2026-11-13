#!/bin/sh
# Head-associated planning decisions: which compiled alternative the candidate
# generator selected, which it rejected and why, with the exact inputs of the
# deterministic comparison, so the comparison can be replayed byte for byte.
# Recording a decision never validates, approves or executes anything.

# Usage: workflow_plan_decision_dir <head>
# Loads the head and sets WORKFLOW_PLAN_DECISIONS to its private decisions directory.
workflow_plan_decision_dir() {
    parallel_head_load "$1" || { echo 'Select an existing head before recording or reading a planning decision.' >&2; return 1; }
    case "$PARALLEL_HEAD_DIR" in *'	'*|*'
'*) echo 'Planning paths cannot contain tabs or newlines.' >&2; return 1 ;; esac
    WORKFLOW_PLAN_DECISIONS="$PARALLEL_HEAD_DIR/planning/decisions"
}

workflow_plan_decision_id_valid() {
    case "$1" in
        ????????????????) case "$1" in *[!0-9a-f]*) return 1 ;; esac; return 0 ;;
        *) return 1 ;;
    esac
}

# Usage: workflow_plan_decision_record <head> --select <compiled.json> <reason>
#            [--reject <compiled.json> <reason>]... [--estimates <file>] [--json]
workflow_plan_decision_record() (
    _wpdr_usage='Usage: hydra workflow plan decision record <head> --select <compiled.json> <reason> [--reject <compiled.json> <reason>]... [--estimates <estimates.json>] [--json]'
    [ $# -ge 1 ] || { echo "$_wpdr_usage" >&2; exit 2; }
    _wpdr_head="$1"
    shift
    _wpdr_selected="" _wpdr_reason="" _wpdr_estimates="-" _wpdr_json=0 _wpdr_rejects=0
    _wpdr_tmp=""
    # Rejected alternatives are appended behind the original arguments as
    # positional pairs, so the native record operation receives one argv.
    _wpdr_left=$#
    while [ "$_wpdr_left" -gt 0 ]; do
        case "$1" in
            --select)
                if [ "$_wpdr_left" -lt 3 ] || [ -n "$_wpdr_selected" ]; then echo "$_wpdr_usage" >&2; exit 2; fi
                _wpdr_selected="$2" _wpdr_reason="$3"
                shift 3; _wpdr_left=$((_wpdr_left - 3)) ;;
            --reject)
                [ "$_wpdr_left" -ge 3 ] || { echo "$_wpdr_usage" >&2; exit 2; }
                _wpdr_rejects=$((_wpdr_rejects + 1))
                [ "$_wpdr_rejects" -le 8 ] || { echo 'A decision records at most eight rejected alternatives.' >&2; exit 2; }
                set -- "$@" "$2" "$3"
                shift 3; _wpdr_left=$((_wpdr_left - 3)) ;;
            --estimates)
                [ "$_wpdr_left" -ge 2 ] || { echo "$_wpdr_usage" >&2; exit 2; }
                _wpdr_estimates="$2"
                shift 2; _wpdr_left=$((_wpdr_left - 2)) ;;
            --json) _wpdr_json=1; shift; _wpdr_left=$((_wpdr_left - 1)) ;;
            *) echo "$_wpdr_usage" >&2; exit 2 ;;
        esac
    done
    [ -n "$_wpdr_selected" ] || { echo "$_wpdr_usage" >&2; exit 2; }
    workflow_plan_decision_dir "$_wpdr_head" || exit 1
    _wpdr_dir="$WORKFLOW_PLAN_DECISIONS"
    umask 077
    hydra_private_mkdir "$_wpdr_dir" || exit 1
    _wpdr_lock="head_${PARALLEL_HEAD_ID}"
    acquire_lock "$_wpdr_lock" 'record planning decision' "$PARALLEL_HEAD_ID" || exit 1
    trap '[ -z "$_wpdr_tmp" ] || rm -rf "$_wpdr_tmp"; release_lock "$_wpdr_lock"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    _wpdr_tmp="$(mktemp -d "$_wpdr_dir/.decision.XXXXXX")" || exit 1
    hydra_private_mkdir "$_wpdr_tmp/inputs" || exit 1
    _wpdr_result="$(workflow_plan_tool decision-record "$_wpdr_tmp" "$(date +%s)" "$_wpdr_estimates" "$_wpdr_selected" "$_wpdr_reason" "$@")" || {
        if [ "$_wpdr_json" -eq 1 ]; then printf '%s\n' "$_wpdr_result"; else printf '%s\n' "$_wpdr_result" >&2; fi
        exit 1
    }
    _wpdr_id="$(printf '%s\n' "$_wpdr_result" | sed -n 's/.*"decision_id":"\([0-9a-f]\{16\}\)".*/\1/p')"
    workflow_plan_decision_id_valid "$_wpdr_id" || exit 1
    if [ -e "$_wpdr_dir/$_wpdr_id" ]; then
        echo "Decision $_wpdr_id already exists for $_wpdr_head; identical records are kept once." >&2
        exit 1
    fi
    mv "$_wpdr_tmp" "$_wpdr_dir/$_wpdr_id" || exit 1
    _wpdr_tmp=""
    if [ "$_wpdr_json" -eq 1 ]; then printf '%s\n' "$_wpdr_result"
    else printf 'Decision %s recorded for %s: selected %.12s, %s rejected. Reasons are recorded, not verified; no validation, approval or execution has occurred.\n' \
        "$_wpdr_id" "$_wpdr_head" "$(printf '%s\n' "$_wpdr_result" | sed -n 's/.*"selected_sha256":"\([0-9a-f]*\)".*/\1/p')" "$_wpdr_rejects"
    fi
)

# Usage: workflow_plan_decision_show <head> <decision-id> [--json]
workflow_plan_decision_show() (
    [ $# -eq 2 ] || { [ $# -eq 3 ] && [ "$3" = --json ]; } || { echo 'Usage: hydra workflow plan decision show <head> <decision-id> [--json]' >&2; exit 2; }
    workflow_plan_decision_id_valid "$2" || { echo 'Decision IDs are 16 lowercase hex characters.' >&2; exit 2; }
    workflow_plan_decision_dir "$1" || exit 1
    _wpds_dir="$WORKFLOW_PLAN_DECISIONS"
    [ -d "$_wpds_dir/$2" ] || { echo "No decision $2 is recorded for $1." >&2; exit 1; }
    if [ $# -eq 3 ]; then workflow_plan_tool decision-show "$_wpds_dir/$2" "$2"
    else workflow_plan_tool decision-view "$_wpds_dir/$2" "$2"; fi
)

# Usage: workflow_plan_decision_list <head> [--json]
workflow_plan_decision_list() (
    [ $# -eq 1 ] || { [ $# -eq 2 ] && [ "$2" = --json ]; } || { echo 'Usage: hydra workflow plan decision list <head> [--json]' >&2; exit 2; }
    workflow_plan_decision_dir "$1" || exit 1
    _wpdl_dir="$WORKFLOW_PLAN_DECISIONS"
    if [ $# -eq 2 ]; then
        [ -d "$_wpdl_dir" ] || { printf '{"schema_version":1,"ok":true,"command":"workflow plan decision list","data":{"decisions":[]}}\n'; exit 0; }
        workflow_plan_tool decision-list "$_wpdl_dir"
        exit $?
    fi
    _wpdl_found=0
    for _wpdl_entry in "$_wpdl_dir"/*/decision.json; do
        [ -f "$_wpdl_entry" ] || continue
        _wpdl_found=1
        _wpdl_id="$(basename "$(dirname "$_wpdl_entry")")"
        workflow_plan_decision_id_valid "$_wpdl_id" || continue
        printf '%s\n' "$_wpdl_id"
    done
    [ "$_wpdl_found" -eq 1 ] || echo "No planning decisions are recorded for $1." >&2
)

workflow_plan_decision() {
    _wpd_action="${1:-}"
    [ $# -eq 0 ] || shift
    case "$_wpd_action" in
        record) workflow_plan_decision_record "$@" ;;
        show) workflow_plan_decision_show "$@" ;;
        list) workflow_plan_decision_list "$@" ;;
        *)
            printf '%s\n' \
                'Usage: hydra workflow plan decision record <head> --select <compiled.json> <reason> [--reject <compiled.json> <reason>]... [--estimates <estimates.json>] [--json]' \
                '       hydra workflow plan decision list <head> [--json]' \
                '       hydra workflow plan decision show <head> <decision-id> [--json]' >&2
            return 2 ;;
    esac
}
