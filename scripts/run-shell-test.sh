#!/bin/sh
# Isolate each shell case's files and tmux servers, including failed cases.
set -eu
case_root=$(mktemp -d /tmp/hydra-sh.XXXXXX)
case_runner=
mkdir -p "$case_root/tmp" "$case_root/tmux"
TMPDIR="$case_root/tmp"
TMUX_TMPDIR="$case_root/tmux"
export TMPDIR TMUX_TMPDIR

# Only used on interruption, while the owned runner PID is still unreaped.
# shellcheck disable=SC2329 # Called by the EXIT trap through cleanup.
stop_case_tree() (
    case_parent=$1
    for case_child in $(ps -eo pid=,ppid= | awk -v parent="$case_parent" '$2 == parent { print $1 }'); do
        stop_case_tree "$case_child"
    done
    kill -KILL "$case_parent" 2>/dev/null || true
)

# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() {
    if [ -n "$case_runner" ]; then
        stop_case_tree "$case_runner"
        wait "$case_runner" 2>/dev/null || true
    fi
    for case_socket in "$TMUX_TMPDIR"/tmux-*/*; do
        [ -S "$case_socket" ] || continue
        tmux -S "$case_socket" kill-server 2>/dev/null || true
    done
    rm -rf "$case_root"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
sh "$(dirname "$0")/run-test.sh" "$1" "$2" "${HYDRA_SHELL_EXEC:?test-shell-exec is required}" sh "$3" &
case_runner=$!
case_status=0
wait "$case_runner" || case_status=$?
case_runner=
exit "$case_status"
