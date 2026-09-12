#!/bin/sh
# Compare the public JSON and native text routes for exactly the same selection.
: "${run:?}" "${fixture:?}" "${reference:?}" "${project:?}" "${native:?}" "${digest:?}"
find "$HYDRA_STATE_V2_ROOT" -type f -exec shasum -a 256 {} \; | LC_ALL=C sort > "$fixture/state.before"
review_selection "$run" produce > "$fixture/result.row"
review_selection "$run" approval > "$fixture/request.row"
[ "$(wc -l < "$fixture/result.row" | tr -d ' ')" -eq 1 ]
[ "$(wc -l < "$fixture/request.row" | tr -d ' ')" -eq 1 ]
review_exact review "$fixture/result.row" "$reference" > "$fixture/exact.json"
review_exact review-data "$fixture/result.row" "$reference" > "$fixture/exact.tsv"
review_frame_valid "$fixture/exact.tsv"
jq -er '.ok and .data.readiness=="ready" and .data.identity.source=="workflow records"' "$fixture/exact.json" >/dev/null
jq -r '.data.identity | ["HYDRA_REVIEW","1",.source,.project_id,.host,.task_id,.run_id,.step_id,.attempt_id,.head_id,.current_instance,.request_id,.binding,.revision_sha256,.identity_sha256] | map(. // "-") | @tsv' "$fixture/exact.json" > "$fixture/expected.header"
sed -n '1p' "$fixture/exact.tsv" > "$fixture/actual.header"
cmp "$fixture/expected.header" "$fixture/actual.header"
grep -q '^TEXT[[:space:]]readiness: ready$' "$fixture/exact.tsv"
review_exact review "$fixture/request.row" > "$fixture/exact-request.json"
review_exact review-data "$fixture/request.row" > "$fixture/exact-request.tsv"
review_frame_valid "$fixture/exact-request.tsv"
jq -e '.ok and .data.readiness=="pending" and .data.request.state=="pending" and .data.request.context_state=="current" and .data.accepted==false' "$fixture/exact-request.json" >/dev/null
# A stale revision is echoed only in the header; the computed body revokes it.
awk -F '\t' 'BEGIN {OFS="\t"} {$15="0000000000000000000000000000000000000000000000000000000000000000"; print}' "$fixture/result.row" > "$fixture/stale.row"
review_exact review-data "$fixture/stale.row" > "$fixture/stale.tsv"
review_frame_valid "$fixture/stale.tsv"
grep -q '^TEXT[[:space:]]readiness: revoked$' "$fixture/stale.tsv"
[ "$(cut -f14 "$fixture/stale.tsv" | sed -n '1p')" = 0000000000000000000000000000000000000000000000000000000000000000 ]
# Invalid references with a valid selected tuple still yield exactly one failed
# text document ending at END, never trailing JSON.
if review_exact review-data "$fixture/result.row" '[{"kind":"log","locator":"ftp://invalid"}]' > "$fixture/error.tsv"; then exit 1; fi
review_frame_valid "$fixture/error.tsv"
grep -q '^TEXT[[:space:]]error: code: invalid_reference$' "$fixture/error.tsv"
# Canonically valid but unobserved request/kind/binding tuples cannot alias a
# result sharing the same run/step/attempt. Recompute the canonical identity.
for field in 3 6 7 11 12 13 14; do
    replacement=unobserved
    [ "$field" -ne 3 ] || replacement=approval
    [ "$field" -ne 14 ] || replacement=0000000000000000000000000000000000000000
    awk -F '\t' -v field="$field" -v replacement="$replacement" 'BEGIN {OFS="\t"} {$field=replacement; print}' "$fixture/result.row" > "$fixture/wrong.row"
    awk -F '\t' '{printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s",$2,$3,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14}' "$fixture/wrong.row" > "$fixture/wrong.identity"
    wrong_hash="$(shasum -a 256 "$fixture/wrong.identity" | awk '{print $1}')"
    awk -F '\t' -v hash="$wrong_hash" 'BEGIN {OFS="\t"} {$16=hash; print}' "$fixture/wrong.row" > "$fixture/wrong-final.row"
    if review_exact review-data "$fixture/wrong-final.row" > "$fixture/wrong.tsv"; then exit 1; fi
    review_frame_valid "$fixture/wrong.tsv"
    grep -q '^TEXT[[:space:]]error: code: candidate_unavailable$' "$fixture/wrong.tsv"
done
# Opening either route leaves all durable records unchanged; decision is a
# distinct public command exercised later by the main test.
# A corrupted link to the real pending request creates two observed candidates
# with the same run/step/attempt. Exact result identity still selects one; the
# convenience tuple is explicitly ambiguous.
projection_run="$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$run"
projection_request="$(cat "$projection_run/steps/approval/request-id")"
cp "$projection_run/approvals/$projection_request/step-id" "$fixture/request-step.saved"
printf 'produce\n' > "$projection_run/approvals/$projection_request/step-id"
cp "$projection_run/steps/approval/request-id" "$projection_run/steps/produce/request-id"
"$native" workflow-data attention "$project" > "$fixture/ambiguous-attention.json"
jq -e --arg run "$run" '[.data.items[] | select(.run_id==$run and .step_id=="produce" and .attempt_id=="attempt-1")] | length==2' "$fixture/ambiguous-attention.json" >/dev/null
review_exact review "$fixture/result.row" > "$fixture/exact-among-candidates.json"
jq -e '.ok and .data.readiness=="ready" and .data.identity.kind=="result"' "$fixture/exact-among-candidates.json" >/dev/null
if "$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/ambiguous-review.json"; then exit 1; fi
jq -e '.ok==false and .error.code=="candidate_unavailable"' "$fixture/ambiguous-review.json" >/dev/null
rm "$projection_run/steps/produce/request-id"
mv "$fixture/request-step.saved" "$projection_run/approvals/$projection_request/step-id"
find "$HYDRA_STATE_V2_ROOT" -type f -exec shasum -a 256 {} \; | LC_ALL=C sort > "$fixture/state.after"
cmp "$fixture/state.before" "$fixture/state.after"
