#!/bin/sh
# Bounded execution, Git evidence, and provenance integration tests.

test_count=0
pass_count=0
fail_count=0
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
test_root="$(mktemp -d)"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    tmux kill-session -t operations-test 2>/dev/null || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > tracked.txt
git add tracked.txt
git commit -qm init

echo "Running operations integration tests..."
echo "======================================="

"$HYDRA_BIN" init --no-agent --trust >/dev/null
assert_success $? "project initializes with explicit trust"
"$HYDRA_BIN" spawn operations-test --no-agent --group release --prompt "Secret task text stays out of provenance" >/dev/null
assert_success $? "task-aware head spawns for operations"

project_id="$(sed -n '1p' .git/hydra/project-id)"
head_dir="$(find "$HYDRA_HOME/state/v2/projects/$project_id/heads" -type f -name branch -exec sh -c '[ "$(sed -n "1p" "$1")" = operations-test ] && dirname "$1"' sh {} \;)"
instance_id="$(sed -n '1p' "$head_dir/current-instance")"
worktree="$("$HYDRA_BIN" path operations-test)"
provenance_dir="$head_dir/provenance"

if test -s "$provenance_dir/task-hash" && test "$(sed -n '1p' "$provenance_dir/task-bytes")" -gt 0; then
    assert_success 0 "spawn records task hash and byte count provenance"
else
    assert_success 1 "spawn records task hash and byte count provenance"
fi
if grep -R "Secret task text" "$provenance_dir" >/dev/null 2>&1; then
    assert_failure 0 "provenance excludes task content"
else
    assert_success 0 "provenance excludes task content"
fi
assert_equal none "$(sed -n '1p' "$head_dir/instances/$instance_id/resolved-profile")" "instance provenance records resolved profile"
provenance_json="$("$HYDRA_BIN" provenance operations-test --json)"
case "$provenance_json" in *'"schema_version":1'*'"command":"provenance"'*) assert_success 0 "provenance has a versioned JSON view" ;; *) assert_success 1 "provenance has a versioned JSON view" ;; esac

printf 'changed\n' >> "$worktree/tracked.txt"
diff_names="$("$HYDRA_BIN" diff operations-test --name-only)"
assert_equal tracked.txt "$diff_names" "diff uses the recorded base reference"
review_json="$("$HYDRA_BIN" review operations-test --json)"
case "$review_json" in *'"dirty_paths":1'*'"changed_files":1'*'"diff_check_passed":true'*) assert_success 0 "review reports Git porcelain evidence" ;; *) assert_success 1 "review reports Git porcelain evidence" ;; esac
list_git_json="$("$HYDRA_BIN" list --git --json)"
case "$list_git_json" in *'"git": {'*'"dirty_paths":1'*) assert_success 0 "list --git reports recorded-base evidence" ;; *) assert_success 1 "list --git reports recorded-base evidence" ;; esac

declared_before="$(sed -n '1p' "$head_dir/instances/$instance_id/declared-outcome" 2>/dev/null || true)"
observed_before="$(sed -n '1p' "$head_dir/instances/$instance_id/observed-status" 2>/dev/null || true)"
exec_json="$("$HYDRA_BIN" exec --branch operations-test --jobs 1 --timeout 5 --json -- printf '%s' 'exec-ok')"
assert_success $? "argv-safe out-of-band exec succeeds"
case "$exec_json" in *'"command":"exec"'*'"exit_code":0'*'"stdout":"exec-ok"'*) assert_success 0 "exec captures a versioned result" ;; *) assert_success 1 "exec captures a versioned result" ;; esac
grep -q '"type":"exec.completed"' "$head_dir/events/events.jsonl"
assert_success $? "exec records a distinct non-steering event"
declared_after="$(sed -n '1p' "$head_dir/instances/$instance_id/declared-outcome" 2>/dev/null || true)"
observed_after="$(sed -n '1p' "$head_dir/instances/$instance_id/observed-status" 2>/dev/null || true)"
if [ "$declared_before" = "$declared_after" ] && [ "$observed_before" = "$observed_after" ]; then
    assert_success 0 "exec does not mutate agent lifecycle channels"
else
    assert_success 1 "exec does not mutate agent lifecycle channels"
fi

group_json="$("$HYDRA_BIN" exec --group release --jobs 2 --timeout 5 --json -- printf '%s' group-ok)"
case "$group_json" in *'"branch":"operations-test"'*'"stdout":"group-ok"'*) assert_success 0 "exec selects a named group" ;; *) assert_success 1 "exec selects a named group" ;; esac
"$HYDRA_BIN" group operations-test verification >/dev/null
assert_success $? "public group mutation succeeds"
assert_equal verification "$(sed -n '1p' "$head_dir/group")" "group mutation updates authoritative state v2"
mutated_group_json="$("$HYDRA_BIN" exec --group verification --json -- printf '%s' regrouped)"
case "$mutated_group_json" in *'"branch":"operations-test"'*'"stdout":"regrouped"'*) assert_success 0 "exec observes a mutated authoritative group" ;; *) assert_success 1 "exec observes a mutated authoritative group" ;; esac
"$HYDRA_BIN" group operations-test --clear >/dev/null
assert_success $? "public group clear succeeds"
assert_equal - "$(sed -n '1p' "$head_dir/group")" "group clear updates authoritative state v2"

"$HYDRA_BIN" exec --branch operations-test --all -- true >/dev/null 2>&1
assert_failure $? "exec rejects ambiguous selection modes"
"$HYDRA_BIN" exec --branch operations-test --shell 'printf unsafe' >/dev/null 2>&1
assert_failure $? "shell-string exec requires explicit acknowledgement"
shell_json="$("$HYDRA_BIN" exec --branch operations-test --shell 'printf shell-ok' --allow-shell --json)"
assert_success $? "trusted acknowledged shell-string exec succeeds"
case "$shell_json" in *'"stdout":"shell-ok"'*) assert_success 0 "shell-string output is captured" ;; *) assert_success 1 "shell-string output is captured" ;; esac

child_pid_file="$test_root/timeout-child.pid"
# shellcheck disable=SC2016 # $! and $1 are intentionally expanded by the child shell.
timeout_json="$("$HYDRA_BIN" exec --branch operations-test --timeout 1 --json -- sh -c 'sleep 20 & echo $! > "$1"; wait' sh "$child_pid_file" 2>/dev/null)"
timeout_code=$?
assert_failure "$timeout_code" "timed-out exec returns failure"
case "$timeout_json" in *'"exit_code":124'*) assert_success 0 "timed-out exec records status 124" ;; *) assert_success 1 "timed-out exec records status 124" ;; esac
child_pid="$(sed -n '1p' "$child_pid_file")"
if kill -0 "$child_pid" 2>/dev/null; then
    assert_success 1 "timeout terminates command descendants"
else
    assert_success 0 "timeout terminates command descendants"
fi

run_dir="$(find "$HYDRA_HOME/state/v2/projects/$project_id/exec" -type f -name stdout -print | head -1 | xargs dirname)"
case "$(uname -s)" in
    Darwin) output_mode="$(stat -f '%Lp' "$run_dir/stdout")" ;;
    *) output_mode="$(stat -c '%a' "$run_dir/stdout")" ;;
esac
assert_equal 600 "$output_mode" "captured exec output is private"

git -C "$worktree" restore -- tracked.txt
"$HYDRA_BIN" kill operations-test >/dev/null
assert_success $? "operations head tears down"

echo "======================================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
