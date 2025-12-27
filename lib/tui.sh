#!/bin/sh
# TUI (Terminal User Interface) for Hydra
# POSIX-compliant shell script
#
# Provides an interactive terminal interface for managing Hydra sessions.
# Uses tput for terminal control and stty for raw input mode.

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

# Initialize terminal control codes via tput
# Usage: tui_init_colors
# Returns: 0 on success
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

# Build session list from state
# Usage: tui_build_list
# Returns: 0 on success
tui_build_list() {
    # Cache current session once per refresh (not per render)
    TUI_CURRENT_SESSION="$(tmux display-message -p '#{session_name}' 2>/dev/null || true)"

    # Clear temp file
    : > "$TUI_TEMP_LIST"
    TUI_ITEM_COUNT=0

    if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        return 0
    fi

    # Read mappings and write to temp file with status
    # Use tab as delimiter (safe - branch/session names can't contain tabs)
    while IFS=' ' read -r branch session ai _group _ts; do
        [ -z "$branch" ] && continue

        if tmux_session_exists "$session" 2>/dev/null; then
            sess_status="ALIVE"

            # Check activity status for alive sessions
            activity="IDLE"
            if [ -n "$TUI_ACTIVITY_DIR" ] && [ -d "$TUI_ACTIVITY_DIR" ]; then
                # Get current pane output hash
                current_hash="$(tmux capture-pane -t "$session" -p 2>/dev/null | cksum)"
                hash_file="$TUI_ACTIVITY_DIR/${session}.hash"
                time_file="$TUI_ACTIVITY_DIR/${session}.time"

                if [ -f "$hash_file" ]; then
                    last_hash="$(cat "$hash_file")"
                    if [ "$current_hash" != "$last_hash" ]; then
                        # Output changed - session is busy
                        activity="BUSY"
                        echo "$current_hash" > "$hash_file"
                        date +%s > "$time_file"
                    else
                        # Check how long since last change
                        if [ -f "$time_file" ]; then
                            last_time="$(cat "$time_file")"
                            current_time="$(date +%s)"
                            idle_secs=$((current_time - last_time))
                            # Consider busy if changed in last 5 seconds
                            if [ "$idle_secs" -lt 5 ]; then
                                activity="BUSY"
                            fi
                        fi
                    fi
                else
                    # First time seeing this session
                    echo "$current_hash" > "$hash_file"
                    date +%s > "$time_file"
                    activity="BUSY"  # Assume busy on first check
                fi
            fi
        else
            sess_status="DEAD"
            activity="-"
        fi

        # Get tag for this branch
        tag="$(tui_get_tag "$branch")"

        # Apply tag filter if set
        if [ -n "$TUI_TAG_FILTER" ]; then
            if [ "$tag" != "$TUI_TAG_FILTER" ]; then
                continue
            fi
        fi

        # Apply search pattern filter if set
        if [ -n "$TUI_SEARCH_PATTERN" ]; then
            # Case-insensitive search using shell pattern matching
            _search_lower="$(printf '%s' "$TUI_SEARCH_PATTERN" | tr '[:upper:]' '[:lower:]')"
            _branch_lower="$(printf '%s' "$branch" | tr '[:upper:]' '[:lower:]')"
            case "$_branch_lower" in
                *"$_search_lower"*)
                    # Match found, include this item
                    ;;
                *)
                    # No match, skip
                    continue
                    ;;
            esac
        fi

        # Format: branch<TAB>session<TAB>ai<TAB>status<TAB>tag<TAB>activity
        # Use "-" as placeholder for empty values (POSIX read collapses consecutive tabs)
        printf "%s\t%s\t%s\t%s\t%s\t%s\n" "$branch" "$session" "${ai:--}" "$sess_status" "${tag:--}" "$activity" >> "$TUI_TEMP_LIST"
        TUI_ITEM_COUNT=$((TUI_ITEM_COUNT + 1))
    done < "$HYDRA_MAP"

    # Adjust selection if out of bounds
    if [ "$TUI_ITEM_COUNT" -gt 0 ]; then
        if [ "$TUI_SELECTED" -ge "$TUI_ITEM_COUNT" ]; then
            TUI_SELECTED=$((TUI_ITEM_COUNT - 1))
        fi
    else
        TUI_SELECTED=0
    fi

    return 0
}

# Get session data at index
# Usage: tui_get_session_at <index>
# Returns: session data on stdout (branch|session|ai|status)
tui_get_session_at() {
    idx="$1"
    sed -n "$((idx + 1))p" "$TUI_TEMP_LIST"
}

# Draw a horizontal line
# Usage: tui_draw_line
tui_draw_line() {
    i=0
    while [ "$i" -lt "$TUI_COLS" ]; do
        printf "-"
        i=$((i + 1))
    done
    printf "\n"
}

# Render help overlay
# Usage: tui_render_help
# Returns: 0 on success
tui_render_help() {
    # Calculate box dimensions
    box_width=50
    if [ "$TUI_COLS" -lt 60 ]; then
        box_width=$((TUI_COLS - 4))
    fi
    box_height=22

    # Center the box
    start_col=$(( (TUI_COLS - box_width) / 2 ))
    start_row=$(( (TUI_ROWS - box_height) / 2 ))
    if [ "$start_row" -lt 1 ]; then
        start_row=1
    fi

    # Move to top and clear
    printf "%s%s" "$TUI_HOME" "$TUI_CLEAR"

    # Print empty lines to position box
    i=0
    while [ "$i" -lt "$start_row" ]; do
        printf "\n"
        i=$((i + 1))
    done

    # Helper to print centered line in box
    # Draw top border
    printf "%*s" "$start_col" ""
    printf "+"
    i=0
    while [ "$i" -lt $((box_width - 2)) ]; do
        printf "-"
        i=$((i + 1))
    done
    printf "+\n"

    # Title
    title="HYDRA TUI - KEYBOARD SHORTCUTS"
    title_len=${#title}
    title_pad=$(( (box_width - 2 - title_len) / 2 ))
    printf "%*s" "$start_col" ""
    printf "|%s%*s%s%*s%s|\n" "$TUI_BOLD" "$title_pad" "" "$title" $((box_width - 2 - title_pad - title_len)) "" "$TUI_RESET"

    # Separator
    printf "%*s" "$start_col" ""
    printf "|"
    i=0
    while [ "$i" -lt $((box_width - 2)) ]; do
        printf "-"
        i=$((i + 1))
    done
    printf "|\n"

    # Help entries (left-aligned with padding)
    tui_help_line() {
        key="$1"
        desc="$2"
        content_width=$((box_width - 4))
        printf "%*s" "$start_col" ""
        printf "| %s%-8s%s %-*s |\n" "$TUI_GREEN" "$key" "$TUI_RESET" $((content_width - 9)) "$desc"
    }

    tui_help_line "q" "Quit TUI"
    tui_help_line "j / DOWN" "Move selection down"
    tui_help_line "k / UP" "Move selection up"
    tui_help_line "s" "Switch to selected session"
    tui_help_line "n" "Spawn new session"
    tui_help_line "d" "Kill selected session"
    tui_help_line "a" "Kill all sessions"
    tui_help_line "r" "Regenerate sessions"
    tui_help_line "t" "Cycle tag (wip/review/priority)"
    tui_help_line "T" "Filter by tag"
    tui_help_line "/" "Search sessions"
    tui_help_line "Esc" "Clear filters"
    tui_help_line "i" "Show session status"
    tui_help_line "?" "Show this help"

    # Empty line
    printf "%*s" "$start_col" ""
    printf "| %-*s |\n" $((box_width - 4)) ""

    # Separator
    printf "%*s" "$start_col" ""
    printf "|"
    i=0
    while [ "$i" -lt $((box_width - 2)) ]; do
        printf "-"
        i=$((i + 1))
    done
    printf "|\n"

    # Footer
    footer="Press any key to close"
    footer_len=${#footer}
    footer_pad=$(( (box_width - 2 - footer_len) / 2 ))
    printf "%*s" "$start_col" ""
    printf "|%s%*s%s%*s%s|\n" "$TUI_DIM" "$footer_pad" "" "$footer" $((box_width - 2 - footer_pad - footer_len)) "" "$TUI_RESET"

    # Bottom border
    printf "%*s" "$start_col" ""
    printf "+"
    i=0
    while [ "$i" -lt $((box_width - 2)) ]; do
        printf "-"
        i=$((i + 1))
    done
    printf "+\n"

    return 0
}

# Initialize tags subsystem
# Usage: tui_init_tags
tui_init_tags() {
    TUI_TAGS_FILE="$HYDRA_HOME/tags"
    if [ ! -f "$TUI_TAGS_FILE" ]; then
        touch "$TUI_TAGS_FILE" 2>/dev/null || true
    fi
}

# Get tag for a branch
# Usage: tui_get_tag <branch>
# Returns: Tag value on stdout (wip, review, priority, or empty)
tui_get_tag() {
    _branch="$1"
    if [ -z "$_branch" ] || [ ! -f "$TUI_TAGS_FILE" ]; then
        return 0
    fi
    # Read tag for branch (format: branch tag)
    while IFS=' ' read -r b t; do
        if [ "$b" = "$_branch" ]; then
            printf "%s" "$t"
            return 0
        fi
    done < "$TUI_TAGS_FILE"
}

# Set tag for a branch
# Usage: tui_set_tag <branch> <tag>
# tag: wip, review, priority, or empty to remove
tui_set_tag() {
    _branch="$1"
    _tag="$2"
    if [ -z "$_branch" ] || [ -z "$TUI_TAGS_FILE" ]; then
        return 1
    fi

    # Create temp file for atomic update
    _tmpfile="$(mktemp)" || return 1

    # Copy all entries except the one being updated
    if [ -f "$TUI_TAGS_FILE" ]; then
        while IFS=' ' read -r b t; do
            if [ "$b" != "$_branch" ] && [ -n "$b" ]; then
                printf "%s %s\n" "$b" "$t" >> "$_tmpfile"
            fi
        done < "$TUI_TAGS_FILE"
    fi

    # Add new tag if not empty
    if [ -n "$_tag" ]; then
        printf "%s %s\n" "$_branch" "$_tag" >> "$_tmpfile"
    fi

    # Atomic move
    mv "$_tmpfile" "$TUI_TAGS_FILE" 2>/dev/null || {
        rm -f "$_tmpfile"
        return 1
    }
    return 0
}

# Cycle tag for selected branch
# Usage: tui_cycle_tag <branch>
# Cycles: (none) -> wip -> review -> priority -> (none)
tui_cycle_tag() {
    _branch="$1"
    if [ -z "$_branch" ]; then
        return 1
    fi

    _current="$(tui_get_tag "$_branch")"

    case "$_current" in
        "")
            tui_set_tag "$_branch" "wip"
            ;;
        "wip")
            tui_set_tag "$_branch" "review"
            ;;
        "review")
            tui_set_tag "$_branch" "priority"
            ;;
        "priority")
            tui_set_tag "$_branch" ""
            ;;
        *)
            tui_set_tag "$_branch" "wip"
            ;;
    esac
}

# Cycle tag filter
# Usage: tui_cycle_tag_filter
# Cycles: (all) -> wip -> review -> priority -> (all)
tui_cycle_tag_filter() {
    case "$TUI_TAG_FILTER" in
        "")
            TUI_TAG_FILTER="wip"
            ;;
        "wip")
            TUI_TAG_FILTER="review"
            ;;
        "review")
            TUI_TAG_FILTER="priority"
            ;;
        "priority")
            TUI_TAG_FILTER=""
            ;;
    esac
}

# Render the TUI screen
# Usage: tui_render
# Returns: 0 on success
tui_render() {
    # Move to top and clear (TUI_CURRENT_SESSION cached in tui_build_list)
    printf "%s%s" "$TUI_HOME" "$TUI_CLEAR"

    # Header
    printf "%s%s Hydra TUI %s- Interactive Session Manager%s\n" "$TUI_BOLD" "$TUI_GREEN" "$TUI_RESET$TUI_DIM" "$TUI_RESET"
    printf "%s\n" "q=quit | j/k=nav | s=switch | n=spawn | d=kill | t=tag | /=search | ?=help"
    tui_draw_line

    # Show active filters
    if [ -n "$TUI_TAG_FILTER" ] || [ -n "$TUI_SEARCH_PATTERN" ]; then
        filter_info=""
        if [ -n "$TUI_TAG_FILTER" ]; then
            filter_info="tag:$TUI_TAG_FILTER"
        fi
        if [ -n "$TUI_SEARCH_PATTERN" ]; then
            if [ -n "$filter_info" ]; then
                filter_info="$filter_info, "
            fi
            filter_info="${filter_info}search:\"$TUI_SEARCH_PATTERN\""
        fi
        printf "%s[Filter: %s] (Esc to clear)%s\n" "$TUI_YELLOW" "$filter_info" "$TUI_RESET"
    fi

    # Handle empty list
    if [ "$TUI_ITEM_COUNT" -eq 0 ]; then
        if [ -n "$TUI_SEARCH_PATTERN" ]; then
            printf "\n%s  No sessions matching '%s'%s\n" "$TUI_YELLOW" "$TUI_SEARCH_PATTERN" "$TUI_RESET"
            printf "\n  Press Esc to clear search\n"
        elif [ -n "$TUI_TAG_FILTER" ]; then
            printf "\n%s  No sessions with tag '%s'%s\n" "$TUI_YELLOW" "$TUI_TAG_FILTER" "$TUI_RESET"
            printf "\n  Press 'T' to change filter\n"
        else
            printf "\n%s  No active Hydra sessions%s\n" "$TUI_YELLOW" "$TUI_RESET"
            printf "\n  Press 'n' to spawn a new session\n"
            printf "  Press 'r' to regenerate sessions from existing worktrees\n"
        fi
        printf "  Press 'q' to quit\n"
        return 0
    fi

    # Calculate visible range (leave room for header and footer)
    max_items=$((TUI_ROWS - 8))
    if [ "$max_items" -lt 1 ]; then
        max_items=5
    fi

    # Adjust offset if selection moved out of view
    if [ "$TUI_SELECTED" -lt "$TUI_OFFSET" ]; then
        TUI_OFFSET="$TUI_SELECTED"
    elif [ "$TUI_SELECTED" -ge $((TUI_OFFSET + max_items)) ]; then
        TUI_OFFSET=$((TUI_SELECTED - max_items + 1))
    fi

    start_idx="$TUI_OFFSET"
    end_idx=$((start_idx + max_items))

    # Show scroll indicator if needed
    if [ "$TUI_OFFSET" -gt 0 ]; then
        printf "%s  [...%d more above...]%s\n" "$TUI_DIM" "$TUI_OFFSET" "$TUI_RESET"
    else
        printf "\n"
    fi

    # Render visible items (tab-delimited with 6 fields)
    idx=0
    while IFS='	' read -r branch session ai status tag activity; do
        [ -z "$branch" ] && continue

        # Skip items before visible range
        if [ "$idx" -lt "$start_idx" ]; then
            idx=$((idx + 1))
            continue
        fi

        # Stop at end of visible range
        if [ "$idx" -ge "$end_idx" ]; then
            break
        fi

        # Status indicator with activity
        if [ "$status" = "ALIVE" ]; then
            if [ "$activity" = "BUSY" ]; then
                status_str="${TUI_YELLOW}[BUSY]${TUI_RESET}"
            else
                status_str="${TUI_GREEN}[IDLE]${TUI_RESET}"
            fi
        else
            status_str="${TUI_RED}[DEAD]${TUI_RESET}"
        fi

        # AI tool indicator ("-" is placeholder for none)
        ai_str=""
        if [ -n "$ai" ] && [ "$ai" != "-" ]; then
            ai_str=" ${TUI_BLUE}[$ai]${TUI_RESET}"
        fi

        # Tag indicator with colors ("-" is placeholder for none)
        tag_str=""
        if [ -n "$tag" ] && [ "$tag" != "-" ]; then
            case "$tag" in
                "wip")
                    tag_str=" ${TUI_YELLOW}[WIP]${TUI_RESET}"
                    ;;
                "review")
                    tag_str=" ${TUI_BLUE}[REVIEW]${TUI_RESET}"
                    ;;
                "priority")
                    tag_str=" ${TUI_RED}[PRIORITY]${TUI_RESET}"
                    ;;
                *)
                    tag_str=" ${TUI_DIM}[$tag]${TUI_RESET}"
                    ;;
            esac
        fi

        # Current session marker (uses cached TUI_CURRENT_SESSION)
        current_str=""
        if [ "$session" = "$TUI_CURRENT_SESSION" ]; then
            current_str=" ${TUI_DIM}(current)${TUI_RESET}"
        fi

        # Build display line
        line="$status_str $branch -> $session$ai_str$tag_str$current_str"

        # Highlight selected row
        if [ "$idx" -eq "$TUI_SELECTED" ]; then
            printf "%s> %s%s\n" "$TUI_REVERSE" "$line" "$TUI_RESET"
        else
            printf "  %s\n" "$line"
        fi

        idx=$((idx + 1))
    done < "$TUI_TEMP_LIST"

    # Show scroll indicator if more items below
    remaining=$((TUI_ITEM_COUNT - end_idx))
    if [ "$remaining" -gt 0 ]; then
        printf "%s  [...%d more below...]%s\n" "$TUI_DIM" "$remaining" "$TUI_RESET"
    fi

    # Footer with session count and return hint
    printf "\n"
    tui_draw_line
    printf "%s%d session(s)%s | After switch: prefix+s to return\n" "$TUI_DIM" "$TUI_ITEM_COUNT" "$TUI_RESET"

    return 0
}

# Read single keypress
# Usage: tui_get_key
# Returns: key character on stdout (may be empty on timeout)
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
        n|N)
            # Spawn new session
            tui_action_spawn
            ;;
        d|D)
            # Kill selected session
            tui_action_kill
            ;;
        r|R)
            # Regenerate sessions
            tui_action_regenerate
            ;;
        a|A)
            # Kill all sessions
            tui_action_kill_all
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
        *)
            # Handle escape key for clearing filters
            # Check if key is escape (octal 033, hex 1b)
            if [ "$key" = "$(printf '\033')" ]; then
                # Clear all filters
                if [ -n "$TUI_SEARCH_PATTERN" ] || [ -n "$TUI_TAG_FILTER" ]; then
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

# Action: Switch to selected session
# Usage: tui_action_switch
tui_action_switch() {
    if [ "$TUI_ITEM_COUNT" -eq 0 ]; then
        return 0
    fi

    # Get selected item
    selected_line="$(tui_get_session_at "$TUI_SELECTED")"

    # Validate we got valid data
    if [ -z "$selected_line" ]; then
        return 0
    fi

    session="$(printf '%s' "$selected_line" | cut -f2)"
    status="$(printf '%s' "$selected_line" | cut -f4)"

    if [ -z "$session" ]; then
        return 0
    fi

    # Can't switch to current session
    if [ "$session" = "$TUI_CURRENT_SESSION" ]; then
        tui_pause_for_interaction
        printf "%s[INFO] Already in session '%s'%s\n" "$TUI_YELLOW" "$session" "$TUI_RESET"
        tui_resume_after_interaction
        return 0
    fi

    # Try to switch even if status shows DEAD (might be stale)
    # tmux switch-client will fail if session truly doesn't exist
    if ! tmux switch-client -t "$session" 2>/dev/null; then
        tui_pause_for_interaction
        printf "%s[ERROR] Cannot switch to session '%s'%s\n" "$TUI_RED" "$session" "$TUI_RESET"
        printf "Session does not exist.\n"
        printf "  - Press 'd' to remove this stale entry\n"
        printf "  - Press 'n' to spawn a new session\n"
        tui_resume_after_interaction
        return 0
    fi

    # Update current session cache and force redraw when user returns
    TUI_CURRENT_SESSION="$session"
    TUI_NEEDS_REDRAW=1
}

# Action: Spawn new session
# Usage: tui_action_spawn
tui_action_spawn() {
    tui_pause_for_interaction

    printf "Enter branch name (or press Enter to cancel): "
    read -r branch

    if [ -n "$branch" ]; then
        printf "\n%s[...] Spawning session for '%s'...%s\n\n" "$TUI_YELLOW" "$branch" "$TUI_RESET"
        # Use existing spawn logic
        if spawn_single "$branch" "default" ""; then
            printf "\n%s[OK] Session created for '%s'%s\n" "$TUI_GREEN" "$branch" "$TUI_RESET"
        else
            printf "\n%s[FAIL] Failed to create session%s\n" "$TUI_RED" "$TUI_RESET"
        fi
    else
        printf "\nCancelled.\n"
    fi

    tui_resume_after_interaction
    tui_build_list
}

# Action: Kill selected session
# Usage: tui_action_kill
tui_action_kill() {
    if [ "$TUI_ITEM_COUNT" -eq 0 ]; then
        return 0
    fi

    # Get selected item
    selected_line="$(tui_get_session_at "$TUI_SELECTED")"

    # Validate we got valid data
    if [ -z "$selected_line" ]; then
        return 0
    fi

    branch="$(printf '%s' "$selected_line" | cut -f1)"
    session="$(printf '%s' "$selected_line" | cut -f2)"

    # Validate extracted values
    if [ -z "$branch" ] || [ -z "$session" ]; then
        return 0
    fi

    # Prevent killing the session we're running in
    if [ "$session" = "$TUI_CURRENT_SESSION" ]; then
        tui_pause_for_interaction
        printf "%s[ERROR] Cannot kill current session '%s'%s\n" "$TUI_RED" "$session" "$TUI_RESET"
        printf "You are running the TUI from this session.\n"
        printf "Switch to another session first, or use 'hydra kill' from outside.\n"
        tui_resume_after_interaction
        return 0
    fi

    tui_pause_for_interaction

    printf "Kill session '%s' for branch '%s'? [y/N] " "$session" "$branch"
    read -r confirm

    case "$confirm" in
        [yY]|[yY][eE][sS])
            printf "\n%s[...] Killing session '%s'...%s\n" "$TUI_YELLOW" "$session" "$TUI_RESET"
            if kill_single_head "$branch" "$session"; then
                printf "%s[OK] Session killed%s\n" "$TUI_GREEN" "$TUI_RESET"
            else
                printf "%s[FAIL] Failed to kill session%s\n" "$TUI_RED" "$TUI_RESET"
            fi
            ;;
        *)
            printf "\nCancelled.\n"
            ;;
    esac

    tui_resume_after_interaction
    tui_build_list
}

# Action: Regenerate sessions
# Usage: tui_action_regenerate
tui_action_regenerate() {
    tui_pause_for_interaction

    printf "%s[...] Regenerating sessions from existing worktrees...%s\n\n" "$TUI_YELLOW" "$TUI_RESET"
    cmd_regenerate
    printf "\n%s[OK] Regeneration complete%s\n" "$TUI_GREEN" "$TUI_RESET"

    tui_resume_after_interaction
    tui_build_list
}

# Action: Kill all sessions
# Usage: tui_action_kill_all
tui_action_kill_all() {
    if [ "$TUI_ITEM_COUNT" -eq 0 ]; then
        return 0
    fi

    tui_pause_for_interaction

    printf "%sWARNING: This will kill ALL Hydra sessions!%s\n" "$TUI_RED" "$TUI_RESET"
    if [ -n "$TUI_CURRENT_SESSION" ]; then
        printf "%s(Current session '%s' will be skipped)%s\n" "$TUI_YELLOW" "$TUI_CURRENT_SESSION" "$TUI_RESET"
    fi
    printf "Are you sure? [y/N] "
    read -r confirm

    case "$confirm" in
        [yY]|[yY][eE][sS])
            printf "\n"
            # Kill all except current session
            killed=0
            skipped=0
            while IFS='	' read -r branch session _ai _status _tag _activity; do
                [ -z "$branch" ] && continue
                if [ "$session" = "$TUI_CURRENT_SESSION" ]; then
                    skipped=1
                    continue
                fi
                if kill_single_head "$branch" "$session" 2>/dev/null; then
                    killed=$((killed + 1))
                fi
            done < "$TUI_TEMP_LIST"
            printf "%s[OK] Killed %d session(s)%s\n" "$TUI_GREEN" "$killed" "$TUI_RESET"
            if [ "$skipped" -eq 1 ]; then
                printf "%s(Skipped current session)%s\n" "$TUI_YELLOW" "$TUI_RESET"
            fi
            ;;
        *)
            printf "\nCancelled.\n"
            ;;
    esac

    tui_resume_after_interaction
    tui_build_list
}

# Action: Show status
# Usage: tui_action_status
tui_action_status() {
    tui_pause_for_interaction

    cmd_status

    tui_resume_after_interaction
}

# Action: Cycle tag for selected session
# Usage: tui_action_tag
tui_action_tag() {
    if [ "$TUI_ITEM_COUNT" -eq 0 ]; then
        return 0
    fi

    # Get selected item
    selected_line="$(tui_get_session_at "$TUI_SELECTED")"
    if [ -z "$selected_line" ]; then
        return 0
    fi

    branch="$(printf '%s' "$selected_line" | cut -f1)"
    if [ -z "$branch" ]; then
        return 0
    fi

    # Cycle the tag
    tui_cycle_tag "$branch"

    # Rebuild list to reflect changes
    tui_build_list
}

# Render search input prompt
# Usage: tui_render_search_prompt
tui_render_search_prompt() {
    # Move to top and clear
    printf "%s%s" "$TUI_HOME" "$TUI_CLEAR"

    # Header
    printf "%s%s Hydra TUI %s- Search Mode%s\n" "$TUI_BOLD" "$TUI_GREEN" "$TUI_RESET$TUI_DIM" "$TUI_RESET"
    printf "%s\n" "Type to search | Enter=confirm | Esc=cancel"
    tui_draw_line

    # Search prompt
    printf "\n%sSearch:%s %s" "$TUI_YELLOW" "$TUI_RESET" "$TUI_SEARCH_PATTERN"
    printf "_"  # Cursor indicator
    printf "\n"

    # Preview count
    printf "\n%s%d matching session(s)%s\n" "$TUI_DIM" "$TUI_ITEM_COUNT" "$TUI_RESET"
}

# Main TUI loop
# Usage: tui_main_loop
# Returns: 0 on normal exit
tui_main_loop() {
    # Use iteration counter instead of date subprocess each loop
    # stty timeout is 0.1s, so ~30 iterations = 3 seconds
    loop_count=0
    refresh_every=30  # iterations (~3 seconds with 0.1s timeout)

    while [ "$TUI_RUNNING" -eq 1 ]; do
        # Handle help overlay mode
        if [ "$TUI_HELP_VISIBLE" -eq 1 ]; then
            tui_render_help
            # Wait for any key to dismiss
            key="$(tui_get_key)"
            if [ -n "$key" ]; then
                TUI_HELP_VISIBLE=0
                TUI_NEEDS_REDRAW=1
            fi
            continue
        fi

        # Handle search mode
        if [ "$TUI_SEARCH_MODE" -eq 1 ]; then
            tui_build_list
            tui_render_search_prompt

            key="$(tui_get_key)"
            if [ -n "$key" ]; then
                # Handle escape (cancel search, keep previous pattern)
                if [ "$key" = "$(printf '\033')" ]; then
                    # Read any remaining escape sequence chars
                    dd bs=1 count=2 2>/dev/null >/dev/null || true
                    TUI_SEARCH_MODE=0
                    TUI_NEEDS_REDRAW=1
                # Handle enter (confirm search)
                elif [ "$key" = "$(printf '\r')" ] || [ "$key" = "$(printf '\n')" ]; then
                    TUI_SEARCH_MODE=0
                    TUI_NEEDS_REDRAW=1
                # Handle backspace (delete last char)
                elif [ "$key" = "$(printf '\177')" ] || [ "$key" = "$(printf '\b')" ]; then
                    if [ -n "$TUI_SEARCH_PATTERN" ]; then
                        TUI_SEARCH_PATTERN="${TUI_SEARCH_PATTERN%?}"
                    fi
                # Printable characters (append to pattern)
                else
                    # Only append printable ASCII chars
                    case "$key" in
                        [[:print:]])
                            TUI_SEARCH_PATTERN="${TUI_SEARCH_PATTERN}${key}"
                            ;;
                    esac
                fi
            fi
            continue
        fi

        # Check if refresh needed (approximately every 3 seconds)
        if [ "$loop_count" -ge "$refresh_every" ] || [ "$loop_count" -eq 0 ]; then
            tui_build_list
            TUI_NEEDS_REDRAW=1
            loop_count=0
        fi

        # Only render when state has changed (reduces flutter)
        if [ "$TUI_NEEDS_REDRAW" -eq 1 ]; then
            tui_render
            TUI_NEEDS_REDRAW=0
        fi

        # Read key (with timeout from stty settings)
        key="$(tui_get_key)"

        # Handle key if pressed
        if [ -n "$key" ]; then
            if ! tui_handle_key "$key"; then
                TUI_RUNNING=0
            fi
            TUI_NEEDS_REDRAW=1
        fi

        loop_count=$((loop_count + 1))
    done

    return 0
}

# Entry point for TUI command
# Usage: cmd_tui
# Returns: 0 on success, 1 on failure
cmd_tui() {
    # Check if in terminal
    if [ ! -t 0 ] || [ ! -t 1 ]; then
        echo "Error: TUI requires an interactive terminal" >&2
        return 1
    fi

    # Check tmux availability
    if ! check_tmux_version 2>/dev/null; then
        echo "Error: TUI requires tmux >= 3.0" >&2
        return 1
    fi

    # Check for tput (warn but don't fail)
    if ! command -v tput >/dev/null 2>&1; then
        echo "Warning: tput not found, TUI will have limited formatting" >&2
    fi

    # Initialize
    if ! tui_init; then
        echo "Error: Failed to initialize TUI" >&2
        return 1
    fi

    # Set up cleanup trap
    trap 'tui_cleanup' EXIT INT TERM HUP

    # Initial data load
    tui_build_list

    # Run main loop
    tui_main_loop

    # Cleanup handled by trap
    return 0
}
