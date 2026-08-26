#!/bin/sh
# Hydra TUI module
# POSIX-compliant shell script
# shellcheck disable=SC2034

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

    # Switch or attach depending on whether we're in tmux
    if [ -n "${TMUX:-}" ]; then
        # Inside tmux - use switch-client
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
    else
        # Outside tmux - attach to the session
        if ! tmux_session_exists "$session"; then
            tui_pause_for_interaction
            printf "%s[ERROR] Session '%s' does not exist%s\n" "$TUI_RED" "$session" "$TUI_RESET"
            printf "  - Press 'd' to remove this stale entry\n"
            printf "  - Press 'n' to spawn a new session\n"
            tui_resume_after_interaction
            return 0
        fi
        tui_cleanup
        exec tmux attach-session -t "$session"
    fi
}

# Action: Spawn new session
# Usage: tui_action_spawn
# Supports inline options: branch --ai codex --template dev --layout full
tui_action_spawn() {
    tui_pause_for_interaction

    printf "Enter branch name (or: branch --ai codex --template dev): "
    read -r input

    if [ -z "$input" ]; then
        printf "\nCancelled.\n"
        tui_resume_after_interaction
        return 0
    fi

    branch=""
    ai_tool=""
    layout="default"
    template=""

    # shellcheck disable=SC2086
    set -- $input
    while [ $# -gt 0 ]; do
        case "$1" in
            --ai|-a)
                shift
                ai_tool="${1:-}"
                ;;
            --template|-t)
                shift
                template="${1:-}"
                ;;
            --layout|-l)
                shift
                layout="${1:-}"
                ;;
            -*)
                ;;
            *)
                if [ -z "$branch" ]; then
                    branch="$1"
                fi
                ;;
        esac
        shift
    done

    if [ -z "$ai_tool" ] && [ -z "$template" ]; then
        printf "\nAI tool (1=claude 2=codex 3=aider 4=gemini 5=custom, Enter=claude): "
        read -r ai_choice
        case "$ai_choice" in
            2) ai_tool="codex" ;;
            3) ai_tool="aider" ;;
            4) ai_tool="gemini" ;;
            5)
                printf "Custom command: "
                read -r ai_tool
                ;;
            *) ai_tool="${HYDRA_AI_COMMAND:-claude}" ;;
        esac
        printf "Template name (optional): "
        read -r template
        printf "Layout (default/dev/full): "
        read -r layout_choice
        case "$layout_choice" in
            dev|full) layout="$layout_choice" ;;
        esac
    fi

    if [ -z "$branch" ]; then
        printf "\n%s[ERROR] Branch name is required%s\n" "$TUI_RED" "$TUI_RESET"
        tui_resume_after_interaction
        return 0
    fi

    printf "\n%s[...] Spawning session for '%s'...%s\n" "$TUI_YELLOW" "$branch" "$TUI_RESET"
    [ -n "$ai_tool" ] && printf "  AI: %s\n" "$ai_tool"
    [ -n "$template" ] && printf "  Template: %s\n" "$template"
    [ "$layout" != "default" ] && printf "  Layout: %s\n" "$layout"
    printf "\n"

    new_session="$(spawn_single "$branch" "$layout" "$ai_tool" "" "" "" "$template")"
    if [ -n "$new_session" ]; then
        printf "\n%s[OK] Session created: '%s'%s\n" "$TUI_GREEN" "$new_session" "$TUI_RESET"
        if [ -z "${TMUX:-}" ]; then
            printf "Attaching to session '%s'...\n" "$new_session"
            tui_cleanup
            exec tmux attach-session -t "$new_session"
        fi
    else
        printf "\n%s[FAIL] Failed to create session%s\n" "$TUI_RED" "$TUI_RESET"
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

    if [ -t 0 ] && [ -z "${CI:-}" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
        printf "Kill session '%s' for branch '%s'? [y/N] " "$session" "$branch"
        read -r confirm
    else
        confirm="y"
    fi

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

    if [ -t 0 ] && [ -z "${CI:-}" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
        printf "%sWARNING: This will kill ALL Hydra sessions!%s\n" "$TUI_RED" "$TUI_RESET"
        if [ -n "$TUI_CURRENT_SESSION" ]; then
            printf "%s(Current session '%s' will be skipped)%s\n" "$TUI_YELLOW" "$TUI_CURRENT_SESSION" "$TUI_RESET"
        fi
        printf "Are you sure? [y/N] "
        read -r confirm
    else
        confirm="y"
    fi

    case "$confirm" in
        [yY]|[yY][eE][sS])
            printf "\n"
            # Kill all except current session
            killed=0
            skipped=0
            while IFS='	' read -r branch session _ai _status _tag _activity _group _pr; do
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

# Action: Bulk kill selected sessions
# Usage: tui_action_bulk_kill
tui_action_bulk_kill() {
    _sel_count="$(tui_selection_count)"
    if [ "$_sel_count" -eq 0 ]; then
        return 0
    fi

    tui_pause_for_interaction

    if [ -t 0 ] && [ -z "${CI:-}" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
        printf "%sWARNING: This will kill %d selected session(s)!%s\n" "$TUI_RED" "$_sel_count" "$TUI_RESET"
        if [ -n "$TUI_CURRENT_SESSION" ]; then
            printf "%s(Current session will be skipped if selected)%s\n" "$TUI_YELLOW" "$TUI_RESET"
        fi
        printf "Are you sure? [y/N] "
        read -r confirm
    else
        confirm="y"
    fi

    case "$confirm" in
        [yY]|[yY][eE][sS])
            printf "\n"
            killed=0
            skipped=0
            # Iterate through selected indices
            for _sel_idx in $TUI_MULTI_SELECT; do
                selected_line="$(tui_get_session_at "$_sel_idx")"
                [ -z "$selected_line" ] && continue

                branch="$(printf '%s' "$selected_line" | cut -f1)"
                session="$(printf '%s' "$selected_line" | cut -f2)"

                if [ "$session" = "$TUI_CURRENT_SESSION" ]; then
                    skipped=$((skipped + 1))
                    continue
                fi

                if kill_single_head "$branch" "$session" 2>/dev/null; then
                    killed=$((killed + 1))
                    printf "%s[OK] Killed %s%s\n" "$TUI_GREEN" "$branch" "$TUI_RESET"
                else
                    printf "%s[FAIL] Failed to kill %s%s\n" "$TUI_RED" "$branch" "$TUI_RESET"
                fi
            done
            printf "\n%s[OK] Killed %d session(s)%s\n" "$TUI_GREEN" "$killed" "$TUI_RESET"
            if [ "$skipped" -gt 0 ]; then
                printf "%s(Skipped %d - current session)%s\n" "$TUI_YELLOW" "$skipped" "$TUI_RESET"
            fi
            tui_clear_selection
            ;;
        *)
            printf "\nCancelled.\n"
            ;;
    esac

    tui_resume_after_interaction
    tui_build_list
}

# Action: Bulk set group for selected sessions
# Usage: tui_action_bulk_group
tui_action_bulk_group() {
    _sel_count="$(tui_selection_count)"
    if [ "$_sel_count" -eq 0 ]; then
        return 0
    fi

    tui_pause_for_interaction

    printf "Set group for %d selected session(s)\n" "$_sel_count"
    printf "Enter group name (or press Enter to cancel): "
    read -r group_name

    if [ -n "$group_name" ]; then
        updated=0
        # Iterate through selected indices
        for _sel_idx in $TUI_MULTI_SELECT; do
            selected_line="$(tui_get_session_at "$_sel_idx")"
            [ -z "$selected_line" ] && continue

            branch="$(printf '%s' "$selected_line" | cut -f1)"
            [ -z "$branch" ] && continue

            # Update group in state
            if set_group "$branch" "$group_name" 2>/dev/null; then
                updated=$((updated + 1))
            fi
        done
        printf "\n%s[OK] Updated group for %d session(s)%s\n" "$TUI_GREEN" "$updated" "$TUI_RESET"
        tui_clear_selection
    else
        printf "\nCancelled.\n"
    fi

    tui_resume_after_interaction
    tui_build_list
}

# Action: Open tmux dashboard
# Usage: tui_action_dashboard
tui_action_dashboard() {
    if [ "$TUI_ITEM_COUNT" -eq 0 ]; then
        return 0
    fi

    tui_cleanup
    trap - EXIT INT TERM HUP
    cmd_dashboard
    tui_init
    trap 'tui_cleanup' EXIT INT TERM HUP
    tui_build_list
    TUI_NEEDS_REDRAW=1
}

# Render search input prompt
# Usage: tui_render_search_prompt
