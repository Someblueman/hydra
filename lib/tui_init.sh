#!/bin/sh
# Hydra TUI module
# POSIX-compliant shell script
# shellcheck disable=SC2034

tui_init_colors() {
    if command -v tput >/dev/null 2>&1; then
        TUI_CLEAR="$(tput clear 2>/dev/null || true)"
        TUI_HOME="$(tput cup 0 0 2>/dev/null || true)"
        TUI_HIDE_CURSOR="$(tput civis 2>/dev/null || true)"
        TUI_SHOW_CURSOR="$(tput cnorm 2>/dev/null || true)"
        TUI_BOLD="$(tput bold 2>/dev/null || true)"
        TUI_REVERSE="$(tput rev 2>/dev/null || true)"
        TUI_RESET="$(tput sgr0 2>/dev/null || true)"
        TUI_GREEN="$(tput setaf 2 2>/dev/null || true)"
        TUI_RED="$(tput setaf 1 2>/dev/null || true)"
        TUI_YELLOW="$(tput setaf 3 2>/dev/null || true)"
        TUI_BLUE="$(tput setaf 4 2>/dev/null || true)"
        TUI_DIM="$(tput dim 2>/dev/null || true)"
    fi
    return 0
}

# Update terminal size
# Usage: tui_update_size
# Returns: 0 on success
tui_update_size() {
    if command -v tput >/dev/null 2>&1; then
        TUI_ROWS="$(tput lines 2>/dev/null || echo 24)"
        TUI_COLS="$(tput cols 2>/dev/null || echo 80)"
    else
        TUI_ROWS=24
        TUI_COLS=80
    fi
    return 0
}

# Initialize terminal for TUI mode
# Usage: tui_init
# Returns: 0 on success, 1 on failure
tui_init() {
    # Save current terminal settings
    TUI_SAVED_STTY="$(stty -g 2>/dev/null || true)"

    # Set raw mode for single-char input (no echo, no line buffering)
    # min 0 time 1 = return immediately if no input, or after 0.1s timeout
    stty -echo -icanon min 0 time 1 2>/dev/null || true

    # Initialize colors
    tui_init_colors

    # Hide cursor
    printf "%s" "$TUI_HIDE_CURSOR"

    # Get terminal size
    tui_update_size
    TUI_LAST_COLS="$TUI_COLS"
    TUI_LAST_ROWS="$TUI_ROWS"

    # Clear screen immediately and show loading message to prevent flutter
    printf "%s%s" "$TUI_CLEAR" "$TUI_HOME"
    printf "Loading Hydra sessions...\n"

    # Create temp file for session list
    TUI_TEMP_LIST="$(mktemp)" || return 1

    # Create temp directory for activity tracking
    TUI_ACTIVITY_DIR="$(mktemp -d)" || return 1

    # Initialize state
    TUI_SELECTED=0
    TUI_OFFSET=0
    TUI_ITEM_COUNT=0
    TUI_RUNNING=1
    TUI_TAG_FILTER=""

    # Initialize tags subsystem
    tui_init_tags

    return 0
}

# Cleanup and restore terminal
# Usage: tui_cleanup
# Returns: 0
tui_cleanup() {
    # Show cursor
    printf "%s" "$TUI_SHOW_CURSOR"

    # Restore terminal settings
    if [ -n "$TUI_SAVED_STTY" ]; then
        stty "$TUI_SAVED_STTY" 2>/dev/null || true
    fi

    # Clear temp file
    if [ -n "$TUI_TEMP_LIST" ] && [ -f "$TUI_TEMP_LIST" ]; then
        rm -f "$TUI_TEMP_LIST"
    fi

    # Clear activity tracking directory
    if [ -n "$TUI_ACTIVITY_DIR" ] && [ -d "$TUI_ACTIVITY_DIR" ]; then
        rm -rf "$TUI_ACTIVITY_DIR"
    fi

    # Clear screen and move cursor to top
    printf "%s%s" "$TUI_CLEAR" "$TUI_HOME"

    return 0
}

# Temporarily restore terminal for user interaction
tui_pause_for_interaction() {
    # Restore terminal for normal input
    if [ -n "$TUI_SAVED_STTY" ]; then
        stty "$TUI_SAVED_STTY" 2>/dev/null || true
    fi
    printf "%s%s%s" "$TUI_SHOW_CURSOR" "$TUI_CLEAR" "$TUI_HOME"
}

# Resume TUI mode after interaction
# Usage: tui_resume_after_interaction
tui_resume_after_interaction() {
    printf "\n%sPress any key to continue...%s" "$TUI_DIM" "$TUI_RESET"
    # Wait for keypress
    stty -echo -icanon min 1 time 0 2>/dev/null || true
    dd bs=1 count=1 2>/dev/null >/dev/null || true
    # Resume raw mode with timeout
    stty -echo -icanon min 0 time 1 2>/dev/null || true
    printf "%s" "$TUI_HIDE_CURSOR"
}
