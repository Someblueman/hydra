#!/bin/sh
# Sourced by the real public review fixture; no receiver decisions are executed.
set -eu
: "${root:?}" "${fixture:?}" "${tab:?}"
cp "$root/tests/fixtures/fleet-review/approval.yml" "$fixture/source/.hydra/workflows/approval.yml"
git -C "$fixture/source" add .hydra/workflows/approval.yml
git -C "$fixture/source" -c commit.gpgSign=false commit -qm approval-fixture
approval_commit="$(git -C "$fixture/source" rev-parse HEAD)"
jq --arg commit "$approval_commit" '.source.commit=$commit|.work.path=".hydra/workflows/approval.yml"|.outputs=[]' "$fixture/spec.json" > "$fixture/approval-spec.json"
task prepare --source "$fixture/source" --spec "$fixture/approval-spec.json" --output "$fixture/approval-package.json" > "$fixture/approval-prepared.json"
approval_digest="$(jq -r '.data.spec_sha256' "$fixture/approval-prepared.json")"
task submit build --input "$fixture/approval-package.json" --key review-approval --trust-spec "$approval_digest" > "$fixture/approval-submitted.json"
approval_task="$(jq -r '.data.task_id' "$fixture/approval-submitted.json")"
tries=0
while [ "$tries" -lt 300 ]; do
    task status build --id "$approval_task" > "$fixture/approval-status.json"
    if jq -e '.data.runtime.state=="waiting_approval"' "$fixture/approval-status.json" >/dev/null; then break; fi
    sleep 0.1; tries=$((tries + 1))
done
jq -e '.data.runtime.state=="waiting_approval"' "$fixture/approval-status.json" >/dev/null
"$root/bin/hydra" fleet attention-data > "$fixture/approval-attention.tsv"
awk -F '\t' -v task="$approval_task" '$1=="ITEM" && $7==task && $3=="approval" && $9=="approval-alpha" {print}' "$fixture/approval-attention.tsv" > "$fixture/approval-selected.tsv"
load_selection "$fixture/approval-selected.tsv"
: "${kind:?}" "${step:?}" "${request:?}" "${run:?}"
[ "$kind" = approval ] && [ "$step" = approval-alpha ] && [ "$request" != - ]
snapshot > "$fixture/request-before.sha256"
review review > "$fixture/approval-review.json"
jq -e --arg request "$request" '.data.readiness=="pending" and .data.accepted==false and .data.request.request_id==$request and .data.request.step_id=="approval-alpha" and .data.request.message=="Review alpha request" and .data.actions.executed==false and .data.actions.approve[8]==$request' "$fixture/approval-review.json" >/dev/null
review review-data > "$fixture/approval-review.tsv"
grep -q "^TEXT${tab}request: message: Review alpha request$" "$fixture/approval-review.tsv"
if grep -q 'Review beta request' "$fixture/approval-review.tsv"; then exit 1; fi
snapshot > "$fixture/request-after.sha256"
cmp "$fixture/request-before.sha256" "$fixture/request-after.sha256"
approval_project="$(jq -r '.data.runtime.execution_project_id' "$fixture/approval-status.json")"
request_dir="$fixture/host/state/v2/projects/$approval_project/workflows/runs/$run/approvals/$request"
[ ! -e "$request_dir/decision" ]
cp "$request_dir/expires-at" "$fixture/expires.saved"
printf '1\n' > "$request_dir/expires-at"
review review > "$fixture/approval-expired.json"
jq -e '.data.readiness=="revoked" and (.data|has("actions")|not)' "$fixture/approval-expired.json" >/dev/null
cp "$fixture/expires.saved" "$request_dir/expires-at"
cp "$request_dir/step-id" "$fixture/request-step.saved"
printf 'approval-beta\n' > "$request_dir/step-id"
review review > "$fixture/approval-replaced.json"
jq -e '.data.readiness=="revoked"' "$fixture/approval-replaced.json" >/dev/null
cp "$fixture/request-step.saved" "$request_dir/step-id"
load_selection "$fixture/selected.tsv"
