#!/bin/sh
# TUI (Terminal User Interface) for Hydra
# POSIX-compliant shell script
#
# Provides an interactive terminal interface for managing Hydra sessions.
# Uses tput for terminal control and stty for raw input mode.
# shellcheck disable=SC2034

# Global TUI state (POSIX-safe: no arrays, simple variables)
TUI_SELECTED=0          # Currently selected item index
TUI_OFFSET=0            # Scroll offset for long lists
TUI_ROWS=24             # Terminal rows
TUI_COLS=80             # Terminal cols
TUI_ITEM_COUNT=0        # Number of items in list
TUI_TEMP_LIST=""        # Temp file for session list
TUI_SAVED_STTY=""       # Saved terminal settings
TUI_RUNNING=1           # Main loop control
TUI_CURRENT_SESSION=""  # Cached current tmux session name
TUI_NEEDS_REDRAW=1      # Flag to avoid unnecessary redraws
TUI_HELP_VISIBLE=0      # Help overlay visibility flag
TUI_TAGS_FILE=""        # Path to tags storage file
TUI_TAG_FILTER=""       # Current tag filter (empty = show all)
TUI_SEARCH_MODE=0       # Search/filter input mode active
TUI_SEARCH_PATTERN=""   # Current search pattern
TUI_ACTIVITY_DIR=""     # Directory for activity tracking
TUI_MULTI_SELECT=""     # Space-separated list of selected indices (multi-select mode)
TUI_PREVIEW_VISIBLE=0   # Preview panel visibility (0=hidden, 1=visible)
TUI_PREVIEW_LINES=5     # Number of preview lines to show
TUI_PREVIEW_FOLLOW=0    # Follow mode for preview (auto-refresh)
TUI_WIDE_COLS=100       # Minimum width for two-panel layout
TUI_HINT_SHOWN=0        # One-time keybinding hint shown
TUI_LAST_COLS=0         # Track terminal resize
TUI_LAST_ROWS=0         # Track terminal resize

# Terminal control codes (initialized by tui_init_colors)
TUI_CLEAR=""
TUI_HOME=""
TUI_HIDE_CURSOR=""
TUI_SHOW_CURSOR=""
TUI_BOLD=""
TUI_REVERSE=""
TUI_RESET=""
TUI_GREEN=""
TUI_RED=""
TUI_YELLOW=""
TUI_BLUE=""
TUI_DIM=""

# Load TUI submodules (order matters)
_tui_load_modules() {
    if [ -n "${_TUI_MODULES_LOADED:-}" ]; then
        return 0
    fi
    _tdir="${HYDRA_LIB_DIR:-}"
    if [ -z "$_tdir" ]; then
        _tdir="$(cd "$(dirname "$0")/../lib" 2>/dev/null && pwd)" || true
    fi
    for _mod in tui_init tui_data tui_tags tui_render tui_input tui_actions tui_main; do
        # shellcheck disable=SC1090
        . "$_tdir/${_mod}.sh"
    done
    _TUI_MODULES_LOADED=1
}
_tui_load_modules
