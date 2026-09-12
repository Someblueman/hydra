#!/bin/sh
# Public historical selection/data/graph tests. Set HYDRA_REVIEW_OBJECT_FORMAT
# to sha256 to create a real SHA256 Git workflow (not edited digest strings).
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# Sourced helpers are linted independently; require the setup's shared values.
# shellcheck source=/dev/null
. "$root/tests/fixtures/workflow-review/setup.sh"
: "${project:?}" "${run:?}" "${repo:?}" "${fixture:?}" "${digest:?}" "${native:?}"
: "${HYDRA_STATE_V2_ROOT:?}"
run_dir="$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$run"
object_format="$(git -C "$repo" rev-parse --show-object-format)"
case "$object_format" in sha1) oid_length=40 ;; sha256) oid_length=64 ;; *) exit 1 ;; esac
[ "$(wc -c < "$run_dir/definition-hash" | tr -d ' ')" -eq "$((oid_length + 1))" ]
[ "$(wc -c < "$run_dir/data-hash" | tr -d ' ')" -eq "$((oid_length + 1))" ]
[ "$(git -C "$repo" hash-object "$run_dir/data.json")" = "$(cat "$run_dir/data-hash")" ]
# Use the opposite caller object format, then remove the original definition.
mkdir "$fixture/caller"
if [ "$object_format" = sha1 ]; then caller_format=sha256; else caller_format=sha1; fi
git -C "$fixture/caller" init -q --object-format="$caller_format"
rm "$fixture/flow.yml"
(cd "$fixture/caller" && "$root/bin/hydra" workflow review "$project" "$run" produce attempt-1 "$digest") > "$fixture/cross-format.json"
jq -e '.ok and .data.readiness == "ready" and .data.retained_contract.state == "passed" and .data.retained_contract.source_base == "recorded provenance only; source contents not independently retained" and .data.current_head_authority == false' "$fixture/cross-format.json" >/dev/null
# A successful immutable result need not have a recorded live head/instance.
attempt_dir="$run_dir/steps/produce/attempt-1"
[ ! -f "$attempt_dir/head" ] && [ ! -f "$attempt_dir/instance" ]
jq -e '.data.identity.head_id == null and .data.identity.current_instance == null and .data.accepted == false' "$fixture/cross-format.json" >/dev/null
# Recompute attention after every mutation. Revision equality alone cannot
# establish data/graph/declaration integrity.
for binding in data.json graph.tsv parallelism; do
    cp "$run_dir/$binding" "$fixture/binding.saved"
    case "$binding" in
        data.json) jq '.steps.produce.outputs.answer.max_bytes += 1' "$fixture/binding.saved" > "$run_dir/$binding" ;;
        graph.tsv) printf 'changed\n' >> "$run_dir/$binding" ;;
        parallelism) printf '9\n' > "$run_dir/$binding" ;;
    esac
    fresh="$(review_revision "$run" produce)"
    "$native" workflow-review "$project" "$run" produce attempt-1 "$fresh" > "$fixture/$binding-review"
    jq -e '.ok and .data.readiness == "revoked" and .data.revision_state == "matches" and .data.retained_contract.state == "failed"' "$fixture/$binding-review" >/dev/null
    mv "$fixture/binding.saved" "$run_dir/$binding"
done
# Even re-recording the data hash cannot make malformed declarations valid.
cp "$run_dir/data.json" "$fixture/data.saved"
cp "$run_dir/data-hash" "$fixture/data-hash.saved"
for mutation in '.steps.produce.outputs={"../escape": {"type":"file","path":"answer.txt","max_bytes":128}}' \
    '.steps.produce.outputs.answer.type="imaginary"' \
    '.steps.produce.outputs.answer.max_bytes="128"' \
    '.steps.produce.outputs.answer=[]'; do
    jq "$mutation" "$fixture/data.saved" > "$run_dir/data.json"
    git -C "$repo" hash-object "$run_dir/data.json" > "$run_dir/data-hash"
    fresh="$(review_revision "$run" produce)"
    "$native" workflow-review "$project" "$run" produce attempt-1 "$fresh" > "$fixture/malformed-review"
    jq -e '.ok and .data.readiness == "revoked" and .data.retained_contract.reason == "malformed_declarations"' "$fixture/malformed-review" >/dev/null
    [ ! -e "$run_dir/steps/produce/escape" ]
done
mv "$fixture/data.saved" "$run_dir/data.json"
mv "$fixture/data-hash.saved" "$run_dir/data-hash"
cp "$attempt_dir/outputs.json" "$fixture/receipt.saved"
for mutation in '.files.answer.bytes="6"' '.files.answer.sha256="bad"' '.files.answer.type="object"' '.files={}' '.files.answer.bytes=-1'; do
    jq "$mutation" "$fixture/receipt.saved" > "$attempt_dir/outputs.json"
    fresh="$(review_revision "$run" produce)"
    "$native" workflow-review "$project" "$run" produce attempt-1 "$fresh" > "$fixture/receipt-review"
    jq -e '.ok and .data.readiness == "revoked" and .data.inventory_state != null' "$fixture/receipt-review" >/dev/null
done
mv "$fixture/receipt.saved" "$attempt_dir/outputs.json"
# Redirected Git must be refused before even private init; the caller repo is
# unchanged, including HEAD/config. This catches the former bare-init accident.
shasum -a 256 "$repo/.git/HEAD" "$repo/.git/config" > "$fixture/git.before"
if GIT_DIR="$repo/.git" "$native" workflow-review "$project" "$run" produce attempt-1 "$digest" > "$fixture/git-redirect"; then exit 1; fi
jq -e '.ok == false and .error.code == "git_environment_redirect"' "$fixture/git-redirect" >/dev/null
shasum -a 256 "$repo/.git/HEAD" "$repo/.git/config" > "$fixture/git.after"
cmp "$fixture/git.before" "$fixture/git.after"
printf '%s\n' "workflow review $object_format historical bindings passed"
