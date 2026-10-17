#!/bin/sh
# Public, head-associated proposal publication and fail-closed replacement.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
export HYDRA_HOME="$root/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck source=/dev/null
. "$REPO/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
e2e="$root/e2e"
cleanup() {
    if [ -x "$e2e/bin/tmux" ]; then
        for e2e_head in bundle-worker planner; do
            (cd "$e2e/repo" && PATH="$e2e/bin:$PATH" "$HYDRA_BIN" kill "$e2e_head" --force) >/dev/null 2>&1
        done
        PATH="$e2e/bin:$PATH" tmux kill-server >/dev/null 2>&1
    fi
    rm -rf "$root"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir "$root/repo"
cd "$root/repo" || exit 1
git init -q && git config user.name Test && git config user.email test@example.invalid
printf 'fixture\n' > README
git add README && git -c commit.gpgSign=false commit -qm fixture
"$HYDRA_BIN" init --no-agent >/dev/null
"$HYDRA_BIN" spawn proposal --headless --no-agent >/dev/null
project="$(cat .git/hydra/project-id)"
head="$(find "$HYDRA_HOME/state/v2/projects/$project/heads" -name current-instance -exec dirname {} \;)"
"$HYDRA_BIN" workflow plan proposal proposal >/dev/null 2>&1
assert_failure $? 'missing proposal lookup refuses without fabricating state'
if [ ! -e "$head/planning" ]; then check=0; else check=1; fi
assert_success "$check" 'read-only lookup creates no planning directory'
"$HYDRA_BIN" workflow plan propose "$REPO/tests/fixtures/plan/plan.json" --branch proposal >/dev/null
assert_success $? 'agent draft publishes through the public CLI'
if [ -f "$head/planning/draft.json" ] && [ ! -e "$head/planning/policy.json" ]; then check=0; else check=1; fi
assert_success "$check" 'publication associates a draft with its head without granting policy'
cp "$head/planning/draft.json" "$root/original.json"
for input in '{"objective":"x","objective":"y","steps":[{}]}' '{"broken":' '{"objective":"x","steps":[]}'; do
    printf '%s\n' "$input" > "$root/invalid.json"
    "$HYDRA_BIN" workflow plan propose "$root/invalid.json" --branch proposal >/dev/null 2>&1
    assert_failure $? 'invalid or ambiguous JSON cannot replace the proposal'
    cmp -s "$head/planning/draft.json" "$root/original.json"
    assert_success $? 'rejected publication preserves the previous draft'
done
dd if=/dev/zero bs=262145 count=1 2>/dev/null | tr '\000' ' ' > "$root/oversized.json"
"$HYDRA_BIN" workflow plan propose "$root/oversized.json" --branch proposal >/dev/null 2>&1
assert_failure $? 'oversized input is refused before replacement'
cmp -s "$head/planning/draft.json" "$root/original.json"
assert_success $? 'oversized input preserves the existing proposal'
HYDRA_INSTANCE_ID=instance_00000000000000000000 "$HYDRA_BIN" workflow plan propose "$root/original.json" --branch proposal >/dev/null 2>&1
assert_failure $? 'a stale agent instance cannot republish'
HYDRA_HEAD_ID=head_00000000000000000000 "$HYDRA_BIN" workflow plan propose "$root/original.json" --branch proposal >/dev/null 2>&1
assert_failure $? 'an agent cannot publish into another head'
"$HYDRA_BIN" workflow plan proposal proposal --local-policy > "$root/projection"
assert_success $? 'explicit local policy produces the versioned path projection'
assert_equal "$(printf 'HYDRA_PLAN_PROPOSAL\t1')" "$(sed -n '1p' "$root/projection")" 'projection is versioned'
policy="$(sed -n '2p' "$root/projection" | cut -f3)"
assert_equal '{"schema_version":1,"envelope":{"hosts":["local"],"tools":["sh","git","make"],"effects":["execute","worktree"],"writes":["@spawned:*"],"parallelism":1,"timeout_seconds":3600,"artifact_bytes":1048576,"max_heads":4,"disk_mb":1024,"retry_budget":0,"repair_budget":0}}' \
    "$(cat "$policy")" 'a head without a profile gets the guided policy without an agent tool'
cp "$head/profile" "$root/profile"
printf 'codex\n' > "$head/profile"
"$HYDRA_BIN" workflow plan proposal proposal --local-policy >/dev/null
grep -q '"tools":\["sh","git","make","profile:codex"\],' "$policy"
assert_success $? 'the guided policy authorizes the head recorded agent profile'
for profile in 'Bad Name' 'bad"quote' '-lead' 'trail-'; do
    printf '%s\n' "$profile" > "$head/profile"
    "$HYDRA_BIN" workflow plan proposal proposal --local-policy >/dev/null
    grep -q '"tools":\["sh","git","make"\],' "$policy"
    assert_success $? "an unusable recorded profile is omitted from the policy: $profile"
done
cp "$root/profile" "$head/profile"
"$HYDRA_BIN" workflow plan proposal proposal --local-policy >/dev/null
"$HYDRA_BIN" workflow plan proposal proposal > "$root/read-only"
assert_success $? 'subsequent lookup is read-only'
cmp -s "$root/projection" "$root/read-only"
assert_success $? 'subsequent lookup preserves the same associated paths'
if [ ! -d "$HYDRA_HOME/state/v2/projects/$project/workflows/runs" ]; then check=0; else check=1; fi
assert_success "$check" 'proposal and policy selection create no execution run'

# Proposal assets: private copies beside the draft, replaced as one set.
bundle="$REPO/tests/fixtures/plan-bundle"
cp "$bundle/draft.json" "$root/bundle-draft.json"
cp "$bundle/verifier.sh" "$root/verifier.sh"
"$HYDRA_BIN" workflow plan propose "$root/bundle-draft.json" --branch proposal --asset verifier="$root/verifier.sh" >/dev/null
assert_success $? 'a draft publishes with its referenced asset'
cmp -s "$head/planning/assets/verifier" "$root/verifier.sh"
assert_success $? 'the asset is copied beside the draft'
assert_equal '' "$(find "$head/planning/assets" -type f -perm -044)" 'asset copies are private'
cp "$head/planning/draft.json" "$root/bundle-original.json"
ln -s "$root/verifier.sh" "$root/verifier-link.sh"
"$HYDRA_BIN" workflow plan propose "$root/bundle-draft.json" --branch proposal --asset verifier="$root/verifier-link.sh" > "$root/asset.out" 2>&1
assert_failure $? 'a symlinked asset is refused'
cmp -s "$head/planning/assets/verifier" "$root/verifier.sh" && cmp -s "$head/planning/draft.json" "$root/bundle-original.json"
assert_success $? 'a refused asset preserves the previous draft and assets'
dd if=/dev/zero bs=65537 count=1 2>/dev/null | tr '\000' 'a' > "$root/oversized-asset"
"$HYDRA_BIN" workflow plan propose "$root/bundle-draft.json" --branch proposal --asset verifier="$root/oversized-asset" >/dev/null 2>&1
assert_failure $? 'an asset over 64 KiB is refused'
"$HYDRA_BIN" workflow plan propose "$root/bundle-draft.json" --branch proposal >/dev/null 2>&1
assert_failure $? 'a draft naming an unpublished asset is refused'
"$HYDRA_BIN" workflow plan propose "$root/bundle-draft.json" --branch proposal --asset verifier="$root/verifier.sh" --asset extra="$root/verifier.sh" > "$root/asset.out" 2>&1
assert_failure $? 'an asset no data input references is refused'
grep -q asset_mismatch "$root/asset.out"
assert_success $? 'the mismatch is reported to the publishing agent'
set --
for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17; do set -- "$@" --asset "a$n=$root/verifier.sh"; done
"$HYDRA_BIN" workflow plan propose "$root/bundle-draft.json" --branch proposal "$@" >/dev/null 2>&1
assert_failure $? 'more than 16 assets are refused'
cmp -s "$head/planning/assets/verifier" "$root/verifier.sh"
assert_success $? 'refused publications keep the published asset set'
"$HYDRA_BIN" workflow plan propose "$REPO/tests/fixtures/plan/plan.json" --branch proposal >/dev/null
assert_success $? 'a draft without assets republishes'
assert_equal '' "$(find "$head/planning/assets" -type f)" 'republishing replaces the previous asset set'
"$HYDRA_BIN" kill proposal >/dev/null

# End to end: a codex-profile planning head proposes a plan whose worker prompt
# is inline and whose verifier is an asset. The source checkout stays clean and
# never contains any of the plan's files; validation, digest binding and
# execution use only what the proposal carries.
mkdir -p "$e2e/bin" "$e2e/repo"
cp "$bundle/codex" "$e2e/bin/codex"
real_tmux="$(command -v tmux)"
printf '#!/bin/sh\nexec "%s" -S "%s" -f /dev/null "$@"\n' "$real_tmux" "$e2e/tmux.sock" > "$e2e/bin/tmux"
chmod +x "$e2e/bin/tmux" "$e2e/bin/codex"
cp "$bundle/repo/Makefile" "$bundle/repo/README" "$e2e/repo/"
cd "$e2e/repo" || exit 1
git init -q && git config user.name Test && git config user.email test@example.invalid
git add . && git -c commit.gpgSign=false commit -qm base
PATH="$e2e/bin:$PATH" "$HYDRA_BIN" init --no-agent --trust >/dev/null
# HYDRA_SKIP_AI would force profile none; the stub stands in for the provider.
(unset HYDRA_SKIP_AI; PATH="$e2e/bin:$PATH" "$HYDRA_BIN" spawn planner --profile codex >/dev/null 2>&1)
assert_success $? 'a planning head starts with the codex profile'
e2e_project="$(cat .git/hydra/project-id)"
planner="$(dirname "$(grep -l '^planner$' "$HYDRA_HOME/state/v2/projects/$e2e_project/heads/"*/branch)")"
assert_equal codex "$(sed -n '1p' "$planner/profile")" 'the planning head records the codex profile'
worktree="$("$HYDRA_BIN" path planner)"
cp "$bundle/draft.json" "$worktree/draft.json"
cp "$bundle/verifier.sh" "$worktree/verifier.sh"
(cd "$worktree" && PATH="$e2e/bin:$PATH" "$HYDRA_BIN" workflow plan propose draft.json --asset verifier=verifier.sh >/dev/null)
assert_success $? 'the planning agent publishes from its own worktree'
"$HYDRA_BIN" workflow plan proposal planner --local-policy > "$e2e/projection"
draft="$(sed -n '2p' "$e2e/projection" | cut -f2)"
policy="$(sed -n '2p' "$e2e/projection" | cut -f3)"
assets="$(dirname "$draft")/assets"
grep -q '"tools":\["sh","git","make","profile:codex"\]' "$policy"
assert_success $? 'the guided policy authorizes the codex profile'
assert_equal '' "$(git status --porcelain)" 'the source checkout has no changes'
assert_equal 'Makefile README' "$(git ls-files | tr '\n' ' ' | sed 's/ $//')" 'the source contains none of the plan files'
assert_equal 1 "$(git rev-list --count HEAD)" 'planning committed nothing to the source'
"$HYDRA_BIN" workflow plan validate "$draft" "$policy" --assets-dir "$assets" > "$e2e/validate.json"
assert_success $? 'the proposal validates against the clean source with its own assets'
"$HYDRA_BIN" workflow plan validate "$draft" "$policy" > "$e2e/missing.json"
assert_failure $? 'validation without the proposal assets fails'
grep -q missing_asset "$e2e/missing.json"
assert_success $? 'the missing asset is named'
"$HYDRA_BIN" workflow plan compile "$draft" "$policy" "$e2e/first.json" --assets-dir "$assets" > "$e2e/first-compile.json"
first="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$e2e/first-compile.json")"
assert_equal 64 "${#first}" 'the first revision has an acceptance digest'
"$HYDRA_BIN" workflow plan show "$e2e/first.json" > "$e2e/first.txt"
grep -q 'prompt text: Inline worker brief (bundle-marker-1)' "$e2e/first.txt" && grep -q 'Input verifier: carried by this plan as assets/verifier' "$e2e/first.txt"
assert_success $? 'the approval preview shows the inline prompt and the carried asset'
"$HYDRA_BIN" workflow plan explain "$e2e/first.json" | grep -q '"prompt":{"bytes":'
assert_success $? 'explain summarizes the inline prompt'
sed 's/make check passed in the worker head/make check passed in the worker head (revision 2)/' "$bundle/verifier.sh" > "$worktree/verifier.sh"
(cd "$worktree" && PATH="$e2e/bin:$PATH" "$HYDRA_BIN" workflow plan propose draft.json --asset verifier=verifier.sh >/dev/null)
"$HYDRA_BIN" workflow plan compile "$draft" "$policy" "$e2e/second.json" --assets-dir "$assets" > "$e2e/second-compile.json"
second="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$e2e/second-compile.json")"
if [ "${#second}" -eq 64 ] && [ "$second" != "$first" ]; then check=0; else check=1; fi
assert_success "$check" 'changing only the asset changes the acceptance digest'
"$HYDRA_BIN" workflow plan compare "$e2e/first.json" "$e2e/second.json" | grep -q '"changed_sections":\[[^]]*"assets"'
assert_success $? 'comparison names the changed assets'
PATH="$e2e/bin:$PATH" "$HYDRA_BIN" workflow plan run "$e2e/second.json" --accept "$first" > "$e2e/stale.out" 2>&1
assert_failure $? 'approval of the previous revision is refused for the changed asset'
runs="$HYDRA_HOME/state/v2/projects/$e2e_project/workflows/runs"
if [ ! -d "$runs" ] || [ -z "$(ls "$runs")" ]; then check=0; else check=1; fi
assert_success "$check" 'a refused approval creates no run'
PATH="$e2e/bin:$PATH" "$HYDRA_BIN" workflow plan run "$e2e/second.json" --accept "$second" > "$e2e/run.out" 2> "$e2e/run.err"
assert_success $? 'the exact approved revision runs'
run="$(sed -n '1p' "$e2e/run.out")"
"$HYDRA_BIN" workflow plan result "$run" > "$e2e/result.json"
grep -q '"verdict":"pass"' "$e2e/result.json" && grep -q '(revision 2)' "$e2e/result.json"
assert_success $? 'the carried asset verified the worker head and wrote the report'
worker="$("$HYDRA_BIN" path bundle-worker)"
grep -q 'bundle-marker-1' "$worker/received-prompt"
assert_success $? 'the worker agent received the inline prompt'
if [ -s "$worker/greeting.txt" ] && [ -f "$worker/tests/greeting.sh" ]; then check=0; else check=1; fi
assert_success "$check" 'the worker changed its own head'
run_dir="$HYDRA_HOME/state/v2/projects/$e2e_project/workflows/runs/$run"
assert_equal succeeded "$(sed -n '1p' "$run_dir/steps/repo-checks/state")" 'the repository check ran on the worker head after implementation'
assert_equal '' "$(git status --porcelain)" 'the source checkout is still clean after the run'
assert_equal 1 "$(git rev-list --count HEAD)" 'the run committed nothing to the source'
cd "$root" || exit 1
printf 'Total: %s\nPassed: %s\nFailed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
