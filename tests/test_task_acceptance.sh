#!/bin/sh
# Public CLI over a controlled SSH boundary and the real receiver, state, and Git.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
HYDRA_HOME="$fixture/client"
HYDRA_TEST_TRANSPORT="$fixture/transport"
export HYDRA_FLEET_BIN HYDRA_HOME HYDRA_TEST_TRANSPORT
mkdir -p "$fixture/source" "$fixture/receiver" "$fixture/transport" "$fixture/bin"
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
while [ "$#" -gt 2 ]; do shift; done
if [ -f "$HYDRA_TEST_TRANSPORT/offline" ]; then exit 255; fi
request="$(mktemp "$HYDRA_TEST_TRANSPORT/request.XXXXXX")"
trap 'rm -f "$request"' 0
cat > "$request"
if [ -f "$HYDRA_TEST_TRANSPORT/lose-ack" ] && grep -q '"operation":"submit"' "$request"; then
    /bin/sh -c "$2" < "$request" > "$HYDRA_TEST_TRANSPORT/lost-response"
    rm "$HYDRA_TEST_TRANSPORT/lose-ack"
    exit 255
fi
exec /bin/sh -c "$2" < "$request"
SSH
chmod +x "$fixture/bin/ssh"
PATH="$fixture/bin:$PATH"
export PATH
for repository in source receiver; do
    git -C "$fixture/$repository" init -q
    git -C "$fixture/$repository" config user.name 'Task test'
    git -C "$fixture/$repository" config user.email task@example.invalid
    git -C "$fixture/$repository" -c commit.gpgSign=false commit --allow-empty -qm initial
done
(cd "$fixture/receiver" && HYDRA_HOME="$fixture/host" "$root/bin/hydra" init --no-agent --json) > "$fixture/initialized"
"$root/bin/hydra" remote add build loopback --hydra "$root/bin/hydra" --home "$fixture/host" >/dev/null
commit="$(git -C "$fixture/source" rev-parse HEAD)"
cat > "$fixture/spec" <<EOF
{"schema_version":1,"host":"build","project":"$fixture/receiver",
"source":{"commit":"$commit"},"work":{"kind":"exec","argv":["true"]},
"inputs":[],"outputs":[],"capabilities":["exec"],"completion":"command-exit",
"limits":{"transport_seconds":30,"queue_seconds":60,"startup_seconds":60,
"execution_seconds":120,"cancellation_seconds":10,"log_bytes":4096,"artifact_bytes":4096}}
EOF
task() { "$root/bin/hydra" fleet task "$@"; }
if printf '{"protocol":1,"action":"handshake"}\000trailing' | "$HYDRA_FLEET_BIN" fleet serve > "$fixture/error"; then exit 1; fi
grep -q '"code":"invalid_request"' "$fixture/error"
task prepare --source "$fixture/source" --spec "$fixture/spec" --output "$fixture/package" > "$fixture/preview"
# Six simultaneous clients must converge on one immutable acceptance.
pids=""
for n in 1 2 3 4 5 6; do
    task submit build --input "$fixture/package" --key same-key > "$fixture/receipt-$n" &
    pids="$pids $!"
done
for pid in $pids; do wait "$pid"; done
id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/receipt-1")"
[ -n "$id" ]
for n in 2 3 4 5 6; do cmp "$fixture/receipt-1" "$fixture/receipt-$n"; done
task status build --id "$id" > "$fixture/status"
grep -q '"state":"accepted"' "$fixture/status"
[ "$(find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" -eq 1 ]
cmp "$fixture/package" "$fixture/host/fleet/tasks/$id/package.json"
cp "$fixture/host/fleet/tasks/$id/acceptance.json" "$fixture/original-acceptance"
# Lost acknowledgment is deliberately after the receiver has finished acceptance.
: > "$fixture/transport/lose-ack"
if task submit build --input "$fixture/package" --key lost-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"outcome_unknown"' "$fixture/error"
lost_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/transport/lost-response")"
[ -n "$lost_id" ]
task submit build --input "$fixture/package" --key lost-key > "$fixture/reconciled"
grep -q "$lost_id" "$fixture/reconciled"
[ "$(find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" -eq 2 ]
# A new valid payload cannot take over an existing key.
sed 's/"true"/"false"/' "$fixture/spec" > "$fixture/changed-spec"
task prepare --source "$fixture/source" --spec "$fixture/changed-spec" --output "$fixture/changed-package" >/dev/null
if task submit build --input "$fixture/changed-package" --key same-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"submission_conflict"' "$fixture/error"
cmp "$fixture/original-acceptance" "$fixture/host/fleet/tasks/$id/acceptance.json"
# Receipt lookup survives loss of the original mapping, including a lost first ack.
mv "$fixture/receiver" "$fixture/moved-receiver"
task submit build --input "$fixture/package" --key same-key > "$fixture/repeated"
grep -q "$id" "$fixture/repeated"
mv "$fixture/moved-receiver" "$fixture/receiver"
mv "$fixture/receiver/.git" "$fixture/receiver/.git-saved"
if task submit build --input "$fixture/package" --key stale-mapping > "$fixture/error"; then exit 1; fi
grep -q '"code":"unmapped_project"' "$fixture/error"
mv "$fixture/receiver/.git-saved" "$fixture/receiver/.git"
# An outage gives no new authority to replay work, and status never invents success.
: > "$fixture/transport/offline"
if task status build --id "$id" > "$fixture/error"; then exit 1; fi
grep -q '"code":"offline"' "$fixture/error"
if task submit build --input "$fixture/package" --key same-key > "$fixture/error"; then exit 1; fi
rm "$fixture/transport/offline"
task status build --id "$id" >/dev/null
[ "$(find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" -eq 2 ]
# Reject requirements and placements before acceptance.
sed 's/\["exec"\]/["exact-provider-resume"]/' "$fixture/spec" > "$fixture/bad-spec"
task prepare --source "$fixture/source" --spec "$fixture/bad-spec" --output "$fixture/bad-package" >/dev/null
if task submit build --input "$fixture/bad-package" --key missing-cap > "$fixture/error"; then exit 1; fi
grep -q '"code":"capability_unavailable"' "$fixture/error"
sed "s@$fixture/receiver@$fixture/source@" "$fixture/spec" > "$fixture/unmapped-spec"
task prepare --source "$fixture/source" --spec "$fixture/unmapped-spec" --output "$fixture/unmapped-package" >/dev/null
if task submit build --input "$fixture/unmapped-package" --key unmapped > "$fixture/error"; then exit 1; fi
grep -q '"code":"unmapped_project"' "$fixture/error"
if task submit build --input "$fixture/unmapped-package" --key same-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"submission_conflict"' "$fixture/error"
# A task-store symlink cannot redirect reads or acceptance publication.
mv "$fixture/host/fleet/tasks" "$fixture/host/fleet/saved-tasks"
ln -s "$fixture/host/fleet/saved-tasks" "$fixture/host/fleet/tasks"
if task submit build --input "$fixture/package" --key same-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"io_failed"' "$fixture/error"
rm "$fixture/host/fleet/tasks"
mv "$fixture/host/fleet/saved-tasks" "$fixture/host/fleet/tasks"
# Storage corruption is recovery-required, never a reason to replace acceptance.
printf '{}' > "$fixture/host/fleet/tasks/$id/state.json"
if task submit build --input "$fixture/package" --key same-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"recovery_required"' "$fixture/error"
cmp "$fixture/original-acceptance" "$fixture/host/fleet/tasks/$id/acceptance.json"
printf 'Task acceptance: concurrent deduplication, lost acknowledgments, conflicts, mapping, capabilities, outages, and corruption passed\n'
