#!/bin/sh
# Sourced by test_t2_headless.sh: owner death, retained reservations and
# cancellation on the tmux-less receiver. Uncertain work is never replayed;
# the absence of a terminal never releases a reservation.
# owned_group and owned_owner are read by the driver's cleanup trap.
# shellcheck disable=SC2034
: "${fixture:?}" "${root:?}" "${receiver_path:?}"
host_claim_state() { cut -d ' ' -f 2 "$1"; }
# Killing an owner cannot authorize a second execution. Stop it before its
# child so it can neither reap the child nor publish a completion first.
sed "s@\[\"true\"\]@[\"sh\",\"-c\",\"printf started > $fixture/crash-started; sleep 30\"]@" "$fixture/spec" > "$fixture/crash-spec"
task prepare --source "$fixture/source" --spec "$fixture/crash-spec" --output "$fixture/crash-package" > "$fixture/crash-preview"
task submit build --input "$fixture/crash-package" --key owner-crash > "$fixture/crash-receipt"
crash_id="$(json_field task_id "$fixture/crash-receipt")"
crash_digest="$(json_field spec_sha256 "$fixture/crash-preview")"
task start build --id "$crash_id" --trust-spec "$crash_digest" >/dev/null
attempt=0
while [ ! -f "$fixture/crash-started" ] && [ "$attempt" -lt 300 ]; do sleep 0.2; attempt=$((attempt + 1)); done
[ -f "$fixture/crash-started" ]
task status build --id "$crash_id" > "$fixture/crash-status"
grep -q '"state":"running"' "$fixture/crash-status"
owner="$(sed -n 's/.*"owner_pid":\([0-9]*\).*/\1/p' "$fixture/crash-status")"
[ -n "$owner" ]
owned_group="$(ps -axo pid=,ppid= | awk -v owner="$owner" '$2 == owner {print $1; exit}')"
[ -n "$owned_group" ]
owned_owner="$owner"
kill -STOP "$owner"
kill -KILL "-$owned_group"
owned_group=""
kill -KILL "$owner"
owned_owner=""
sleep 0.2
task status build --id "$crash_id" > "$fixture/crash-status"
grep -q '"state":"outcome_unknown"' "$fixture/crash-status"
grep -q '"failure":"owner_unavailable"' "$fixture/crash-status"
if task start build --id "$crash_id" --trust-spec "$crash_digest" > "$fixture/error"; then exit 1; fi
grep -q '"code":"outcome_unknown"' "$fixture/error"
if task resume build --id "$crash_id" --trust-spec "$crash_digest" > "$fixture/error"; then exit 1; fi
grep -q '"code":"outcome_unknown"' "$fixture/error"
# The reservation of uncertain work stays held: neither the dead owner nor the
# missing terminal released it, and an unconfirmed cancel keeps it held.
crash_claim="$(find "$fixture/host/admission" -name "$crash_id.run_*.request" -print)"
[ -f "$crash_claim" ]
[ "$(host_claim_state "$crash_claim")" = reserved ]
"$root/bin/hydra" fleet admission build -- inspect "$(basename "$crash_claim" .request)" | grep -q '"state":"reserved"'
task cancel build --id "$crash_id" > "$fixture/unknown-cancel"
grep -q '"cancellation":"unknown"' "$fixture/unknown-cancel"
grep -q '"cancel_requested_at":' "$fixture/unknown-cancel"
case "$(host_claim_state "$crash_claim")" in reserved|unknown) ;; *) cat "$crash_claim" >&2; exit 1 ;; esac
[ ! -s "$fixture/tmux-refused" ]
printf 'T2: owner death is outcome_unknown, never replayed; the reservation stays held\n'

# A task that never launched cancels with a scoped record and no logs.
task submit build --input "$fixture/crash-package" --key never-launched > "$fixture/pending-receipt"
pending_id="$(json_field task_id "$fixture/pending-receipt")"
task cancel build --id "$pending_id" > "$fixture/pending-cancel"
grep -q '"state":"cancelled"' "$fixture/pending-cancel"
grep -q '"cancellation_scope":"not_launched"' "$fixture/pending-cancel"
[ ! -e "$fixture/host/fleet/tasks/$pending_id/launch.json" ]
task logs build --id "$pending_id" > "$fixture/pending-logs"
grep -q '"available":false' "$fixture/pending-logs"

# Cancel a live command: a lost cancel acknowledgment is unknown until the
# receiver confirms the stop; confirmation releases the reservation and keeps
# the worktree for inspection.
sed -e 's/crash-started/cancel-started/' -e 's/"cancellation_seconds":10/"cancellation_seconds":1/' "$fixture/crash-spec" > "$fixture/control-spec"
task prepare --source "$fixture/source" --spec "$fixture/control-spec" --output "$fixture/control-package" > "$fixture/control-preview"
control_digest="$(json_field spec_sha256 "$fixture/control-preview")"
task submit build --input "$fixture/control-package" --key cancel-exec --trust-spec "$control_digest" > "$fixture/control-receipt"
control_id="$(json_field task_id "$fixture/control-receipt")"
attempt=0
while [ ! -f "$fixture/cancel-started" ] && [ "$attempt" -lt 600 ]; do sleep 0.1; attempt=$((attempt + 1)); done
[ -f "$fixture/cancel-started" ]
task logs build --id "$control_id" > "$fixture/live-logs"
grep -q '"available":true' "$fixture/live-logs"
: > "$fixture/transport/lose-ack"
if task cancel build --id "$control_id" > "$fixture/error"; then exit 1; fi
grep -q '"code":"outcome_unknown"' "$fixture/error"
attempt=0
while [ "$attempt" -lt 100 ]; do
    task status build --id "$control_id" > "$fixture/control-status"
    if grep -q '"state":"cancelled"' "$fixture/control-status"; then break; fi
    sleep 0.2; attempt=$((attempt + 1))
done
grep -q '"cancellation":"confirmed_stopped"' "$fixture/control-status" || { cat "$fixture/control-status" >&2; exit 1; }
grep -q '"admission_cleanup":"confirmed"' "$fixture/control-status"
for control_claim in "$fixture/host/admission/$control_id"*.request; do
    case "$(host_claim_state "$control_claim")" in reserved|unknown|queued) exit 1 ;; esac
done
[ -d "$fixture/host/fleet/tasks/$control_id/workspace/.git" ]
task cancel build --id "$control_id" > "$fixture/repeat-cancel"
grep -q '"cancel_response":"already_terminal"' "$fixture/repeat-cancel"
printf 'T2: cancellation with a lost acknowledgment confirmed on reconnect; not-launched cancel scoped\n'
