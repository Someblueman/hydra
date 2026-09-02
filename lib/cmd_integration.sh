#!/bin/sh
# Typed context, sync, and land command handlers.

cmd_context() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra context create <head> [--diff] [--file <path>] [--note <text>] [--history <count>] [--artifact <path>] [--json]'; return 0 ;; esac
    _ccx_action="${1:-}"
    [ "$_ccx_action" = create ] || { cli_error context invalid_input "action must be create" "run hydra context create <head> [options]"; return 1; }
    shift
    _ccx_branch="${1:-}"
    [ -n "$_ccx_branch" ] || return 1
    shift
    _ccx_diff=0 _ccx_files="" _ccx_notes="" _ccx_history=0 _ccx_artifacts="" _ccx_json=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --diff) _ccx_diff=1; shift ;;
            --file)
                [ $# -ge 2 ] || return 1
                if [ -n "$_ccx_files" ]; then _ccx_files="$_ccx_files
$2"; else _ccx_files="$2"; fi
                shift 2
                ;;
            --note) [ $# -ge 2 ] || return 1; _ccx_notes="$2"; shift 2 ;;
            --history) [ $# -ge 2 ] || return 1; _ccx_history="$2"; shift 2 ;;
            --artifact)
                [ $# -ge 2 ] || return 1
                if [ -n "$_ccx_artifacts" ]; then _ccx_artifacts="$_ccx_artifacts
$2"; else _ccx_artifacts="$2"; fi
                shift 2
                ;;
            --json) _ccx_json=1; shift ;;
            *) cli_error context invalid_input "unknown option '$1'" "use --diff, --file, --note, --history, or --artifact"; return 1 ;;
        esac
    done
    if [ "$_ccx_diff" -eq 0 ] && [ -z "$_ccx_files$_ccx_notes$_ccx_artifacts" ] && [ "$_ccx_history" = 0 ]; then
        cli_error context invalid_input "context pack is empty" "select at least one typed input"
        return 1
    fi
    _ccx_path="$(integration_context_create "$_ccx_branch" "$_ccx_diff" "$_ccx_files" "$_ccx_notes" "$_ccx_history" "$_ccx_artifacts")" || return 1
    if [ "$_ccx_json" -eq 1 ]; then
        json_success context "{\"branch\":\"$(json_escape "$_ccx_branch")\",\"pack_id\":\"$(basename "$_ccx_path")\",\"path\":\"$(json_escape "$_ccx_path")\"}"
    else
        echo "Context pack $(basename "$_ccx_path")"
        echo "  path: $_ccx_path"
    fi
}

cmd_sync() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra sync <head> --from <ref> --gate <name> [--dry-run]'; return 0 ;; esac
    _csy_branch="${1:-}"
    [ -n "$_csy_branch" ] || { cli_error sync invalid_input "head is required" "run hydra sync <head> --from <ref> --gate <name> [--dry-run]"; return 1; }
    shift
    _csy_from="" _csy_gate="" _csy_dry=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --from) [ $# -ge 2 ] || return 1; _csy_from="$2"; shift 2 ;;
            --gate) [ $# -ge 2 ] || return 1; _csy_gate="$2"; shift 2 ;;
            --dry-run) _csy_dry=1; shift ;;
            *) cli_error sync invalid_input "unknown option '$1'" "use --from, --gate, or --dry-run"; return 1 ;;
        esac
    done
    if [ -z "$_csy_from" ] || [ -z "$_csy_gate" ]; then
        cli_error sync invalid_input "--from and --gate are required" "name an approved verification gate and source ref"
        return 1
    fi
    if _csy_run="$(integration_sync "$_csy_branch" "$_csy_from" "$_csy_gate" "$_csy_dry")"; then
        if [ "$_csy_dry" -eq 1 ]; then printf '%s\n' "$_csy_run"; else echo "Sync completed: $_csy_run"; fi
    else
        return $?
    fi
}

cmd_land() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra land <head> --into <branch> --gate <name> [--dry-run] [--keep-head]'; return 0 ;; esac
    _cl_branch="${1:-}"
    [ -n "$_cl_branch" ] || { cli_error land invalid_input "head is required" "run hydra land <head> --into <branch> --gate <name> [--dry-run] [--keep-head]"; return 1; }
    shift
    _cl_into="" _cl_gate="" _cl_dry=0 _cl_keep=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --into) [ $# -ge 2 ] || return 1; _cl_into="$2"; shift 2 ;;
            --gate) [ $# -ge 2 ] || return 1; _cl_gate="$2"; shift 2 ;;
            --dry-run) _cl_dry=1; shift ;;
            --keep-head) _cl_keep=1; shift ;;
            *) cli_error land invalid_input "unknown option '$1'" "use --into, --gate, --dry-run, or --keep-head"; return 1 ;;
        esac
    done
    if [ -z "$_cl_into" ] || [ -z "$_cl_gate" ]; then
        cli_error land invalid_input "--into and --gate are required" "name the current target branch and an approved gate"
        return 1
    fi
    if _cl_run="$(integration_land "$_cl_branch" "$_cl_into" "$_cl_gate" "$_cl_dry" "$_cl_keep")"; then
        if [ "$_cl_dry" -eq 1 ]; then printf '%s\n' "$_cl_run"; else echo "Land completed: $_cl_run"; fi
    else
        return $?
    fi
}

cmd_integrate() {
    case "${1:-}" in
        -h|--help|'')
            printf '%s\n' \
                'Usage: hydra integrate <run-or-group> --base <ref> --target <ref> --dry-run' \
                '       hydra integrate <run-or-group> --base <ref> --target <ref> --execute [--gate <command>]...' \
                '       hydra integrate train <run-or-group> --base <ref> --target <ref> --dry-run' \
                '       hydra integrate train <run-or-group> --base <ref> --target <ref> --execute [--gate <command>]...' \
                '       hydra integrate status <integration-run>' \
                '       hydra integrate cancel <integration-run>' \
                '       hydra integrate resume <integration-run>' \
                '       hydra integrate approve <integration-run> --by <actor>' \
                '       hydra integrate promote <integration-run>' \
                '       hydra integrate cleanup <integration-run> --apply'
            [ -n "${1:-}" ]
            return
            ;;
        status|report)
            [ "$#" -eq 2 ] || return 1
            integration_verified_status "$2"
            return
            ;;
        promote)
            [ "$#" -eq 2 ] || return 1
            integration_verified_promote "$2"
            return
            ;;
        cancel)
            [ "$#" -eq 2 ] || return 1
            integration_verified_cancel "$2"
            return
            ;;
        resume)
            [ "$#" -eq 2 ] || return 1
            integration_verified_resume "$2"
            return
            ;;
        cleanup)
            if [ "$#" -eq 2 ]; then
                integration_verified_status "$2"
                return
            fi
            if [ "$#" -ne 3 ] || [ "$3" != --apply ]; then
                return 1
            fi
            integration_verified_cleanup "$2"
            return
            ;;
        approve)
            if [ "$#" -ne 4 ] || [ "$3" != --by ]; then
                return 1
            fi
            integration_verified_approve "$2" "$4"
            return
            ;;
    esac

    _ci_mode=integration
    if [ "$1" = train ]; then
        [ "$#" -ge 2 ] || return 1
        _ci_mode=train
        _ci_selector="$2"
        shift 2
    else
        _ci_selector="$1"
        shift
    fi
    _ci_base=""
    _ci_target=""
    _ci_action=""
    _ci_tmp="$(mktemp -d "${TMPDIR:-/tmp}/hydra-integrate.XXXXXX")" || return 1
    _ci_gates="$_ci_tmp/gates.argv"
    : > "$_ci_gates"
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --base)
                [ "$#" -ge 2 ] || { rm -rf "$_ci_tmp"; return 1; }
                _ci_base="$2"
                shift 2
                ;;
            --target|--into)
                [ "$#" -ge 2 ] || { rm -rf "$_ci_tmp"; return 1; }
                _ci_target="$2"
                shift 2
                ;;
            --dry-run)
                [ -z "$_ci_action" ] || { rm -rf "$_ci_tmp"; return 1; }
                _ci_action=dry-run
                shift
                ;;
            --execute)
                [ -z "$_ci_action" ] || { rm -rf "$_ci_tmp"; return 1; }
                _ci_action=execute
                shift
                ;;
            --gate)
                [ "$#" -ge 2 ] || { rm -rf "$_ci_tmp"; return 1; }
                case "$2" in *'
'*|*'	'*) rm -rf "$_ci_tmp"; return 1 ;; esac
                printf '%s\n' "$2" >> "$_ci_gates"
                shift 2
                ;;
            *)
                echo "Error: unknown integrate option '$1'" >&2
                rm -rf "$_ci_tmp"
                return 1
                ;;
        esac
    done
    if [ -z "$_ci_base" ] || [ -z "$_ci_target" ] || [ -z "$_ci_action" ]; then
        echo "Error: --base, --target, and --dry-run or --execute are required" >&2
        rm -rf "$_ci_tmp"
        return 1
    fi
    case "$_ci_action" in
        dry-run) integration_verified_preview "$_ci_selector" "$_ci_base" "$_ci_target" "$_ci_gates" "$_ci_mode" ;;
        execute) integration_verified_execute "$_ci_selector" "$_ci_base" "$_ci_target" "$_ci_gates" "$_ci_mode" ;;
    esac
    _ci_status=$?
    rm -rf "$_ci_tmp"
    return "$_ci_status"
}
