#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

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

    # Check dependencies
    echo "Checking dependencies..."
    errors=0
    
    # Check tmux
    if command -v tmux >/dev/null 2>&1; then
        if check_tmux_version; then
            echo "  [OK] tmux $(tmux -V)"
        else
            echo "  [FAIL] tmux version too old (need 3.0+)"
            errors=$((errors + 1))
        fi
    else
        echo "  [FAIL] tmux not installed"
        errors=$((errors + 1))
    fi
    
    # Check git
    if command -v git >/dev/null 2>&1; then
        echo "  [OK] $(git --version)"
    else
        echo "  [FAIL] git not installed"
        errors=$((errors + 1))
    fi
    
    # Check performance
    echo ""
    echo "Running performance tests..."
    
    # Test command dispatch
    start_time=$(date +%s%N 2>/dev/null || date +%s)
    "$0" version >/dev/null 2>&1
    end_time=$(date +%s%N 2>/dev/null || date +%s)
    
    if [ ${#start_time} -gt 10 ]; then
        # Nanosecond precision available
        elapsed=$(( (end_time - start_time) / 1000000 ))
        echo "  Command dispatch: ${elapsed}ms"
    else
        # Only second precision
        echo "  Command dispatch: <1000ms (no precise timing available)"
    fi
    
    # Check state file
    echo ""
    echo "Checking state management..."
    if [ -f "$HYDRA_MAP" ]; then
        echo "  [OK] State file exists: $HYDRA_MAP"
        echo "    Size: $(wc -c < "$HYDRA_MAP") bytes"
        echo "    Entries: $(wc -l < "$HYDRA_MAP" | tr -d ' ')"
    else
        echo "  [INFO] No state file (this is normal for new installations)"
    fi

    # Consistency checks
    echo ""
    echo "Consistency Checks:"
    consistency_issues=0

    # Check for dead sessions (mapping exists but tmux session doesn't)
    dead_count="$(count_dead_sessions)"
    if [ "$dead_count" -gt 0 ]; then
        print_warning "Dead sessions: $dead_count (run 'hydra regenerate' to restore)"
        consistency_issues=$((consistency_issues + 1))
    else
        print_success "No dead sessions"
    fi

    # Check for orphaned worktrees (worktree exists without mapping)
    orphan_wt="$(count_orphan_worktrees)"
    if [ "$orphan_wt" -gt 0 ]; then
        print_warning "Orphaned worktrees: $orphan_wt (run 'hydra cleanup' to remove)"
        consistency_issues=$((consistency_issues + 1))
    else
        print_success "No orphaned worktrees"
    fi

    # Check for stale locks
    stale_lock_count="$(count_stale_locks)"
    if [ "$stale_lock_count" -gt 0 ]; then
        print_warning "Stale locks: $stale_lock_count (run 'hydra cleanup' to remove)"
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
        echo "[FAIL] Found $errors issue(s). Please install missing dependencies."
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

