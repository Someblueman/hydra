#!/bin/sh
# Isolated verified integration acceptance coverage in a throwaway repository.

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
    for _tvi_session in verified-one verified-two conflict-one conflict-two; do
        tmux kill-session -t "$_tvi_session" 2>/dev/null || true
    done
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain | sed -n 's/^worktree //p' | while IFS= read -r _tvi_wt; do
            [ "$_tvi_wt" = "$repo" ] || git -C "$repo" worktree remove --force "$_tvi_wt" 2>/dev/null || true
        done
    fi
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

wait_for_file() {
    _tvif_file="$1"
    _tvif_count=0
    while [ ! -f "$_tvif_file" ] && [ "$_tvif_count" -lt 100 ]; do
        sleep 0.1
        _tvif_count=$((_tvif_count + 1))
    done
    [ -f "$_tvif_file" ]
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

for _tvi_head in verified-one verified-two; do
    "$HYDRA_BIN" spawn "$_tvi_head" --group verified --no-agent >/dev/null || exit 1
done
one="$("$HYDRA_BIN" path verified-one)"
two="$("$HYDRA_BIN" path verified-two)"
printf 'one\n' > "$one/one.txt"
git -C "$one" add one.txt
git -C "$one" commit -qm one
printf 'two\n' > "$two/two.txt"
git -C "$two" add two.txt
git -C "$two" commit -qm two
"$HYDRA_BIN" outcome verified-one "done" --actor human >/dev/null
"$HYDRA_BIN" outcome verified-two "done" --actor human >/dev/null
before="$(git rev-parse main)"
worktrees_before="$(git worktree list --porcelain | grep -c '^worktree ')"
preview="$("$HYDRA_BIN" integrate verified --base "$base_commit" --target main --dry-run --gate true)"
assert_success $? "dry-run previews a completed group"
assert_equal "$before" "$(git rev-parse main)" "dry-run leaves target unchanged"
assert_equal "$worktrees_before" "$(git worktree list --porcelain | grep -c '^worktree ')" "dry-run creates no worktree"
printf '%s\n' "$preview" | grep -q 'candidate 1:' &&
    printf '%s\n' "$preview" | grep -q 'candidate 2:' &&
    printf '%s\n' "$preview" | grep -q 'merge-base=' &&
    printf '%s\n' "$preview" | grep -q 'required gate: true' &&
    printf '%s\n' "$preview" | grep -q 'planned integration worktree:'
assert_success $? "dry-run reports order, bases, gates, target, and path"

run="$("$HYDRA_BIN" integrate verified --base "$base_commit" --target main --execute --gate 'test -f one.txt' --gate 'test -f two.txt')"
assert_success $? "clean candidates assemble and verify in order"
assert_equal "$before" "$(git rev-parse main)" "assembly leaves target unchanged"
"$HYDRA_BIN" integrate status ../config.yml >/dev/null 2>&1
assert_failure $? "integration actions reject traversing run IDs"

cat > "$test_root/crash-gate.sh" <<'EOF'
#!/bin/sh
[ -f "$1" ] && exit 0
: > "$1"
sleep 30 &
printf '%s\n' "$!" > "$3"
: > "$2"
wait
EOF
chmod +x "$test_root/crash-gate.sh"
"$HYDRA_BIN" integrate verified --base "$base_commit" --target main --execute \
    --gate "sh $test_root/crash-gate.sh $test_root/crash-once $test_root/crash-started $test_root/crash-child" \
    > "$test_root/crash-run.out" 2>&1 &
crash_owner=$!
wait_for_file "$test_root/crash-started" || exit 1
crash_run="$(sed -n '1p' "$test_root/crash-run.out")"
crash_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/integrations/$crash_run" -print | sed -n '1p')"
kill -KILL "$crash_owner" 2>/dev/null || true
wait "$crash_owner" 2>/dev/null || true
find "$HYDRA_HOME/locks" -type d -name 'integration_target_*.lock' -print | grep -q .
assert_success $? "owner crash leaves the serialized target lock for recovery"
"$HYDRA_BIN" integrate resume "$crash_run" >/dev/null
assert_success $? "stale assembling integration removes its stale lock and resumes"
assert_equal verified "$(sed -n '1p' "$crash_dir/state")" "crash recovery revalidates the integration"
kill -0 "$(sed -n '1p' "$test_root/crash-child")" 2>/dev/null
assert_failure $? "crash recovery terminates the abandoned gate descendant"
"$HYDRA_BIN" integrate cleanup "$crash_run" --apply >/dev/null

mutation_output="$("$HYDRA_BIN" integrate verified --base "$base_commit" --target main --execute --gate 'touch gate-output; false' 2>/dev/null)"
assert_failure $? "a mutating gate cannot produce a verified result"
mutation_run="$(printf '%s\n' "$mutation_output" | sed -n '1p')"
mutation_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/integrations/$mutation_run" -print | sed -n '1p')"
assert_equal gate-mutation "$(sed -n '1p' "$mutation_dir/failure-class")" "gate mutation has a distinct failure class"
grep -q 'remove gate-created changes.*hydra integrate resume' "$mutation_dir/recovery-action"
assert_success $? "gate mutation records an actionable recovery path"
rm -f "$(sed -n '1p' "$mutation_dir/worktree")/gate-output"
"$HYDRA_BIN" integrate cleanup "$mutation_run" --apply >/dev/null
"$HYDRA_BIN" integrate promote "$run" >/dev/null 2>&1
assert_failure $? "promotion requires explicit current approval"
"$HYDRA_BIN" integrate approve "$run" --by tester
assert_success $? "passing result accepts explicit approval"
"$HYDRA_BIN" integrate promote "$run"
assert_success $? "approved result promotes explicitly"
git show main:one.txt >/dev/null && git show main:two.txt >/dev/null
assert_success $? "promotion contains every ordered candidate"
assert_equal "" "$(git status --porcelain=v1)" "promotion keeps a checked-out target worktree consistent"
"$HYDRA_BIN" integrate cleanup "$run" --apply
assert_success $? "clean recorded integration worktree is safely removed"

git branch conflict-target "$base_commit"
for _tvi_head in conflict-one conflict-two; do
    "$HYDRA_BIN" spawn "$_tvi_head" --group conflicts --no-agent >/dev/null || exit 1
done
cone="$("$HYDRA_BIN" path conflict-one)"
ctwo="$("$HYDRA_BIN" path conflict-two)"
printf 'left\n' > "$cone/base.txt"
git -C "$cone" add base.txt
git -C "$cone" commit -qm left
printf 'right\n' > "$ctwo/base.txt"
git -C "$ctwo" add base.txt
git -C "$ctwo" commit -qm right
"$HYDRA_BIN" outcome conflict-one "done" --actor human >/dev/null
"$HYDRA_BIN" outcome conflict-two "done" --actor human >/dev/null
conflict_preview="$("$HYDRA_BIN" integrate conflicts --base "$base_commit" --target refs/heads/conflict-target --dry-run 2>&1 || true)"
printf '%s\n' "$conflict_preview" | grep -q 'overlap: base.txt' &&
    printf '%s\n' "$conflict_preview" | grep -q 'predicted conflict:'
assert_success $? "preview distinguishes overlap from predicted conflict"
target_now="$(git rev-parse conflict-target)"
"$HYDRA_BIN" integrate conflicts --base "$base_commit" --target conflict-target --execute >/dev/null 2>&1
assert_failure $? "observed conflict stops execution"
assert_equal "$target_now" "$(git rev-parse conflict-target)" "observed conflict preserves target"

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
