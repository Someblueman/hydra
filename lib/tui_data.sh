#!/bin/sh
# Hydra TUI module
# POSIX-compliant shell script
# shellcheck disable=SC2034

tui_build_list() {
    # Cache current session once per refresh (not per render)
    # Only get current session if actually inside tmux (check $TMUX env var)
    if [ -n "${TMUX:-}" ]; then
        # shellcheck disable=SC2034
        TUI_CURRENT_SESSION="$(tmux display-message -p '#{session_name}' 2>/dev/null || true)"
    else
        # shellcheck disable=SC2034
        TUI_CURRENT_SESSION=""
    fi

    # Clear temp file
    : > "$TUI_TEMP_LIST"
    TUI_ITEM_COUNT=0

    if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        return 0
    fi

    # Read mappings and write to temp file with status
    # Use tab as delimiter (safe - branch/session names can't contain tabs)
    while IFS=' ' read -r branch session ai group _ts _deps _pr; do
        [ -z "$branch" ] && continue

        if tmux_session_exists "$session" 2>/dev/null; then
            sess_status="ALIVE"

            # Activity check — skip capture-pane if cached hash is fresh
            activity="IDLE"
            if [ -n "$TUI_ACTIVITY_DIR" ] && [ -d "$TUI_ACTIVITY_DIR" ]; then
                hash_file="$TUI_ACTIVITY_DIR/${session}.hash"
                time_file="$TUI_ACTIVITY_DIR/${session}.time"
                _activity_interval="${HYDRA_TUI_ACTIVITY_INTERVAL:-3}"
                _now="$(date +%s)"
                _skip_capture=0
                if [ -f "$hash_file" ] && [ -f "$time_file" ]; then
                    _last_time="$(cat "$time_file" 2>/dev/null || echo 0)"
                    if [ "$(( _now - _last_time ))" -lt "$_activity_interval" ]; then
                        _skip_capture=1
                        if [ "$(( _now - _last_time ))" -lt 5 ]; then
                            activity="BUSY"
                        fi
                    fi
                fi
                if [ "$_skip_capture" -eq 0 ]; then
                    current_hash="$(tmux capture-pane -t "$session" -p 2>/dev/null | cksum)"
                    if [ -f "$hash_file" ]; then
                        last_hash="$(cat "$hash_file")"
                        if [ "$current_hash" != "$last_hash" ]; then
                            activity="BUSY"
                            echo "$current_hash" > "$hash_file"
                            echo "$_now" > "$time_file"
                        else
                            if [ -f "$time_file" ]; then
                                last_time="$(cat "$time_file")"
                                idle_secs=$((_now - last_time))
                                if [ "$idle_secs" -lt 5 ]; then
                                    activity="BUSY"
                                fi
                            fi
                        fi
                    else
                        echo "$current_hash" > "$hash_file"
                        echo "$_now" > "$time_file"
                        activity="BUSY"
                    fi
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

        # Apply search pattern filter if set (branch, session, group, ai)
        if [ -n "$TUI_SEARCH_PATTERN" ]; then
            _search_lower="$(printf '%s' "$TUI_SEARCH_PATTERN" | tr '[:upper:]' '[:lower:]')"
            _branch_lower="$(printf '%s' "$branch" | tr '[:upper:]' '[:lower:]')"
            _session_lower="$(printf '%s' "$session" | tr '[:upper:]' '[:lower:]')"
            _group_lower="$(printf '%s' "${group:-}" | tr '[:upper:]' '[:lower:]')"
            _ai_lower="$(printf '%s' "${ai:-}" | tr '[:upper:]' '[:lower:]')"
            _matched=0
            case "$_branch_lower" in *"$_search_lower"*) _matched=1 ;; esac
            if [ "$_matched" -eq 0 ]; then
                case "$_session_lower" in *"$_search_lower"*) _matched=1 ;; esac
            fi
            if [ "$_matched" -eq 0 ] && [ -n "$group" ] && [ "$group" != "-" ]; then
                case "$_group_lower" in *"$_search_lower"*) _matched=1 ;; esac
            fi
            if [ "$_matched" -eq 0 ] && [ -n "$ai" ] && [ "$ai" != "-" ]; then
                case "$_ai_lower" in *"$_search_lower"*) _matched=1 ;; esac
            fi
            if [ "$_matched" -eq 0 ]; then
                continue
            fi
        fi

        # Format: branch<TAB>session<TAB>ai<TAB>status<TAB>tag<TAB>activity<TAB>group<TAB>pr
        printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
            "$branch" "$session" "${ai:--}" "$sess_status" "${tag:--}" "$activity" \
            "${group:--}" "${_pr:--}" >> "$TUI_TEMP_LIST"
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

# Capture preview content for a session
# Usage: tui_capture_preview <session_name>
# Returns: Preview content on stdout, truncated to fit terminal
tui_capture_preview() {
    _session="$1"
    _lines="${TUI_PREVIEW_LINES:-5}"

    if [ -z "$_session" ]; then
        printf "%s(no session)%s\n" "$TUI_DIM" "$TUI_RESET"
        return 0
    fi

    # Check if session exists
    if ! tmux_session_exists "$_session" 2>/dev/null; then
        printf "%s(session not running)%s\n" "$TUI_DIM" "$TUI_RESET"
        return 0
    fi

    # Capture pane content (last N lines)
    _raw_output="$(tmux capture-pane -t "$_session" -p -S -"$_lines" 2>/dev/null || true)"

    if [ -z "$_raw_output" ]; then
        printf "%s(no output)%s\n" "$TUI_DIM" "$TUI_RESET"
        return 0
    fi

    # Truncate each line to terminal width - 2 (for border padding)
    _max_width=$((TUI_COLS - 2))
    printf "%s" "$_raw_output" | while IFS= read -r _line || [ -n "$_line" ]; do
        _len="${#_line}"
        if [ "$_len" -gt "$_max_width" ]; then
            # Truncate and add ellipsis
            printf "%s...\n" "$(printf '%s' "$_line" | cut -c1-$((_max_width - 3)))"
        else
            printf "%s\n" "$_line"
        fi
    done
}

# Render help overlay
# Usage: tui_render_help
# Returns: 0 on success
