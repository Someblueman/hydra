#!/bin/sh
# CLI handlers for execution, Git evidence, and provenance.

cmd_exec() {
    _ce_branches=""
    _ce_group=""
    _ce_all=0
    _ce_jobs=4
    _ce_timeout=300
    _ce_json=0
    _ce_shell=""
    _ce_allow_shell=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --branch)
                [ $# -ge 2 ] || return 1
                if [ -n "$_ce_branches" ]; then
                    _ce_branches="$(printf '%s\n%s' "$_ce_branches" "$2")"
                else
                    _ce_branches="$2"
                fi
                shift 2
                ;;
            --group) [ $# -ge 2 ] || return 1; _ce_group="$2"; shift 2 ;;
            --all) _ce_all=1; shift ;;
            --jobs) [ $# -ge 2 ] || return 1; _ce_jobs="$2"; shift 2 ;;
            --timeout) [ $# -ge 2 ] || return 1; _ce_timeout="$2"; shift 2 ;;
            --json) _ce_json=1; shift ;;
            --shell) [ $# -ge 2 ] || return 1; _ce_shell="$2"; shift 2 ;;
            --allow-shell) _ce_allow_shell=1; shift ;;
            --) shift; break ;;
            *) break ;;
        esac
    done
    case "$_ce_jobs:$_ce_timeout" in *[!0-9:]*) echo "Error: jobs and timeout must be integers" >&2; return 1 ;; esac
    [ "$_ce_jobs" -ge 1 ] && [ "$_ce_jobs" -le 16 ] || { echo "Error: jobs must be between 1 and 16" >&2; return 1; }
    if [ -n "$_ce_shell" ]; then
        [ $# -eq 0 ] || { echo "Error: --shell cannot be combined with argv after --" >&2; return 1; }
        if [ "$_ce_allow_shell" -ne 1 ] || ! project_is_trusted; then
            echo "Error: shell execution requires --allow-shell and currently trusted project configuration" >&2
            return 1
        fi
        set -- sh -c "$_ce_shell"
    else
        [ $# -gt 0 ] || { echo "Usage: hydra exec [selection] [--jobs N] [--timeout N] -- command [args...]" >&2; return 1; }
    fi
    _ce_selectors=0
    [ -z "$_ce_branches" ] || _ce_selectors=$((_ce_selectors + 1))
    [ -z "$_ce_group" ] || _ce_selectors=$((_ce_selectors + 1))
    [ "$_ce_all" -eq 0 ] || _ce_selectors=$((_ce_selectors + 1))
    [ "$_ce_selectors" -le 1 ] || {
        echo "Error: --branch, --group, and --all are mutually exclusive selection modes" >&2
        return 1
    }
    _ce_project="$(hydra_get_project_id)" || return 1
    LIFECYCLE_PROJECT_ID="$_ce_project"
    export LIFECYCLE_PROJECT_ID
    _ce_selection="$(mktemp)" || return 1
    operations_select_heads "$_ce_selection" "$_ce_branches" "$_ce_group" "$_ce_all" || { rm -f "$_ce_selection"; return 1; }
    _ce_run="$(hydra_new_id run "$_ce_project|exec")" || { rm -f "$_ce_selection"; return 1; }
    _ce_run_dir="$HYDRA_STATE_V2_ROOT/projects/$_ce_project/exec/$_ce_run"
    mkdir -p "$_ce_run_dir" || { rm -f "$_ce_selection"; return 1; }
    chmod 700 "$_ce_run_dir" 2>/dev/null || true
    _ce_max="${HYDRA_EXEC_MAX_BYTES:-1048576}"
    case "$_ce_max" in ''|*[!0-9]*) rm -f "$_ce_selection"; return 1 ;; esac
    _ce_active=0
    _ce_selection_error=0
    while IFS= read -r _ce_head; do
        if ! operations_load_selected_head "$_ce_head"; then _ce_selection_error=1; break; fi
        operations_exec_worker "$_ce_run" "$_ce_head" "$OPERATIONS_BRANCH" "$OPERATIONS_WORKTREE" "$_ce_timeout" "$_ce_max" "$@" &
        _ce_active=$((_ce_active + 1))
        if [ "$_ce_active" -ge "$_ce_jobs" ]; then
            wait
            _ce_active=0
        fi
    done < "$_ce_selection"
    wait
    if [ "$_ce_selection_error" -eq 1 ]; then rm -f "$_ce_selection"; return 1; fi
    _ce_failed=0
    if [ "$_ce_json" -eq 1 ]; then
        printf '{"schema_version":1,"ok":true,"command":"exec","data":{"run_id":"%s","results":[' "$_ce_run"
    else
        echo "Exec run $_ce_run"
    fi
    _ce_first=1
    _ce_selection_error=0
    while IFS= read -r _ce_head; do
        if ! operations_load_selected_head "$_ce_head"; then _ce_selection_error=1; break; fi
        _ce_branch="$OPERATIONS_BRANCH"
        _ce_result="$_ce_run_dir/$_ce_head"
        _ce_status="$(sed -n '1p' "$_ce_result/status" 2>/dev/null || echo 1)"
        [ "$_ce_status" -eq 0 ] || _ce_failed=1
        if [ "$_ce_json" -eq 1 ]; then
            [ "$_ce_first" -eq 1 ] || printf ','
            _ce_first=0
            printf '{"branch":"%s","head_id":"%s","exit_code":%s,"stdout":"%s","stderr":"%s"}' \
                "$(json_escape "$_ce_branch")" "$_ce_head" "$_ce_status" \
                "$(json_escape "$(cat "$_ce_result/stdout")")" "$(json_escape "$(cat "$_ce_result/stderr")")"
        else
            echo "  $_ce_branch: exit $_ce_status"
            [ ! -s "$_ce_result/stdout" ] || sed 's/^/    stdout: /' "$_ce_result/stdout"
            [ ! -s "$_ce_result/stderr" ] || sed 's/^/    stderr: /' "$_ce_result/stderr" >&2
        fi
    done < "$_ce_selection"
    if [ "$_ce_selection_error" -eq 1 ]; then rm -f "$_ce_selection"; return 1; fi
    [ "$_ce_json" -eq 0 ] || printf ']}}\n'
    rm -f "$_ce_selection"
    [ "$_ce_failed" -eq 0 ]
}

cmd_diff() {
    _cd_branch="${1:-}"
    [ -n "$_cd_branch" ] || { echo "Usage: hydra diff <branch> [--stat|--name-only|--json]" >&2; return 1; }
    shift
    _cd_mode="patch"
    _cd_json=0
    while [ $# -gt 0 ]; do
        case "$1" in --stat) _cd_mode=stat ;; --name-only) _cd_mode=name-only ;; --json) _cd_json=1 ;; *) return 1 ;; esac
        shift
    done
    lifecycle_load_head "$_cd_branch" || return 1
    _cd_worktree="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/worktree")"
    _cd_base="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/base-ref")"
    [ -d "$_cd_worktree" ] || { echo "Error: worktree is unavailable" >&2; return 1; }
    case "$_cd_mode" in
        stat) _cd_output="$(git -C "$_cd_worktree" diff --stat "$_cd_base" --)" ;;
        name-only) _cd_output="$(git -C "$_cd_worktree" diff --name-only "$_cd_base" --)" ;;
        patch) _cd_output="$(git -C "$_cd_worktree" diff "$_cd_base" --)" ;;
    esac || return 1
    if [ "$_cd_json" -eq 1 ]; then
        json_success diff "{\"branch\":\"$(json_escape "$_cd_branch")\",\"base_ref\":\"$_cd_base\",\"mode\":\"$_cd_mode\",\"output\":\"$(json_escape "$_cd_output")\"}"
    else
        printf '%s\n' "$_cd_output"
    fi
}

cmd_review() {
    _crv_branch="${1:-}"
    [ -n "$_crv_branch" ] || { echo "Usage: hydra review <branch> [--json]" >&2; return 1; }
    shift
    _crv_json=0
    if [ $# -eq 1 ] && [ "$1" = --json ]; then
        _crv_json=1
    elif [ $# -ne 0 ]; then
        echo "Usage: hydra review <branch> [--json]" >&2
        return 1
    fi
    lifecycle_load_head "$_crv_branch" || return 1
    _crv_worktree="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/worktree")"
    _crv_base="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/base-ref")"
    [ -d "$_crv_worktree" ] || return 1
    operations_git_counts "$_crv_worktree" "$_crv_base"
    _crv_files="$(git -C "$_crv_worktree" diff --name-only "$_crv_base" -- | wc -l | tr -d ' ')"
    _crv_insertions=0
    _crv_deletions=0
    _crv_tab="$(printf '\t')"
    while IFS="$_crv_tab" read -r _crv_add _crv_del _crv_path; do
        case "$_crv_add:$_crv_del" in *[!0-9:]*) continue ;; esac
        _crv_insertions=$((_crv_insertions + _crv_add))
        _crv_deletions=$((_crv_deletions + _crv_del))
    done <<EOF
$(git -C "$_crv_worktree" diff --numstat "$_crv_base" --)
EOF
    if git -C "$_crv_worktree" diff --check "$_crv_base" -- >/dev/null 2>&1; then _crv_check=true; else _crv_check=false; fi
    if [ "$_crv_json" -eq 1 ]; then
        json_success review "{\"branch\":\"$(json_escape "$_crv_branch")\",\"base_ref\":\"$_crv_base\",\"ahead\":$OPERATIONS_AHEAD,\"behind\":$OPERATIONS_BEHIND,\"dirty_paths\":$OPERATIONS_DIRTY,\"changed_files\":$_crv_files,\"insertions\":$_crv_insertions,\"deletions\":$_crv_deletions,\"diff_check_passed\":$_crv_check}"
    else
        echo "Review evidence for $_crv_branch"
        echo "  base: $_crv_base"
        echo "  ahead/behind: $OPERATIONS_AHEAD/$OPERATIONS_BEHIND"
        echo "  dirty paths: $OPERATIONS_DIRTY"
        echo "  changed files: $_crv_files (+$_crv_insertions/-$_crv_deletions)"
        echo "  diff check passed: $_crv_check"
    fi
}

cmd_provenance() {
    _cpv_branch="${1:-}"
    [ -n "$_cpv_branch" ] || { echo "Usage: hydra provenance <branch> [--json]" >&2; return 1; }
    shift
    _cpv_json=0
    if [ $# -eq 1 ] && [ "$1" = --json ]; then
        _cpv_json=1
    elif [ $# -ne 0 ]; then
        echo "Usage: hydra provenance <branch> [--json]" >&2
        return 1
    fi
    lifecycle_load_head "$_cpv_branch" || return 1
    _cpv_dir="$LIFECYCLE_HEAD_DIR/provenance"
    [ -d "$_cpv_dir" ] || { echo "Error: provenance is unavailable for this migrated head" >&2; return 1; }
    if [ "$_cpv_json" -eq 1 ]; then
        json_success provenance "{\"branch\":\"$(json_escape "$_cpv_branch")\",\"project_id\":\"$LIFECYCLE_PROJECT_ID\",\"head_id\":\"$LIFECYCLE_HEAD_ID\",\"instance_id\":\"$LIFECYCLE_INSTANCE_ID\",\"hydra_version\":\"$(json_escape "$(sed -n '1p' "$_cpv_dir/hydra-version")")\",\"base_ref\":\"$(sed -n '1p' "$_cpv_dir/base-ref")\",\"task_hash\":\"$(sed -n '1p' "$_cpv_dir/task-hash")\",\"task_bytes\":$(sed -n '1p' "$_cpv_dir/task-bytes"),\"trusted_config_hash\":\"$(sed -n '1p' "$_cpv_dir/trusted-config-hash")\",\"profile\":\"$(json_escape "$(sed -n '1p' "$LIFECYCLE_INSTANCE_DIR/resolved-profile")")\",\"profile_version\":\"$(json_escape "$(sed -n '1p' "$LIFECYCLE_INSTANCE_DIR/profile-version")")\"}"
    else
        echo "Provenance for $_cpv_branch"
        for _cpv_field in hydra-version git-version tmux-version base-ref task-hash task-bytes trusted-config-hash lifecycle-sources; do
            echo "  $_cpv_field: $(sed -n '1p' "$_cpv_dir/$_cpv_field")"
        done
        echo "  profile: $(sed -n '1p' "$LIFECYCLE_INSTANCE_DIR/resolved-profile")"
        echo "  profile-version: $(sed -n '1p' "$LIFECYCLE_INSTANCE_DIR/profile-version")"
    fi
}
