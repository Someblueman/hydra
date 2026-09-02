#!/bin/sh
# Local workflow -> gates -> verified integration -> explicit promotion acceptance.

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
    for _twe_session in e2e-implementation e2e-review; do
        tmux kill-session -t "$_twe_session" 2>/dev/null || true
    done
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | while IFS= read -r _twe_worktree; do
            [ "$_twe_worktree" = "$repo" ] || git -C "$repo" worktree remove --force "$_twe_worktree" 2>/dev/null || true
        done
    fi
    if [ "${HYDRA_TEST_KEEP:-0}" = 1 ]; then
        echo "Preserved workflow E2E fixture: $test_root" >&2
    else
        rm -rf "$test_root"
    fi
}
trap cleanup EXIT HUP INT TERM

run_dir_for() {
    find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$1" -print | sed -n '1p'
}

integration_dir_for() {
    find "$HYDRA_HOME/state/v2/projects" -type d -path "*/integrations/$1" -print | sed -n '1p'
}

mkdir -p "$repo/.hydra/workflows"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > product.txt
git add product.txt
git commit -qm base
git branch -M main

worker="$test_root/worker.sh"
# shellcheck disable=SC2016
printf '%s\n' '#!/bin/sh' 'set -eu' 'file="$1"' 'content="$2"' 'hydra="$3"' 'head="$4"' 'printf "%s\n" "$content" > "$file"' 'git add "$file"' 'git commit -qm "$head result"' '"$hydra" outcome "$head" "done" --actor agent >/dev/null' > "$worker"
chmod +x "$worker"

workflow="$repo/.hydra/workflows/issue-review.yml"
apply_patch_marker="$test_root/accept-integration"
printf '%s\n' \
    'version: 1' \
    'id: issue-review' \
    'description: One local issue with parallel implementation and review' \
    'parallelism: 2' \
    'resources:' \
    '  disk_mb: 1' \
    '  max_heads: 2' \
    'steps:' \
    '  - id: spawn-implementation' \
    '    kind: spawn' \
    '    needs: []' \
    '    retry: 0' \
    '    idempotent: false' \
    '    args:' \
    '      branch: e2e-implementation' \
    '      group: issue-review' \
    '  - id: spawn-review' \
    '    kind: spawn' \
    '    needs: []' \
    '    retry: 0' \
    '    idempotent: false' \
    '    args:' \
    '      branch: e2e-review' \
    '      group: issue-review' \
    '  - id: implementation' \
    '    kind: exec' \
    '    needs: [spawn-implementation]' \
    '    retry: 0' \
    '    idempotent: true' \
    '    args:' \
    '      head: e2e-implementation' \
    "      argv: [sh, $worker, implementation.txt, implemented, $HYDRA_BIN, e2e-implementation]" \
    '  - id: review' \
    '    kind: exec' \
    '    needs: [spawn-review]' \
    '    retry: 0' \
    '    idempotent: true' \
    '    args:' \
    '      head: e2e-review' \
    "      argv: [sh, $worker, review.txt, reviewed, $HYDRA_BIN, e2e-review]" \
    '  - id: gate-implementation' \
    '    kind: gate' \
    '    needs: [implementation]' \
    '    retry: 0' \
    '    idempotent: true' \
    '    args:' \
    '      head: e2e-implementation' \
    '      name: acceptance' \
    '      argv: [test, -f, implementation.txt]' \
    '  - id: gate-review' \
    '    kind: gate' \
    '    needs: [review]' \
    '    retry: 0' \
    '    idempotent: true' \
    '    args:' \
    '      head: e2e-review' \
    '      name: acceptance' \
    '      argv: [test, -f, review.txt]' \
    '  - id: approve-implementation' \
    '    kind: approve' \
    '    needs: [gate-implementation]' \
    '    retry: 0' \
    '    idempotent: false' \
    '    args:' \
    '      head: e2e-implementation' \
    '      name: acceptance' \
    '      by: local-reviewer' \
    '  - id: approve-review' \
    '    kind: approve' \
    '    needs: [gate-review]' \
    '    retry: 0' \
    '    idempotent: false' \
    '    args:' \
    '      head: e2e-review' \
    '      name: acceptance' \
    '      by: local-reviewer' \
    '  - id: join' \
    '    kind: exec' \
    '    needs: [approve-implementation, approve-review]' \
    '    retry: 0' \
    '    idempotent: true' \
    '    args:' \
    '      head: e2e-implementation' \
    '      argv: [true]' > "$workflow"

"$HYDRA_BIN" init --no-agent --trust >/dev/null
git add .hydra product.txt
git commit -qm workflow-fixture
"$HYDRA_BIN" init --no-agent --trust >/dev/null
base_commit="$(git rev-parse main)"
"$HYDRA_BIN" workflow validate issue-review >/dev/null
assert_success $? "repository workflow validates under its recorded trust binding"

workflow_run="$($HYDRA_BIN workflow run issue-review)"
assert_success $? "local issue workflow completes its parallel implementation and review"
workflow_dir="$(run_dir_for "$workflow_run")"
assert_equal succeeded "$(sed -n '1p' "$workflow_dir/state")" "fan-out, gates, approvals, and join are durably complete"
grep -q 'step_id":"implementation"' "$workflow_dir/events.jsonl" && grep -q 'step_id":"review"' "$workflow_dir/events.jsonl"
assert_success $? "workflow audit stream correlates both parallel heads"

implementation_provenance="$($HYDRA_BIN provenance e2e-implementation --json)"
review_provenance="$($HYDRA_BIN provenance e2e-review --json)"
printf '%s\n%s\n' "$implementation_provenance" "$review_provenance" | grep -q '"ok":true'
assert_success $? "both candidate provenance records remain locally inspectable"

target_before="$(git rev-parse main)"
integration_output="$($HYDRA_BIN integrate "$workflow_run" --base "$base_commit" --target main --execute --gate "test -f $apply_patch_marker" 2>/dev/null)"
integration_status=$?
assert_failure "$integration_status" "end-to-end verification failure preserves recovery evidence"
integration_run="$(printf '%s\n' "$integration_output" | sed -n '1p')"
integration_dir="$(integration_dir_for "$integration_run")"
assert_equal "$target_before" "$(git rev-parse main)" "failed verification does not alter the target"
assert_equal verification-failed "$(sed -n '1p' "$integration_dir/state")" "failed integration records its exact terminal class"

: > "$apply_patch_marker"
"$HYDRA_BIN" integrate resume "$integration_run"
assert_success $? "recorded integration resumes after the documented recovery action"
"$HYDRA_BIN" integrate approve "$integration_run" --by release-reviewer >/dev/null &&
    "$HYDRA_BIN" integrate promote "$integration_run"
assert_success $? "explicit approval promotes the fully verified result"
git show main:implementation.txt >/dev/null && git show main:review.txt >/dev/null
assert_success $? "promoted target contains implementation and review candidates"
if [ -s "$integration_dir/manifest.tsv" ] && [ -s "$integration_dir/gate-0-1-attempt-2/exit-status" ] && [ -s "$integration_dir/promoted-at" ]; then
    report_status=0
else
    report_status=1
fi
assert_success "$report_status" "final manifest, recovered gate, and promotion evidence are inspectable"
"$HYDRA_BIN" integrate cleanup "$integration_run" --apply >/dev/null

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
