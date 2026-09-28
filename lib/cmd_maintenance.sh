#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

# Print a doctor failure with a concrete next action
# Usage: doctor_fail <message> <next_action>
doctor_fail() {
    echo "  [FAIL] $1"
    echo "         Next: $2"
}

# Print a doctor info line with optional next action
# Usage: doctor_info <message> [next_action]
doctor_info() {
    echo "  [INFO] $1"
    if [ -n "${2:-}" ]; then
        echo "         Next: $2"
    fi
}

# Print up to ten entries of a newline-separated list, then a remainder count.
# Usage: doctor_list_entries <entries>
doctor_list_entries() {
    printf '%s\n' "$1" | sed -n '1,10s/^/         - /p'
    _dle_count="$(printf '%s\n' "$1" | grep -c .)"
    [ "$_dle_count" -le 10 ] || echo "         ... and $((_dle_count - 10)) more"
}

# Report group- or other-writable Hydra state, which readers refuse. With
# repair set, first remove group and other write from your own regular files
# and directories (never following links); foreign-owned entries are only
# reported.
# Usage: doctor_state_permissions <repair:0|1> <root>...
# Returns: 0 when no writable entry remains, 1 otherwise
doctor_state_permissions() {
    _dsp_repair="$1"
    shift
    _dsp_owned="$(state_writable_entries owned "$@")"
    if [ -n "$_dsp_owned" ] && [ "$_dsp_repair" -eq 1 ]; then
        _dsp_count="$(printf '%s\n' "$_dsp_owned" | grep -c .)"
        state_repair_permissions "$@" || true
        _dsp_owned="$(state_writable_entries owned "$@")"
        _dsp_left="$(printf '%s' "$_dsp_owned" | grep -c . || true)"
        print_success "Removed group and other write from $((_dsp_count - _dsp_left)) Hydra state entries (chmod go-w)"
    fi
    _dsp_other="$(state_writable_entries other "$@")"
    _dsp_status=0
    if [ -n "$_dsp_owned" ]; then
        doctor_fail "Hydra state is group- or other-writable, so Hydra refuses it:" \
            "hydra doctor --fix-permissions   (chmod go-w on your own files and directories only)"
        doctor_list_entries "$_dsp_owned"
        _dsp_status=1
    fi
    if [ -n "$_dsp_other" ]; then
        doctor_fail "Writable Hydra state that is not your own file or directory (left unchanged):" \
            "inspect these entries, then remove them or correct their owner and mode yourself"
        doctor_list_entries "$_dsp_other"
        _dsp_status=1
    fi
    [ "$_dsp_status" -ne 0 ] || print_success "Hydra state is private (no group- or other-writable entries)"
    return "$_dsp_status"
}

cmd_doctor() {
    # Parse flags. --fix repairs consistency issues; --fix-permissions removes
    # group and other write from your own Hydra state.
    fix_mode=0
    fix_permissions=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --fix|-f)
                fix_mode=1
                shift
                ;;
            --fix-permissions)
                fix_permissions=1
                shift
                ;;
            *)
                shift
                ;;
        esac
    done

    echo "Hydra Doctor - System Health Check"
    echo "================================="
    echo ""

    errors=0

    echo "Installation:"
    echo "  Binary: ${HYDRA_BIN_CMD:-unknown}"
    echo "  Libraries: ${HYDRA_LIB_DIR:-unknown}"
    if [ -n "${HYDRA_ROOT:-}" ]; then
        echo "  HYDRA_ROOT: $HYDRA_ROOT"
    fi

    _layout="unknown"
    if [ -n "${HYDRA_ROOT:-}" ] && [ "$HYDRA_LIB_DIR" = "$HYDRA_ROOT/lib" ]; then
        _layout="HYDRA_ROOT override"
    elif [ -f "${HYDRA_BIN_DIR:-}/../lib/git.sh" ]; then
        _layout="source checkout"
    elif [ -f "${HYDRA_BIN_DIR:-}/../lib/hydra/git.sh" ]; then
        _layout="PREFIX install"
    elif [ "$HYDRA_LIB_DIR" = "/usr/local/lib/hydra" ]; then
        _layout="legacy /usr/local"
    fi
    echo "  Layout: $_layout"

    if [ -z "${HYDRA_LIB_DIR:-}" ] || [ ! -f "$HYDRA_LIB_DIR/git.sh" ]; then
        doctor_fail "Library directory is missing or incomplete" \
            "run bin/hydra from a source checkout, set HYDRA_ROOT, or reinstall with PREFIX=\$HOME/.local ./install.sh"
        errors=$((errors + 1))
    elif [ ! -x "${HYDRA_BIN_CMD:-}" ] && [ ! -f "${HYDRA_BIN_CMD:-}" ]; then
        doctor_fail "Hydra binary path is not usable: ${HYDRA_BIN_CMD:-unset}" \
            "reinstall with PREFIX=\$HOME/.local ./install.sh or run bin/hydra from the checkout"
        errors=$((errors + 1))
    else
        print_success "Install and library paths resolve"
    fi

    echo ""
    echo "Dependencies:"

    if check_tmux_version 2>/dev/null; then
        print_success "$(tmux -V) (supports interactive terminals)"
    elif state_has_interactive_heads; then
        doctor_fail "Interactive heads require tmux 3.0 or newer" \
            "install or upgrade tmux, then re-run hydra doctor"
        errors=$((errors + 1))
    else
        doctor_info "Interactive terminals unavailable; headless execution does not require tmux" \
            "install tmux 3.0 or newer to use interactive spawn and attach"
    fi

    if command -v git >/dev/null 2>&1; then
        print_success "$(git --version)"
    else
        doctor_fail "git is not installed" \
            "install git, then re-run hydra doctor"
        errors=$((errors + 1))
    fi

    echo ""
    echo "State:"
    if [ -d "$HYDRA_HOME" ] && [ -w "$HYDRA_HOME" ]; then
        print_success "HYDRA_HOME writable: $HYDRA_HOME"
    else
        doctor_fail "HYDRA_HOME is not writable: $HYDRA_HOME" \
            "export HYDRA_HOME=\"\$HOME/.hydra\" and ensure that directory is writable"
        errors=$((errors + 1))
    fi

    # Hydra's own state: HYDRA_HOME and this repository's host-local records.
    _doctor_common="$(hydra_git_common_dir 2>/dev/null || true)"
    if ! doctor_state_permissions "$fix_permissions" "$HYDRA_HOME" ${_doctor_common:+"$_doctor_common/hydra"}; then
        errors=$((errors + 1))
    fi

    _doctor_project_dir="$(_state_project_dir 2>/dev/null || true)"
    if [ -d "$_doctor_project_dir" ]; then
        echo "  [OK] State v2 project exists: $_doctor_project_dir"
        echo "    Active heads: $(state_list_heads | wc -l | tr -d ' ')"
    else
        doctor_info "No state file (this is normal for new installations)"
    fi

    echo ""
    echo "Repository:"
    if git rev-parse --git-dir >/dev/null 2>&1; then
        _repo_root="$(get_repo_root 2>/dev/null || git rev-parse --show-toplevel)"
        print_success "Git repository: $_repo_root"
        _wt_parent="$(get_hydra_worktree_parent "$_repo_root" 2>/dev/null || dirname "$_repo_root")"
        if [ -d "$_wt_parent" ] && [ -w "$_wt_parent" ]; then
            print_success "Worktree parent writable: $_wt_parent"
        else
            print_warning "Worktree parent is not writable: $_wt_parent"
            echo "         Next: run hydra spawn from a repository whose parent is writable (see README Quick Start)"
        fi
    else
        doctor_info "Not in a git repository" \
            "cd into a git repo, or create a throwaway repo (see README Quick Start)"
    fi

    echo ""
    echo "Agents:"
    _detected=""
    for _agent in claude aider gemini codex cursor agy opencode copilot; do
        if profile_executable_path "$_agent" >/dev/null 2>&1; then
            if [ -z "$_detected" ]; then
                _detected="$_agent"
            else
                _detected="$_detected, $_agent"
            fi
        fi
    done
    if [ -n "$_detected" ]; then
        print_success "Detected: $_detected"
    else
        doctor_info "No coding agent found on PATH. Install Claude Code or Codex, or start a plain terminal task with 'hydra spawn <branch> --no-agent'."
    fi

    echo ""
    echo "Performance:"
    start_time=$(date +%s%N 2>/dev/null || date +%s)
    "$0" version >/dev/null 2>&1
    end_time=$(date +%s%N 2>/dev/null || date +%s)

    if [ ${#start_time} -gt 10 ]; then
        elapsed=$(( (end_time - start_time) / 1000000 ))
        echo "  Command dispatch: ${elapsed}ms"
    else
        echo "  Command dispatch: <1000ms (no precise timing available)"
    fi

    # Consistency checks
    echo ""
    echo "Consistency Checks:"
    consistency_issues=0

    # Check for dead sessions (mapping exists but tmux session doesn't)
    dead_count="$(count_dead_sessions)"
    if [ "$dead_count" -gt 0 ]; then
        print_warning "Dead sessions: $dead_count (run 'hydra regenerate' to restore)"
        echo "         Next: hydra regenerate   or   hydra doctor --fix"
        consistency_issues=$((consistency_issues + 1))
    else
        print_success "No dead sessions"
    fi

    # Leftover worktrees: the same authority as 'hydra gc --policy orphaned'.
    orphan_rows="$(list_orphan_worktree_rows)"
    orphan_wt="$(printf '%s\n' "$orphan_rows" | grep -c . || true)"
    if [ "$orphan_wt" -gt 0 ]; then
        print_warning "$(summarize_orphan_worktrees "$orphan_rows")"
        print_orphan_worktree_rows "$orphan_rows" "         - "
        echo "         Next: hydra gc --policy orphaned --dry-run   then   hydra gc --policy orphaned --apply"
        echo "         Branches are kept; worktrees with uncommitted changes need --include-dirty."
        consistency_issues=$((consistency_issues + 1))
    else
        print_success "No leftover worktrees"
    fi

    # Check for stale locks
    stale_lock_count="$(count_stale_locks)"
    if [ "$stale_lock_count" -gt 0 ]; then
        print_warning "Stale locks: $stale_lock_count (run 'hydra cleanup' to remove)"
        echo "         Next: hydra cleanup   or   hydra doctor --fix"
        consistency_issues=$((consistency_issues + 1))
    else
        print_success "No stale locks"
    fi

    # Summary and auto-fix
    echo ""
    if [ "$errors" -eq 0 ] && [ "$consistency_issues" -eq 0 ]; then
        echo "[OK] All checks passed! Hydra is ready to use."
    elif [ "$errors" -eq 0 ]; then
        if [ "$fix_mode" -eq 1 ]; then
            echo "[INFO] Found $consistency_issues consistency issue(s). Auto-fixing..."
            echo ""

            # Run regenerate if there are dead sessions
            if [ "$dead_count" -gt 0 ]; then
                echo "Regenerating dead sessions..."
                cmd_regenerate
                echo ""
            fi

            # Run cleanup for orphaned worktrees and stale locks
            if [ "$orphan_wt" -gt 0 ] || [ "$stale_lock_count" -gt 0 ]; then
                echo "Cleaning up..."
                cmd_cleanup --auto
                echo ""
            fi

            echo "[OK] Auto-fix complete. Run 'hydra doctor' again to verify."
        else
            echo "[WARN] Found $consistency_issues consistency issue(s). Run 'hydra doctor --fix' to auto-fix."
        fi
    else
        echo "[FAIL] Found $errors issue(s). See Next: lines above for recovery."
        return 1
    fi
}

# Cleanup leftover worktrees, stale locks, and dead mappings
# Usage: cmd_cleanup [--auto] [--include-dirty]
# --auto: Non-interactive mode for doctor --fix (reports, never removes worktrees)
# --include-dirty: also remove leftover worktrees with uncommitted changes
cmd_cleanup() {
    auto_mode=0
    include_dirty=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --auto)
                auto_mode=1
                shift
                ;;
            --include-dirty)
                include_dirty=1
                shift
                ;;
            *)
                shift
                ;;
        esac
    done

    echo "Hydra Cleanup"
    echo "============="
    echo ""

    cleaned_total=0

    # Clean stale locks
    echo "Cleaning stale locks..."
    stale_cleaned="$(clean_stale_locks)"
    print_info "Cleaned $stale_cleaned stale lock(s)"
    cleaned_total=$((cleaned_total + stale_cleaned))

    # Clean dead head records
    echo ""
    echo "Cleaning dead head records..."
    dead_cleaned="$(clean_dead_heads)"
    if [ "$dead_cleaned" -gt 0 ]; then
        echo "  Marked $dead_cleaned dead head(s) stopped"
    fi
    print_info "Cleaned $dead_cleaned dead head record(s)"
    cleaned_total=$((cleaned_total + dead_cleaned))

    echo ""
    cleanup_orphan_worktrees "$auto_mode" "$include_dirty"
    cleaned_total=$((cleaned_total + CLEANUP_ORPHANS_REMOVED))

    echo ""
    echo "Cleanup complete. Total items cleaned: $cleaned_total"
}

# Report leftover worktrees and, after interactive confirmation, remove them
# through 'hydra gc --policy orphaned', which keeps its dirty-work protection.
# Usage: cleanup_orphan_worktrees <auto 0|1> <include_dirty 0|1>
# Sets CLEANUP_ORPHANS_REMOVED to the number of worktrees removed.
cleanup_orphan_worktrees() {
    CLEANUP_ORPHANS_REMOVED=0
    echo "Checking for leftover worktrees from removed tasks..."
    _cow_rows="$(list_orphan_worktree_rows)"
    _cow_count="$(printf '%s\n' "$_cow_rows" | grep -c . || true)"
    if [ "$_cow_count" -eq 0 ]; then
        print_success "No leftover worktrees found"
        return 0
    fi
    _cow_dirty="$(printf '%s\n' "$_cow_rows" | grep -c '^dirty' || true)"
    echo "Found $(summarize_orphan_worktrees "$_cow_rows")"
    echo "These are the worktrees 'hydra gc --policy orphaned --dry-run' reports:"
    print_orphan_worktree_rows "$_cow_rows" "  "
    echo "Removing a leftover worktree deletes its directory only; branches are kept."
    _cow_removable="$_cow_count"
    if [ "$_cow_dirty" -gt 0 ] && [ "$2" -eq 0 ]; then
        _cow_removable=$((_cow_count - _cow_dirty))
        echo "Worktrees with uncommitted changes are kept; pass --include-dirty to remove them too."
    fi
    if [ "$1" -eq 1 ]; then
        print_warning "Leftover worktrees were not removed (run 'hydra cleanup' or 'hydra gc --policy orphaned --apply')"
        return 0
    fi
    if [ "$_cow_removable" -eq 0 ]; then
        return 0
    fi
    if ! [ -t 0 ] || [ -n "${CI:-}" ] || [ -n "${HYDRA_NONINTERACTIVE:-}" ]; then
        print_warning "Run interactively, or run 'hydra gc --policy orphaned --apply', to remove them"
        return 0
    fi
    printf "\nRemove %s leftover worktree(s)? Branches are kept. [y/N] " "$_cow_removable"
    read -r _cow_response
    case "$_cow_response" in
        [yY][eE][sS]|[yY]) ;;
        *) echo "Skipped leftover worktree cleanup"; return 0 ;;
    esac
    _cow_result="$(worktree_gc_orphaned_rows 1 "$2")"
    _cow_status=$?
    printf '%s\n' "$_cow_result" | sed '/^$/d; s/^/  /'
    CLEANUP_ORPHANS_REMOVED="$(printf '%s\n' "$_cow_result" | grep -c '^removed-orphan' || true)"
    if [ "$_cow_status" -eq 0 ]; then
        print_info "Removed $CLEANUP_ORPHANS_REMOVED leftover worktree(s); branches are kept"
    else
        print_warning "Some leftover worktrees were not removed; see the rows above"
    fi
}
