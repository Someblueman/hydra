#!/bin/sh
# Real request/evidence navigation, expiration and explicit CLI decisions.
: "${run:?}" "${repo:?}" "${root:?}" "${fixture:?}" "${project:?}" "${native:?}"
run_dir="$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$run"
request_id="$(cat "$run_dir/steps/approval/request-id")"
request_dir="$run_dir/approvals/$request_id"
(cd "$repo" && "$root/bin/hydra" workflow requests "$run" --json) > "$fixture/requests.json"
jq -e --slurpfile review "$fixture/exact-request.json" '.data.requests[0] as $r | $review[0].data.request as $v | $r.request_id==$v.request_id and $r.state==$v.state and $r.binding==$v.binding and ($r.expires_at|tostring)==$v.expires_at and $r.message==$v.message and $r.evidence_path==$v.evidence_locator' "$fixture/requests.json" >/dev/null
[ -f "$(jq -r '.data.request.evidence_locator' "$fixture/exact-request.json")" ]
# A changed revision cannot retain current decision navigation.
awk -F '\t' 'BEGIN {OFS="\t"} {$15="0000000000000000000000000000000000000000000000000000000000000000"; print}' "$fixture/request.row" > "$fixture/stale-request.row"
review_exact review "$fixture/stale-request.row" > "$fixture/stale-request.json"
jq -e '.ok and .data.readiness=="revoked" and .data.request.context_state!="current" and .data.actions.approve_argv==null and .data.actions.reject_argv==null' "$fixture/stale-request.json" >/dev/null
cp "$request_dir/expires-at" "$fixture/expiry.saved"
printf '1\n' > "$request_dir/expires-at"
expired_revision="$(review_revision "$run" approval)"
"$native" workflow-review "$project" "$run" approval attempt-1 "$expired_revision" > "$fixture/expired-request.json"
jq -e '.ok and .data.readiness=="revoked" and .data.request.observed_freshness=="expired" and .data.request.expires_at=="1" and .data.actions.approve_argv==null and .data.actions.reject_argv==null' "$fixture/expired-request.json" >/dev/null
mv "$fixture/expiry.saved" "$request_dir/expires-at"
# Stale instance state similarly loses decision context while preserving the
# original recorded binding and evidence locator for inspection.
head="$(jq -r '.data.request.head' "$fixture/exact-request.json")"
head_dir="$HYDRA_STATE_V2_ROOT/projects/$project/heads/$head"
cp "$head_dir/current-instance" "$fixture/instance.saved"
printf 'instance_00000000000000000000000000000000\n' > "$head_dir/current-instance"
stale_revision="$(review_revision "$run" approval)"
"$native" workflow-review "$project" "$run" approval attempt-1 "$stale_revision" > "$fixture/stale-instance.json"
jq -e '.ok and .data.readiness=="revoked" and .data.request.observed_reason=="approval_binding_unknown" and .data.actions.approve_argv==null and .data.actions.reject_argv==null' "$fixture/stale-instance.json" >/dev/null
mv "$fixture/instance.saved" "$head_dir/current-instance"
[ ! -e "$request_dir/decision" ]
# Execute the argv exposed by review as argv, never as interpolated shell code.
jq -r '.data.actions.reject_argv[2:] | .[]' "$fixture/exact-request.json" > "$fixture/decision.args"
(
    cd "$repo"
    IFS='
'
    set -f
    # shellcheck disable=SC2046 # One bounded, newline-delimited CLI argument per line.
    set -- $(cat "$fixture/decision.args")
    "$root/bin/hydra" workflow "$@" > "$fixture/reject.out"
)
[ "$(cat "$request_dir/decision/action")" = reject ]
[ "$(cat "$run_dir/state")" = waiting-approval ]
current_revision="$(review_revision "$run" approval)"
"$native" workflow-review "$project" "$run" approval attempt-1 "$current_revision" > "$fixture/decided-request.json"
jq -e '.data.readiness=="revoked" and .data.request.decision=="reject" and .data.actions.approve_argv==null and .data.actions.reject_argv==null' "$fixture/decided-request.json" >/dev/null
# A second real request exercises approval independently of rejection.
set +e
(cd "$repo" && "$root/bin/hydra" workflow run "$fixture/flow.yml") > "$fixture/approve-run.out" 2> "$fixture/approve-run.err"
approve_status=$?
set -e
[ "$approve_status" -eq 3 ] || { cat "$fixture/approve-run.err" >&2; exit 1; }
approve_run="$(sed -n '1p' "$fixture/approve-run.out")"
review_selection "$approve_run" approval > "$fixture/approve-request.row"
review_exact review "$fixture/approve-request.row" > "$fixture/approve-request.json"
jq -e '.data.readiness=="pending" and .data.request.context_state=="current"' "$fixture/approve-request.json" >/dev/null
jq -r '.data.actions.approve_argv[2:] | .[]' "$fixture/approve-request.json" > "$fixture/decision.args"
(
    cd "$repo"
    IFS='
'
    set -f
    # shellcheck disable=SC2046 # One bounded, newline-delimited CLI argument per line.
    set -- $(cat "$fixture/decision.args")
    "$root/bin/hydra" workflow "$@" > "$fixture/approve.out"
)
approve_dir="$HYDRA_STATE_V2_ROOT/projects/$project/workflows/runs/$approve_run"
[ "$(cat "$approve_dir/state")" = waiting-approval ]
(cd "$repo" && "$root/bin/hydra" workflow resume "$approve_run") > "$fixture/approve-resume.out"
[ "$(cat "$approve_dir/state")" = succeeded ]
