#!/bin/sh

# hydra snapshot: the native helper when one qualifies, else the shell
# implementation, with the same output either way. --native also names on
# stderr why the helper was not used; --shell never runs the helper.
cmd_snapshot() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra snapshot [--native|--shell] [--json]'; return 0 ;; esac
    _cs_mode=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --native|--shell)
                if [ -n "$_cs_mode" ] && [ "$_cs_mode" != "$1" ]; then
                    cli_error snapshot invalid-arguments "--native and --shell cannot be combined" "run: hydra snapshot [--native|--shell]"
                    return 1
                fi
                _cs_mode="$1"
                ;;
            --json|-j) : ;;
            *)
                cli_error snapshot invalid-arguments "unknown option: $1" "run: hydra snapshot [--native|--shell]"
                return 1
                ;;
        esac
        shift
    done
    case "$_cs_mode" in
        --shell) LC_ALL=C hydra_snapshot_shell ;;
        --native) hydra_snapshot explain ;;
        *) hydra_snapshot ;;
    esac
}
