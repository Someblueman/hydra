#!/bin/sh
# Hydra TUI module
# POSIX-compliant shell script
# shellcheck disable=SC2034

tui_get_key() {
    # Read single character (stty already set with timeout)
    key=""
    # Use dd for POSIX compatibility
    key="$(dd bs=1 count=1 2>/dev/null || true)"

    # Handle escape sequences (arrow keys)
    if [ "$key" = "$(printf '\033')" ]; then
        # Read potential escape sequence
        seq1="$(dd bs=1 count=1 2>/dev/null || true)"
        if [ "$seq1" = "[" ]; then
            seq2="$(dd bs=1 count=1 2>/dev/null || true)"
            case "$seq2" in
                A) key="UP" ;;      # Up arrow
                B) key="DOWN" ;;    # Down arrow
                C) key="RIGHT" ;;   # Right arrow
                D) key="LEFT" ;;    # Left arrow
            esac
        fi
    fi

    printf "%s" "$key"
}

# Handle keypress
# Usage: tui_handle_key <key>
# Returns: 0 to continue, 1 to exit
tui_handle_key() {
    key="$1"

    # Empty string is timeout (from dd), not a key press - ignore
    if [ -z "$key" ]; then
        return 0
    fi

    case "$key" in
        q|Q)
            return 1  # Exit
            ;;
        j|J|DOWN)
            # Move down
            if [ "$TUI_SELECTED" -lt $((TUI_ITEM_COUNT - 1)) ]; then
                TUI_SELECTED=$((TUI_SELECTED + 1))
            fi
            ;;
        k|K|UP)
            # Move up
            if [ "$TUI_SELECTED" -gt 0 ]; then
                TUI_SELECTED=$((TUI_SELECTED - 1))
            fi
            ;;
        s|S)
            # Switch to selected session
            tui_action_switch
            ;;
        "$(printf '\r')"|"$(printf '\n')")
            # Enter — switch alias
            tui_action_switch
            ;;
        n|N)
            # Spawn new session
            tui_action_spawn
            ;;
        d)
            tui_action_kill
            ;;
        D)
            tui_action_dashboard
            ;;
        r|R)
            # Regenerate sessions
            tui_action_regenerate
            ;;
        a)
            # Kill all sessions
            tui_action_kill_all
            ;;
        A)
            # Select all (alias)
            tui_select_all
            ;;
        i|I)
            # Show status
            tui_action_status
            ;;
        t)
            # Cycle tag for selected session
            tui_action_tag
            ;;
        T)
            # Cycle tag filter
            tui_cycle_tag_filter
            tui_build_list
            ;;
        "/")
            # Enter search mode
            TUI_SEARCH_MODE=1
            ;;
        "?")
            # Show help overlay
            TUI_HELP_VISIBLE=1
            ;;
        " ")
            # Space - toggle multi-select for current item
            tui_toggle_select "$TUI_SELECTED"
            ;;
        x)
            # Bulk kill selected sessions
            if [ "$(tui_selection_count)" -gt 0 ]; then
                tui_action_bulk_kill
            fi
            ;;
        G)
            # Bulk set group for selected sessions
            if [ "$(tui_selection_count)" -gt 0 ]; then
                tui_action_bulk_group
            fi
            ;;
        p)
            # Toggle preview panel
            if [ "$TUI_PREVIEW_VISIBLE" -eq 0 ]; then
                TUI_PREVIEW_VISIBLE=1
            else
                TUI_PREVIEW_VISIBLE=0
            fi
            TUI_NEEDS_REDRAW=1
            ;;
        f|F)
            # Toggle preview follow mode
            if [ "$TUI_PREVIEW_FOLLOW" -eq 0 ]; then
                TUI_PREVIEW_FOLLOW=1
            else
                TUI_PREVIEW_FOLLOW=0
            fi
            TUI_NEEDS_REDRAW=1
            ;;
        *)
            # Handle escape key for clearing filters and selection
            # Check if key is escape (octal 033, hex 1b)
            if [ "$key" = "$(printf '\033')" ]; then
                # Clear selection first, then filters
                if [ "$(tui_selection_count)" -gt 0 ]; then
                    tui_clear_selection
                elif [ -n "$TUI_SEARCH_PATTERN" ] || [ -n "$TUI_TAG_FILTER" ]; then
                    TUI_SEARCH_PATTERN=""
                    TUI_TAG_FILTER=""
                    tui_build_list
                fi
            fi
            # Unknown key or timeout - ignore
            ;;
    esac

    return 0
}

# Temporarily restore terminal for user interaction
# Usage: tui_pause_for_interaction
