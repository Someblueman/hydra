#!/bin/sh
# Hydra TUI module
# POSIX-compliant shell script
# shellcheck disable=SC2034

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

# =============================================================================
# Multi-Select Functions
# =============================================================================

# Check if an index is selected
# Usage: tui_is_selected <index>
# Returns: 0 if selected, 1 if not
tui_is_selected() {
    _idx="$1"
    case " $TUI_MULTI_SELECT " in
        *" $_idx "*) return 0 ;;
        *) return 1 ;;
    esac
}

# Toggle selection for an index
# Usage: tui_toggle_select <index>
tui_toggle_select() {
    _idx="$1"
    if tui_is_selected "$_idx"; then
        # Remove from selection
        TUI_MULTI_SELECT="$(echo " $TUI_MULTI_SELECT " | sed "s/ $_idx / /g" | tr -s ' ' | sed 's/^ *//;s/ *$//')"
    else
        # Add to selection
        if [ -z "$TUI_MULTI_SELECT" ]; then
            TUI_MULTI_SELECT="$_idx"
        else
            TUI_MULTI_SELECT="$TUI_MULTI_SELECT $_idx"
        fi
    fi
}

# Clear all selections
# Usage: tui_clear_selection
tui_clear_selection() {
    TUI_MULTI_SELECT=""
}

# Get count of selected items
# Usage: tui_selection_count
# Returns: count on stdout
tui_selection_count() {
    if [ -z "$TUI_MULTI_SELECT" ]; then
        echo "0"
    else
        # Count space-separated items
        echo "$TUI_MULTI_SELECT" | tr ' ' '\n' | grep -c .
    fi
}

# Select all visible items
# Usage: tui_select_all
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

# Render the TUI screen
# Usage: tui_render
# Returns: 0 on success
