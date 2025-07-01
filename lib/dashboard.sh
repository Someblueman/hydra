#!/bin/sh
# Dashboard management functions for Hydra
# POSIX-compliant shell script

# Dashboard session name and restoration tracking
DASHBOARD_SESSION="hydra-dashboard"
DASHBOARD_RESTORE_MAP="$HYDRA_HOME/.dashboard_restore"

# Create a temporary dashboard session
# Usage: create_dashboard_session
# Returns: 0 on success, 1 on failure
create_dashboard_session() {
    if [ -z "$HYDRA_HOME" ]; then
        echo "Error: HYDRA_HOME not set" >&2
        return 1
    fi
    
    # Check if dashboard already exists
    if tmux_session_exists "$DASHBOARD_SESSION"; then
        echo "Error: Dashboard session already exists" >&2
        echo "Use 'tmux kill-session -t $DASHBOARD_SESSION' to clean up" >&2
        return 1
    fi
    
    # Create dashboard session in background
    tmux new-session -d -s "$DASHBOARD_SESSION" -c "$(pwd)" || return 1
    
    # Set up dashboard keybinding to exit
    tmux bind-key -T root q run-shell "hydra dashboard-exit"
    
    return 0
}

# Collect panes from all active Hydra sessions
# Usage: collect_session_panes
# Returns: 0 on success, 1 on failure
collect_session_panes() {
    if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        echo "Error: No active Hydra sessions found" >&2
        return 1
    fi
    
    # Clear restoration map
    : > "$DASHBOARD_RESTORE_MAP"
    
    # Counter for collected panes
    collected=0
    
    while IFS=' ' read -r branch session; do
        # Skip if session doesn't exist
        if ! tmux_session_exists "$session"; then
            continue
        fi
        
        # Get the first pane from the session with its window ID
        pane_info="$(tmux list-panes -t "$session" -F '#{pane_id} #{window_id}' | head -1 2>/dev/null)" || continue
        
        if [ -z "$pane_info" ]; then
            continue
        fi
        
        # Extract pane_id and window_id
        pane_id="$(echo "$pane_info" | cut -d' ' -f1)"
        window_id="$(echo "$pane_info" | cut -d' ' -f2)"
        
        # Record original location for restoration
        echo "$pane_id $session $window_id $branch" >> "$DASHBOARD_RESTORE_MAP"
        
        # Set pane title to show branch name
        tmux select-pane -t "$pane_id" -T "$branch"
        
        # Move pane to dashboard (except the first one)
        if [ "$collected" -gt 0 ]; then
            tmux join-pane -s "$pane_id" -t "$DASHBOARD_SESSION:0" 2>/dev/null || {
                echo "Warning: Failed to collect pane from session '$session'" >&2
                continue
            }
        else
            # For the first pane, move it directly
            tmux join-pane -s "$pane_id" -t "$DASHBOARD_SESSION:0" 2>/dev/null || {
                echo "Warning: Failed to collect pane from session '$session'" >&2
                continue
            }
        fi
        
        collected=$((collected + 1))
    done < "$HYDRA_MAP"
    
    if [ "$collected" -eq 0 ]; then
        echo "Error: No panes could be collected" >&2
        return 1
    fi
    
    echo "Collected $collected panes for dashboard"
    return 0
}

# Arrange dashboard layout based on pane count
# Usage: arrange_dashboard_layout
# Returns: 0 on success, 1 on failure
arrange_dashboard_layout() {
    if ! tmux_session_exists "$DASHBOARD_SESSION"; then
        echo "Error: Dashboard session does not exist" >&2
        return 1
    fi
    
    # Get pane count
    pane_count="$(tmux list-panes -t "$DASHBOARD_SESSION:0" | wc -l | tr -d ' ')"
    
    # Apply grid layout with equal percentages
    case "$pane_count" in
        1)
            # Single pane (100%), no layout needed
            ;;
        2)
            # Two panes (50%/50%) horizontally
            tmux select-layout -t "$DASHBOARD_SESSION:0" even-horizontal
            ;;
        3)
            # Three panes: one on top (100%), two on bottom (50%/50%)
            tmux select-layout -t "$DASHBOARD_SESSION:0" main-horizontal
            tmux resize-pane -t "$DASHBOARD_SESSION:0.0" -y 50%
            ;;
        4)
            # Four panes (25%/25%/25%/25%) in a 2x2 grid
            tmux select-layout -t "$DASHBOARD_SESSION:0" tiled
            ;;
        5|6)
            # 5-6 panes: use 2x3 grid (tiled with manual adjustments)
            tmux select-layout -t "$DASHBOARD_SESSION:0" tiled
            ;;
        7|8)
            # 7-8 panes: use 2x4 grid
            tmux select-layout -t "$DASHBOARD_SESSION:0" tiled
            ;;
        9)
            # 9 panes: perfect 3x3 grid
            tmux select-layout -t "$DASHBOARD_SESSION:0" tiled
            ;;
        *)
            # Many panes, use tiled and let tmux handle it
            tmux select-layout -t "$DASHBOARD_SESSION:0" tiled
            ;;
    esac
    
    # Set window title
    tmux rename-window -t "$DASHBOARD_SESSION:0" "Hydra Dashboard ($pane_count heads)"
    
    return 0
}

# Restore all panes to their original sessions
# Usage: restore_panes
# Returns: 0 on success, 1 on failure
restore_panes() {
    if [ ! -f "$DASHBOARD_RESTORE_MAP" ]; then
        return 0
    fi
    
    restored=0
    failed=0
    
    while IFS=' ' read -r pane_id session window_id branch; do
        # Check if original session still exists
        if ! tmux_session_exists "$session"; then
            echo "Warning: Original session '$session' no longer exists" >&2
            failed=$((failed + 1))
            continue
        fi
        
        # Move pane back to original session
        if tmux join-pane -s "$pane_id" -t "$session:$window_id" 2>/dev/null; then
            restored=$((restored + 1))
        else
            echo "Warning: Failed to restore pane for branch '$branch'" >&2
            failed=$((failed + 1))
        fi
    done < "$DASHBOARD_RESTORE_MAP"
    
    # Clean up restoration map
    rm -f "$DASHBOARD_RESTORE_MAP"
    
    if [ "$restored" -gt 0 ]; then
        echo "Restored $restored panes to original sessions"
    fi
    
    if [ "$failed" -gt 0 ]; then
        echo "Warning: Failed to restore $failed panes" >&2
        return 1
    fi
    
    return 0
}

# Clean up dashboard session and restore panes
# Usage: cleanup_dashboard
# Returns: 0 on success, 1 on failure
cleanup_dashboard() {
    # Restore panes first
    restore_panes
    
    # Kill dashboard session
    if tmux_session_exists "$DASHBOARD_SESSION"; then
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
    fi
    
    # Clean up any remaining files
    rm -f "$DASHBOARD_RESTORE_MAP"
    
    return 0
}

# Main dashboard entry point
# Usage: cmd_dashboard
# Returns: 0 on success, 1 on failure
cmd_dashboard() {
    # Check tmux availability
    if ! check_tmux_version; then
        exit 1
    fi
    
    # Check if we have any active sessions
    if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        echo "No active Hydra sessions found"
        echo "Use 'hydra spawn <branch>' to create sessions first"
        exit 1
    fi
    
    # Count active sessions
    active_count=0
    while IFS=' ' read -r branch session; do
        if tmux_session_exists "$session"; then
            active_count=$((active_count + 1))
        fi
    done < "$HYDRA_MAP"
    
    if [ "$active_count" -eq 0 ]; then
        echo "No active Hydra sessions found"
        echo "Use 'hydra regenerate' to restore sessions"
        exit 1
    fi
    
    # Set up cleanup trap
    trap 'cleanup_dashboard' EXIT INT TERM
    
    echo "Creating dashboard for $active_count active sessions..."
    
    # Create dashboard session
    if ! create_dashboard_session; then
        exit 1
    fi
    
    # Collect panes from active sessions
    if ! collect_session_panes; then
        cleanup_dashboard
        exit 1
    fi
    
    # Arrange layout
    if ! arrange_dashboard_layout; then
        cleanup_dashboard
        exit 1
    fi
    
    # Display instructions
    echo ""
    echo "Dashboard created successfully!"
    echo "Controls:"
    echo "  q - Exit dashboard and restore panes"
    echo "  Ctrl+C - Force exit (emergency cleanup)"
    echo ""
    echo "Switching to dashboard..."
    sleep 1
    
    # Switch to dashboard
    if ! switch_to_session "$DASHBOARD_SESSION"; then
        cleanup_dashboard
        exit 1
    fi
    
    # Cleanup will be handled by trap
    return 0
}

# Internal command to exit dashboard (called by 'q' key)
# Usage: cmd_dashboard_exit
# Returns: 0 on success, 1 on failure  
cmd_dashboard_exit() {
    echo "Exiting dashboard and restoring panes..."
    cleanup_dashboard
    
    # Try to switch back to a regular session if possible
    if [ -f "$HYDRA_MAP" ] && [ -s "$HYDRA_MAP" ]; then
        # Find first active session to switch to
        while IFS=' ' read -r branch session; do
            if tmux_session_exists "$session"; then
                echo "Switching to session '$session' ($branch)"
                switch_to_session "$session"
                return 0
            fi
        done < "$HYDRA_MAP"
    fi
    
    # If no sessions available, just detach
    if [ -n "${TMUX:-}" ]; then
        tmux detach-client
    fi
    
    return 0
}