#!/bin/sh
# Hydra TUI module
# POSIX-compliant shell script
# shellcheck disable=SC2034

tui_main_loop() {
    loop_count=0
    # Default ~3s refresh; HYDRA_TUI_REFRESH_MS in milliseconds
    _refresh_ms="${HYDRA_TUI_REFRESH_MS:-3000}"
    case "$_refresh_ms" in
        *[!0-9]*) refresh_every=30 ;;
        0) refresh_every=0 ;;
        *) refresh_every=$((_refresh_ms / 100)) ;;
    esac
    if [ "$refresh_every" -lt 1 ]; then
        refresh_every=1
    fi

    while [ "$TUI_RUNNING" -eq 1 ]; do
        # Terminal resize detection
        _cur_cols="$(tput cols 2>/dev/null || echo "$TUI_COLS")"
        _cur_rows="$(tput lines 2>/dev/null || echo "$TUI_ROWS")"
        if [ "$_cur_cols" != "$TUI_LAST_COLS" ] || [ "$_cur_rows" != "$TUI_LAST_ROWS" ]; then
            tui_update_size
            TUI_LAST_COLS="$TUI_COLS"
            TUI_LAST_ROWS="$TUI_ROWS"
            TUI_NEEDS_REDRAW=1
        fi

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

        # Periodic refresh (or follow mode for preview)
        _do_refresh=0
        if [ "$loop_count" -ge "$refresh_every" ] || [ "$loop_count" -eq 0 ]; then
            _do_refresh=1
        fi
        if [ "$TUI_PREVIEW_FOLLOW" -eq 1 ] && [ "$TUI_PREVIEW_VISIBLE" -eq 1 ]; then
            _do_refresh=1
        fi
        if [ "$_do_refresh" -eq 1 ]; then
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
    case "${1:-}" in
        --basic)
            shift
            [ $# -eq 0 ] || {
                echo "Error: hydra tui --basic does not accept additional arguments" >&2
                return 2
            }
            ;;
        --native)
            shift
            tui_native_run "$@"
            return $?
            ;;
        --capabilities)
            shift
            if [ "${1:-}" = --json ]; then
                shift
                [ $# -eq 0 ] || return 2
                tui_native_capabilities 1
            else
                [ $# -eq 0 ] || return 2
                tui_native_capabilities 0
            fi
            return $?
            ;;
        --data)
            shift
            [ $# -eq 0 ] || return 2
            tui_native_emit_data
            return $?
            ;;
        --headless-fixture)
            shift
            tui_native_exec --headless-fixture "$@"
            return $?
            ;;
        '') ;;
        *)
            echo "Error: unknown TUI option '$1'" >&2
            echo "Usage: hydra tui [--basic|--native|--capabilities [--json]|--headless-fixture FILE ...]" >&2
            return 2
            ;;
    esac

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
    if [ -n "${HYDRA_TUI_PREVIEW_LINES:-}" ]; then
        case "${HYDRA_TUI_PREVIEW_LINES}" in
            *[!0-9]*|'')
                ;;
            *)
                TUI_PREVIEW_LINES="${HYDRA_TUI_PREVIEW_LINES}"
                ;;
        esac
    fi

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
