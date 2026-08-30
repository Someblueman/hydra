#!/bin/sh
# Hydra 1.7 parallel-safety command handlers.

cmd_claim() {
    _cc_action="${1:-list}"
    [ $# -eq 0 ] || shift
    case "$_cc_action" in
        add)
            _cc_branch="${1:-}"
            [ -n "$_cc_branch" ] || { cli_error claim invalid_input "head is required" "run hydra claim add <head> --path <pattern> --access read|write --reason <text> --expires-at <epoch>"; return 1; }
            shift
            _cc_path="" _cc_access="" _cc_reason="" _cc_expiry=""
            while [ $# -gt 0 ]; do
                case "$1" in
                    --path) [ $# -ge 2 ] || return 1; _cc_path="$2"; shift 2 ;;
                    --access) [ $# -ge 2 ] || return 1; _cc_access="$2"; shift 2 ;;
                    --reason) [ $# -ge 2 ] || return 1; _cc_reason="$2"; shift 2 ;;
                    --expires-at) [ $# -ge 2 ] || return 1; _cc_expiry="$2"; shift 2 ;;
                    *) cli_error claim invalid_input "unknown option '$1'" "run hydra claim add --help"; return 1 ;;
                esac
            done
            if _cc_id="$(parallel_claim_add "$_cc_branch" "$_cc_path" "$_cc_access" "$_cc_reason" "$_cc_expiry")"; then
                echo "Claim $_cc_id added for $_cc_branch"
            else
                cli_error claim invalid_input "claim could not be created" "check the head, relative pattern, access mode, and future expiry"
                return 1
            fi
            ;;
        remove)
            _cc_id="${1:-}"
            if [ $# -ne 1 ] || ! parallel_claim_remove "$_cc_id"; then
                cli_error claim not_found "claim could not be removed" "run hydra claim list"
                return 1
            fi
            echo "Claim $_cc_id removed"
            ;;
        list)
            _cc_json=0
            [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = --json ]; } || { cli_error claim invalid_input "unexpected arguments" "run hydra claim list --json"; return 1; }
            [ $# -eq 0 ] || _cc_json=1
            _cc_rows="$(parallel_claim_rows)" || return 1
            _cc_tab="$(printf '\t')"
            if [ "$_cc_json" -eq 1 ]; then
                printf '{"schema_version":1,"ok":true,"command":"claim list","data":{"claims":['
                _cc_first=1
                while IFS="$_cc_tab" read -r _cc_id _cc_head _cc_path _cc_access _cc_expiry _cc_reason; do
                    [ -n "$_cc_id" ] || continue
                    [ "$_cc_first" -eq 1 ] || printf ','
                    _cc_first=0
                    printf '{"claim_id":"%s","owner_head":"%s","path_pattern":"%s","access":"%s","expires_at":%s,"reason":"%s"}' \
                        "$_cc_id" "$_cc_head" "$(json_escape "$_cc_path")" "$_cc_access" "$_cc_expiry" "$(json_escape "$_cc_reason")"
                done <<EOF
$_cc_rows
EOF
                printf ']}}\n'
            else
                if [ -z "$_cc_rows" ]; then
                    echo "No active claims"
                else
                    printf '%s\n' "$_cc_rows" | while IFS="$_cc_tab" read -r _cc_id _cc_head _cc_path _cc_access _cc_expiry _cc_reason; do
                        printf '%s  %s  %s  %s  expires=%s  %s\n' "$_cc_id" "$_cc_head" "$_cc_access" "$_cc_path" "$_cc_expiry" "$_cc_reason"
                    done
                fi
            fi
            ;;
        *) cli_error claim invalid_input "unknown action '$_cc_action'" "use add, list, or remove"; return 1 ;;
    esac
}

cmd_scope() {
    _cs_action="${1:-}"
    [ -n "$_cs_action" ] || { cli_error scope invalid_input "action is required" "use set, show, or check"; return 1; }
    shift
    _cs_branch="${1:-}"
    [ -n "$_cs_branch" ] || { cli_error scope invalid_input "head is required" "run hydra scope $_cs_action <head>"; return 1; }
    shift
    case "$_cs_action" in
        set)
            _cs_rules=""
            _cs_tab="$(printf '\t')"
            while [ $# -gt 0 ]; do
                case "$1" in
                    --read|--write)
                        [ $# -ge 2 ] || return 1
                        _cs_mode="${1#--}"
                        parallel_validate_path_pattern "$2" || { cli_error scope invalid_input "invalid relative scope pattern '$2'" "use a repository-relative shell pattern"; return 1; }
                        if [ -n "$_cs_rules" ]; then
                            _cs_rules="$_cs_rules
$_cs_mode$_cs_tab$2"
                        else
                            _cs_rules="$_cs_mode$_cs_tab$2"
                        fi
                        shift 2
                        ;;
                    *) cli_error scope invalid_input "unknown option '$1'" "use --read or --write"; return 1 ;;
                esac
            done
            [ -n "$_cs_rules" ] || { cli_error scope invalid_input "at least one scope is required" "pass --write <pattern> or --read <pattern>"; return 1; }
            parallel_scope_write "$_cs_branch" "$_cs_rules" || return 1
            echo "Scope updated for $_cs_branch"
            ;;
        show)
            [ $# -eq 0 ] || return 1
            parallel_head_load "$_cs_branch" || return 1
            if [ -s "$LIFECYCLE_HEAD_DIR/scopes" ]; then
                cat "$LIFECYCLE_HEAD_DIR/scopes"
            else
                echo "No scope configured for $_cs_branch"
            fi
            ;;
        check)
            _cs_json=0
            [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = --json ]; } || return 1
            [ $# -eq 0 ] || _cs_json=1
            if _cs_rows="$(parallel_scope_rows "$_cs_branch")"; then
                :
            else
                _cs_status=$?
                if [ "$_cs_status" -eq 2 ]; then
                    cli_error scope unavailable "head has no configured scope" "run hydra scope set $_cs_branch --write '<pattern>'"
                fi
                return 1
            fi
            _cs_violations=0
            _cs_tab="$(printf '\t')"
            while IFS="$_cs_tab" read -r _cs_access _cs_path; do
                case "$_cs_access" in read-only|out-of-scope) _cs_violations=$((_cs_violations + 1)) ;; esac
            done <<EOF
$_cs_rows
EOF
            if [ "$_cs_json" -eq 1 ]; then
                printf '{"schema_version":1,"ok":true,"command":"scope check","data":{"branch":"%s","violations":%s,"paths":[' "$(json_escape "$_cs_branch")" "$_cs_violations"
                _cs_first=1
                while IFS="$_cs_tab" read -r _cs_access _cs_path; do
                    [ -n "$_cs_path" ] || continue
                    [ "$_cs_first" -eq 1 ] || printf ','
                    _cs_first=0
                    printf '{"path":"%s","access":"%s"}' "$(json_escape "$_cs_path")" "$_cs_access"
                done <<EOF
$_cs_rows
EOF
                printf ']}}\n'
            else
                if [ -z "$_cs_rows" ]; then
                    echo "Scope check: no changed paths"
                else
                    printf '%s\n' "$_cs_rows"
                fi
                echo "Scope violations: $_cs_violations"
            fi
            [ "$_cs_violations" -eq 0 ]
            ;;
        *) cli_error scope invalid_input "unknown action '$_cs_action'" "use set, show, or check"; return 1 ;;
    esac
}

cmd_collision() {
    _cco_json=0
    _cco_left="" _cco_right=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --json) _cco_json=1 ;;
            *)
                if [ -z "$_cco_left" ]; then _cco_left="$1"
                elif [ -z "$_cco_right" ]; then _cco_right="$1"
                else cli_error collision invalid_input "too many heads" "run hydra collision <left> <right> --json"; return 1
                fi
                ;;
        esac
        shift
    done
    [ -n "$_cco_left" ] && [ -n "$_cco_right" ] || { cli_error collision invalid_input "two heads are required" "run hydra collision <left> <right>"; return 1; }
    _cco_rows="$(parallel_collision_rows "$_cco_left" "$_cco_right")" || return 1
    _cco_tab="$(printf '\t')"
    if [ "$_cco_json" -eq 1 ]; then
        printf '{"schema_version":1,"ok":true,"command":"collision","data":{"left":"%s","right":"%s","findings":[' "$(json_escape "$_cco_left")" "$(json_escape "$_cco_right")"
        _cco_first=1
        while IFS="$_cco_tab" read -r _cco_kind _cco_path; do
            [ -n "$_cco_kind" ] || continue
            [ "$_cco_first" -eq 1 ] || printf ','
            _cco_first=0
            printf '{"kind":"%s","path":"%s"}' "$_cco_kind" "$(json_escape "$_cco_path")"
        done <<EOF
$_cco_rows
EOF
        printf ']}}\n'
    elif [ -z "$_cco_rows" ]; then
        echo "No claim, overlap, predicted conflict, or observed conflict found"
    else
        printf '%s\n' "$_cco_rows"
    fi
}

cmd_resource() {
    _cr_action="${1:-}"
    [ -n "$_cr_action" ] || { cli_error resource invalid_input "action is required" "use allocate, status, env, or release"; return 1; }
    shift
    _cr_branch="${1:-}"
    [ -n "$_cr_branch" ] || { cli_error resource invalid_input "head is required" "run hydra resource $_cr_action <head>"; return 1; }
    shift
    case "$_cr_action" in
        allocate)
            _cr_ports="" _cr_compose="" _cr_database=""
            _cr_tab="$(printf '\t')"
            while [ $# -gt 0 ]; do
                case "$1" in
                    --port)
                        [ $# -ge 2 ] || return 1
                        _cr_spec="$2"
                        _cr_name="${_cr_spec%%=*}"; _cr_range="${_cr_spec#*=}"
                        [ "$_cr_name" != "$_cr_range" ] || { cli_error resource invalid_input "port must be NAME=START-END" "example: --port http=3000-3999"; return 1; }
                        if [ -n "$_cr_ports" ]; then _cr_ports="$_cr_ports
$_cr_name$_cr_tab$_cr_range"; else _cr_ports="$_cr_name$_cr_tab$_cr_range"; fi
                        shift 2
                        ;;
                    --compose-project) [ $# -ge 2 ] || return 1; _cr_compose="$2"; shift 2 ;;
                    --database) [ $# -ge 2 ] || return 1; _cr_database="$2"; shift 2 ;;
                    *) cli_error resource invalid_input "unknown option '$1'" "use --port, --compose-project, or --database"; return 1 ;;
                esac
            done
            [ -n "$_cr_ports$_cr_compose$_cr_database" ] || { cli_error resource invalid_input "at least one resource is required" "pass --port, --compose-project, or --database"; return 1; }
            parallel_resource_allocate "$_cr_branch" "$_cr_ports" "$_cr_compose" "$_cr_database" || {
                cli_error resource conflict "resource allocation failed" "release the existing profile or choose a distinct range/name"
                return 1
            }
            echo "Resources allocated for $_cr_branch"
            ;;
        release)
            [ $# -eq 0 ] && parallel_resource_release "$_cr_branch" || return 1
            echo "Resources released for $_cr_branch"
            ;;
        status|env)
            _cr_json=0
            [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = --json ] && [ "$_cr_action" = status ]; } || return 1
            [ $# -eq 0 ] || _cr_json=1
            _cr_rows="$(parallel_resource_rows "$_cr_branch")" || return 1
            _cr_tab="$(printf '\t')"
            if [ "$_cr_action" = env ]; then
                while IFS="$_cr_tab" read -r _cr_kind _cr_name _cr_value; do
                    case "$_cr_kind" in
                        port) _cr_env_name="$(printf '%s' "$_cr_name" | tr '[:lower:].-' '[:upper:]__')"; printf 'HYDRA_PORT_%s=%s\n' "$_cr_env_name" "$_cr_value" ;;
                        compose-project) [ -z "$_cr_value" ] || printf 'COMPOSE_PROJECT_NAME=%s\n' "$_cr_value" ;;
                        database) [ -z "$_cr_value" ] || printf 'HYDRA_DATABASE=%s\n' "$_cr_value" ;;
                    esac
                done <<EOF
$_cr_rows
EOF
            elif [ "$_cr_json" -eq 1 ]; then
                printf '{"schema_version":1,"ok":true,"command":"resource status","data":{"branch":"%s","resources":[' "$(json_escape "$_cr_branch")"
                _cr_first=1
                while IFS="$_cr_tab" read -r _cr_kind _cr_name _cr_value; do
                    [ "$_cr_first" -eq 1 ] || printf ','
                    _cr_first=0
                    printf '{"kind":"%s","name":"%s","value":"%s"}' "$_cr_kind" "$(json_escape "$_cr_name")" "$(json_escape "$_cr_value")"
                done <<EOF
$_cr_rows
EOF
                printf ']}}\n'
            else
                printf '%s\n' "$_cr_rows"
            fi
            ;;
        *) cli_error resource invalid_input "unknown action '$_cr_action'" "use allocate, status, env, or release"; return 1 ;;
    esac
}

cmd_gate() {
    _cg_action="${1:-}"
    [ -n "$_cg_action" ] || { cli_error gate invalid_input "action is required" "use run, approve, or status"; return 1; }
    shift
    _cg_branch="${1:-}"
    [ -n "$_cg_branch" ] || { cli_error gate invalid_input "head is required" "run hydra gate $_cg_action <head>"; return 1; }
    shift
    case "$_cg_action" in
        run)
            _cg_name=""
            while [ $# -gt 0 ]; do
                case "$1" in
                    --name) [ $# -ge 2 ] || return 1; _cg_name="$2"; shift 2 ;;
                    --) shift; break ;;
                    *) cli_error gate invalid_input "expected --name or --" "run hydra gate run <head> --name <name> -- <command>"; return 1 ;;
                esac
            done
            [ -n "$_cg_name" ] && [ $# -gt 0 ] || { cli_error gate invalid_input "gate name and command are required" "run hydra gate run <head> --name <name> -- <command>"; return 1; }
            if _cg_run="$(parallel_gate_run "$_cg_branch" "$_cg_name" "$@")"; then
                echo "Gate $_cg_name passed: $_cg_run"
            else
                _cg_status=$?
                echo "Gate $_cg_name failed with exit $_cg_status" >&2
                return "$_cg_status"
            fi
            ;;
        approve)
            _cg_name="" _cg_actor="" _cg_reason=""
            while [ $# -gt 0 ]; do
                case "$1" in
                    --name) [ $# -ge 2 ] || return 1; _cg_name="$2"; shift 2 ;;
                    --by) [ $# -ge 2 ] || return 1; _cg_actor="$2"; shift 2 ;;
                    --reason) [ $# -ge 2 ] || return 1; _cg_reason="$2"; shift 2 ;;
                    *) return 1 ;;
                esac
            done
            parallel_gate_approve "$_cg_branch" "$_cg_name" "$_cg_actor" "$_cg_reason" || {
                cli_error gate precondition_failed "only a successful verification gate can be approved" "run the named gate successfully, then approve it explicitly"
                return 1
            }
            echo "Gate $_cg_name approved by $_cg_actor"
            ;;
        status)
            _cg_json=0
            [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = --json ]; } || return 1
            [ $# -eq 0 ] || _cg_json=1
            _cg_rows="$(parallel_gate_rows "$_cg_branch")" || return 1
            _cg_tab="$(printf '\t')"
            if [ "$_cg_json" -eq 1 ]; then
                printf '{"schema_version":1,"ok":true,"command":"gate status","data":{"branch":"%s","gates":[' "$(json_escape "$_cg_branch")"
                _cg_first=1
                while IFS="$_cg_tab" read -r _cg_name _cg_run _cg_status _cg_actor _cg_at; do
                    [ -n "$_cg_name" ] || continue
                    [ "$_cg_first" -eq 1 ] || printf ','
                    _cg_first=0
                    printf '{"name":"%s","run_id":"%s","exit_code":%s,"approved_by":"%s","approved_at":"%s"}' \
                        "$_cg_name" "$_cg_run" "$_cg_status" "$(json_escape "$_cg_actor")" "$_cg_at"
                done <<EOF
$_cg_rows
EOF
                printf ']}}\n'
            elif [ -z "$_cg_rows" ]; then
                echo "No gates recorded for $_cg_branch"
            else
                printf '%s\n' "$_cg_rows"
            fi
            ;;
        *) cli_error gate invalid_input "unknown action '$_cg_action'" "use run, approve, or status"; return 1 ;;
    esac
}
