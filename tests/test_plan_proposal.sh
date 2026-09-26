#!/bin/sh
# Public, head-associated proposal publication and fail-closed replacement.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
root="$(mktemp -d)"
export HYDRA_HOME="$root/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck source=/dev/null
. "$REPO/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
trap 'rm -rf "$root"' EXIT
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
[ ! -d "$HYDRA_HOME/state/v2/projects/$project/workflows/runs" ]
assert_success $? 'proposal and policy selection create no execution run'
"$HYDRA_BIN" kill proposal >/dev/null
printf 'Total: %s\nPassed: %s\nFailed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
