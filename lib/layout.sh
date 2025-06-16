#!/bin/sh
# Layout management functions for Hydra
# POSIX-compliant shell script

# Apply a predefined layout to the current tmux session
# Usage: apply_layout <layout_name>
# Returns: 0 on success, 1 on failure
apply_layout() {
    layout="$1"
    
    if [ -z "$layout" ]; then
        echo "Error: Layout name is required" >&2
        return 1
    fi
    
    # Check if we're in a tmux session
    if [ -z "$TMUX" ]; then
        echo "Error: Not in a tmux session" >&2
        return 1
    fi
    
    case "$layout" in
        default)
            # Single pane, full screen
            tmux kill-pane -a -t 0 2>/dev/null || true
            tmux select-layout even-horizontal
            ;;
            
        dev)
            # Two panes: editor (left 70%) and terminal (right 30%)
            tmux kill-pane -a -t 0 2>/dev/null || true
            tmux split-window -h -p 30
            tmux select-pane -t 0
            ;;
            
        full)
            # Three panes: editor (top-left), terminal (top-right), logs (bottom)
            tmux kill-pane -a -t 0 2>/dev/null || true
            tmux split-window -h -p 30
            tmux split-window -v -p 30 -t 1
            tmux select-pane -t 0
            ;;
            
        *)
            echo "Error: Unknown layout '$layout'" >&2
            echo "Available layouts: default, dev, full" >&2
            return 1
            ;;
    esac
    
    return 0
}

# Get the current layout name (if it matches a known layout)
# Usage: get_current_layout
# Returns: Layout name on stdout, "custom" if not recognized
get_current_layout() {
    if [ -z "$TMUX" ]; then
        return 1
    fi
    
    # Get pane count
    pane_count="$(tmux list-panes | wc -l | tr -d ' ')"
    
    case "$pane_count" in
        1)
            echo "default"
            ;;
        2)
            echo "dev"
            ;;
        3)
            echo "full"
            ;;
        *)
            echo "custom"
            ;;
    esac
}

# Cycle through available layouts
# Usage: cycle_layout
# Returns: 0 on success, 1 on failure
cycle_layout() {
    if [ -z "$TMUX" ]; then
        echo "Error: Not in a tmux session" >&2
        return 1
    fi
    
    current="$(get_current_layout)"
    
    case "$current" in
        default)
            next="dev"
            ;;
        dev)
            next="full"
            ;;
        full|custom|*)
            next="default"
            ;;
    esac
    
    apply_layout "$next"
    tmux display-message "Layout: $next"
    
    return 0
}

# Save current pane layout
# Usage: save_layout <session_name>
# Returns: 0 on success, 1 on failure
save_layout() {
    session="$1"
    
    if [ -z "$session" ]; then
        echo "Error: Session name is required" >&2
        return 1
    fi
    
    if [ -z "$HYDRA_HOME" ]; then
        echo "Error: HYDRA_HOME not set" >&2
        return 1
    fi
    
    layout_file="$HYDRA_HOME/layouts/$session"
    mkdir -p "$HYDRA_HOME/layouts"
    
    # Save layout string
    tmux list-windows -t "$session" -F '#{window_layout}' > "$layout_file" || return 1
    
    return 0
}

# Restore saved pane layout
# Usage: restore_layout <session_name>
# Returns: 0 on success, 1 on failure
restore_layout() {
    session="$1"
    
    if [ -z "$session" ]; then
        echo "Error: Session name is required" >&2
        return 1
    fi
    
    if [ -z "$HYDRA_HOME" ]; then
        echo "Error: HYDRA_HOME not set" >&2
        return 1
    fi
    
    layout_file="$HYDRA_HOME/layouts/$session"
    
    if [ ! -f "$layout_file" ]; then
        # No saved layout, use default
        return 0
    fi
    
    # Read and apply layout
    layout="$(cat "$layout_file")"
    if [ -n "$layout" ]; then
        tmux select-layout -t "$session" "$layout" 2>/dev/null || true
    fi
    
    return 0
}

# Setup layout hotkeys for a session
# Usage: setup_layout_hotkeys <session_name>
# Returns: 0 on success, 1 on failure
setup_layout_hotkeys() {
    session="$1"
    
    if [ -z "$session" ]; then
        echo "Error: Session name is required" >&2
        return 1
    fi
    
    # Set up C-l to cycle layouts (for current session only)
    tmux bind-key -n C-l run-shell "hydra cycle-layout" \; display-message "Layout cycled"
    
    return 0
}