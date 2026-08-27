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

cmd_doctor() {
    # Parse --fix flag
    fix_mode=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --fix|-f)
                fix_mode=1
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

    if command -v tmux >/dev/null 2>&1; then
        if check_tmux_version 2>/dev/null; then
            print_success "$(tmux -V) (need >= 3.0)"
        else
            _tmux_ver="$(tmux -V 2>/dev/null || echo tmux)"
            doctor_fail "$_tmux_ver is too old (need 3.0+)" \
                "upgrade tmux to 3.0 or newer, then re-run hydra doctor"
            errors=$((errors + 1))
        fi
    else
        doctor_fail "tmux is not installed" \
            "install tmux 3.0 or newer (apt/brew), then re-run hydra doctor"
        errors=$((errors + 1))
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

    if [ -f "$HYDRA_MAP" ]; then
        echo "  [OK] State file exists: $HYDRA_MAP"
        echo "    Size: $(wc -c < "$HYDRA_MAP") bytes"
        echo "    Entries: $(wc -l < "$HYDRA_MAP" | tr -d ' ')"
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
    for _agent in claude aider gemini codex cursor copilot; do
        if command -v "$_agent" >/dev/null 2>&1; then
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
        doctor_info "No agent CLI detected" \
            "export HYDRA_SKIP_AI=1 for a shell-only head, or install an agent (see README Quick Start)"
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

    # Check for orphaned worktrees (worktree exists without mapping)
    orphan_wt="$(count_orphan_worktrees)"
    if [ "$orphan_wt" -gt 0 ]; then
        print_warning "Orphaned worktrees: $orphan_wt (run 'hydra cleanup' to remove)"
        echo "         Next: hydra cleanup   or   hydra doctor --fix"
        consistency_issues=$((consistency_issues + 1))
    else
        print_success "No orphaned worktrees"
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

# Cleanup orphaned worktrees, stale locks, and dead mappings
# Usage: cmd_cleanup [--auto]
# --auto: Non-interactive mode for doctor --fix (skips orphan removal prompt)
cmd_cleanup() {
    auto_mode=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --auto)
                auto_mode=1
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

    # Clean dead session mappings
    echo ""
    echo "Cleaning dead session mappings..."
    dead_cleaned="$(clean_dead_mappings)"
    if [ "$dead_cleaned" -gt 0 ]; then
        echo "  Removed $dead_cleaned dead mapping(s)"
    fi
    print_info "Cleaned $dead_cleaned dead mapping(s)"
    cleaned_total=$((cleaned_total + dead_cleaned))

    # Find and offer to clean orphaned worktrees
    echo ""
    echo "Checking for orphaned worktrees..."
    orphan_list=""
    while IFS= read -r wt; do
        [ -z "$wt" ] && continue
        orphan_list="${orphan_list}${wt}
"
    done <<EOF
$(list_orphan_worktree_paths)
EOF

    if [ -n "$orphan_list" ]; then
        orphan_count="$(echo "$orphan_list" | grep -c . || true)"
        echo "Found $orphan_count orphaned worktree(s):"
        echo "$orphan_list" | while IFS= read -r wt; do
            [ -z "$wt" ] && continue
            echo "  $wt"
        done

        # Ask for confirmation in interactive mode (skip in auto mode)
        if [ "$auto_mode" -eq 1 ]; then
            print_warning "Orphaned worktrees found but not removed (run 'hydra cleanup' to remove)"
        elif [ -t 0 ] && [ -z "${CI:-}" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
            printf "\nRemove these orphaned worktrees? [y/N] "
            read -r response
            case "$response" in
                [yY][eE][sS]|[yY])
                    orphan_cleaned=0
                    echo "$orphan_list" | while IFS= read -r wt; do
                        [ -z "$wt" ] && continue
                        echo "  Removing $wt..."
                        if git worktree remove "$wt" --force 2>/dev/null; then
                            orphan_cleaned=$((orphan_cleaned + 1))
                        else
                            rm -rf "$wt" 2>/dev/null || true
                        fi
                    done
                    print_info "Removed orphaned worktrees"
                    ;;
                *)
                    echo "Skipped orphan cleanup"
                    ;;
            esac
        else
            print_warning "Run interactively to remove orphaned worktrees"
        fi
    else
        print_success "No orphaned worktrees found"
    fi

    echo ""
    echo "Cleanup complete. Total items cleaned: $cleaned_total"
}
