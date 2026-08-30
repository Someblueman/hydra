#!/bin/sh
# Disk accounting, policy GC, and worktree doctor commands.

cmd_du() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra du [--json]'; return 0 ;; esac
    _cdu_json=0
    [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = --json ]; } || return 1
    [ $# -eq 0 ] || _cdu_json=1
    _cdu_rows="$(worktree_du_rows)" || return 1
    _cdu_tab="$(printf '\t')"
    if [ "$_cdu_json" -eq 1 ]; then
        printf '{"schema_version":1,"ok":true,"command":"du","data":{"heads":['
        _cdu_first=1
        while IFS="$_cdu_tab" read -r _cdu_head _cdu_branch _cdu_worktree _cdu_state _cdu_path; do
            [ -n "$_cdu_head" ] || continue
            [ "$_cdu_first" -eq 1 ] || printf ','
            _cdu_first=0
            printf '{"head_id":"%s","branch":"%s","worktree_kib":%s,"state_kib":%s,"path":"%s"}' \
                "$_cdu_head" "$(json_escape "$_cdu_branch")" "$_cdu_worktree" "$_cdu_state" "$(json_escape "$_cdu_path")"
        done <<EOF
$_cdu_rows
EOF
        printf ']}}\n'
    else
        printf 'HEAD\tBRANCH\tWORKTREE_KIB\tSTATE_KIB\tPATH\n'
        printf '%s\n' "$_cdu_rows"
    fi
}

cmd_gc() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra gc --policy orphaned|stopped|archives [--dry-run|--apply] [--include-dirty] [--older-than <days>]'; return 0 ;; esac
    _cgc_policy="" _cgc_apply=0 _cgc_include_dirty=0 _cgc_days=30
    while [ $# -gt 0 ]; do
        case "$1" in
            --policy) [ $# -ge 2 ] || return 1; _cgc_policy="$2"; shift 2 ;;
            --apply) _cgc_apply=1; shift ;;
            --include-dirty) _cgc_include_dirty=1; shift ;;
            --older-than) [ $# -ge 2 ] || return 1; _cgc_days="$2"; shift 2 ;;
            --dry-run) _cgc_apply=0; shift ;;
            *) cli_error gc invalid_input "unknown option '$1'" "use --policy orphaned|stopped|archives, --dry-run, or --apply"; return 1 ;;
        esac
    done
    case "$_cgc_policy" in
        orphaned) worktree_gc_orphaned_rows "$_cgc_apply" "$_cgc_include_dirty" ;;
        stopped) worktree_gc_stopped_rows "$_cgc_apply" "$_cgc_include_dirty" ;;
        archives) worktree_gc_archive_rows "$_cgc_apply" "$_cgc_days" ;;
        *) cli_error gc invalid_input "a known policy is required" "choose orphaned, stopped, or archives"; return 1 ;;
    esac
}

cmd_worktree() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra worktree doctor status' '       hydra worktree doctor lock|unlock <head> [--dry-run]' '       hydra worktree doctor move <head> <path> [--dry-run]' '       hydra worktree doctor repair [--dry-run|--apply]' '       hydra worktree doctor prune [--apply]'; return 0 ;; esac
    [ "${1:-}" = doctor ] || { cli_error worktree invalid_input "expected doctor" "run hydra worktree doctor <action>"; return 1; }
    shift
    _cwd_action="${1:-status}"
    [ $# -eq 0 ] || shift
    case "$_cwd_action" in
        status)
            [ $# -eq 0 ] || return 1
            git -C "$(get_repo_root)" worktree list --porcelain
            ;;
        lock)
            _cwd_branch="${1:-}"; [ -n "$_cwd_branch" ] || return 1; shift
            _cwd_reason="" _cwd_dry=0
            while [ $# -gt 0 ]; do
                case "$1" in --reason) [ $# -ge 2 ] || return 1; _cwd_reason="$2"; shift 2 ;; --dry-run) _cwd_dry=1; shift ;; *) return 1 ;; esac
            done
            worktree_doctor_lock "$_cwd_branch" "$_cwd_reason" "$_cwd_dry"
            ;;
        unlock)
            _cwd_branch="${1:-}"; [ -n "$_cwd_branch" ] || return 1; shift
            _cwd_dry=0
            [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = --dry-run ] && _cwd_dry=1; } || return 1
            worktree_doctor_unlock "$_cwd_branch" "$_cwd_dry"
            ;;
        move)
            _cwd_branch="${1:-}" _cwd_target="${2:-}"; [ -n "$_cwd_branch" ] && [ -n "$_cwd_target" ] || return 1; shift 2
            _cwd_dry=0
            [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = --dry-run ] && _cwd_dry=1; } || return 1
            worktree_doctor_move "$_cwd_branch" "$_cwd_target" "$_cwd_dry"
            ;;
        repair)
            _cwd_apply=0
            while [ $# -gt 0 ]; do
                case "$1" in --apply) _cwd_apply=1 ;; --dry-run) _cwd_apply=0 ;; *) return 1 ;; esac
                shift
            done
            worktree_doctor_repair "$_cwd_apply"
            ;;
        prune)
            _cwd_apply=0
            [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = --apply ] && _cwd_apply=1; } || return 1
            worktree_doctor_prune "$_cwd_apply"
            ;;
        *) cli_error worktree invalid_input "unknown doctor action '$_cwd_action'" "use status, lock, unlock, move, repair, or prune"; return 1 ;;
    esac
}
