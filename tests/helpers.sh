#!/bin/sh
# Common test helper functions for Hydra tests (POSIX)

# Functions rely on global counters:
# - test_count, pass_count, fail_count

assert_equal() {
    expected="$1"
    actual="$2"
    message="$3"

    test_count=$((test_count + 1))
    if [ "${expected}" = "${actual}" ]; then
        pass_count=$((pass_count + 1))
        echo "[PASS] ${message}"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] ${message}"
        echo "  Expected: '${expected}'"
        echo "  Actual:   '${actual}'"
    fi
}

assert_success() {
    exit_code="$1"
    message="$2"

    test_count=$((test_count + 1))
    if [ "${exit_code}" -eq 0 ]; then
        pass_count=$((pass_count + 1))
        echo "[PASS] ${message}"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] ${message}"
        echo "  Expected: success (exit code 0)"
        echo "  Actual:   failure (exit code ${exit_code})"
    fi
}

assert_failure() {
    exit_code="$1"
    message="$2"

    test_count=$((test_count + 1))
    if [ "${exit_code}" -ne 0 ]; then
        pass_count=$((pass_count + 1))
        echo "[PASS] ${message}"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] ${message}"
        echo "  Expected: failure (non-zero exit code)"
        echo "  Actual:   success (exit code 0)"
    fi
}

# Private temporary paths. A bare `mktemp -d` ignores TMPDIR on macOS; the test
# runner points TMPDIR at a per-case directory that it removes after the case,
# including after a failure.
test_mktemp_dir() {
    _tmd_base="${TMPDIR:-/tmp}"
    mktemp -d "${_tmd_base%/}/hydra-test.XXXXXX"
}

test_mktemp_file() {
    _tmf_base="${TMPDIR:-/tmp}"
    mktemp "${_tmf_base%/}/hydra-test.XXXXXX"
}

# Poll until a command succeeds instead of sleeping for a fixed time.
# Usage: wait_for <description> <command> [args...]
# WAIT_FOR_TIMEOUT (seconds, default 30) and WAIT_FOR_INTERVAL (seconds,
# default 0.1) tune the loop. The command's last output is kept in
# WAIT_FOR_OUTPUT and printed on timeout so a failure shows what was observed.
wait_for() {
    _wf_description="$1"
    shift
    _wf_timeout="${WAIT_FOR_TIMEOUT:-30}"
    _wf_deadline=$(($(date +%s) + _wf_timeout))
    while :; do
        if WAIT_FOR_OUTPUT="$("$@" 2>&1)"; then
            return 0
        fi
        if [ "$(date +%s)" -ge "$_wf_deadline" ]; then
            printf 'wait_for: timed out after %ss waiting for %s\n' "$_wf_timeout" "$_wf_description" >&2
            printf 'wait_for: last output of [%s]:\n%s\n' "$*" "$WAIT_FOR_OUTPUT" >&2
            return 1
        fi
        sleep "${WAIT_FOR_INTERVAL:-0.1}"
    done
}

# PIDs of processes whose arguments name a fixture path. The path travels in
# the environment so neither ps nor awk lists it in its own arguments.
test_processes_naming() {
    ps -eo pid=,args= 2>/dev/null | TEST_PROCESS_PATH="$1" awk 'index($0, ENVIRON["TEST_PROCESS_PATH"]) { print $1 }'
}

# Signal a process and its descendants, deepest first (default KILL).
test_signal_tree() (
    _tst_pid="$1"
    _tst_signal="${2:-KILL}"
    for _tst_child in $(ps -eo pid=,ppid= 2>/dev/null | awk -v parent="$_tst_pid" '$2 == parent { print $1 }'); do
        test_signal_tree "$_tst_child" "$_tst_signal"
    done
    kill "-$_tst_signal" "$_tst_pid" 2>/dev/null || true
)

# Cleanup for every exit path: stop whatever a fixture started (background
# CLI owners, exec workers, watchdogs and commands), descendants first.
test_stop_processes_naming() {
    for _tsp_pid in $(test_processes_naming "$1"); do
        test_signal_tree "$_tsp_pid" KILL
    done
}
