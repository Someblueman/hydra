#!/bin/sh
# Isolate each shell case's files and tmux servers, including failed cases.
# Usage: BUILD_DIR=<dir> run-shell-test.sh <log-dir> <name> <test-file>
set -eu
# Tests locate native helpers from BUILD_DIR only; never fall back to ./build,
# which may hold a stale binary from another configuration.
: "${BUILD_DIR:?BUILD_DIR is required; run make test, make test-one T=<name>, or set BUILD_DIR}"
BUILD_DIR=$(CDPATH='' cd -- "$BUILD_DIR" && pwd) || {
    printf 'run-shell-test: BUILD_DIR does not exist: %s\n' "$BUILD_DIR" >&2
    exit 2
}
HYDRA_FLEET_BIN="$BUILD_DIR/hydra-fleet"
HYDRA_TUI_BIN="$BUILD_DIR/hydra-tui"
HYDRA_CORE="$BUILD_DIR/hydra-core"
HYDRA_SHELL_EXEC="$BUILD_DIR/test-shell-exec"
export BUILD_DIR HYDRA_FLEET_BIN HYDRA_TUI_BIN HYDRA_CORE HYDRA_SHELL_EXEC
for case_binary in "$HYDRA_SHELL_EXEC" "$HYDRA_FLEET_BIN" "$HYDRA_TUI_BIN"; do
    [ -x "$case_binary" ] && continue
    printf 'run-shell-test: required test binary is missing: %s (build it with make BUILD_DIR=%s test)\n' \
        "$case_binary" "$BUILD_DIR" >&2
    exit 2
done
case_root=$(mktemp -d /tmp/hydra-sh.XXXXXX)
case_runner=
# Private whatever the caller's umask: under 002 a group-writable TMPDIR
# ancestor makes Hydra's ownership checks refuse fixture stores beneath it.
(umask 077 && mkdir -p "$case_root/tmp" "$case_root/tmux")
TMPDIR="$case_root/tmp"
TMUX_TMPDIR="$case_root/tmux"
export TMPDIR TMUX_TMPDIR
# A case started from inside tmux must not see (or act on) the caller's server.
unset TMUX TMUX_PANE

# Stops an interrupted runner while its PID is still unreaped, and processes
# a finished case left behind.
# shellcheck disable=SC2329,SC2317 # Called by the EXIT trap through cleanup.
stop_case_tree() (
    case_parent=$1
    for case_child in $(ps -eo pid=,ppid= | awk -v parent="$case_parent" '$2 == parent { print $1 }'); do
        stop_case_tree "$case_child"
    done
    kill -KILL "$case_parent" 2>/dev/null || true
)

stop_case_servers() {
    for case_socket in "$TMUX_TMPDIR"/tmux-*/*; do
        [ -S "$case_socket" ] || continue
        tmux -S "$case_socket" kill-server 2>/dev/null || true
    done
}

# Processes that name this case's private directory in their arguments: its
# fixtures live under TMPDIR, so anything the case started and left running
# (a background CLI owner, worker, or watchdog) almost always carries it. The
# tag travels in the environment so ps and awk never match themselves.
case_tag=${case_root##*/}
case_processes() {
    ps -eo pid=,args= 2>/dev/null | CASE_TAG=$case_tag awk 'index($0, ENVIRON["CASE_TAG"])'
}

# A finished case must not leave processes behind. Allow a short grace period
# for processes that are already exiting, then report, stop, and fail.
check_case_processes() {
    case_polls=0
    while case_leaked=$(case_processes) && [ -n "$case_leaked" ] && [ "$case_polls" -lt 50 ]; do
        sleep 0.1
        case_polls=$((case_polls + 1))
    done
    [ -n "$case_leaked" ] || return 0
    {
        printf 'LEAK %s: the case left processes running under %s:\n' "$2" "$case_root"
        printf '%s\n' "$case_leaked"
    } | tee -a "$1/$2.log" >&2
    for case_pid in $(printf '%s\n' "$case_leaked" | awk '{ print $1 }'); do
        stop_case_tree "$case_pid"
    done
    return 1
}

# shellcheck disable=SC2329,SC2317 # Invoked by the EXIT trap.
cleanup() {
    if [ -n "$case_runner" ]; then
        stop_case_tree "$case_runner"
        wait "$case_runner" 2>/dev/null || true
    fi
    stop_case_servers
    for case_pid in $(case_processes | awk '{ print $1 }'); do
        stop_case_tree "$case_pid"
    done
    rm -rf "$case_root"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
sh "$(dirname "$0")/run-test.sh" "$1" "$2" "$HYDRA_SHELL_EXEC" sh "$3" &
case_runner=$!
case_status=0
wait "$case_runner" || case_status=$?
case_runner=
stop_case_servers
if ! check_case_processes "$1" "$2" && [ "$case_status" -eq 0 ]; then
    case_status=1
fi
exit "$case_status"
