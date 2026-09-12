#!/bin/sh
# Public fleet attention projection checks over controlled SSH responses.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
cleanup() { rm -rf "$fixture"; }
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
HYDRA_HOME="$fixture/home"
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
mkdir -p "$fixture/bin"
export HYDRA_HOME HYDRA_FLEET_BIN
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
while [ "$#" -gt 2 ]; do shift; done
if [ -n "${HYDRA_TEST_OFFLINE_FILE:-}" ] && [ -f "$HYDRA_TEST_OFFLINE_FILE" ]; then exit 255; fi
request="$(cat)"
case "$request" in
  *'"action":"handshake"'*) printf '%s\n' '{"schema_version":1,"ok":true,"command":"fleet-handshake","data":{"hydra_version":"2.1.0","fleet_protocol":1,"state_schema":2,"event_schema":1,"json_schema":1,"capabilities":["overview"]}}' ;;
  *)
    if [ -n "${HYDRA_TEST_MALFORMED:-}" ]; then printf '%s\n' '{malformed'; exit 0; fi
    cat "$HYDRA_TEST_ATTENTION_FIXTURE" > "/tmp/hydra-attention.$$"
    case "${HYDRA_TEST_EXPIRY:-}" in
      past) sed 's/"expires_at" *: *"4102444800"/"expires_at":"1"/g' "/tmp/hydra-attention.$$" ;;
      malformed) sed 's/"expires_at" *: *"4102444800"/"expires_at":"not-a-time"/g' "/tmp/hydra-attention.$$" ;;
      null) sed 's/"expires_at" *: *"4102444800"/"expires_at":null/g' "/tmp/hydra-attention.$$" ;;
      zero) sed 's/"expires_at" *: *"4102444800"/"expires_at":"0"/g' "/tmp/hydra-attention.$$" ;;
      *) cat "/tmp/hydra-attention.$$" ;;
    esac
    rm -f "/tmp/hydra-attention.$$"
    ;;
esac
SSH
chmod +x "$fixture/bin/ssh"
PATH="$fixture/bin:$PATH"
export PATH HYDRA_TEST_ATTENTION_FIXTURE="$root/tests/fixtures/fleet/attention.json"
"$root/bin/hydra" remote add build loopback --hydra "$root/bin/hydra"
"$root/bin/hydra" fleet attention --json > "$fixture/attention"
grep -q '"command":"fleet-attention"' "$fixture/attention"
grep -q '"kind":"result"' "$fixture/attention"
[ "$(grep -o '"kind":"approval"' "$fixture/attention" | wc -l | tr -d ' ')" -eq 2 ]
grep -q '"request_id":"request_a"' "$fixture/attention"
grep -q '"request_id":"request_b"' "$fixture/attention"
grep -q '"reason":"missing_identity"' "$fixture/attention"
grep -q '"reason":"approval_binding_unknown"' "$fixture/attention"
grep -q '"reason":"result_binding_unknown"' "$fixture/attention"
if grep -q 'task_cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc' "$fixture/attention"; then exit 1; fi
if grep -q '"fresh_action":true' "$fixture/attention"; then exit 1; fi
if grep -q '"accepted":true' "$fixture/attention"; then exit 1; fi
grep -q '"step_id":"step_a","attempt_id":"attempt-a"' "$fixture/attention"
grep -q '"step_id":"step_b","attempt_id":"attempt-b"' "$fixture/attention"
if grep -q '"step_id":"step_b","attempt_id":"attempt-a"' "$fixture/attention"; then exit 1; fi
unset HYDRA_TEST_OFFLINE_FILE
cache_good="$fixture/cache-good"
cp "$HYDRA_HOME/fleet/observations/build.json" "$cache_good"
"$root/bin/hydra" fleet attention-data --json > "$fixture/attention-data"
grep -q '^HYDRA_ATTENTION	1$' "$fixture/attention-data"
grep -q "^ITEM$(printf '\t').*$(printf '\t')run_parallel$(printf '\t')step_a$(printf '\t')attempt-a$(printf '\t')" "$fixture/attention-data"
grep -q "^ITEM$(printf '\t').*$(printf '\t')run_parallel$(printf '\t')step_b$(printf '\t')attempt-b$(printf '\t')" "$fixture/attention-data"
[ "$(awk -F '\t' '$1 == "ITEM" && $13 == "request_a" { n++ } END { print n + 0 }' "$fixture/attention-data")" -eq 1 ]
[ "$(awk -F '\t' '$1 == "ITEM" && $13 == "request_b" { n++ } END { print n + 0 }' "$fixture/attention-data")" -eq 1 ]
json_items="$(grep -o '"kind":"\(approval\|result\|unknown\|approval_expired\)"' "$fixture/attention" | wc -l | tr -d ' ')"
data_items="$(grep -c '^ITEM	' "$fixture/attention-data")"
[ "$data_items" -eq "$json_items" ]
data_count="$(awk -F '\t' '/^END	/ { print $2 }' "$fixture/attention-data")"
[ "$data_count" -eq "$data_items" ]
awk -F '\t' '/^ITEM	/ { if (NF != 19 || length($15) != 64 || length($16) != 64 || $15 !~ /^[0-9a-f]+$/ || $16 !~ /^[0-9a-f]+$/) exit 1 }' "$fixture/attention-data"
cat > "$fixture/bin/shasum" <<'SHASUM'
#!/bin/sh
exit 1
SHASUM
chmod +x "$fixture/bin/shasum"
if "$root/bin/hydra" fleet attention-data --json > "$fixture/attention-data-malformed"; then exit 1; fi
grep -q '"code":"projection_failed"' "$fixture/attention-data-malformed"
if grep -q '^HYDRA_ATTENTION	1$' "$fixture/attention-data-malformed"; then exit 1; fi
rm "$fixture/bin/shasum"
for case in past malformed null zero; do
  HYDRA_TEST_EXPIRY="$case"
  export HYDRA_TEST_EXPIRY
  "$root/bin/hydra" fleet attention --json > "$fixture/expiry-$case"
done
grep -q '"kind":"approval_expired"' "$fixture/expiry-past"
grep -q '"reason":"malformed_expiry"' "$fixture/expiry-malformed"
grep -q '"reason":"malformed_expiry"' "$fixture/expiry-null"
grep -q '"kind":"approval"' "$fixture/expiry-zero"
rm "$HYDRA_HOME/fleet/observations/build.json"
unset HYDRA_TEST_OFFLINE_FILE
export HYDRA_TEST_MALFORMED=1
"$root/bin/hydra" fleet attention --json > "$fixture/malformed-host"
grep -q '"reason":"invalid_response"' "$fixture/malformed-host"
unset HYDRA_TEST_MALFORMED
: > "$fixture/offline"
export HYDRA_TEST_OFFLINE_FILE="$fixture/offline"
"$root/bin/hydra" fleet attention --json > "$fixture/no-cache-offline"
grep -q '"kind":"unknown"' "$fixture/no-cache-offline"
grep -q '"reason":"offline"' "$fixture/no-cache-offline"
grep -q '"partial":true' "$fixture/no-cache-offline"
# An explicit offline marker must select the cached observation path.
: > "$fixture/offline"
cp "$cache_good" "$HYDRA_HOME/fleet/observations/build.json"
export HYDRA_TEST_OFFLINE_FILE="$fixture/offline"
"$root/bin/hydra" fleet attention --json > "$fixture/cached-stale"
grep -q '"freshness":"stale"' "$fixture/cached-stale"
grep -q '"kind":"approval"' "$fixture/cached-stale"
grep -q '"partial":true' "$fixture/cached-stale"
if grep -q '"fresh_action":true' "$fixture/cached-stale"; then exit 1; fi
if grep -q '"accepted":true' "$fixture/cached-stale"; then exit 1; fi
printf '%s\n' 'fleet attention public CLI checks passed'
