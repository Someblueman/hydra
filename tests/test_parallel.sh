#!/bin/sh
# Hydra 1.7 claims, scopes, collision, resources, and gates integration tests.

test_count=0
pass_count=0
fail_count=0
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
test_root="$(mktemp -d)"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    for _tp_session in parallel-left parallel-right; do
        tmux kill-session -t "$_tp_session" 2>/dev/null || true
    done
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | while IFS= read -r _tp_worktree; do
            [ "$_tp_worktree" = "$repo" ] || git -C "$repo" worktree remove --force "$_tp_worktree" 2>/dev/null || true
        done
    fi
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$repo/src" "$repo/docs"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > src/shared.txt
printf 'docs\n' > docs/readme.md
git add src/shared.txt docs/readme.md
git commit -qm init

echo "Running Hydra 1.7 parallel-safety tests..."
echo "=========================================="

"$HYDRA_BIN" init --no-agent --trust >/dev/null
assert_success $? "project initializes"
"$HYDRA_BIN" spawn parallel-left --no-agent --scope-write 'src/*' --scope-read 'docs/*' --prompt 'Implement the left side' >/dev/null
assert_success $? "scoped left head spawns"
"$HYDRA_BIN" spawn parallel-right --no-agent --scope-write 'src/*' >/dev/null
assert_success $? "scoped right head spawns"

left_worktree="$("$HYDRA_BIN" path parallel-left)"
right_worktree="$("$HYDRA_BIN" path parallel-right)"
project_id="$(sed -n '1p' .git/hydra/project-id)"
left_head_dir="$(find "$HYDRA_HOME/state/v2/projects/$project_id/heads" -type f -name branch -exec sh -c '[ "$(sed -n "1p" "$1")" = parallel-left ] && dirname "$1"' sh {} \;)"
case "$(cat "$left_head_dir/task")" in
    *'Hydra scope (coordination guidance, not a security boundary):'*'hydra scope check parallel-left'*) assert_success 0 "scope instructions are injected into the task" ;;
    *) assert_success 1 "scope instructions are injected into the task" ;;
esac
if grep -q "write.*src/\*" "$left_head_dir/scopes" && grep -q "read.*docs/\*" "$left_head_dir/scopes"; then
    assert_success 0 "read and write scopes are durable"
else
    assert_success 1 "read and write scopes are durable"
fi

printf 'left\n' > "$left_worktree/src/shared.txt"
printf 'changed docs\n' > "$left_worktree/docs/readme.md"
printf 'right\n' > "$right_worktree/src/shared.txt"
scope_output="$("$HYDRA_BIN" scope check parallel-left 2>&1)"
scope_status=$?
assert_failure "$scope_status" "scope check rejects read-only changes"
case "$scope_output" in *'writable'*'src/shared.txt'*) scope_writable=1 ;; *) scope_writable=0 ;; esac
case "$scope_output" in *'read-only'*'docs/readme.md'*'Scope violations: 1'*) scope_readonly=1 ;; *) scope_readonly=0 ;; esac
if [ "$scope_writable" -eq 1 ] && [ "$scope_readonly" -eq 1 ]; then
    assert_success 0 "scope output distinguishes writable and read-only paths"
else
    assert_success 1 "scope output distinguishes writable and read-only paths"
fi

expiry=$(($(date +%s) + 3600))
left_claim_output="$("$HYDRA_BIN" claim add parallel-left --path 'src/*' --access write --reason implementation --expires-at "$expiry")"
assert_success $? "write claim is recorded"
left_claim="$(printf '%s\n' "$left_claim_output" | awk '{print $2}')"
case "$left_claim" in claim_*) assert_success 0 "claim receives an opaque identifier" ;; *) assert_success 1 "claim receives an opaque identifier" ;; esac
right_claim_output="$("$HYDRA_BIN" claim add parallel-right --path 'src/*' --access read --reason review --expires-at "$expiry")"
assert_success $? "read claim is recorded"
right_claim="$(printf '%s\n' "$right_claim_output" | awk '{print $2}')"
collision_output="$("$HYDRA_BIN" collision parallel-left parallel-right)"
case "$collision_output" in *'claim'*'src/*'*'overlap'*'src/shared.txt'*'predicted-conflict'*'src/shared.txt'*) assert_success 0 "collision analysis keeps claim, overlap, and prediction distinct" ;; *) assert_success 1 "collision analysis keeps claim, overlap, and prediction distinct" ;; esac

git -C "$left_worktree" checkout -- docs/readme.md
git -C "$left_worktree" add src/shared.txt
git -C "$left_worktree" commit -qm left-change
git -C "$right_worktree" add src/shared.txt
git -C "$right_worktree" commit -qm right-change
collision_output="$("$HYDRA_BIN" collision parallel-left parallel-right)"
case "$collision_output" in *'observed-conflict'*'src/shared.txt'*) assert_success 0 "actual merge simulation reports an observed conflict" ;; *) assert_success 1 "actual merge simulation reports an observed conflict" ;; esac

"$HYDRA_BIN" resource allocate parallel-left --port http=41000-41001 --compose-project hydra-left --database db-left >/dev/null &
left_resource_pid=$!
"$HYDRA_BIN" resource allocate parallel-right --port http=41000-41001 --compose-project hydra-right --database db-right >/dev/null &
right_resource_pid=$!
wait "$left_resource_pid"
left_resource_status=$?
wait "$right_resource_pid"
right_resource_status=$?
assert_success "$left_resource_status" "left resource profile allocates under contention"
assert_success "$right_resource_status" "right resource profile allocates under contention"
left_port="$("$HYDRA_BIN" resource status parallel-left | awk -F '\t' '$1 == "port" { print $3 }')"
right_port="$("$HYDRA_BIN" resource status parallel-right | awk -F '\t' '$1 == "port" { print $3 }')"
if [ -n "$left_port" ] && [ -n "$right_port" ] && [ "$left_port" != "$right_port" ]; then
    assert_success 0 "concurrent resource allocations are unique"
else
    assert_success 1 "concurrent resource allocations are unique"
fi
resource_env="$("$HYDRA_BIN" resource env parallel-left)"
case "$resource_env" in *"HYDRA_PORT_HTTP=$left_port"*'COMPOSE_PROJECT_NAME=hydra-left'*'HYDRA_DATABASE=db-left'*) assert_success 0 "resource profile exports ports, compose, and database values" ;; *) assert_success 1 "resource profile exports ports, compose, and database values" ;; esac

"$HYDRA_BIN" gate run parallel-left --name tests -- sh -c 'printf gate-ok' >/dev/null
assert_success $? "verification gate captures a passing command"
gate_status="$("$HYDRA_BIN" gate status parallel-left)"
case "$gate_status" in *'tests'*'0'*) assert_success 0 "gate status records passing evidence" ;; *) assert_success 1 "gate status records passing evidence" ;; esac
"$HYDRA_BIN" gate approve parallel-left --name tests --by human-reviewer --reason reviewed >/dev/null
assert_success $? "passing gate receives explicit approval"
approved_status="$("$HYDRA_BIN" gate status parallel-left)"
case "$approved_status" in *'human-reviewer'*) assert_success 0 "approval identity is durable" ;; *) assert_success 1 "approval identity is durable" ;; esac
"$HYDRA_BIN" gate run parallel-left --name failing -- sh -c 'exit 7' >/dev/null 2>&1
assert_failure $? "failing verification gate returns its failure"
"$HYDRA_BIN" gate approve parallel-left --name failing --by human-reviewer >/dev/null 2>&1
assert_failure $? "failed gate cannot be approved"

"$HYDRA_BIN" claim remove "$right_claim" >/dev/null
assert_success $? "claim removal is explicit"

# Restore the throwaway worktrees so normal teardown can prove safe cleanup.
"$HYDRA_BIN" kill parallel-left >/dev/null
assert_success $? "left head teardown succeeds"
if [ ! -d "$HYDRA_HOME/state/v2/projects/$project_id/resources/$(basename "$left_head_dir")" ] && \
   [ -z "$(grep -R -l -F "$(basename "$left_head_dir")" "$HYDRA_HOME/state/v2/projects/$project_id/claims" 2>/dev/null || true)" ]; then
    assert_success 0 "teardown releases owned resources and claims"
else
    assert_success 1 "teardown releases owned resources and claims"
fi
"$HYDRA_BIN" kill parallel-right >/dev/null
assert_success $? "right head teardown succeeds"

echo "=========================================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

[ "$fail_count" -eq 0 ]
