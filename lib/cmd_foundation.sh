#!/bin/sh
# State v2 and event command handlers.

cmd_state() {
    _cs_action="${1:-verify}"
    [ $# -eq 0 ] || shift
    case "$_cs_action" in
        verify)
            if [ "$(sed -n '1p' "$HYDRA_HOME/state/active-schema" 2>/dev/null || true)" = "2" ]; then
                state_v2_verify
            else
                state_v2_verify_legacy_map "$HYDRA_MAP"
            fi
            echo "State is valid"
            ;;
        backup)
            _cs_backup="$(state_v2_backup)" || return 1
            echo "Backup: $_cs_backup"
            ;;
        migrate)
            case "${1:-}" in
                '') state_v2_migrate ;;
                --dry-run) state_v2_migrate --dry-run ;;
                *) echo "Usage: hydra state migrate [--dry-run]" >&2; return 1 ;;
            esac
            ;;
        rollback)
            [ $# -eq 1 ] || { echo "Usage: hydra state rollback <backup-path>" >&2; return 1; }
            state_v2_rollback "$1"
            ;;
        *)
            echo "Usage: hydra state <verify|backup|migrate|rollback>" >&2
            return 1
            ;;
    esac
}

_cmd_events_target() {
    _cet_project="${1:-}"
    _cet_head="${2:-}"
    if [ -z "$_cet_project" ]; then
        _cet_project="$(hydra_get_project_id)" || return 1
    fi
    if [ -z "$_cet_head" ]; then
        _cet_branch="$(git branch --show-current 2>/dev/null)" || return 1
        _cet_head="$(state_v2_find_head_by_branch "$_cet_project" "$_cet_branch")" || return 1
    fi
    printf '%s %s\n' "$_cet_project" "$_cet_head"
}

cmd_events() {
    _ce_action="${1:-tail}"
    [ $# -eq 0 ] || shift
    _ce_project=""
    _ce_head=""
    _ce_type=""
    _ce_max="1000"
    while [ $# -gt 0 ]; do
        case "$1" in
            --project) [ $# -ge 2 ] || return 1; _ce_project="$2"; shift 2 ;;
            --head) [ $# -ge 2 ] || return 1; _ce_head="$2"; shift 2 ;;
            --type) [ $# -ge 2 ] || return 1; _ce_type="$2"; shift 2 ;;
            --max-events) [ $# -ge 2 ] || return 1; _ce_max="$2"; shift 2 ;;
            *) echo "Error: unknown events option '$1'" >&2; return 1 ;;
        esac
    done
    # shellcheck disable=SC2046
    set -- $(_cmd_events_target "$_ce_project" "$_ce_head") || return 1
    _ce_file="$(event_file_for_head "$1" "$2")" || return 1
    case "$_ce_action" in
        verify) event_verify_file "$_ce_file" && echo "Events are valid" ;;
        tail) [ -f "$_ce_file" ] && tail -n "$_ce_max" "$_ce_file" ;;
        filter)
            [ -n "$_ce_type" ] || { echo "Usage: hydra events filter --type <type>" >&2; return 1; }
            grep -F "\"type\":\"$(json_escape "$_ce_type")\"" "$_ce_file" || true
            ;;
        repair) _ce_backup="$(event_repair_file "$_ce_file")" && echo "Corrupt input saved to $_ce_backup" ;;
        retain)
            _ce_archive="$(event_retain_file "$_ce_file" "$_ce_max")" || return 1
            [ -z "$_ce_archive" ] || echo "Archive: $_ce_archive"
            ;;
        *) echo "Usage: hydra events <verify|tail|filter|repair|retain>" >&2; return 1 ;;
    esac
}
