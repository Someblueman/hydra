#!/bin/sh

cmd_snapshot() {
    case "${1:-}" in -h|--help) printf '%s\n' 'Usage: hydra snapshot [--native] [--json]'; return 0 ;; esac
    _cs_native=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --native) _cs_native=1 ;;
            --json|-j) : ;;
            *)
                cli_error snapshot invalid-arguments "unknown option: $1" "run: hydra snapshot [--native]"
                return 1
                ;;
        esac
        shift
    done
    if [ "$_cs_native" -eq 1 ]; then
        hydra_snapshot_native
    else
        LC_ALL=C hydra_snapshot_shell
    fi
}
