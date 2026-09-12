#!/bin/sh
# Public compiled-plan review negative: execution succeeds, verification fails.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
repo="$fixture/repo"
native="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
[ -x "$native" ]
# shellcheck source=/dev/null
. "$root/tests/fixtures/workflow-review/helpers.sh"
review_fixture_environment
mkdir -p "$repo"
cp "$root/tests/fixtures/plan/repo/"* "$repo/"
git -C "$repo" init -q
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name test
git -C "$repo" add .
git -C "$repo" commit -qm fixture
export HYDRA_HOME="$fixture/home"
export HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
export HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
(cd "$repo" && "$root/bin/hydra" init --no-agent --trust >/dev/null)
# Compile and run through the public plan path; the digest is emitted by compile.
(cd "$repo" && "$root/bin/hydra" workflow plan compile "$root/tests/fixtures/plan/plan.json" \
    "$root/tests/fixtures/plan/policy.json" "$fixture/compiled.json" > "$fixture/compile.json")
digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/compile.json")"
[ "${#digest}" -eq 64 ]
(cd "$repo" && "$root/bin/hydra" workflow plan run "$fixture/compiled.json" --accept "$digest" > "$fixture/positive.out")
positive_run="$(sed -n '1p' "$fixture/positive.out")"
[ -n "$positive_run" ]
positive_dir="$(find "$HYDRA_STATE_V2_ROOT/projects" -type d -path "*/workflows/runs/$positive_run" -print | sed -n '1p')"
[ "$(sed -n '1p' "$positive_dir/state")" = succeeded ]
project="$(find "$HYDRA_STATE_V2_ROOT/projects" -mindepth 1 -maxdepth 1 -type d -name 'project_*' -exec basename {} \; | sed -n '1p')"
"$native" workflow-data attention "$project" > "$fixture/positive-attention.json"
positive_row="$(jq -c --arg run "$positive_run" '.data.items[] | select(.kind == "result" and .run_id == $run and .step_id == "compose" and .attempt_id == "attempt-1")' "$fixture/positive-attention.json")"
[ -n "$positive_row" ]
positive_revision="$(printf '%s' "$positive_row" | jq -r '.revision')"
printf '%s' "$positive_revision" > "$fixture/positive-revision"
positive_digest="$(shasum -a 256 "$fixture/positive-revision" | awk '{print $1}')"
HYDRA_FLEET_BIN="$native" "$root/bin/hydra" workflow review "$project" "$positive_run" compose attempt-1 "$positive_digest" > "$fixture/positive-review"
jq -e '.ok and .data.readiness == "ready" and .data.checks.state == "passed"' "$fixture/positive-review" >/dev/null
# Losing a real accepted compiled artifact is a failed gate, even with a
# freshly observed attention revision. Exercise both retained plan markers.
mv "$positive_dir/compiled.json" "$fixture/compiled.saved"
fresh="$(review_revision "$positive_run" compose)"
"$native" workflow-review "$project" "$positive_run" compose attempt-1 "$fresh" > "$fixture/missing-compiled"
jq -e '.ok and .data.readiness == "revoked" and .data.checks.state == "failed" and .data.retained_contract.compiled' "$fixture/missing-compiled" >/dev/null
mv "$positive_dir/plan-accepted" "$fixture/accepted.saved"
fresh="$(review_revision "$positive_run" compose)"
"$native" workflow-review "$project" "$positive_run" compose attempt-1 "$fresh" > "$fixture/missing-plan-markers"
jq -e '.ok and .data.readiness == "revoked" and .data.checks.state == "failed" and .data.retained_contract.compiled' "$fixture/missing-plan-markers" >/dev/null
mv "$fixture/compiled.saved" "$positive_dir/compiled.json"
mv "$fixture/accepted.saved" "$positive_dir/plan-accepted"
# Restore the positive state before testing graph/data/source bindings.
for binding in graph.tsv data.json base-commit parallelism; do
    cp "$positive_dir/$binding" "$fixture/binding.saved"
    case "$binding" in
        graph.tsv) printf 'changed\n' >> "$positive_dir/$binding" ;;
        data.json) jq '.steps.compose.outputs.report.max_bytes += 1' "$fixture/binding.saved" > "$positive_dir/$binding" ;;
        base-commit) printf '%040d\n' 0 > "$positive_dir/$binding" ;;
        parallelism) printf '9\n' > "$positive_dir/$binding" ;;
    esac
    fresh="$(review_revision "$positive_run" compose)"
    "$native" workflow-review "$project" "$positive_run" compose attempt-1 "$fresh" > "$fixture/plan-$binding-review"
    jq -e '.ok and .data.readiness == "revoked" and .data.retained_contract.state == "failed"' "$fixture/plan-$binding-review" >/dev/null
    mv "$fixture/binding.saved" "$positive_dir/$binding"
done
# Freshly matching blob hashes still cannot detach retained data/recipe from
# the accepted compiled plan. The manifest mutation is structurally valid.
cp "$positive_dir/data.json" "$fixture/data.saved"
cp "$positive_dir/data-hash" "$fixture/data-hash.saved"
jq '.steps.compose.outputs.report.max_bytes += 1' "$fixture/data.saved" > "$positive_dir/data.json"
git -C "$repo" hash-object "$positive_dir/data.json" > "$positive_dir/data-hash"
fresh="$(review_revision "$positive_run" compose)"
"$native" workflow-review "$project" "$positive_run" compose attempt-1 "$fresh" > "$fixture/compiled-data-binding"
jq -e '.data.readiness=="revoked" and .data.retained_contract.reason=="compiled_binding_mismatch"' "$fixture/compiled-data-binding" >/dev/null
mv "$fixture/data.saved" "$positive_dir/data.json"
mv "$fixture/data-hash.saved" "$positive_dir/data-hash"
cp "$positive_dir/resolved.yml" "$fixture/recipe.saved"
cp "$positive_dir/definition-hash" "$fixture/definition-hash.saved"
sed '/^id:/a\
description: changed retained recipe
' "$fixture/recipe.saved" > "$positive_dir/resolved.yml"
git -C "$repo" hash-object "$positive_dir/resolved.yml" > "$positive_dir/definition-hash"
fresh="$(review_revision "$positive_run" compose)"
"$native" workflow-review "$project" "$positive_run" compose attempt-1 "$fresh" > "$fixture/compiled-recipe-binding"
jq -e '.data.readiness=="revoked" and .data.retained_contract.reason=="compiled_binding_mismatch"' "$fixture/compiled-recipe-binding" >/dev/null
mv "$fixture/recipe.saved" "$positive_dir/resolved.yml"
mv "$fixture/definition-hash.saved" "$positive_dir/definition-hash"
sed 's/"verdict":"pass"/"verdict":"fail"/'  "$repo/check.sh" > "$fixture/check.sh"
mv "$fixture/check.sh" "$repo/check.sh"
git -C "$repo" add check.sh
git -C "$repo" commit -qm 'negative assessment fixture'
sed 's/plan-smoke/plan-negative/g' "$root/tests/fixtures/plan/plan.json" > "$fixture/negative-plan.json"
(
cd "$repo" && "$root/bin/hydra" workflow plan compile "$fixture/negative-plan.json" "$root/tests/fixtures/plan/policy.json" "$fixture/negative.json" > "$fixture/negative-compile.json"
)
digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/negative-compile.json")"
set +e
(cd "$repo" && HYDRA_FLEET_BIN="$native" "$root/bin/hydra" workflow plan run \
    "$fixture/negative.json" --accept "$digest" > "$fixture/run.out" 2> "$fixture/run.err")
status=$?
set -e
[ "$status" -ne 0 ]
run="$(sed -n '1p' "$fixture/run.out")"
[ -n "$run" ]
run_dir="$(find "$HYDRA_STATE_V2_ROOT/projects" -type d -path "*/workflows/runs/$run" -print | sed -n '1p')"
[ -n "$run_dir" ]
[ "$(sed -n '1p' "$run_dir/steps/compose/state")" = succeeded ]
[ "$(sed -n '1p' "$run_dir/steps/verify/state")" = succeeded ]
[ "$(sed -n '1p' "$run_dir/state")" = failed ]
project="$(find "$HYDRA_STATE_V2_ROOT/projects" -mindepth 1 -maxdepth 1 -type d -name 'project_*' -exec basename {} \; | sed -n '1p')"
attention="$fixture/attention.json"
"$native" workflow-data attention "$project" > "$attention"
row="$(jq -c --arg run "$run" '.data.items[] | select(.kind == "result" and .run_id == $run and .step_id == "verify" and .attempt_id == "attempt-1")' "$attention")"
[ -n "$row" ]
revision="$(printf '%s' "$row" | jq -r '.revision')"
printf '%s' "$revision" > "$fixture/revision"
review_digest="$(shasum -a 256 "$fixture/revision" | awk '{print $1}')"
review="$fixture/review.json"
HYDRA_FLEET_BIN="$native" "$root/bin/hydra" workflow review "$project" "$run" verify attempt-1 "$review_digest" > "$review"
jq -e '.ok and .data.accepted == false and .data.readiness == "revoked" and .data.checks.state == "failed"' "$review" >/dev/null
printf '%s\n' 'workflow review compiled-plan negative passed'
