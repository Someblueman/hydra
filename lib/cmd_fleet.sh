#!/bin/sh
# Fleet has one optional C execution path; local head policy stays in the CLI.
cmd_fleet_dispatch() {
    _fd_binary="${HYDRA_FLEET_BIN:-}"
    if [ -z "$_fd_binary" ]; then
        for _fd_candidate in "$HYDRA_BIN_DIR/../build/hydra-fleet" "$HYDRA_BIN_DIR/../libexec/hydra/hydra-fleet"; do
            if [ -x "$_fd_candidate" ]; then _fd_binary="$_fd_candidate"; break; fi
        done
    fi
    if [ -z "$_fd_binary" ] || [ ! -x "$_fd_binary" ]; then
        cli_error fleet missing_dependency 'optional hydra-fleet executable is unavailable' 'run make build-fleet with a C compiler and JSON-C development files'
        return 1
    fi
    exec "$_fd_binary" "$@"
}
