#!/bin/sh
# Guarded merge-train cancellation, recovery, failure reporting, and promotion.

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
    for _tmt_session in train-one train-two fail-one fail-two; do
        tmux kill-session -t "$_tmt_session" 2>/dev/null || true
    done
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | while IFS= read -r _tmt_worktree; do
            [ "$_tmt_worktree" = "$repo" ] || git -C "$repo" worktree remove --force "$_tmt_worktree" 2>/dev/null || true
        done
    fi
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

wait_for_file() {
    _tmt_file="$1"
    _tmt_wait=0
    while [ ! -f "$_tmt_file" ] && [ "$_tmt_wait" -lt 100 ]; do
        sleep 0.1
        _tmt_wait=$((_tmt_wait + 1))
    done
    [ -f "$_tmt_file" ]
}

integration_dir() {
    find "$HYDRA_HOME/state/v2/projects" -type d -path "*/integrations/$1" -print | sed -n '1p'
}

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > base.txt
git add base.txt
git commit -qm base
git branch -M main
"$HYDRA_BIN" init --no-agent --trust >/dev/null
git add .hydra/config.yml
git commit -qm config
base_commit="$(git rev-parse main)"

for _tmt_head in train-one train-two; do
    "$HYDRA_BIN" spawn "$_tmt_head" --group train-success --no-agent >/dev/null || exit 1
done
one="$($HYDRA_BIN path train-one)"
two="$($HYDRA_BIN path train-two)"
printf 'one\n' > "$one/one.txt"
git -C "$one" add one.txt
git -C "$one" commit -qm one
printf 'two\n' > "$two/two.txt"
git -C "$two" add two.txt
git -C "$two" commit -qm two
"$HYDRA_BIN" outcome train-one "done" --actor human >/dev/null
"$HYDRA_BIN" outcome train-two "done" --actor human >/dev/null

preview="$($HYDRA_BIN integrate train train-success --base "$base_commit" --target main --dry-run --gate true)"
printf '%s\n' "$preview" | grep -q 'mode: train' &&
    printf '%s\n' "$preview" | grep -q 'candidate gate: true'
assert_success $? "train dry-run previews per-candidate gates without mutation"

gate="$test_root/pause-gate.sh"
apply_gate="$gate $test_root/pause-started $test_root/pause-release"
# The quoted fixture lines are shell source, not expressions in this test shell.
# shellcheck disable=SC2016
printf '%s\n' '#!/bin/sh' 'if [ -f two.txt ]; then' '  : > "$1"' '  trap "exit 143" HUP INT TERM' '  while [ ! -f "$2" ]; do sleep 0.1; done' 'fi' > "$gate"
chmod +x "$gate"
before="$(git rev-parse main)"
"$HYDRA_BIN" integrate train train-success --base "$base_commit" --target main --execute --gate "$apply_gate" > "$test_root/train.out" 2>&1 &
train_pid=$!
wait_for_file "$test_root/pause-started" || exit 1
train_run="$(sed -n '1p' "$test_root/train.out")"
train_dir="$(integration_dir "$train_run")"
merged_before_cancel="$(sed -n '1p' "$train_dir/last-result-commit")"
"$HYDRA_BIN" integrate cancel "$train_run" >/dev/null
assert_success $? "active train cancellation reaches its gate process"
wait "$train_pid" 2>/dev/null || true
assert_equal cancelled "$(sed -n '1p' "$train_dir/state")" "cancelled train records resumable state"
assert_equal "$before" "$(git rev-parse main)" "cancellation leaves target unchanged"

: > "$test_root/pause-release"
"$HYDRA_BIN" integrate resume "$train_run"
assert_success $? "cancelled train resumes from its recorded stage"
assert_equal "$merged_before_cancel" "$(sed -n '1p' "$train_dir/result-commit")" "resume does not replay completed merge effects"
grep -q '^candidate' "$train_dir/manifest.tsv" && grep -q '^initial_target_commit' "$train_dir/manifest.tsv"
assert_success $? "train manifest records immutable order and initial target"
"$HYDRA_BIN" integrate approve "$train_run" --by tester >/dev/null && "$HYDRA_BIN" integrate promote "$train_run"
assert_success $? "verified train promotes only after current approval"
git show main:one.txt >/dev/null && git show main:two.txt >/dev/null
assert_success $? "promotion is all-or-nothing across ordered candidates"
"$HYDRA_BIN" integrate cleanup "$train_run" --apply >/dev/null

failure_base="$(git rev-parse main)"
git branch fail-target "$failure_base"
for _tmt_head in fail-one fail-two; do
    "$HYDRA_BIN" spawn "$_tmt_head" --group train-failure --no-agent >/dev/null || exit 1
done
fail_one="$($HYDRA_BIN path fail-one)"
fail_two="$($HYDRA_BIN path fail-two)"
printf 'good\n' > "$fail_one/good.txt"
git -C "$fail_one" add good.txt
git -C "$fail_one" commit -qm good
printf 'blocked\n' > "$fail_two/blocker.txt"
git -C "$fail_two" add blocker.txt
git -C "$fail_two" commit -qm blocker
"$HYDRA_BIN" outcome fail-one "done" --actor human >/dev/null
"$HYDRA_BIN" outcome fail-two "done" --actor human >/dev/null

failure_output="$($HYDRA_BIN integrate train train-failure --base "$failure_base" --target fail-target --execute --gate "test ! -f blocker.txt || test -f $test_root/waiver" 2>/dev/null)"
failure_status=$?
assert_failure "$failure_status" "middle candidate gate failure stops the train"
failure_run="$(printf '%s\n' "$failure_output" | sed -n '1p')"
failure_dir="$(integration_dir "$failure_run")"
assert_equal "$failure_base" "$(git rev-parse fail-target)" "failed train leaves target unchanged"
status_output="$($HYDRA_BIN integrate status "$failure_run")"
printf '%s\n' "$status_output" | grep -q 'failed candidate: fail-two' &&
    printf '%s\n' "$status_output" | grep -q 'failed gate: 1' &&
    printf '%s\n' "$status_output" | grep -q 'expected target:' &&
    printf '%s\n' "$status_output" | grep -q 'observed target:' &&
    printf '%s\n' "$status_output" | grep -q 'recovery action: hydra integrate resume'
assert_success $? "failure report identifies candidate, gate, refs, and recovery"

failed_result="$(sed -n '1p' "$failure_dir/last-result-commit")"
: > "$test_root/waiver"
"$HYDRA_BIN" integrate resume "$failure_run"
assert_success $? "failed candidate gate resumes after its recovery action"
assert_equal "$failed_result" "$(sed -n '1p' "$failure_dir/result-commit")" "gate recovery does not replay candidate merges"
"$HYDRA_BIN" integrate approve "$failure_run" --by tester >/dev/null
git commit --allow-empty -qm target-moved
git branch -f fail-target HEAD
moved_target="$(git rev-parse fail-target)"
"$HYDRA_BIN" integrate promote "$failure_run" >/dev/null 2>&1
assert_failure $? "promotion rejects concurrent target movement"
assert_equal "$moved_target" "$(git rev-parse fail-target)" "stale target failure preserves observed target ref"

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
