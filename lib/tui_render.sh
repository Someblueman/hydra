#!/bin/sh
# Hydra TUI module
# POSIX-compliant shell script
# shellcheck disable=SC2034

tui_render_help() {
    # Calculate box dimensions
    box_width=50
    if [ "$TUI_COLS" -lt 60 ]; then
        box_width=$((TUI_COLS - 4))
    fi
    box_height=26

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
    tui_help_line "s / Enter" "Switch to selected session"
    tui_help_line "n" "Spawn new session (wizard)"
    tui_help_line "d" "Kill selected session"
    tui_help_line "D" "Open dashboard"
    tui_help_line "a" "Kill all sessions"
    tui_help_line "A" "Select all sessions"
    tui_help_line "r" "Regenerate sessions"
    tui_help_line "t" "Cycle tag (wip/review/priority)"
    tui_help_line "T" "Filter by tag"
    tui_help_line "/" "Search (branch/session/group/ai)"
    tui_help_line "i" "Show session status"
    tui_help_line "p" "Toggle preview panel"
    tui_help_line "f" "Toggle preview follow mode"
    tui_help_line "?" "Show this help"
    tui_help_line "SPACE" "Toggle multi-select"
    tui_help_line "x" "Bulk kill selected"
    tui_help_line "G" "Bulk set group"
    tui_help_line "Esc" "Clear selection/filters"

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

# Render the main TUI screen
# Usage: tui_render
tui_render() {
    # Move to top and clear (TUI_CURRENT_SESSION cached in tui_build_list)
    printf "%s%s" "$TUI_HOME" "$TUI_CLEAR"

    # One-time keybinding hint
    if [ "$TUI_HINT_SHOWN" -eq 0 ]; then
        printf "%sTip: Enter also switches (s still works). Press ? for all keys.%s\n" "$TUI_DIM" "$TUI_RESET"
        TUI_HINT_SHOWN=1
    fi

    # Header
    printf "%s%s Hydra TUI %s- Session Manager%s\n" "$TUI_BOLD" "$TUI_GREEN" "$TUI_RESET$TUI_DIM" "$TUI_RESET"
    printf "%s\n" "Enter/s=switch | n=spawn | d=kill | D=dash | p=preview | ?=help | q=quit"
    tui_draw_line

    # Show selection count if any items are selected
    _sel_count="$(tui_selection_count)"
    if [ "$_sel_count" -gt 0 ]; then
        printf "%s[%d selected] x=bulk kill | G=set group | Esc=clear selection%s\n" "$TUI_YELLOW" "$_sel_count" "$TUI_RESET"
    fi

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

    # Calculate visible range (leave room for header, footer, and optional preview)
    _preview_height=0
    if [ "$TUI_PREVIEW_VISIBLE" -eq 1 ]; then
        _preview_height=$((TUI_PREVIEW_LINES + 3))  # +3 for header line, separator, and spacing
    fi
    max_items=$((TUI_ROWS - 8 - _preview_height))
    if [ "$max_items" -lt 3 ]; then
        # If terminal too small, disable preview temporarily
        if [ "$TUI_PREVIEW_VISIBLE" -eq 1 ]; then
            TUI_PREVIEW_VISIBLE=0
            _preview_height=0
            max_items=$((TUI_ROWS - 8))
        fi
    fi
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

    # Render visible items (tab-delimited: branch session ai status tag activity group pr)
    idx=0
    while IFS='	' read -r branch session ai status tag activity group pr; do
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

        group_str=""
        if [ -n "$group" ] && [ "$group" != "-" ]; then
            group_str=" ${TUI_DIM}[$group]${TUI_RESET}"
        fi
        pr_str=""
        if [ -n "$pr" ] && [ "$pr" != "-" ]; then
            pr_str=" ${TUI_DIM}#${pr}${TUI_RESET}"
        fi

        # Current session marker (uses cached TUI_CURRENT_SESSION)
        current_str=""
        if [ "$session" = "$TUI_CURRENT_SESSION" ]; then
            current_str=" ${TUI_DIM}(current)${TUI_RESET}"
        fi

        # Build display line
        line="$status_str $branch -> $session$ai_str$tag_str$group_str$pr_str$current_str"

        # Multi-select indicator
        select_marker="  "
        if tui_is_selected "$idx"; then
            select_marker="${TUI_GREEN}[x]${TUI_RESET}"
        fi

        # Highlight selected row
        if [ "$idx" -eq "$TUI_SELECTED" ]; then
            printf "%s>%s %s%s\n" "$TUI_REVERSE" "$select_marker" "$line" "$TUI_RESET"
        else
            printf " %s %s\n" "$select_marker" "$line"
        fi

        idx=$((idx + 1))
    done < "$TUI_TEMP_LIST"

    # Show scroll indicator if more items below
    remaining=$((TUI_ITEM_COUNT - end_idx))
    if [ "$remaining" -gt 0 ]; then
        printf "%s  [...%d more below...]%s\n" "$TUI_DIM" "$remaining" "$TUI_RESET"
    fi

    # Render preview panel if visible
    if [ "$TUI_PREVIEW_VISIBLE" -eq 1 ] && [ "$TUI_ITEM_COUNT" -gt 0 ]; then
        # Get currently selected session info
        _selected_idx=0
        _selected_branch=""
        _selected_session=""
        _selected_status=""
        while IFS='	' read -r _b _s _a _st _tag _act _grp _pr; do
            if [ "$_selected_idx" -eq "$TUI_SELECTED" ]; then
                _selected_branch="$_b"
                _selected_session="$_s"
                _selected_status="$_st"
                _selected_group="$_grp"
                _selected_pr="$_pr"
                break
            fi
            _selected_idx=$((_selected_idx + 1))
        done < "$TUI_TEMP_LIST"

        # Detail sidebar on wide terminals
        if [ "$TUI_COLS" -ge "$TUI_WIDE_COLS" ]; then
            tui_draw_line
            printf "%s%s Detail:%s\n" "$TUI_BOLD" "$TUI_BLUE" "$TUI_RESET"
            printf "  Branch: %s\n" "$_selected_branch"
            printf "  Session: %s\n" "$_selected_session"
            if [ -n "$_selected_group" ] && [ "$_selected_group" != "-" ]; then
                printf "  Group: %s\n" "$_selected_group"
            fi
            if [ -n "$_selected_pr" ] && [ "$_selected_pr" != "-" ]; then
                printf "  PR: #%s\n" "$_selected_pr"
            fi
        fi

        if [ -n "$_selected_session" ]; then
            printf "\n"
            tui_draw_line
            printf "%s%s PREVIEW: %s (%s)%s\n" "$TUI_BOLD" "$TUI_BLUE" "$_selected_branch" "$_selected_session" "$TUI_RESET"

            if [ "$_selected_status" = "ALIVE" ]; then
                tui_capture_preview "$_selected_session"
            else
                printf "%s(session not running - cannot preview)%s\n" "$TUI_DIM" "$TUI_RESET"
            fi
        fi
    fi

    # Footer with session count and return hint
    printf "\n"
    tui_draw_line
    _footer_extra=""
    if [ "$TUI_PREVIEW_VISIBLE" -eq 1 ]; then
        _footer_extra=" | p=hide"
        if [ "$TUI_PREVIEW_FOLLOW" -eq 1 ]; then
            _footer_extra="${_footer_extra} | f=follow:on"
        else
            _footer_extra="${_footer_extra} | f=follow:off"
        fi
    else
        _footer_extra=" | p=preview"
    fi
    printf "%s%d session(s)%s | Enter/s switch | n spawn | d kill | D dashboard%s\n" \
        "$TUI_DIM" "$TUI_ITEM_COUNT" "$_footer_extra" "$TUI_RESET"

    return 0
}

# Render search input prompt
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
