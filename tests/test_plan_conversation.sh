#!/bin/sh
# Returned proposals and planning-conversation notices through the public CLI.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
export HYDRA_TEST_ROOT="$REPO"
# shellcheck source=/dev/null
. "$REPO/tests/fixture-tools.sh"
root="$(mktemp -d)"
export HYDRA_HOME="$root/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck source=/dev/null
. "$REPO/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
cleanup() {
    for head in planner plan-ok plan-bad; do
        (cd "$root/repo" && "$HYDRA_BIN" kill "$head" --force >/dev/null 2>&1) || true
    done
    if [ "$fail_count" -ne 0 ]; then printf "Failure evidence: %s\n" "$root"; cat "$root"/*.compile 2>/dev/null; return; fi
    rm -rf "$root"
}
test_code=0
trap 'test_code=$?; cleanup; exit "$test_code"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir "$root/repo"
cp "$REPO/tests/fixtures/plan/repo/"* "$root/repo/"
printf 'Unexpected report\n' > "$root/repo/wrong.txt"
cd "$root/repo" || exit 1
git init -q && git config user.name Test && git config user.email test@example.invalid
git add . && git -c commit.gpgSign=false commit -qm fixture
"$HYDRA_BIN" init --no-agent --trust >/dev/null
"$HYDRA_BIN" spawn planner --headless --no-agent >/dev/null
project="$(cat .git/hydra/project-id)"
head_id="$(basename "$(dirname "$(grep -lx planner "$HYDRA_HOME/state/v2/projects/$project/heads"/*/branch)")")"
head_dir="$HYDRA_HOME/state/v2/projects/$project/heads/$head_id"
instance="$(sed -n '1p' "$head_dir/current-instance")"

# Usage: plan_variant <branch> <expected-input> <objective> <out>
plan_variant() {
    sed -e "s/plan-smoke/$1/g" -e "s/expected.txt/$2/" \
        -e "s/Deliver a report and verify its contents/$3/" -e 's/"disk_mb": *1\([,}]\)/"disk_mb":1024\1/' "$root/base.json" > "$4"
}
cp "$REPO/tests/fixtures/plan/plan.json" "$root/base.json"
fixture_json headless-plan "$root/base.json"
assert_success $? 'fixture plan uses headless worker heads'

# Usage: compile_digest <compiled-output> -> prints the accepted digest
compile_digest() {
    "$HYDRA_BIN" workflow plan compile "$head_dir/planning/draft.json" "$head_dir/planning/policy.json" "$1" > "$1.compile" &&
        "$HYDRA_BIN" workflow plan show "$1" | sed -n 's/^Acceptance digest: //p'
}

# Usage: notices <run-id> -> notice keys recorded for the run
notices() {
    find "$HYDRA_HOME/state/v2/projects/$project/workflows/runs/$1/planning-notices" -mindepth 1 -maxdepth 1 -type d \
        -exec basename {} \; 2>/dev/null | LC_ALL=C sort | tr '\n' ' '
}

inbox() {
    find "$HYDRA_HOME" -path '*/queue/*' -type f -exec cat {} \; 2>/dev/null
}

plan_variant plan-bad expected.txt 'Returned objective' "$root/returned.json"
"$HYDRA_BIN" workflow plan propose "$root/returned.json" --branch planner >/dev/null
"$HYDRA_BIN" workflow plan proposal planner --local-policy >/dev/null
assert_success $? 'published proposal is reviewable before a return'
compile_digest "$root/returned-compiled.json" > "$root/returned-digest"
returned_digest="$(cat "$root/returned-digest")"
assert_equal 64 "${#returned_digest}" 'the proposal compiles to an exact digest'
"$HYDRA_BIN" workflow plan proposal planner --return 'Split the verify step' > "$root/return.out" 2>&1
assert_success $? 'request changes records a returned revision'
grep -q 'returned for changes' "$root/return.out"
assert_success $? 'return explains that execution is blocked'
assert_equal "$(shasum -a 256 "$head_dir/planning/draft.json" | cut -d' ' -f1)" \
    "$(sed -n '1p' "$head_dir/planning/returned")" 'the return binds the exact draft digest'
assert_equal 'Split the verify step' "$(cat "$head_dir/planning/feedback")" 'feedback is kept beside the draft'
"$HYDRA_BIN" workflow plan proposal planner >/dev/null 2>&1
assert_failure $? 'a returned proposal cannot be reviewed again'
"$HYDRA_BIN" workflow plan proposal planner --local-policy >/dev/null 2>&1
assert_failure $? 'a returned proposal cannot be re-armed with a policy'
"$HYDRA_BIN" workflow plan proposal planner --return '' >/dev/null 2>&1
assert_failure $? 'an empty return is refused'
"$HYDRA_BIN" workflow plan --workspace-owner "$returned_digest" "$head_id" "$instance" < "$root/returned-compiled.json"
assert_failure $? 'the launch owner refuses a returned proposal'
assert_equal failed "$(sed -n '1p' "$HYDRA_HOME/state/v2/projects/$project/workflows/launches/$returned_digest/state")" \
    'the refused launch is recorded as failed'
assert_equal 0 "$(find "$HYDRA_HOME/state/v2/projects/$project/workflows" -path '*/runs/run_*' -prune -type d 2>/dev/null | wc -l | tr -d ' ')" \
    'a returned proposal creates no run'

plan_variant plan-ok expected.txt 'Revised objective' "$root/revised.json"
"$HYDRA_BIN" workflow plan propose "$root/revised.json" --branch planner >/dev/null
cleared=0
[ ! -e "$head_dir/planning/returned" ] && [ ! -e "$head_dir/planning/feedback" ] || cleared=1
assert_success "$cleared" 'republication clears the return'
"$HYDRA_BIN" workflow plan proposal planner --local-policy >/dev/null
assert_success $? 'the revised proposal is reviewable'
ok_digest="$(compile_digest "$root/ok-compiled.json")"
"$HYDRA_BIN" workflow plan --workspace-owner "$ok_digest" "$head_id" "$instance" < "$root/ok-compiled.json" >/dev/null 2>&1
assert_success $? 'the launch owner runs an associated plan'
ok_run="$(sed -n '1p' "$HYDRA_HOME/state/v2/projects/$project/workflows/launches/$ok_digest/run-id")"
assert_equal "$head_id" "$(sed -n '1p' "$HYDRA_HOME/state/v2/projects/$project/workflows/runs/$ok_run/planning-head")" \
    'the run records its exact planning head'
assert_equal "$instance" "$(sed -n '1p' "$HYDRA_HOME/state/v2/projects/$project/workflows/runs/$ok_run/planning-instance")" \
    'the run records its exact planning instance'
assert_equal 'run.created.run run.succeeded.run ' "$(notices "$ok_run")" 'a successful run records start and completion notices'
inbox > "$root/inbox-ok"
assert_equal 1 "$(grep -c "run $ok_run started" "$root/inbox-ok")" 'the start notice reaches the planning inbox once'
assert_equal 1 "$(grep -c "run $ok_run finished: succeeded" "$root/inbox-ok")" 'the completion notice reaches the planning inbox once'
"$HYDRA_BIN" workflow resume "$ok_run" >/dev/null 2>&1 || true
inbox > "$root/inbox-again"
assert_equal 1 "$(grep -c "run $ok_run finished: succeeded" "$root/inbox-again")" 'notices are deduplicated per run and event'

plan_variant plan-bad wrong.txt 'Failing objective' "$root/failing.json"
"$HYDRA_BIN" workflow plan propose "$root/failing.json" --branch planner >/dev/null
"$HYDRA_BIN" workflow plan proposal planner --local-policy >/dev/null
bad_digest="$(compile_digest "$root/bad-compiled.json")"
"$HYDRA_BIN" workflow plan --workspace-owner "$bad_digest" "$head_id" "$instance" < "$root/bad-compiled.json" >/dev/null 2>&1
assert_failure $? 'a failing associated plan reports failure'
bad_run="$(sed -n '1p' "$HYDRA_HOME/state/v2/projects/$project/workflows/launches/$bad_digest/run-id")"
assert_equal 'run.created.run run.failed.run step.failed.verify ' "$(notices "$bad_run")" 'a failing run records step failure and completion notices'
inbox > "$root/inbox-bad"
assert_equal 1 "$(grep -c "step verify failed in run $bad_run" "$root/inbox-bad")" 'the failed step reaches the planning inbox once'
assert_equal 1 "$(grep -c "run $bad_run finished: failed" "$root/inbox-bad")" 'the failed run reaches the planning inbox once'

# A different instance of the same branch is not the planning conversation.
plan_variant plan-other expected.txt 'Stale instance objective' "$root/stale.json"
"$HYDRA_BIN" workflow plan propose "$root/stale.json" --branch planner >/dev/null
"$HYDRA_BIN" workflow plan proposal planner --local-policy >/dev/null
stale_digest="$(compile_digest "$root/stale-compiled.json")"
"$HYDRA_BIN" workflow plan --workspace-owner "$stale_digest" "$head_id" instance_00000000000000000000 < "$root/stale-compiled.json" >/dev/null 2>&1
assert_failure $? 'the launch owner refuses a planning instance that is not current'
printf 'Total: %s\nPassed: %s\nFailed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
