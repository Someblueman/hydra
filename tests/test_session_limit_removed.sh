#!/bin/sh
# HYDRA_MAX_SESSIONS and `hydra queue` were removed (admission is the only
# spawn limit). Old usage must fail or warn visibly, and a queue left on disk
# by an older release must be ignored, never an error.

test_count=0
pass_count=0
fail_count=0

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO_ROOT/bin/hydra"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_LOCK_RETRIES=1
unset HYDRA_NO_SWITCH TMUX HYDRA_MAX_SESSIONS

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

assert_contains() {
    test_count=$((test_count + 1))
    if printf '%s\n' "$1" | grep -Fq -- "$2"; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $3"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $3"
        echo "  Expected to contain: '$2'"
        echo "  Actual: $1"
    fi
}

assert_not_contains() {
    test_count=$((test_count + 1))
    if printf '%s\n' "$1" | grep -Fq -- "$2"; then
        fail_count=$((fail_count + 1))
        echo "[FAIL] $3"
        echo "  Expected not to contain: '$2'"
        echo "  Actual: $1"
    else
        pass_count=$((pass_count + 1))
        echo "[PASS] $3"
    fi
}

test_base_dir="$(test_mktemp_dir)"
# shellcheck disable=SC2329,SC2317  # invoked by the EXIT trap
cleanup() {
    "$HYDRA_BIN" kill --all --force >/dev/null 2>&1 || true
    rm -rf "$test_base_dir"
}
trap cleanup EXIT
mkdir -p "$test_base_dir/repo"
cd "$test_base_dir/repo" || exit 1
export HYDRA_HOME="$test_base_dir/.hydra"
git init >/dev/null 2>&1
git config user.name "Test User"
git config user.email "test@example.com"
echo "# Test" > README.md
git add README.md
git commit -m init >/dev/null 2>&1
"$HYDRA_BIN" init --no-agent --trust >/dev/null

echo "Running removed session limit tests..."
echo "================================"

echo "Testing hydra queue is an unknown command..."
output="$("$HYDRA_BIN" queue 2>&1)"
assert_equal "1" "$?" "hydra queue exits 1"
assert_equal "Error: Unknown command 'queue'
Run 'hydra help' for usage information" "$output" "hydra queue prints the unknown-command message"
for queue_args in "process" "clear" "remove feature" "--json" "-h"; do
    # shellcheck disable=SC2086  # word splitting of the fixed argument list is intended
    output="$("$HYDRA_BIN" queue $queue_args 2>&1)"
    assert_equal "1" "$?" "hydra queue $queue_args exits 1"
    assert_contains "$output" "Unknown command 'queue'" "hydra queue $queue_args is unknown too"
done

echo "Testing help no longer documents the removed feature..."
output="$("$HYDRA_BIN" help 2>&1)"
assert_not_contains "$output" "HYDRA_MAX_SESSIONS" "help omits HYDRA_MAX_SESSIONS"
assert_not_contains "$output" "queue clear" "help omits the queue subcommands"
assert_contains "$output" "HYDRA_ADMISSION_QUEUE_SECONDS" "help keeps the admission wait setting"

echo "Testing HYDRA_MAX_SESSIONS warns on spawn and limits nothing..."
output="$(HYDRA_MAX_SESSIONS=1 "$HYDRA_BIN" spawn limit-a --no-agent 2>&1)"
assert_success $? "spawn with HYDRA_MAX_SESSIONS=1 succeeds"
assert_contains "$output" "Warning: HYDRA_MAX_SESSIONS is no longer supported and is ignored" "spawn warns that the variable is ignored"
assert_contains "$output" "hydra admission configure" "the warning names the replacement"
assert_equal "1" "$(printf '%s\n' "$output" | grep -c 'HYDRA_MAX_SESSIONS')" "the warning is printed once"
output="$(HYDRA_MAX_SESSIONS=1 "$HYDRA_BIN" spawn limit-b --no-agent 2>&1)"
assert_success $? "a second spawn is not limited by HYDRA_MAX_SESSIONS=1"
output="$(HYDRA_MAX_SESSIONS=0 "$HYDRA_BIN" spawn limit-c --no-agent 2>&1)"
assert_success $? "spawn with HYDRA_MAX_SESSIONS=0 succeeds"
assert_not_contains "$output" "HYDRA_MAX_SESSIONS" "the old 'unlimited' value 0 does not warn"
output="$("$HYDRA_BIN" spawn limit-d --no-agent 2>&1)"
assert_not_contains "$output" "HYDRA_MAX_SESSIONS" "no warning without the variable"
output="$(HYDRA_MAX_SESSIONS=1 "$HYDRA_BIN" list 2>&1)"
assert_success $? "list works with HYDRA_MAX_SESSIONS set"
assert_not_contains "$output" "HYDRA_MAX_SESSIONS" "list stays silent about it"
output="$(HYDRA_MAX_SESSIONS=1 "$HYDRA_BIN" status 2>&1)"
assert_success $? "status works with HYDRA_MAX_SESSIONS set"
assert_not_contains "$output" "HYDRA_MAX_SESSIONS" "status stays silent about it"
output="$(HYDRA_MAX_SESSIONS=1 "$HYDRA_BIN" spawn limit-e --no-agent --dry-run 2>&1)"
assert_not_contains "$output" "HYDRA_MAX_SESSIONS" "dry-run stays silent about it"

echo "Testing a leftover queue from an older release is ignored..."
project_dir="$(find "$HYDRA_HOME/state/v2/projects" -mindepth 1 -maxdepth 1 -type d | sed -n '1p')"
assert_contains "$project_dir" "/projects/project_" "found the project state directory"
mkdir -p "$project_dir/queue"
printf 'branch=queued-old\nai_tool=none\ngroup=\nlayout=default\npriority=50\nrequested_at=1\n' > "$project_dir/queue/949_1_000001_queued-old.queue"
printf 'not a queue entry\n' > "$project_dir/queue/950_2_000002_garbage.queue"
printf '2\n' > "$project_dir/queue/.seq"
chmod 700 "$project_dir/queue"

output="$("$HYDRA_BIN" spawn after-queue --no-agent 2>&1)"
assert_success $? "spawn succeeds beside a leftover queue"
output="$("$HYDRA_BIN" list 2>&1)"
assert_success $? "list succeeds beside a leftover queue"
assert_contains "$output" "after-queue" "list shows the new head"
assert_not_contains "$output" "queued-old" "list does not show the queued branch"
output="$("$HYDRA_BIN" status 2>&1)"
assert_success $? "status succeeds beside a leftover queue"
output="$("$HYDRA_BIN" kill limit-a --force 2>&1)"
assert_success $? "kill succeeds beside a leftover queue"
assert_not_contains "$output" "Processing queued spawn" "kill no longer drains the queue"
output="$("$HYDRA_BIN" kill --all --force 2>&1)"
assert_success $? "kill --all succeeds beside a leftover queue"
assert_not_contains "$output" "Processing queued spawn" "kill --all no longer drains the queue"
output="$("$HYDRA_BIN" path queued-old 2>&1)"
assert_failure $? "the queued branch was never spawned"
assert_equal "1" "$(find "$project_dir/queue" -name "*queued-old.queue" 2>/dev/null | wc -l | tr -d ' ')" "the leftover entry is left untouched on disk"

echo "================================"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
fi
echo "Some tests failed!"
exit 1
