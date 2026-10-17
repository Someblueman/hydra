#!/bin/sh
# Workflow recovery's liveness check: a running process is alive, an exited
# one is not, and neither is a zombie nobody has reaped yet. A SIGKILLed
# step worker whose coordinator also died stays a zombie until init reaps it,
# which some container inits do late or never; recovery must not wait on it.

test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"
# shellcheck disable=SC1091
. "$root/lib/workflow_runtime.sh"

base="$(test_mktemp_dir)"
trap 'test_stop_processes_naming "$base"; rm -rf "$base"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

echo "Testing workflow process liveness..."

sleep 30 &
running=$!
workflow_pid_alive "$running"
assert_success $? "a running process is alive"
kill "$running"
wait "$running" 2>/dev/null
workflow_pid_alive "$running"
assert_failure $? "a reaped process is not alive"

# The parent replaces itself with a sleep that never waits, so its exited
# child stays a zombie for as long as the sleep runs.
sh -c 'true & printf "%s\n" "$!" > "$1/zombie"; exec sleep 30' sh "$base" &
parent=$!
zombie=""
tries=0
while [ "$tries" -lt 50 ]; do
    zombie="$(sed -n '1p' "$base/zombie" 2>/dev/null || true)"
    if [ -n "$zombie" ]; then
        case "$(ps -o stat= -p "$zombie" 2>/dev/null)" in *Z*) break ;; esac
    fi
    tries=$((tries + 1))
    sleep 0.1
done
case "$(ps -o stat= -p "$zombie" 2>/dev/null)" in
    *Z*)
        kill -0 "$zombie" 2>/dev/null
        assert_success $? "the fixture zombie still answers kill -0"
        workflow_pid_alive "$zombie"
        assert_failure $? "an unreaped zombie is not alive"
        ;;
    *) echo "SKIP zombie liveness: this platform's ps does not report the zombie state" ;;
esac
kill "$parent" 2>/dev/null
wait "$parent" 2>/dev/null

workflow_pid_alive ""
assert_failure $? "an empty PID is not alive"
workflow_pid_alive "12x"
assert_failure $? "a malformed PID is not alive"

echo ""
echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
