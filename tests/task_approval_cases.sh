#!/bin/sh
# Sourced by test_task_acceptance.sh with its real receiver and controlled SSH.
# shellcheck disable=SC2154
cat >> "$fixture/source/.hydra/workflows/remote.yml" <<'WORKFLOW'
  - id: approval
    kind: approval-wait
    needs: [verify]
    idempotent: false
    args:
      head: wf-worker
      name: review
  - id: after
    kind: exec
    needs: [approval]
    idempotent: false
    args:
      head: wf-worker
      argv: [sh, -c, 'printf after >> approved.txt']
WORKFLOW
git -C "$fixture/source" add .hydra/workflows/remote.yml
git -C "$fixture/source" -c commit.gpgSign=false commit -qm approval-workflow
approval_commit="$(git -C "$fixture/source" rev-parse HEAD)"
sed "s/$workflow_commit/$approval_commit/" "$fixture/workflow-spec" > "$fixture/approval-spec"
task prepare --source "$fixture/source" --spec "$fixture/approval-spec" --output "$fixture/approval-package" > "$fixture/approval-preview"
approval_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/approval-preview")"
approval_wait() {
    attempt=0
    while [ "$attempt" -lt 150 ]; do
        task status build --id "$approval_id" > "$fixture/approval-status"
        if grep -q "\"state\":\"$1\"" "$fixture/approval-status"; then return; fi
        if grep -Eq '"state":"(failed|outcome_unknown)"' "$fixture/approval-status"; then cat "$fixture/approval-status"; cat "$fixture/host/fleet/tasks/$approval_id/stderr"; exit 1; fi
        sleep 0.1; attempt=$((attempt + 1))
    done
    cat "$fixture/approval-status"; exit 1
}
for approval_case in approve cancel; do
    task submit build --input "$fixture/approval-package" --key "approval-$approval_case" --trust-spec "$approval_digest" > "$fixture/approval-receipt"
    approval_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/approval-receipt")"
    approval_wait waiting_approval
    approval_dir="$fixture/host/fleet/tasks/$approval_id"
    [ ! -e "$approval_dir/result.json" ]
    task requests build --id "$approval_id" > "$fixture/requests"
    request_id="$(sed -n 's/.*"request_id":"\([^"]*\)".*/\1/p' "$fixture/requests")"
    [ -n "$request_id" ]
    approval_run="$(sed -n 's/.*"run_id":"\([^"]*\)".*/\1/p' "$fixture/approval-status")"
    if [ "$approval_case" = cancel ]; then
        # Suspended cancellation seals evidence synchronously: exceed the old
        # five-second transport budget, then exercise the bounded override.
        for invalid_timeout in 0 301 invalid; do
            if task cancel build --id "$approval_id" --timeout "$invalid_timeout" > "$fixture/error"; then exit 1; fi
            grep -q '"code":"invalid_input"' "$fixture/error"
        done
        : > "$fixture/transport/slow-cancel"
        if task cancel build --id "$approval_id" --timeout 1 > "$fixture/error"; then exit 1; fi
        grep -q '"code":"outcome_unknown"' "$fixture/error"
        task status build --id "$approval_id" > "$fixture/approval-status"
        grep -q '"state":"waiting_approval"' "$fixture/approval-status"
        task cancel build --id "$approval_id" > "$fixture/approval-cancelled"
        rm "$fixture/transport/slow-cancel"
        grep -q '"state":"cancelled"' "$fixture/approval-cancelled"
        grep -q '"cancellation":"confirmed_stopped"' "$fixture/approval-cancelled"
        grep -q '"result_state":"ready"' "$fixture/approval-cancelled"
        continue
    fi
    if task decide build --id "$approval_id" --request "$request_id" --decision approve --trust-spec wrong > "$fixture/error"; then exit 1; fi
    grep -q '"code":"trust_required"' "$fixture/error"
    : > "$fixture/transport/lose-ack"
    if task decide build --id "$approval_id" --request "$request_id" --decision approve --trust-spec "$approval_digest" > "$fixture/error"; then exit 1; fi
    grep -q '"code":"outcome_unknown"' "$fixture/error"
    task requests build --id "$approval_id" > "$fixture/requests"
    grep -q '"decision":"approve"' "$fixture/requests"
    for approval_head in "$approval_dir"/heads/*; do [ ! -e "$approval_head/approved.txt" ]; done
    : > "$fixture/transport/lose-ack"
    if task resume build --id "$approval_id" --trust-spec "$approval_digest" > "$fixture/error"; then exit 1; fi
    grep -q '"code":"outcome_unknown"' "$fixture/error"
    approval_wait succeeded
    grep -q "\"run_id\":\"$approval_run\"" "$fixture/approval-status"
    for approval_head in "$approval_dir"/heads/*; do
        [ "$(cat "$approval_head/approved.txt")" = after ]
        cmp "$fixture/source/context" "$approval_head/result.txt"
    done
    task resume build --id "$approval_id" --trust-spec "$approval_digest" >/dev/null
    for approval_head in "$approval_dir"/heads/*; do [ "$(cat "$approval_head/approved.txt")" = after ]; done
done
# Restore the original fixture definition for subsequent cancellation cases.
git -C "$fixture/source" checkout "$workflow_commit" -- .hydra/workflows/remote.yml
printf 'Remote approval: durable suspension, bound decisions, lost acknowledgments, exact resume and suspended cancellation passed\n'
