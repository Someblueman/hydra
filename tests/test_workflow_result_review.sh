#!/bin/sh
# A completed agent plan run: attention names only its results, the review
# explains what was produced and checked, statistics carry exec-receipt usage,
# and per-user seen markers persist. All routes are read-only except seen.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
repo="$fixture/repo"
native="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test or make test-one T=<name>}"
[ -x "$native" ]
export HYDRA_HOME="$fixture/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
export HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2" HYDRA_FLEET_BIN="$native"
cleanup() {
    for branch in kill-dry-worker kill-dry-verifier; do
        (cd "$repo" && "$root/bin/hydra" kill "$branch" --force) >/dev/null 2>&1 || :
    done
    if [ "${passed:-0}" = 1 ]; then rm -rf "$fixture"; else printf 'Result review evidence: %s\n' "$fixture" >&2; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
hydra() { (cd "$repo" && "$root/bin/hydra" "$@"); }
tab="$(printf '\t')"

mkdir -p "$repo" "$fixture/assets"
cp "$root/tests/fixtures/result-review/verify.sh" "$fixture/assets/verify"
printf '{"schema_version":1,"executable":"%s","argv":["exec","--json","-"],"resume_argv":["exec","resume","--json",{"input":"session_id"},"-"],"prompt":"stdin","adapter":"codex-jsonl","session":"observed","probe_argv":["--help"],"probe_tokens":["exec"]}\n' \
    "$root/tests/fixtures/result-review/worker.sh" > "$fixture/profile.json"
"$root/bin/hydra" agent import fixture-codex "$fixture/profile.json" >/dev/null
git -C "$repo" init -q
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name test
printf 'readme\n' > "$repo/README.md"
git -C "$repo" add README.md
git -C "$repo" -c commit.gpgSign=false commit -qm base
hydra init --no-agent --trust >/dev/null
hydra workflow plan compile "$root/tests/fixtures/result-review/plan.json" "$root/tests/fixtures/result-review/policy.json" \
    "$fixture/compiled.json" --assets-dir "$fixture/assets" > "$fixture/compile.json"
digest="$(jq -r '.data.sha256' "$fixture/compile.json")"
hydra workflow plan run "$fixture/compiled.json" --accept "$digest" > "$fixture/run.out"
run="$(sed -n '1p' "$fixture/run.out")"
project="$(find "$HYDRA_STATE_V2_ROOT/projects" -mindepth 1 -maxdepth 1 -name 'project_*' -exec basename {} \;)"
run_dir="$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$run"
[ "$(cat "$run_dir/state")" = succeeded ]

# Attention: spawn steps declare no outputs, so they are not results and the
# snapshot is complete. Wire version 2 carries the plan name as a label and
# version 3 a detail field ("-" for a result).
hydra workflow attention-data > "$fixture/attention.tsv"
[ "$(sed -n '1p' "$fixture/attention.tsv")" = "HYDRA_ATTENTION${tab}3" ]
[ "$(awk -F '\t' '$1=="ITEM" && (NF!=21 || $21!="-") {n++} END {print n+0}' "$fixture/attention.tsv")" -eq 0 ]
[ "$(tail -n 1 "$fixture/attention.tsv")" = "END${tab}2${tab}0${tab}0" ]
[ "$(awk -F '\t' '$1=="ITEM" && ($3!="result" || $4!="result_ready" || $20!="kill-dry-run") {n++} END {print n+0}' "$fixture/attention.tsv")" -eq 0 ]
[ "$(awk -F '\t' '$1=="ITEM" {print $9}' "$fixture/attention.tsv" | LC_ALL=C sort | tr '\n' ' ')" = "implement verify " ]
if grep -q 'spawn-' "$fixture/attention.tsv"; then exit 1; fi
hydra workflow attention --json > "$fixture/attention.json"
jq -e '.data.partial == false and .data.truncated == false and all(.data.items[]; .label == "kill-dry-run")' "$fixture/attention.json" >/dev/null
spawn_revision="$(printf '%064d' 0)"
if "$native" workflow-review "$project" "$run" spawn-worker attempt-1 "$spawn_revision" > "$fixture/spawn-review.json"; then exit 1; fi
jq -e '.error.code == "candidate_unavailable"' "$fixture/spawn-review.json" >/dev/null

select_row() { awk -F '\t' -v step="$1" '$1=="ITEM" && $9==step' "$fixture/attention.tsv"; }
review() (
    IFS="$tab" read -r _tag _source kind _reason rproject host task rrun step attempt head instance request binding revision identity _rest <<EOF
$2
EOF
    hydra workflow "$1" "$kind" "$rproject" "$host" "$task" "$rrun" "$step" "$attempt" "$head" "$instance" "$request" "$binding" "$revision" "$identity"
)
verify_row="$(select_row verify)"
implement_row="$(select_row implement)"
review review "$verify_row" > "$fixture/review.json"
jq -e '.ok and .data.readiness == "ready" and .data.accepted == false and .data.checks.state == "passed"' "$fixture/review.json" >/dev/null
jq -e '.data.result as $r | $r.workflow == "kill-dry-run" and $r.verdict == "pass" and $r.run_state == "succeeded"
    and ($r.deliverables[0].text | contains("Implemented hydra kill --dry-run")) and $r.deliverables[0].state == "verified"
    and ([$r.requirements[].state] == ["pass","pass"])
    and $r.checks[0].argv == ["sh","@input/verify"] and $r.checks[0].head == "kill-dry-verifier" and $r.checks[0].exit_code == "0"
    and $r.checks[0].verified and ($r.checks[0].evidence | contains("2 passed"))
    and $r.checks[0].summary.counts.pass == 2 and ($r.checks[0].summary.lines | index("Passed: 2  Failed: 0  Total: 2")) != null
    and (($r.checks[0].summary.lines | map(select(contains("unrelated")))) | length) == 0
    and ($r.checks[0].log | endswith("/stdout"))' "$fixture/review.json" >/dev/null
jq -e '.data.result.steps | map({(.id): .}) | add | .implement.agent as $a
    | $a.profile == "fixture-codex" and $a.executable_version == "codex-cli 0.99.0-fixture"
    and $a.tokens_in == 3850000 and $a.tokens_cached == 3740000 and $a.tokens_out == 23000
    and $a.cost_usd == null and $a.model == null and ."spawn-worker".agent == null
    and (."spawn-worker".seconds | type) == "number" and .verify.attempts == "1"' "$fixture/review.json" >/dev/null
jq -e '.data.result.changes as $c | $c.state == "observed" and $c.branch == "kill-dry-worker"
    and ($c.commits | length) == 2 and ($c.commits | map(.subject) | index("feat(kill): add --dry-run preview")) != null
    and ($c.files | map(.path) | sort) == ["README.md","lib/kill_dry.sh"]
    and ($c.diff | contains("+hydra kill --dry-run previews the targets")) and $c.uncommitted_changes == false
    and (.data.result.next.land | endswith("git merge --no-ff kill-dry-worker"))
    and .data.result.next.cleanup == ["hydra kill kill-dry-worker","hydra kill kill-dry-verifier"]' "$fixture/review.json" >/dev/null
review review-data "$verify_row" > "$fixture/review.tsv"
for text in 'RESULT  kill-dry-run  -  verdict PASS (verified)  -  run succeeded' 'WHAT WAS PRODUCED' \
    '  PASS    preview-targets - Dry run lists targets (check: check)' "    Ran: sh @input/verify  (@input/NAME is the run's sealed copy of that input)" \
    '      Passed: 2  Failed: 0  Total: 2' '      tokens in 3.85M, cached 3.74M, out 23.0k; cost not reported' \
    'CHANGES ON kill-dry-worker  (2 commits, 2 files, +4 -0; the branch as it is now)' 'DIFF  (complete)' \
    '  Land the worker branch yourself; Hydra never merges for you:' 'TECHNICAL DETAILS  (exact identity and retained-contract checks)' \
    'readiness: ready'; do
    grep -Fqx "TEXT$tab$text" "$fixture/review.tsv" || { printf 'missing review line: %s\n' "$text" >&2; exit 1; }
done
[ "$(awk -F '\t' '$1=="REF" && $2=="log" && $3=="available" {n++} END {print n+0}' "$fixture/review.tsv")" -eq 1 ]
if grep -q 'no recorded diff projection' "$fixture/review.tsv"; then exit 1; fi

# While the run is still active, a sealed result waits for the delivery gate:
# checks are pending, never failed, and requirements are pending.
cp "$run_dir/state" "$fixture/state.saved"
mv "$run_dir/steps/verify/attempt-1/artifacts/check" "$fixture/check.saved"
printf 'running\n' > "$run_dir/state"
printf 'running\n' > "$run_dir/steps/verify/state"
hydra workflow attention-data > "$fixture/running.tsv"
running_row="$(awk -F '\t' '$1=="ITEM" && $9=="implement"' "$fixture/running.tsv")"
review review "$running_row" > "$fixture/running.json"
jq -e '.data.checks.state == "pending" and .data.readiness == "in_progress" and .data.candidate_state == "run_in_progress"
    and .data.result.verdict == "pending" and ([.data.result.requirements[].state] | unique) == ["pending"]' "$fixture/running.json" >/dev/null
# A check step that failed before sealing a report has decided: its
# requirements fail rather than stay pending once the run has failed.
printf 'failed\n' > "$run_dir/state"
printf 'failed\n' > "$run_dir/steps/verify/state"
review review "$running_row" > "$fixture/failed.json"
jq -e '.data.result.verdict == "fail" and ([.data.result.requirements[].state] | unique) == ["fail"]' "$fixture/failed.json" >/dev/null
review review-data "$running_row" > "$fixture/failed.tsv"
grep -Fqx "TEXT$tab  FAIL    preview-targets - Dry run lists targets (check: check)" "$fixture/failed.tsv"
# The failed check is its own attention item: it names the requirements the
# check decides, and its review shows the failed verdict and requirements.
hydra workflow attention-data > "$fixture/failure.tsv"
failure_row="$(awk -F '\t' '$1=="ITEM" && $3=="failure"' "$fixture/failure.tsv")"
[ "$(printf '%s\n' "$failure_row" | grep -c .)" -eq 1 ]
[ "$(printf '%s\n' "$failure_row" | cut -f4,9,10,18,19,20,21)" = \
    "check_failed${tab}verify${tab}attempt-1${tab}workflow-evidence${tab}1${tab}kill-dry-run${tab}preview-targets, no-mutation" ]
hydra workflow attention --json > "$fixture/failure.json"
jq -e '[.data.items[] | select(.kind == "failure")] | length == 1 and .[0].requirements == ["preview-targets","no-mutation"]
    and .[0].route.kind == "workflow-evidence" and .[0].accepted == false' "$fixture/failure.json" >/dev/null
review review "$failure_row" > "$fixture/failure-review.json"
jq -e '.ok and .data.identity.kind == "failure" and .data.readiness == "failed" and .data.candidate_state == "failed_needs_decision"
    and .data.accepted == false and .data.result.verdict == "fail"
    and ([.data.result.requirements[].state] | unique) == ["fail"]' "$fixture/failure-review.json" >/dev/null
review review-data "$failure_row" > "$fixture/failure-review.tsv"
grep -Fqx "TEXT$tab  FAIL    preview-targets - Dry run lists targets (check: check)" "$fixture/failure-review.tsv"
grep -Fqx "TEXT${tab}readiness: failed" "$fixture/failure-review.tsv"
# Marking it seen is a client preference: the failure stays listed.
hydra workflow attention-seen mark "$(printf '%s\n' "$failure_row" | cut -f16)" "$(printf '%s\n' "$failure_row" | cut -f15)" >/dev/null
[ "$(hydra workflow attention-data | awk -F '\t' '$1=="ITEM" && $3=="failure"' | grep -c .)" -eq 1 ]
hydra workflow attention-seen clear "$(printf '%s\n' "$failure_row" | cut -f16)" >/dev/null
# A later run of the same plan that succeeded resolves it.
later_run="run_$(printf '%032x' 1)"
cp -R "$run_dir" "$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$later_run"
printf '2999-01-01T00:00:00Z\n' > "$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$later_run/created-at"
cp "$fixture/state.saved" "$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$later_run/state"
cp "$fixture/state.saved" "$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$later_run/steps/verify/state"
[ "$(hydra workflow attention-data | awk -F '\t' '$1=="ITEM" && $3=="failure"' | grep -c .)" -eq 0 ]
rm -rf "$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$later_run"
# A run that failed with every step succeeded (its delivery was rejected) is
# one run-level failure, reviewable from the run's own records.
cp "$fixture/state.saved" "$run_dir/steps/verify/state"
mv "$fixture/check.saved" "$run_dir/steps/verify/attempt-1/artifacts/check"
hydra workflow attention-data > "$fixture/run-failure.tsv"
run_failure_row="$(awk -F '\t' '$1=="ITEM" && $3=="failure"' "$fixture/run-failure.tsv")"
[ "$(printf '%s\n' "$run_failure_row" | cut -f4,9,10)" = "run_failed${tab}-${tab}-" ]
review review "$run_failure_row" > "$fixture/run-failure-review.json"
jq -e '.ok and .data.identity.kind == "failure" and .data.identity.step_id == "-" and .data.readiness == "failed"
    and .data.inventory_state == "not_applicable" and .data.result.run_state == "failed"' "$fixture/run-failure-review.json" >/dev/null
cp "$fixture/state.saved" "$run_dir/state"
cp "$fixture/state.saved" "$run_dir/steps/verify/state"
[ "$(hydra workflow attention-data | awk -F '\t' '$1=="ITEM" && $3=="failure"' | grep -c .)" -eq 0 ]
[ -n "$implement_row" ]

# A genuine unknown stays explicit, but a step without deliverables is never
# described as malformed declarations or failed checks.
printf '12\n' > "$run_dir/steps/spawn-worker/authoritative-attempt"
hydra workflow attention --json > "$fixture/unknown.json"
jq -e '.data.partial == true and ([.data.items[] | select(.kind == "unknown" and .step_id == "spawn-worker" and .reason == "missing_authoritative_attempt")] | length) == 1' "$fixture/unknown.json" >/dev/null
hydra workflow attention-data > "$fixture/unknown.tsv"
unknown_row="$(awk -F '\t' '$1=="ITEM" && $9=="spawn-worker"' "$fixture/unknown.tsv")"
review review "$unknown_row" > "$fixture/unknown-review.json"
jq -e '.ok and .data.inventory_state == "not_applicable" and .data.checks.state != "failed" and .data.identity.kind == "unknown"' "$fixture/unknown-review.json" >/dev/null
printf '1\n' > "$run_dir/steps/spawn-worker/authoritative-attempt"

# Statistics schema 4 carries the exec receipt evidence for the agent step.
hydra workflow statistics-data > "$fixture/statistics.tsv"
[ "$(sed -n '1p' "$fixture/statistics.tsv" | cut -f1-2)" = "HYDRA_STATISTICS${tab}4" ]
grep -Fqx "U${tab}$run${tab}implement${tab}fixture-codex${tab}codex-cli 0.99.0-fixture${tab}-${tab}-${tab}3850000${tab}3740000${tab}23000${tab}-" "$fixture/statistics.tsv"
[ "$(grep -c "^U$tab" "$fixture/statistics.tsv")" -eq 1 ]

# Seen markers: per user under HYDRA_HOME, keyed by identity and revision.
seen_file="$HYDRA_HOME/attention/seen.tsv"
verify_identity="$(printf '%s\n' "$verify_row" | cut -f16)"
verify_revision="$(printf '%s\n' "$verify_row" | cut -f15)"
hydra workflow attention-seen list > "$fixture/seen-empty"
[ "$(cat "$fixture/seen-empty")" = "$(printf 'HYDRA_ATTENTION_SEEN\t1\nEND\t0')" ]
hydra workflow attention-seen mark "$verify_identity" "$verify_revision" > "$fixture/seen-marked"
grep -Fqx "SEEN$tab$verify_identity$tab$verify_revision" "$fixture/seen-marked"
other_revision="$(printf '%064d' 7)"
hydra workflow attention-seen mark "$verify_identity" "$other_revision" > "$fixture/seen-replaced"
[ "$(grep -c "^SEEN$tab" "$fixture/seen-replaced")" -eq 1 ]
grep -Fqx "SEEN$tab$verify_identity$tab$other_revision" "$fixture/seen-replaced"
(cd "$fixture" && "$root/bin/hydra" workflow attention-seen list) > "$fixture/seen-outside"
cmp "$fixture/seen-replaced" "$fixture/seen-outside"
hydra workflow attention-seen clear "$verify_identity" > "$fixture/seen-cleared"
[ "$(tail -n 1 "$fixture/seen-cleared")" = "END${tab}0" ]
if hydra workflow attention-seen mark not-a-digest "$verify_revision" 2>/dev/null; then exit 1; fi
if hydra workflow attention-seen forget 2>/dev/null; then exit 1; fi
awk 'BEGIN {for (i = 1; i <= 600; i++) printf "%064x\t%064x\t%d\n", i, i, i}' > "$seen_file"
[ "$(hydra workflow attention-seen list | grep -c "^SEEN$tab")" -eq 512 ]
hydra workflow attention-seen mark "$verify_identity" "$verify_revision" >/dev/null
[ "$(wc -l < "$seen_file" | tr -d ' ')" -eq 512 ]
tail -n 1 "$seen_file" | grep -q "^$verify_identity$tab$verify_revision$tab"
# Seen never changes workflow state.
[ "$(cat "$run_dir/state")" = succeeded ]
passed=1
printf '%s\n' 'workflow result review, attention, statistics and seen markers passed'
