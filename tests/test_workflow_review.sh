#!/bin/sh
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# Sourced helpers are linted independently; require the setup's shared values.
# shellcheck source=/dev/null
. "$root/tests/fixtures/workflow-review/setup.sh"
: "${run:?}" "${attention:?}" "${fixture:?}" "${native:?}" "${project:?}" "${digest:?}" "${repo:?}"
: "${HYDRA_STATE_V2_ROOT:?}"
# Approval rows expose their exact durable request context through the same
# read-only review route, even though they cannot be ready evidence.
approval_row="$(jq -c --arg run "$run" '.data.items[] | select(.kind == "approval" and .run_id == $run and .step_id == "approval" and .attempt_id == "attempt-1")' "$attention")"
[ -n "$approval_row" ]
approval_revision="$(printf '%s' "$approval_row" | jq -r '.revision')"
printf '%s' "$approval_revision" > "$fixture/approval-revision"
approval_digest="$(shasum -a 256 "$fixture/approval-revision" | awk '{print $1}')"
HYDRA_FLEET_BIN="$native" "$root/bin/hydra" workflow review "$project" "$run" approval attempt-1 "$approval_digest" > "$fixture/approval-review" || { cat "$fixture/approval-review" >&2; exit 1; }
jq -e --arg run "$run" '.ok and .data.readiness == "pending" and (.data.request.request_id | length) > 0 and .data.request.state == "pending" and .data.request.message == "Review output" and .data.actions.request_route == ("hydra workflow requests " + $run + " --json") and .data.actions.approve_argv == ["hydra","workflow","decide",$run,.data.request.request_id,"approve"] and .data.actions.reject_argv == ["hydra","workflow","decide",$run,.data.request.request_id,"reject"] and .data.request.context_state == "current" and (.data.request.evidence_locator | endswith("/binding.tsv"))' "$fixture/approval-review" >/dev/null
reference="$(jq -cn --arg path "$fixture/run.out" '[{"kind":"log","locator":$path}]')"
review="$fixture/review.json"
HYDRA_FLEET_BIN="$native" "$root/bin/hydra" workflow review "$project" "$run" produce attempt-1 "$digest" "$reference" > "$review" || { cat "$review" >&2; exit 1; }
jq -e '.ok and .data.accepted == false and .data.readiness == "ready" and (.data.subjects.answer.sha256|length)==64 and (.data.references[0].preview|length)>0' "$review" >/dev/null
# shellcheck source=/dev/null
. "$root/tests/fixtures/workflow-review/projection.sh"
# shellcheck source=/dev/null
. "$root/tests/fixtures/workflow-review/manifest.sh"
if [ "${HYDRA_REVIEW_FIXTURE_ONLY:-0}" = 1 ]; then
    printf 'Review fixture manifest: %s/manifest.json\n' "$fixture"
    exit 0
fi
# shellcheck source=/dev/null
. "$root/tests/fixtures/workflow-review/approval.sh"
# The implicit HYDRA_HOME state root is equivalent to the explicit root, while
# a genuinely different configured root cannot resolve this immutable run.
env -u HYDRA_STATE_V2_ROOT HYDRA_HOME="$HYDRA_HOME" "$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/default-root"
jq -e '.ok and .data.readiness == "ready"' "$fixture/default-root" >/dev/null
set +e
HYDRA_STATE_V2_ROOT="$fixture/other-state" "$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/other-root"
other_status=$?
set -e
[ "$other_status" -ne 0 ]
jq -e '.ok == false' "$fixture/other-root" >/dev/null
# Current checkout movement does not alter retained workflow evidence.
printf changed > "$repo/current-only"
git -C "$repo" add current-only && git -C "$repo" commit -qm current-only
"$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/head-moved"
jq -e '.ok and .data.readiness == "ready"' "$fixture/head-moved" >/dev/null
run_dir="$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$run"
printf '2\n' > "$run_dir/steps/produce/attempts"
"$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/drift"
jq -e '.ok and .data.readiness == "revoked" and .data.checks.state == "unavailable"' "$fixture/drift" >/dev/null
printf '1\n' > "$run_dir/steps/produce/attempts"
printf '%5000s' x > "$fixture/large.log"
refs="$(jq -cn --arg p "$fixture/large.log" --arg m "$fixture/missing.log" '[{kind:"log",locator:$p},{kind:"transcript",locator:$m}]')"
"$native" workflow-review "$project" "$run" produce attempt-1 "$digest" "$refs" > "$fixture/refs"
jq -e '.data.references[0].state == "available" and (.data.references[0].preview|length) == 4096 and .data.references[1].state == "inaccessible"' "$fixture/refs" >/dev/null
if "$native" workflow-review "$project" "$run" produce attempt-1 "$digest" '[{"kind":"log","locator":"ftp://invalid"}]' >/dev/null 2>&1; then exit 1; fi
# Unknown attention rows remain reviewable by exact identity and expose the
# retained inventory state rather than fabricating a ready result.
artifact="$run_dir/steps/produce/attempt-1/artifacts/answer"
cp "$artifact" "$fixture/answer.saved"
printf tampered > "$artifact"
"$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/tampered"
jq -e '.ok and .data.readiness == "revoked" and .data.artifacts[0].state == "tampered"' "$fixture/tampered" >/dev/null
rm "$artifact"
"$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/missing"
jq -e '.ok and .data.readiness == "revoked" and .data.artifacts[0].state == "missing"' "$fixture/missing" >/dev/null
ln -s "$fixture/answer.saved" "$artifact"
"$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/symlink"
jq -e '.ok and .data.readiness == "revoked" and .data.artifacts[0].state == "symlink"' "$fixture/symlink" >/dev/null
rm "$artifact"
mv "$fixture/answer.saved" "$artifact"
touch "$run_dir/retention.json"
"$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/expired"
jq -e '.ok and .data.readiness == "revoked" and .data.artifacts[0].state == "expired"' "$fixture/expired" >/dev/null
rm "$run_dir/retention.json"
# Mutating the retained receipt or recipe changes the exact sealed subject.
cp "$run_dir/steps/produce/attempt-1/outputs.json" "$fixture/outputs.saved"
sed 's/"bytes":6/"bytes":7/' "$fixture/outputs.saved" > "$run_dir/steps/produce/attempt-1/outputs.json"
"$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/receipt-change"
jq -e '.ok and .data.readiness == "revoked" and .data.artifacts[0].state == "tampered"' "$fixture/receipt-change" >/dev/null
mv "$fixture/outputs.saved" "$run_dir/steps/produce/attempt-1/outputs.json"
cp "$run_dir/resolved.yml" "$fixture/resolved.saved"
printf '\n# changed retained recipe\n' >> "$run_dir/resolved.yml"
"$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/recipe-change"
jq -e '.ok and .data.readiness == "revoked" and .data.revision_state == "changed"' "$fixture/recipe-change" >/dev/null
"$native" workflow-data attention "$project" > "$fixture/recipe-attention"
fresh_revision="$(jq -r --arg run "$run" '.data.items[]|select(.run_id==$run and .step_id=="produce" and .attempt_id=="attempt-1")|.revision' "$fixture/recipe-attention" | sed -n '1p')"
printf '%s' "$fresh_revision" > "$fixture/fresh-revision"
fresh_digest="$(shasum -a 256 "$fixture/fresh-revision" | awk '{print $1}')"
"$native" workflow-review "$project" "$run" produce attempt-1 "$fresh_digest" > "$fixture/recipe-fresh"
jq -e '.ok and .data.readiness == "revoked"' "$fixture/recipe-fresh" >/dev/null
mv "$fixture/resolved.saved" "$run_dir/resolved.yml"
bad="$(printf '0%.0s' $(seq 1 64))"
"$native" workflow-review "$project" "$run" produce attempt-1 "$bad" > "$fixture/revoked"
jq -e '.ok and .data.readiness == "revoked"' "$fixture/revoked" >/dev/null
printf '%s\n' 'workflow review native public checks passed'
