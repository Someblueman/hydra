#!/bin/sh
# Optional planning projections feed the existing workflow runtime.
# Initialization is called inside cmd_workflow_plan's subshell, where these
# private variables are intentionally scoped for the nested cmd_workflow call.
# shellcheck disable=SC2030,SC2031

workflow_plan_tool() (
    cmd_fleet_dispatch workflow-plan "$@"
)

cmd_workflow_plan() (
    _cwp_action="${1:-}"
    case "$_cwp_action" in
        --workspace-owner|--workspace-status)
            _load_lib workflow_plan_launch
            shift
            if [ "$_cwp_action" = --workspace-owner ]; then workflow_plan_launch_owner "$@"
            else workflow_plan_launch_status "$@"; fi
            ;;
        schema)
            [ "$#" -eq 1 ] || exit 1
            workflow_plan_tool schema
            ;;
        validate|compile)
            if [ "$_cwp_action" = validate ]; then _cwp_count=3; else _cwp_count=4; fi
            [ "$#" -eq "$_cwp_count" ] || { cli_error 'workflow plan' invalid_arguments 'use validate <plan.json> <policy.json> or compile <plan.json> <policy.json> <new-output.json>' 'run hydra workflow plan --help'; exit 1; }
            _cwp_root="$(workflow_repo_root)" || exit 1
            if [ "$_cwp_action" = validate ]; then workflow_plan_tool validate "$2" "$3" "$_cwp_root"
            else workflow_plan_tool compile "$2" "$3" "$_cwp_root" "$4"; fi
            ;;
        show)
            if [ "$#" -eq 2 ]; then workflow_plan_tool preview "$2"
            elif [ "$#" -eq 3 ] && [ "$3" = --json ]; then workflow_plan_tool show "$2"
            else exit 1; fi
            ;;
        obligations)
            if [ "$#" -eq 2 ]; then workflow_plan_tool obligations "$2"
            elif [ "$#" -eq 3 ] && [ "$3" = --json ]; then workflow_plan_tool obligations "$2" --json
            else exit 1; fi
            ;;
        tui-data)
            [ "$#" -eq 2 ] || exit 1
            workflow_plan_tool tui-data "$2"
            ;;
        check-definition)
            [ "$#" -eq 3 ] || exit 1
            workflow_plan_tool check-definition "$2" "$3"
            ;;
        result)
            [ "$#" -eq 2 ] && hydra_valid_id "$2" || exit 1
            workflow_plan_tool result "$(workflow_runs_dir)/$2"
            ;;
        run)
            if [ "$#" -ne 4 ] || [ "$3" != --accept ]; then
                cli_error 'workflow plan' acceptance_required 'run requires <compiled.json> --accept <sha256>' 'review workflow plan show and accept its exact digest'
                exit 1
            fi
            _cwp_root="$(workflow_repo_root)" || exit 1
            _workflow_plan_stage="$(mktemp -d)" || exit 1
            trap 'rm -rf "$_workflow_plan_stage"' EXIT
            trap 'exit 130' INT
            trap 'exit 143' TERM
            trap 'exit 129' HUP
            workflow_plan_tool admit "$2" "$_cwp_root" "$4" "$_workflow_plan_stage" > "$_workflow_plan_stage/admission.json" || {
                cat "$_workflow_plan_stage/admission.json" >&2
                exit 1
            }
            _cwp_project="$(hydra_get_project_id)" || exit 1
            workflow_plan_tool heads "$_workflow_plan_stage/compiled.json" > "$_workflow_plan_stage/heads" || exit 1
            while IFS= read -r _cwp_head; do
                if state_v2_find_head_by_branch "$_cwp_project" "$_cwp_head" >/dev/null 2>&1; then
                    cli_error 'workflow plan' head_exists "durable head state exists for $_cwp_head" 'use fresh head names in a newly compiled and accepted plan'
                    exit 1
                fi
            done < "$_workflow_plan_stage/heads"
            _workflow_plan_accepted="$4"
            cmd_workflow run "$_workflow_plan_stage/workflow.yml"
            ;;
        ''|-h|--help)
            printf '%s\n' \
                'Usage: hydra workflow plan schema' \
                '       hydra workflow plan validate <plan.json> <policy.json>' \
                '       hydra workflow plan compile <plan.json> <policy.json> <new-output.json>' \
                '       hydra workflow plan show <compiled.json> [--json]' \
                '       hydra workflow plan obligations <compiled.json> [--json]' \
                '       hydra workflow plan run <compiled.json> --accept <sha256>' \
                '       hydra workflow plan result <run-id>' \
                '       hydra workflow plan check-definition <compiled.json> <check-id>' \
                'Compile from the source repository. Keep compiled output outside it.' \
                'Execution uses the existing workflow status, cancel and resume commands.'
            ;;
        *) cli_error 'workflow plan' invalid_arguments 'unknown planning command' 'run hydra workflow plan --help'; exit 1 ;;
    esac
)

workflow_plan_initialize() {
    [ -n "${_workflow_plan_stage:-}" ] || return 0
    cp "$_workflow_plan_stage/compiled.json" "$1/compiled.json" || return 1
    workflow_atomic_scalar "$1/plan-accepted" "$_workflow_plan_accepted" || return 1
    _wpi_timeout="$(workflow_plan_tool timeout "$1/compiled.json")" || return 1
    workflow_atomic_scalar "$1/plan-deadline" "$(($(date +%s) + _wpi_timeout))" || return 1
    workflow_plan_bindings_match "$1"
}

workflow_plan_bindings_match() (
    [ -f "$1/compiled.json" ] || exit 0
    _wpb_dir="$1"
    workflow_plan_tool bindings "$_wpb_dir/compiled.json" "$(workflow_repo_root)" "$(sed -n '1p' "$_wpb_dir/plan-accepted")" >/dev/null || exit 1
    workflow_plan_tool data-match "$_wpb_dir/compiled.json" "$_wpb_dir" >/dev/null || exit 1
    _wpb_tmp="$(mktemp -d)" || exit 1
    trap 'rm -rf "$_wpb_tmp"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    workflow_plan_tool projection "$_wpb_dir/compiled.json" > "$_wpb_tmp/workflow.yml" || exit 1
    workflow_parse "$_wpb_tmp/workflow.yml" normalized > "$_wpb_tmp/expected.yml" || exit 1
    workflow_parse "$_wpb_tmp/workflow.yml" runtime > "$_wpb_tmp/expected.tsv" || exit 1
    cmp -s "$_wpb_tmp/expected.yml" "$_wpb_dir/resolved.yml" &&
        cmp -s "$_wpb_tmp/expected.tsv" "$_wpb_dir/graph.tsv" || exit 1
    IFS="$(printf '\t')" read -r _wpb_header _wpb_id _wpb_parallel _wpb_disk _wpb_heads < "$_wpb_tmp/expected.tsv"
    [ "$(sed -n '1p' "$_wpb_dir/parallelism")" = "$_wpb_parallel" ] &&
        [ "$(sed -n '1p' "$_wpb_dir/disk-mb")" = "$_wpb_disk" ] &&
        [ "$(sed -n '1p' "$_wpb_dir/max-heads")" = "$_wpb_heads" ]
)

workflow_plan_expired() {
    [ -f "$1/compiled.json" ] || return 1
    _wpe_deadline="$(sed -n '1p' "$1/plan-deadline")"
    case "$_wpe_deadline" in ''|*[!0-9]*) return 0 ;; esac
    [ "$(date +%s)" -ge "$_wpe_deadline" ]
}

workflow_plan_finish() {
    [ -f "$1/compiled.json" ] || return 0
    workflow_plan_bindings_match "$1" && workflow_plan_tool finish "$1" > "$1/plan-verification.json" || return 1
    # Historical timing of the independent gate, bound to the accepted revision.
    # Optional telemetry must not change delivery. Invalidate old timing first,
    # and publish the binding last so a partial write remains unknown.
    rm -f "$1/verification-plan-sha256" "$1/verified-at" || return 0
    if ! { workflow_atomic_scalar "$1/verified-at" "$(date +%s)" &&
        workflow_atomic_scalar "$1/verification-plan-sha256" "$(sed -n '1p' "$1/plan-accepted")"; }; then
        rm -f "$1/verification-plan-sha256" "$1/verified-at" || true
    fi
    return 0
}

workflow_plan_repair() {
    if [ ! -f "$1/compiled.json" ]; then printf '0\n'; return 0; fi
    workflow_plan_tool "${2:-repair}" "$1" || {
        workflow_atomic_scalar "$1/state" recovery-required
        rm -rf "$1/.drive.lock"
        return 1
    }
}
