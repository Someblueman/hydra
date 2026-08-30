#!/bin/sh
# Typed context packs and recoverable sync/land integration tests.

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
    for _tis_session in integration-head conflict-head; do
        tmux kill-session -t "$_tis_session" 2>/dev/null || true
    done
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | while IFS= read -r _tis_worktree; do
            [ "$_tis_worktree" = "$repo" ] || git -C "$repo" worktree remove --force "$_tis_worktree" 2>/dev/null || true
        done
    fi
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

echo "Running Hydra 1.7 integration-safety tests..."
echo "============================================"

"$HYDRA_BIN" init --no-agent --trust >/dev/null
assert_success $? "project initializes"
git add .hydra/config.yml
git commit -qm hydra-config
"$HYDRA_BIN" spawn integration-head --no-agent >/dev/null
assert_success $? "integration head spawns"
worktree="$("$HYDRA_BIN" path integration-head)"
printf 'feature context\n' >> "$worktree/tracked.txt"
printf 'artifact\n' > "$test_root/result.log"
context_output="$("$HYDRA_BIN" context create integration-head --diff --file tracked.txt --note 'review note' --history 1 --artifact "$test_root/result.log")"
assert_success $? "typed context pack is created"
context_path="$(printf '%s\n' "$context_output" | sed -n 's/^  path: //p')"
if [ -s "$context_path/manifest.tsv" ] && [ -f "$context_path/diff.patch" ] && \
   [ -f "$context_path/notes.txt" ] && [ -f "$context_path/history.tsv" ] && [ -f "$context_path/artifacts.tsv" ]; then
    assert_success 0 "context pack keeps each selected input typed"
else
    assert_success 1 "context pack keeps each selected input typed"
fi
printf 'SECRET=value\n' > "$worktree/.env"
"$HYDRA_BIN" context create integration-head --file .env >/dev/null 2>&1
assert_failure $? "sensitive files are never silently packed"
rm -f "$worktree/.env"
git -C "$worktree" checkout -- tracked.txt

printf 'upstream\n' > upstream.txt
git add upstream.txt
git commit -qm upstream
upstream_commit="$(git rev-parse HEAD)"
"$HYDRA_BIN" gate run integration-head --name ready -- true >/dev/null
assert_success $? "sync verification gate passes"
"$HYDRA_BIN" gate approve integration-head --name ready --by human-reviewer >/dev/null
assert_success $? "sync verification gate is explicitly approved"
"$HYDRA_BIN" sync integration-head --from main --gate ready --dry-run >/dev/null
assert_success $? "sync dry-run simulates without mutation"
"$HYDRA_BIN" sync integration-head --from main --gate ready >/dev/null
assert_success $? "approved clean head syncs"
assert_equal "$upstream_commit" "$(sed -n '1p' "$(dirname "$(find "$HYDRA_HOME" -type f -path '*/heads/*/branch' -exec grep -l '^integration-head$' {} \;)")/base-ref")" "sync advances the recorded base"
if [ -f "$worktree/upstream.txt" ] && find "$HYDRA_HOME" -path '*/archives/sync/*/result' -exec grep -q '^success$' {} \; -print | grep -q .; then
    assert_success 0 "sync preserves an archive and records success"
else
    assert_success 1 "sync preserves an archive and records success"
fi

"$HYDRA_BIN" land integration-head --into main --gate ready --dry-run >/dev/null 2>&1
assert_failure $? "gate approval becomes stale when the head commit changes"

printf 'landed\n' > "$worktree/landed.txt"
git -C "$worktree" add landed.txt
git -C "$worktree" commit -qm landed
"$HYDRA_BIN" gate run integration-head --name ready -- true >/dev/null
assert_success $? "post-sync land gate is rerun against the new commit"
"$HYDRA_BIN" gate approve integration-head --name ready --by human-reviewer >/dev/null
assert_success $? "post-sync land gate is approved"
"$HYDRA_BIN" land integration-head --into main --gate ready --dry-run >/dev/null
assert_success $? "land dry-run simulates without mutation"
"$HYDRA_BIN" land integration-head --into main --gate ready >/dev/null
assert_success $? "approved clean head lands"
if [ -f landed.txt ] && ! tmux has-session -t integration-head 2>/dev/null && [ ! -d "$worktree" ]; then
    assert_success 0 "successful land merges and tears down the head"
else
    assert_success 1 "successful land merges and tears down the head"
fi

"$HYDRA_BIN" spawn conflict-head --no-agent >/dev/null
assert_success $? "conflict recovery head spawns"
conflict_worktree="$("$HYDRA_BIN" path conflict-head)"
printf 'head side\n' > "$conflict_worktree/tracked.txt"
git -C "$conflict_worktree" add tracked.txt
git -C "$conflict_worktree" commit -qm head-conflict
printf 'main side\n' > tracked.txt
git add tracked.txt
git commit -qm main-conflict
"$HYDRA_BIN" gate run conflict-head --name ready -- true >/dev/null
assert_success $? "conflict head gate passes"
"$HYDRA_BIN" gate approve conflict-head --name ready --by human-reviewer >/dev/null
assert_success $? "conflict head gate is approved"
conflict_pre="$(git -C "$conflict_worktree" rev-parse HEAD)"
"$HYDRA_BIN" sync conflict-head --from main --gate ready >/dev/null 2>&1
assert_failure $? "conflicting sync fails"
if [ "$(git -C "$conflict_worktree" rev-parse HEAD)" = "$conflict_pre" ] && \
   [ -z "$(git -C "$conflict_worktree" status --porcelain=v1)" ] && \
   find "$HYDRA_HOME" -path '*/archives/sync/*/result' -exec grep -q '^recovered$' {} \; -print | grep -q .; then
    assert_success 0 "failed sync aborts cleanly and preserves recovery evidence"
else
    assert_success 1 "failed sync aborts cleanly and preserves recovery evidence"
fi
"$HYDRA_BIN" kill conflict-head >/dev/null
assert_success $? "recovered conflict head tears down"

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

[ "$fail_count" -eq 0 ]
