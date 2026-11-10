#!/bin/sh
# 'hydra tui': option dispatch for the native control centre and the private
# data, output and attach entry points it calls back into.
# POSIX-compliant shell script

# Entry point for TUI command
# Usage: cmd_tui [option ...]
# Returns: the native control centre's status, 1 when it is unavailable,
#          2 on a usage error
cmd_tui() {
    case "${1:-}" in
        --attach)
            shift
            _load_lib tui_attach
            tui_attach_instance "$@"
            return $?
            ;;
        --basic)
            echo "Error: the basic shell TUI was removed; 'hydra tui' opens the native control centre" >&2
            echo "Next: run 'hydra tui', or use 'hydra list' and 'hydra switch' without the native helper" >&2
            return 2
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
        --orphan-rows)
            shift
            [ $# -eq 0 ] || return 2
            tui_native_emit_orphans
            return $?
            ;;
        --head-output)
            shift
            [ $# -eq 1 ] || return 2
            _load_lib cmd_fleet
            _load_lib tui_head_output
            tui_head_output "$1"
            return $?
            ;;
        --headless-fixture)
            shift
            tui_native_exec --headless-fixture "$@"
            return $?
            ;;
        --print-keys)
            shift
            [ $# -eq 0 ] || return 2
            tui_native_exec --print-keys
            return $?
            ;;
        '')
            tui_native_run
            return $?
            ;;
        --task|--view|--theme|--ascii|--no-color)
            tui_native_run "$@"
            return $?
            ;;
        *)
            echo "Error: unknown TUI option '$1'" >&2
            echo "Usage: hydra tui [--capabilities [--json]|--task BRANCH|--view NAME|--theme NAME|--ascii|--no-color|--print-keys|--headless-fixture FILE ...]" >&2
            return 2
            ;;
    esac
}
