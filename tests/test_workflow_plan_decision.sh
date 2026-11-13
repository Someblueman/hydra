#!/bin/sh
# Head-associated planning decisions: recorded privately with their exact
# analysis inputs, listed and replayed byte for byte through the public CLI.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
export HYDRA_HOME="$root/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck source=/dev/null
. "$REPO/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
# Print the permission string of one entry; ls -ld is portable for one entry.
mode_of() {
    # shellcheck disable=SC2012
    ls -ld "$1" | awk '{ print substr($1, 1, 10) }'
}
cleanup() {
    (cd "$root/repo" && "$HYDRA_BIN" kill planner --force) >/dev/null 2>&1
    rm -rf "$root"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir "$root/repo"
cp -R "$REPO/tests/fixtures/plan/repo/." "$root/repo/"
cd "$root/repo" || exit 1
git init -q && git config user.name Test && git config user.email test@example.invalid
git add . && git -c commit.gpgSign=false commit -qm fixture
"$HYDRA_BIN" init --no-agent >/dev/null
"$HYDRA_BIN" spawn planner --headless --no-agent >/dev/null
project="$(cat .git/hydra/project-id)"
head="$(find "$HYDRA_HOME/state/v2/projects/$project/heads" -name current-instance -exec dirname {} \;)"

# Two matched alternatives: the fixture, and the fixture with an exec step that
# reaches no deliverable or check.
"$HYDRA_BIN" workflow plan compile "$REPO/tests/fixtures/plan/plan.json" "$REPO/tests/fixtures/plan/policy.json" "$root/left.json" > "$root/left.out"
assert_success $? 'the fixture plan compiles'
sed 's/"steps": \[/"steps": [{"id":"extra","role":"work","kind":"exec","needs":["spawn"],"writes":[],"args":{"head":"plan-smoke","argv":["sh","compose.sh"],"timeout":30}},/' \
    "$REPO/tests/fixtures/plan/plan.json" > "$root/right.plan.json"
"$HYDRA_BIN" workflow plan compile "$root/right.plan.json" "$REPO/tests/fixtures/plan/policy.json" "$root/right.json" > "$root/right.out"
assert_success $? 'the alternative with an unconnected step compiles'
left_sha="$(sed -n 's/.*"sha256":"\([0-9a-f]*\)".*/\1/p' "$root/left.out")"
right_sha="$(sed -n 's/.*"sha256":"\([0-9a-f]*\)".*/\1/p' "$root/right.out")"

"$HYDRA_BIN" workflow plan evaluate "$root/left.json" "$root/right.json" > "$root/evaluate.json"
assert_success $? 'evaluate compares two compiled alternatives'
grep -q '"decomposition":{"left":{"nodes":3,"edges":2,"required_nodes":3,"unconnected_nodes":0' "$root/evaluate.json"
assert_success $? 'evaluate separates decomposition metrics'
grep -q '"scheduling":{"left":{"depth_nodes":3,"max_level_width":1,"hard_budgets":{"parallelism":1,"timeout_seconds":180,"max_heads":1},"critical_path_milliseconds":null' "$root/evaluate.json"
assert_success $? 'evaluate keeps unknown schedule ranges unknown and copies hard budgets'
grep -q '"placement":{.*"modeled_preference":"not_assessed"' "$root/evaluate.json"
assert_success $? 'evaluate does not assess placement'

"$HYDRA_BIN" workflow plan decision list planner > "$root/list.out" 2>&1
assert_success $? 'listing without decisions succeeds'
grep -q 'No planning decisions' "$root/list.out"
assert_success $? 'listing names the absence of decisions'
if [ ! -e "$head/planning" ]; then check=0; else check=1; fi
assert_success "$check" 'listing creates no planning state'

"$HYDRA_BIN" workflow plan decision record planner --select "$root/left.json" 'the fixture covers every outcome' --reject "$root/right.json" 'extra reaches no deliverable' > "$root/record.out"
assert_success $? 'a decision records through the public CLI'
id="$(sed -n 's/^Decision \([0-9a-f]\{16\}\) recorded.*/\1/p' "$root/record.out")"
assert_equal 16 "${#id}" 'the decision ID is sixteen hex characters'
grep -q 'no validation, approval or execution has occurred' "$root/record.out"
assert_success $? 'recording grants nothing'
assert_equal "drwx------" "$(mode_of "$head/planning/decisions/$id")" 'the decision directory is private'
assert_equal "-rw-------" "$(mode_of "$head/planning/decisions/$id/decision.json")" 'the decision record is private'
assert_equal "-rw-------" "$(mode_of "$head/planning/decisions/$id/inputs/$left_sha.json")" 'the selected input is kept privately under its digest'
test -f "$head/planning/decisions/$id/inputs/$right_sha.json"
assert_success $? 'the rejected input is kept under its digest'
grep -q '"analysis":"hydra-plan-evaluate-1","compiler":"hydra-plan-1"' "$head/planning/decisions/$id/decision.json"
assert_success $? 'the record names the compiler and analysis versions'
grep -q '"policy":{"schema_version":1,"sha256":"[0-9a-f]\{64\}"}' "$head/planning/decisions/$id/decision.json"
assert_success $? 'the record names the policy version'

"$HYDRA_BIN" workflow plan decision list planner > "$root/list.out"
assert_equal "$id" "$(cat "$root/list.out")" 'listing prints the decision ID'
"$HYDRA_BIN" workflow plan decision list planner --json > "$root/list.json"
assert_success $? 'listing has a JSON form'
grep -q "\"decision_id\":\"$id\".*\"replay\":\"reproduced\"" "$root/list.json"
assert_success $? 'the JSON listing replays each record'
"$HYDRA_BIN" workflow plan decision show planner "$id" > "$root/show.out"
assert_success $? 'show prints the decision'
grep -q "^Selected ${left_sha%"${left_sha#????????????}"}: the fixture covers every outcome" "$root/show.out"
assert_success $? 'show names the selected alternative and reason'
grep -q '^  decomposition: left$' "$root/show.out"
assert_success $? 'show reports the decomposition preference'
grep -q '^Replay: reproduced$' "$root/show.out"
assert_success $? 'show reports the replay'
"$HYDRA_BIN" workflow plan decision show planner "$id" --json > "$root/show.json"
assert_success $? 'show has a JSON form'
grep -q '"replay":{"status":"reproduced","comparisons":\[{"rejected_sha256":"'"$right_sha"'","recorded_sha256":"\([0-9a-f]\{64\}\)","replayed_sha256":"\1","reproduced":true}\]' "$root/show.json"
assert_success $? 'the JSON show reproduces the recorded comparison byte for byte'

cp "$head/planning/decisions/$id/decision.json" "$root/decision.bak"
sed 's/"unconnected_nodes":1/"unconnected_nodes":0/' "$root/decision.bak" > "$head/planning/decisions/$id/decision.json"
"$HYDRA_BIN" workflow plan decision show planner "$id" --json > "$root/tampered.json" 2>&1
assert_failure $? 'a changed record is refused'
grep -q 'invalid_decision' "$root/tampered.json"
assert_success $? 'the refusal names the invalid decision'
cp "$root/decision.bak" "$head/planning/decisions/$id/decision.json"
sed 's/"timeout":30/"timeout":31/' "$head/planning/decisions/$id/inputs/$right_sha.json" > "$root/input.changed"
cp "$head/planning/decisions/$id/inputs/$right_sha.json" "$root/input.bak"
cp "$root/input.changed" "$head/planning/decisions/$id/inputs/$right_sha.json"
"$HYDRA_BIN" workflow plan decision show planner "$id" --json > "$root/diverged.json"
grep -q '"status":"diverged"' "$root/diverged.json"
assert_success $? 'a changed input is reported as a diverged replay'
cp "$root/input.bak" "$head/planning/decisions/$id/inputs/$right_sha.json"

printf '{"schema_version":1,"plans":{"%s":{"source":"test","cost_unit":"USD","steps":{"compose":{"milliseconds":[200000,300000]}}}}}\n' "$left_sha" > "$root/over.json"
"$HYDRA_BIN" workflow plan decision record planner --select "$root/left.json" 'x' --reject "$root/right.json" 'y' --estimates "$root/over.json" --json > "$root/over.out"
assert_failure $? 'an estimate above the hard budget cannot be selected'
grep -q '"code":"estimate_budget_conflict"' "$root/over.out"
assert_success $? 'the refusal says estimates do not relax budgets'
assert_equal 1 "$(find "$head/planning/decisions" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')" 'a refused decision leaves no record'
printf '{"schema_version":1,"plans":{"%s":{"source":"test","cost_unit":"USD","steps":{"compose":{"milliseconds":[10,20]}}}}}\n' "$left_sha" > "$root/under.json"
"$HYDRA_BIN" workflow plan decision record planner --select "$root/left.json" 'x' --reject "$root/right.json" 'y' --estimates "$root/under.json" --json > "$root/under.out"
assert_success $? 'a decision records with estimates'
id2="$(sed -n 's/.*"decision_id":"\([0-9a-f]\{16\}\)".*/\1/p' "$root/under.out")"
grep -q '"hard_budgets":{"artifact_bytes":4096,"disk_mb":1,"effects":\["worktree","execute"\],"hosts":\["local"\],"max_heads":1,"parallelism":1,"repair_budget":0,"retry_budget":0,"timeout_seconds":180,"tools":\["sh"\],"writes":\[\]}' "$head/planning/decisions/$id2/decision.json"
assert_success $? 'hard budgets are recorded unchanged beside the estimates'
test -f "$head/planning/decisions/$id2/inputs/estimates.json"
assert_success $? 'the estimates input is recorded'

"$HYDRA_BIN" workflow plan decision record planner --select "$root/left.json" >/dev/null 2>&1
assert_failure $? 'a selection without a reason is a usage error'
"$HYDRA_BIN" workflow plan decision show planner notahexid >/dev/null 2>&1
assert_failure $? 'an invalid decision ID is refused'
"$HYDRA_BIN" workflow plan decision record missing --select "$root/left.json" 'x' >/dev/null 2>&1
assert_failure $? 'an unknown head is refused'
"$HYDRA_BIN" workflow plan observed run_00000000000000000000 >/dev/null 2>&1
assert_failure $? 'observed refuses an unknown run'

echo ""
echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
