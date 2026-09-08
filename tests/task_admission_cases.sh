#!/bin/sh
# Sourced by the real remote-task fixture before intentionally orphaning owners.
# shellcheck disable=SC2154
host_admission() { HYDRA_HOME="$fixture/host" "$root/bin/hydra" admission "$@"; }
admission_task_state() {
    _ats_id="$1" _ats_state="$2" _ats_tries=0
    while [ "$_ats_tries" -lt 100 ]; do
        task status build --id "$_ats_id" > "$fixture/admission-status"
        if [ "$_ats_state" = queued ]; then
            if grep -q '"state":"queued"' "$fixture/admission-status"; then return 0; fi
        elif grep -q "\"runtime\":{\"schema_version\":1,\"state\":\"$_ats_state\"" "$fixture/admission-status"; then return 0;
        fi
        sleep 0.1; _ats_tries=$((_ats_tries + 1))
    done
    cat "$fixture/admission-status" >&2
    return 1
}

host_admission configure 1 1 0 2 - >/dev/null
"$root/bin/hydra" fleet admission build -- status --summary > "$fixture/stale-admission"
grep -q '"reserved":0,"queued":0' "$fixture/stale-admission"
grep -q '"requests_omitted":true' "$fixture/stale-admission"
grep -q '"observed_at":' "$fixture/stale-admission"
if "$root/bin/hydra" fleet admission build -- configure 9 9 0 9 - > "$fixture/error"; then exit 1; fi
grep -q '"code":"invalid_input"' "$fixture/error"
host_admission request admission-fixture fixture 60 - >/dev/null
task submit build --input "$fixture/package" --key admission-queued --trust-spec "$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/preview")" > "$fixture/admission-receipt"
admission_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/admission-receipt")"
admission_task_state "$admission_id" queued
grep -q '"reason":"host_limit"' "$fixture/admission-status"
"$root/bin/hydra" fleet admission build -- inspect "$admission_id" | grep -q '"state":"queued"'
[ ! -e "$fixture/host/fleet/tasks/$admission_id/workspace" ]
host_admission inspect "$admission_id" > "$fixture/admission-before"
task start build --id "$admission_id" --trust-spec "$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/preview")" >/dev/null
host_admission inspect "$admission_id" > "$fixture/admission-after"
[ "$(sed -n 's/.*"deadline":\([0-9]*\).*/\1/p' "$fixture/admission-before")" = "$(sed -n 's/.*"deadline":\([0-9]*\).*/\1/p' "$fixture/admission-after")" ]
task cancel build --id "$admission_id" >/dev/null
admission_task_state "$admission_id" cancelled
grep -q '"cancellation":"confirmed_stopped"' "$fixture/admission-status"
host_admission inspect "$admission_id" | grep -q '"state":"cancelled"'
[ ! -e "$fixture/host/fleet/tasks/$admission_id/workspace" ]

# A full receiver queue refuses promptly and preserves the explanation.
host_admission configure 1 1 0 0 - >/dev/null
task submit build --input "$fixture/package" --key admission-full --trust-spec "$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/preview")" > "$fixture/admission-receipt"
grep -q '"failure":"queue_full"' "$fixture/admission-receipt"
host_admission configure 1 1 0 2 - >/dev/null
sed 's/"queue_seconds":60/"queue_seconds":3/' "$fixture/spec" > "$fixture/admission-spec"
task prepare --source "$fixture/source" --spec "$fixture/admission-spec" --output "$fixture/admission-package" > "$fixture/admission-preview"
admission_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/admission-preview")"
task submit build --input "$fixture/admission-package" --key admission-expiry --trust-spec "$admission_digest" > "$fixture/admission-receipt"
admission_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/admission-receipt")"
admission_task_state "$admission_id" failed
grep -q '"failure":"queue_deadline"' "$fixture/admission-status"
[ ! -e "$fixture/host/fleet/tasks/$admission_id/workspace" ]
host_admission release admission-fixture --confirmed >/dev/null

# Labels are explicit requirements even when capacity is otherwise available.
sed 's/\["exec"\]/["exec","label.gpu"]/' "$fixture/spec" > "$fixture/admission-spec"
task prepare --source "$fixture/source" --spec "$fixture/admission-spec" --output "$fixture/admission-label-package" > "$fixture/admission-preview"
admission_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/admission-preview")"
task submit build --input "$fixture/admission-label-package" --key admission-label --trust-spec "$admission_digest" > "$fixture/admission-receipt"
admission_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/admission-receipt")"
admission_task_state "$admission_id" failed
grep -q 'capability_unavailable' "$fixture/admission-status"
[ ! -e "$fixture/host/fleet/tasks/$admission_id/workspace" ]

# Two independent remote tasks must share the mapped project limit despite
# receiving different isolated clones. The real commands reject any overlap.
host_admission configure 4 1 0 8 gpu >/dev/null
task submit build --input "$fixture/admission-label-package" --key admission-label-ok --trust-spec "$admission_digest" > "$fixture/admission-receipt"
admission_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/admission-receipt")"
admission_task_state "$admission_id" succeeded
host_admission inspect "$admission_id" | grep -q '"required_labels":"gpu"'
sed "s@\[\"true\"\]@[\"sh\",\"-c\",\"mkdir $fixture/admission-exclusive || exit 42; sleep 4; rmdir $fixture/admission-exclusive\"]@" "$fixture/spec" > "$fixture/admission-spec"
task prepare --source "$fixture/source" --spec "$fixture/admission-spec" --output "$fixture/admission-concurrent-package" > "$fixture/admission-preview"
admission_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/admission-preview")"
task submit build --input "$fixture/admission-concurrent-package" --key admission-one --trust-spec "$admission_digest" > "$fixture/admission-one" &
admission_pid=$!
task submit build --input "$fixture/admission-concurrent-package" --key admission-two --trust-spec "$admission_digest" > "$fixture/admission-two"
wait "$admission_pid"
for admission_name in one two; do
    admission_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/admission-$admission_name")"
    admission_task_state "$admission_id" succeeded
    admission_project="$(cat "$fixture/receiver/.git/hydra/project-id")"
    for admission_record in "$fixture/host/admission/$admission_id"*.request; do
        [ "$(cut -d ' ' -f 1 "$admission_record")" = "$admission_project" ]
        [ "$(cut -d ' ' -f 2 "$admission_record")" = released ]
    done
done
host_admission status --json | grep -q '"reserved":0,"queued":0'
host_admission configure 0 0 0 128 - >/dev/null
printf 'Remote admission: bounded queue, cancellation, deadline, labels, original project concurrency and release passed\n'
