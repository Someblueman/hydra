#!/bin/sh
# Disk accounting, policy GC, and worktree doctor commands.

cmd_du() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra du [--json]' '       Lists head worktrees, then leftover worktrees from removed tasks (KIND leftover).'; return 0 ;; esac
    _cdu_json=0
    [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = --json ]; } || return 1
    [ $# -eq 0 ] || _cdu_json=1
    _cdu_rows="$(worktree_du_rows)" || return 1
    # Leftover worktrees come from the same authority as gc --policy orphaned.
    _cdu_leftover="$(worktree_orphan_sized_rows fresh 2>/dev/null || true)"
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
        printf '],"leftover":['
        cmd_du_leftover_json "$_cdu_leftover"
        printf ']}}\n'
    else
        printf 'HEAD\tBRANCH\tWORKTREE_KIB\tSTATE_KIB\tPATH\tKIND\n'
        printf '%s\n' "$_cdu_rows" | sed '/^$/d; s/$/	head/'
        # Leftover rows have no head record: HEAD and STATE_KIB are "-", and an
        # unmeasured size is "unknown" rather than zero.
        while IFS="$_cdu_tab" read -r _cdu_state _cdu_branch _cdu_kib _cdu_path; do
            [ -n "$_cdu_path" ] || continue
            _cdu_kind=leftover
            [ "$_cdu_state" = clean ] || _cdu_kind=leftover-dirty
            printf -- '-\t%s\t%s\t-\t%s\t%s\n' "$_cdu_branch" "$_cdu_kib" "$_cdu_path" "$_cdu_kind"
        done <<EOF
$_cdu_leftover
EOF
    fi
}

cmd_du_leftover_json() {
    _cdlj_tab="$(printf '\t')"
    _cdlj_first=1
    while IFS="$_cdlj_tab" read -r _cdlj_state _cdlj_branch _cdlj_kib _cdlj_path; do
        [ -n "$_cdlj_path" ] || continue
        [ "$_cdlj_first" -eq 1 ] || printf ','
        _cdlj_first=0
        case "$_cdlj_kib" in ''|*[!0-9]*) _cdlj_kib=null ;; esac
        _cdlj_dirty=false
        [ "$_cdlj_state" = clean ] || _cdlj_dirty=true
        if [ "$_cdlj_branch" = - ]; then
            _cdlj_branch=null
        else
            _cdlj_branch="\"$(json_escape "$_cdlj_branch")\""
        fi
        printf '{"branch":%s,"worktree_kib":%s,"uncommitted_changes":%s,"path":"%s"}' \
            "$_cdlj_branch" "$_cdlj_kib" "$_cdlj_dirty" "$(json_escape "$_cdlj_path")"
    done <<EOF
$1
EOF
}

cmd_gc() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra gc --policy orphaned|stopped|archives [--dry-run|--apply] [--include-dirty] [--older-than <days>] [--path <worktree>]' '       --path limits the orphaned policy to one listed worktree'; return 0 ;; esac
    _cgc_policy="" _cgc_apply=0 _cgc_include_dirty=0 _cgc_days=30 _cgc_path=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --policy|--older-than|--path)
                if [ $# -lt 2 ] || [ -z "$2" ]; then cli_error gc invalid_input "$1 requires a value" "run hydra gc --help"; return 1; fi
                case "$1" in --policy) _cgc_policy="$2" ;; --older-than) _cgc_days="$2" ;; --path) _cgc_path="$2" ;; esac
                shift 2 ;;
            --apply) _cgc_apply=1; shift ;;
            --include-dirty) _cgc_include_dirty=1; shift ;;
            --dry-run) _cgc_apply=0; shift ;;
            *) cli_error gc invalid_input "unknown option '$1'" "use --policy orphaned|stopped|archives, --dry-run, or --apply"; return 1 ;;
        esac
    done
    if [ -n "$_cgc_path" ] && [ "$_cgc_policy" != orphaned ]; then
        cli_error gc invalid_input "--path applies only to --policy orphaned" "run hydra gc --policy orphaned --dry-run"
        return 1
    fi
    case "$_cgc_policy" in
        orphaned) worktree_gc_orphaned_rows "$_cgc_apply" "$_cgc_include_dirty" "$_cgc_path" ;;
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
