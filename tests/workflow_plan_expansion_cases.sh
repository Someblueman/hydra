#!/bin/sh
# Expansion cases sourced by tests/test_workflow_plan_expansion.sh after the
# base run succeeded. Uses: $HYDRA_BIN, $ROOT, $run, $base_sha, sha_of,
# code_of, run_dir_of and the assertion helpers.
# shellcheck disable=SC2154

# proposal <file> <base-sha> <n> <claims-json> <input> <dependency> <deliverable> <requirement> <criterion> <check> <definition> [<work-argv-json>]
# Writes a one-branch expansion: spawn -> [work ->] compose (the join) -> verify.
proposal() {
    _p_file="$1" _p_base="$2" _p_n="$3" _p_claims="$4" _p_input="$5" _p_dependency="$6" _p_deliverable="$7"
    _p_requirement="$8" _p_criterion="$9"
    shift 9
    _p_check="$1" _p_definition="$2" _p_work="${3:-}"
    _p_head="plan-expansion-$_p_n"
    if [ -n "$_p_work" ]; then
        _p_work_step=",{\"id\":\"work$_p_n\",\"role\":\"work\",\"kind\":\"exec\",\"needs\":[\"spawn$_p_n\"],\"writes\":[],\"args\":{\"head\":\"$_p_head\",\"argv\":$_p_work,\"timeout\":30}}"
        _p_compose_needs="[\"work$_p_n\"]"
    else
        _p_work_step="" _p_compose_needs="[\"spawn$_p_n\"]"
    fi
    cat > "$_p_file" <<EOF
{"schema_version":1,"expansion":"hydra-plan-expansion-1","base_sha256":"$_p_base",
 "claims":$_p_claims,
 "dependencies":[{"input":"$_p_input","deliverable":"$_p_dependency"}],
 "joins":["compose$_p_n"],
 "steps":[{"id":"spawn$_p_n","role":"work","kind":"spawn","needs":[],"writes":[],"args":{"branch":"$_p_head"}}$_p_work_step,
  {"id":"compose$_p_n","role":"compose","kind":"exec","needs":$_p_compose_needs,"writes":[],"args":{"head":"$_p_head","argv":["sh","expansion-compose.sh"],"timeout":30}},
  {"id":"verify$_p_n","role":"verify","kind":"exec","needs":["compose$_p_n"],"writes":[],"args":{"head":"$_p_head","argv":["sh","expansion-check.sh","$_p_requirement"],"timeout":30}}],
 "deliverables":[{"id":"$_p_deliverable","description":"Report of round $_p_n","step":"compose$_p_n","output":"report","destination":"run-artifact"}],
 "requirements":[{"id":"$_p_requirement","criterion":"$_p_criterion","deliverable":"$_p_deliverable","check":"$_p_check"}],
 "checks":[{"id":"$_p_check","method":"executable","definition":"$_p_definition","step":"verify$_p_n","input":"subject","report":"check","deliverable":"$_p_deliverable"}],
 "data":{"steps":{"compose$_p_n":{"inputs":{"base":{"input":"$_p_input"}},"outputs":{"report":{"type":"file","path":"report.txt","max_bytes":1024}}},
  "verify$_p_n":{"inputs":{"subject":{"step":"compose$_p_n","output":"report"},"expected":{"input":"$_p_input"}},"outputs":{"check":{"type":"object","path":"check.json","max_bytes":2048}}}}}}
EOF
}
expand_cli() { "$HYDRA_BIN" workflow plan expand plan-smoke --run "$run" "$@"; }
refuse() {
    expand_cli --proposal "$1" --json > "$ROOT/refusal.json" 2>&1
    assert_failure $? "$2"
    assert_equal "$3" "$(code_of "$ROOT/refusal.json")" "$2 is typed $3"
}

# Round 1: a new claim consuming the sealed base deliverable.
proposal "$ROOT/round1.json" "$base_sha" 1 '[{"id":"second","kind":"new"}]' base report report2 second 'Second report equals the base report' check2 'Compare with base report'
expand_cli --proposal "$ROOT/round1.json" --json > "$ROOT/admit1.json"
assert_success $? 'round 1 is admitted under the base policy'
grep -q '"data":{"round":1,' "$ROOT/admit1.json"
assert_success $? 'admission reports the round number'
sha1="$(grep -o '"compiled_sha256":"[a-f0-9]*"' "$ROOT/admit1.json" | sed -n '1p' | sed 's/.*://; s/"//g')"
# JSON escapes the slashes of the reported path.
compiled1="$(grep -o '"compiled":"[^"]*"' "$ROOT/admit1.json" | sed -n '1p' | sed 's/^"compiled":"//; s/"$//; s|\\/|/|g')"
[ -f "$compiled1" ] && [ "${#sha1}" -eq 64 ]
assert_success $? 'the admitted round has a compiled artifact and digest'
chain_dir="$(dirname "$compiled1")"
expansions="$(dirname "$chain_dir")"
[ "$(basename "$expansions")" = "$run" ] && [ -f "$chain_dir/expansion.json" ] && [ -f "$chain_dir/proposal.json" ]
assert_success $? 'the round lives under the head planning state of the base run'
assert_equal 700 "$(stat -c %a "$chain_dir" 2>/dev/null || stat -f %Lp "$chain_dir")" 'round directories are private'
assert_equal 600 "$(stat -c %a "$chain_dir/expansion.json" 2>/dev/null || stat -f %Lp "$chain_dir/expansion.json")" 'expansion records are private'
grep -q '"effects":"artifact_only"' "$chain_dir/expansion.json" && grep -q '"joins":{"compose1":\["spawn1"\]}' "$chain_dir/expansion.json"
assert_success $? 'the record freezes the join membership and names the dependency effects'
"$HYDRA_BIN" workflow plan show "$compiled1" --json > "$ROOT/show1.json"
assert_success $? 'the round compiled artifact is an ordinary reviewable plan'
assert_equal "$sha1" "$(sha_of "$ROOT/show1.json")" 'the admitted digest is the artifact digest the user reviews'
grep -q '"assets":{"base":"Delivered report\\n"}' "$ROOT/show1.json"
assert_success $? 'the sealed base bytes travel inside the round artifact'
expand_cli --json > "$ROOT/chain1a.json"; expand_cli --json > "$ROOT/chain1b.json"
cmp -s "$ROOT/chain1a.json" "$ROOT/chain1b.json"
assert_success $? 'the chain view replays byte for byte over unchanged state'
grep -q "\"latest_sha256\":\"$sha1\",\"rounds\":1,\"integrity\":\"intact\"" "$ROOT/chain1a.json"
assert_success $? 'round 1 is the latest version of the chain'
grep -q '"run_state":"not_executed"' "$ROOT/chain1a.json"
assert_success $? 'an admitted round is not executed by admission'

# Refusals before any execution.
refuse "$ROOT/round1.json" 'a proposal bound to a superseded version is refused' stale_base
proposal "$ROOT/restated.json" "$sha1" 2 '[{"id":"again","kind":"new"}]' base report report3 again 'Report has the expected contents' check3 'Other rule'
refuse "$ROOT/restated.json" 'restating the base claim as a new claim is refused' claim_restated
proposal "$ROOT/changed.json" "$sha1" 2 '[{"id":"again","kind":"retry","of":"content"}]' base report report3 again 'Report has different contents' check3 'Compare expected text'
refuse "$ROOT/changed.json" 'a changed claim is not a retry' claim_changed
proposal "$ROOT/verified.json" "$sha1" 2 '[{"id":"again","kind":"retry","of":"content"}]' base report report3 again 'Report has the expected contents' check3 'Compare expected text'
refuse "$ROOT/verified.json" 'retrying a verified claim duplicates verified work' duplicate_work
proposal "$ROOT/unexecuted.json" "$sha1" 2 '[{"id":"third","kind":"new"}]' base2 report2 report3 third 'Third report equals the second report' check3 'Compare with second report'
refuse "$ROOT/unexecuted.json" 'depending on a deliverable of an unexecuted round is refused' unknown_outcome
proposal "$ROOT/enlarged.json" "$sha1" 2 '[{"id":"third","kind":"new"}]' base report report3 third 'Third report equals the base report' check3 'Compare with base report'
sed 's/"data":{"steps"/"envelope":{"hosts":["local"],"tools":["sh","make"],"effects":["worktree","execute"],"writes":[],"parallelism":1,"timeout_seconds":180,"artifact_bytes":4096,"max_heads":1,"disk_mb":1,"retry_budget":0,"repair_budget":0},"data":{"steps"/' "$ROOT/enlarged.json" > "$ROOT/enlarged2.json"
refuse "$ROOT/enlarged2.json" 'an expansion envelope that adds a tool is refused' authority_enlarged
printf '{"schema_version":1}\n' > "$ROOT/empty.json"
refuse "$ROOT/empty.json" 'a document outside the closed schema is refused' invalid_expansion
state_file="$(run_dir_of "$run")/state"
cp "$state_file" "$ROOT/state.keep"; printf 'running\n' > "$state_file"
refuse "$ROOT/enlarged.json" 'a base run without a recorded terminal outcome blocks dependents' unknown_outcome
expand_cli --json > "$ROOT/unknown.json"
grep -q '"round":0,"compiled_sha256":"[a-f0-9]*","run_id":"[^"]*","run_state":"unknown"' "$ROOT/unknown.json" && grep -q '"id":"content","kind":"new","of":null,"deliverable":"report","status":"unknown"' "$ROOT/unknown.json"
assert_success $? 'the chain view reports the unknown outcome instead of guessing'
cp "$ROOT/state.keep" "$state_file"

# Execution of round 1 needs its exact admitted digest and happens once.
"$HYDRA_BIN" workflow plan run "$compiled1" --accept "$base_sha" > "$ROOT/wrong.out" 2>&1
assert_failure $? 'a round refuses another accepted digest'
"$HYDRA_BIN" workflow plan run "$compiled1" --accept "$sha1" > "$ROOT/run1.out" 2> "$ROOT/run1.err"
assert_success $? 'round 1 runs through the ordinary exact-digest path'
run1="$(sed -n '1p' "$ROOT/run1.out")"
assert_equal "$run1" "$(sed -n '1p' "$chain_dir/run-id")" 'the round records its run'
cmp -s "$chain_dir/expansion.json" "$(run_dir_of "$run1")/plan-expansion.json"
assert_success $? 'the run records the round that admitted it'
cmp -s "$(run_dir_of "$run1")/steps/compose1/attempt-1/artifacts/report" "$(run_dir_of "$run")/steps/compose/attempt-1/artifacts/report"
assert_success $? 'the round consumed the exact sealed base bytes'
"$HYDRA_BIN" workflow plan result "$run1" > "$ROOT/result1.json"
assert_success $? 'the round result is verified by the public result gate'
"$HYDRA_BIN" workflow plan run "$compiled1" --accept "$sha1" > "$ROOT/again.out" 2> "$ROOT/again.err"
assert_failure $? 'a round executes once and is never replayed'
grep -q '"code":"round_executed"' "$ROOT/again.err"
assert_success $? 'the second execution is a typed refusal'
if [ ! -s "$ROOT/again.out" ]; then empty_status=0; else empty_status=1; fi
assert_success "$empty_status" 'the refused execution creates no run'
expand_cli --json > "$ROOT/chain1.json"
grep -q '"id":"second","kind":"new","of":null,"deliverable":"report2","status":"pass"' "$ROOT/chain1.json" && grep -q '"status":"intact"' "$ROOT/chain1.json"
assert_success $? 'the executed round shows a passing claim and an intact dependency'

# Round 2: a failing branch never reaches its frozen join; nothing completes falsely.
proposal "$ROOT/round2.json" "$sha1" 2 '[{"id":"third","kind":"new"}]' base2 report2 report3 third 'Third report equals the second report' check3 'Compare with second report' '["sh","fail.sh"]'
expand_cli --proposal "$ROOT/round2.json" > "$ROOT/admit2.txt"
assert_success $? 'round 2 is admitted with a dependency on the round 1 deliverable'
grep -q '^Expansion round 2 admitted' "$ROOT/admit2.txt" && grep -q 'join compose2 <- work2' "$ROOT/admit2.txt"
assert_success $? 'the text admission prints the chain with the frozen join'
sha2="$(sed -n 's/^Round 2: [a-f0-9]*, not executed; run: hydra workflow plan run .* --accept \([a-f0-9]\{64\}\)$/\1/p' "$ROOT/admit2.txt")"
compiled2="$expansions/round-2/compiled.json"
[ "${#sha2}" -eq 64 ] && [ -f "$compiled2" ]
assert_success $? 'the text view names the run command of the new round'
"$HYDRA_BIN" workflow plan run "$compiled2" --accept "$sha2" > "$ROOT/run2.out" 2> "$ROOT/run2.err"
assert_failure $? 'a failing branch fails the round run'
run2="$(sed -n '1p' "$ROOT/run2.out")"
"$HYDRA_BIN" workflow status "$run2" --json > "$ROOT/status2.json"
grep -q '"state":"failed"' "$ROOT/status2.json" && grep -q '"step_id":"compose2","state":"cancelled","attempts":0' "$ROOT/status2.json"
assert_success $? 'the join behind the failed branch never ran'
[ ! -f "$(run_dir_of "$run2")/delivery.json" ]
assert_success $? 'a skipped join produces no delivery'
expand_cli --json > "$ROOT/chain2.json"
grep -q '"id":"third","kind":"new","of":null,"deliverable":"report3","status":"fail"' "$ROOT/chain2.json"
assert_success $? 'the chain records the failed claim as failed, not unknown'
proposal "$ROOT/onfailed.json" "$sha2" 3 '[{"id":"fourth","kind":"new"}]' base3 report3 report4 fourth 'Fourth question' check4 'Compare with third report'
refuse "$ROOT/onfailed.json" 'a failed deliverable is no input for later work' dependency_failed

# Round 3: the failed claim is retried once, unchanged, within its repair budget.
proposal "$ROOT/round3.json" "$sha2" 3 '[{"id":"third-retry","kind":"retry","of":"third"}]' base2 report2 report4 third-retry 'Third report equals the second report' check4 'Compare with second report'
expand_cli --proposal "$ROOT/round3.json" --json > "$ROOT/admit3.json"
assert_success $? 'a retry of the failed claim is admitted'
grep -q '"claims":\[{"id":"third-retry","kind":"retry","of":"third","repair_attempt":1}\]' "$ROOT/admit3.json"
assert_success $? 'the retry consumes the original claim repair budget'
sha3="$(grep -o '"compiled_sha256":"[a-f0-9]*"' "$ROOT/admit3.json" | sed -n '1p' | sed 's/.*://; s/"//g')"
"$HYDRA_BIN" workflow plan run "$expansions/round-3/compiled.json" --accept "$sha3" > "$ROOT/run3.out" 2> "$ROOT/run3.err"
assert_success $? 'the retry round runs to a verified result'
expand_cli --json > "$ROOT/chain3.json"
grep -q '"id":"third-retry","kind":"retry","of":"third","repair_attempt":1,"deliverable":"report4","status":"pass"' "$ROOT/chain3.json"
assert_success $? 'the retried claim is recorded as passing in its own version'
proposal "$ROOT/exhausted.json" "$sha3" 4 '[{"id":"third-again","kind":"retry","of":"third"}]' base2 report2 report5 third-again 'Third report equals the second report' check5 'Compare with second report'
refuse "$ROOT/exhausted.json" 'a second retry exceeds the per-claim repair budget' expansion_bound_exceeded
grep -q '"bound":"max_repairs_per_claim"' "$ROOT/refusal.json"
assert_success $? 'the exhausted budget names its bound'
proposal "$ROOT/ofretry.json" "$sha3" 4 '[{"id":"third-again","kind":"retry","of":"third-retry"}]' base2 report2 report5 third-again 'Third report equals the second report' check5 'Compare with second report'
refuse "$ROOT/ofretry.json" 'a retry names the original claim, not an earlier retry' retry_of_retry

# Round 4: an interrupted admission leaves only scratch; the next admission continues the chain.
mkdir "$expansions/.round.interrupted" && printf '{}' > "$expansions/.round.interrupted/proposal.json"
proposal "$ROOT/round4.json" "$sha3" 4 '[{"id":"fourth","kind":"new"}]' base4 report4 report5 fourth 'Fourth report equals the retried report' check5 'Compare with retried report'
expand_cli --proposal "$ROOT/round4.json" --json > "$ROOT/admit4.json"
assert_success $? 'admission resumes after an interrupted one without replaying anything'
grep -q '"data":{"round":4,' "$ROOT/admit4.json"
assert_success $? 'the interrupted scratch directory did not consume a round'
sha4="$(grep -o '"compiled_sha256":"[a-f0-9]*"' "$ROOT/admit4.json" | sed -n '1p' | sed 's/.*://; s/"//g')"
proposal "$ROOT/round5.json" "$sha4" 5 '[{"id":"fifth","kind":"new"}]' base4 report4 report6 fifth 'Fifth question' check6 'Compare again'
refuse "$ROOT/round5.json" 'the round budget is exhausted' expansion_bound_exceeded
grep -q '"bound":"max_rounds"' "$ROOT/refusal.json"
assert_success $? 'the exhausted round budget names its bound'

# A changed round invalidates its admission and approval; restoring it restores both.
cp "$expansions/round-4/compiled.json" "$ROOT/compiled4.keep"
sed 's/Compare with retried report/Changed acceptance rule/' "$ROOT/compiled4.keep" > "$expansions/round-4/compiled.json"
expand_cli --json > "$ROOT/tampered.json"
grep -q '"integrity":"invalid"' "$ROOT/tampered.json" && grep -q '"code":"expansion_changed"' "$ROOT/tampered.json"
assert_success $? 'a changed round is reported as changed'
"$HYDRA_BIN" workflow plan run "$expansions/round-4/compiled.json" --accept "$sha4" > "$ROOT/tampered-run.out" 2>&1
assert_failure $? 'the admitted digest no longer admits the changed round'
"$HYDRA_BIN" workflow plan show "$expansions/round-4/compiled.json" --json > "$ROOT/tampered-show.json"
"$HYDRA_BIN" workflow plan run "$expansions/round-4/compiled.json" --accept "$(sha_of "$ROOT/tampered-show.json")" > "$ROOT/tampered-run2.out" 2> "$ROOT/tampered-run2.err"
assert_failure $? 'accepting the changed digest does not execute an unadmitted round'
grep -q '"code":"invalid_chain"' "$ROOT/tampered-run2.err"
assert_success $? 'the changed round is refused as a broken chain link'
cp "$ROOT/compiled4.keep" "$expansions/round-4/compiled.json"
expand_cli --json > "$ROOT/restored.json"
grep -q '"integrity":"intact"' "$ROOT/restored.json"
assert_success $? 'restoring the admitted bytes restores the chain'
# Changed dependency bytes invalidate exactly the round that consumed them.
report4="$(run_dir_of "$(sed -n '1p' "$expansions/round-3/run-id")")/steps/compose3/attempt-1/artifacts/report"
cp "$report4" "$ROOT/report4.keep"; printf 'tampered\n' > "$report4"
"$HYDRA_BIN" workflow plan run "$expansions/round-4/compiled.json" --accept "$sha4" > "$ROOT/dep-run.out" 2> "$ROOT/dep-run.err"
assert_failure $? 'a round whose sealed dependency changed does not execute'
grep -q '"code":"dependency_changed"' "$ROOT/dep-run.err"
assert_success $? 'the changed dependency is a typed refusal'
expand_cli --json > "$ROOT/dep-chain.json"
grep -q '"round":1,.*"dependencies":\[{[^]]*"status":"intact"' "$ROOT/dep-chain.json" && grep -q '"round":4,.*"status":"invalid"' "$ROOT/dep-chain.json"
assert_success $? 'only the round that consumed the changed bytes is invalidated'
cp "$ROOT/report4.keep" "$report4"
expand_cli > "$ROOT/final.txt"
assert_success $? 'the final chain view is readable'
grep -q "^Expansions of run $run: 4 admitted, integrity intact" "$ROOT/final.txt" && grep -q '^Bounds: nodes 1[0-9]/16 work_seconds [0-9]*/600 artifact_bytes [0-9]*/65536 rounds 4/4 repairs_per_claim 1$' "$ROOT/final.txt"
assert_success $? 'the text view totals the bounds over every version'
