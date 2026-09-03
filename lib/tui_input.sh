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

# Check if an index is selected
tui_is_selected() {
    _idx="$1"
    case " $TUI_MULTI_SELECT " in
        *" $_idx "*) return 0 ;;
        *) return 1 ;;
    esac
}

# Toggle selection for an index
tui_toggle_select() {
    _idx="$1"
    if tui_is_selected "$_idx"; then
        TUI_MULTI_SELECT="$(echo " $TUI_MULTI_SELECT " | sed "s/ $_idx / /g" | tr -s ' ' | sed 's/^ *//;s/ *$//')"
    elif [ -z "$TUI_MULTI_SELECT" ]; then
        TUI_MULTI_SELECT="$_idx"
    else
        TUI_MULTI_SELECT="$TUI_MULTI_SELECT $_idx"
    fi
}

tui_clear_selection() {
    TUI_MULTI_SELECT=""
}

tui_selection_count() {
    if [ -z "$TUI_MULTI_SELECT" ]; then
        echo "0"
    else
        echo "$TUI_MULTI_SELECT" | tr ' ' '\n' | grep -c .
    fi
}

# Select all visible items
tui_select_all() {
    TUI_MULTI_SELECT=""
    _i=0
    while [ "$_i" -lt "$TUI_ITEM_COUNT" ]; do
        if [ -z "$TUI_MULTI_SELECT" ]; then
            TUI_MULTI_SELECT="$_i"
        else
            TUI_MULTI_SELECT="$TUI_MULTI_SELECT $_i"
        fi
        _i=$((_i + 1))
    done
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
        A)
            # Select all (alias)
            tui_select_all
            ;;
        i|I)
            # Show status
            tui_action_status
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
        *)
            # Handle escape key for clearing filters and selection
            # Check if key is escape (octal 033, hex 1b)
            if [ "$key" = "$(printf '\033')" ]; then
                # Clear selection first, then filters
                if [ "$(tui_selection_count)" -gt 0 ]; then
                    tui_clear_selection
                elif [ -n "$TUI_SEARCH_PATTERN" ]; then
                    TUI_SEARCH_PATTERN=""
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
