#!/bin/sh
# Public fleet CLI tests with a controlled SSH transport and real C server.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
HYDRA_HOME="$fixture/home"
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
export HYDRA_HOME HYDRA_FLEET_BIN
mkdir -p "$fixture/bin"
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
while [ $# -gt 2 ]; do shift; done
case "$1" in
    key) echo 'Host key verification failed.' >&2; exit 255 ;;
    auth) echo 'Permission denied (publickey).' >&2; exit 255 ;;
    offline) echo 'Connection refused' >&2; exit 255 ;;
    slow) [ -z "${HYDRA_TEST_SSH_PID:-}" ] || echo $$ > "$HYDRA_TEST_SSH_PID"; exec sleep 30 ;;
    malformed) echo '{bad json'; exit ;;
    skew) printf '{"schema_version":1,"ok":true,"command":"fleet-handshake","data":{"hydra_version":"2.0.0","fleet_protocol":9,"capabilities":[]}}\n'; exit ;;
esac
exec /bin/sh -c "$2"
SSH
chmod +x "$fixture/bin/ssh"
PATH="$fixture/bin:$PATH"
export PATH
"$root/bin/hydra" remote add good good --hydra "$root/bin/hydra"
"$root/bin/hydra" fleet list --json > "$fixture/result"
grep -q '"heads":\[\]' "$fixture/result"
if "$root/bin/hydra" remote add bad 'host;touch nope' >/dev/null 2>&1; then exit 1; fi
for host in key auth offline slow malformed skew; do
    "$root/bin/hydra" remote add "$host" "$host" >/dev/null
done
if "$root/bin/hydra" fleet list --json --timeout 1 --jobs 8 > "$fixture/result"; then exit 1; fi
for code in host_key_failed authentication_failed offline timeout invalid_response version_mismatch; do
    grep -q "\"code\":\"$code\"" "$fixture/result"
done
grep -q '"partial":true' "$fixture/result"
for host in key auth offline malformed skew good; do "$root/bin/hydra" remote remove "$host" >/dev/null; done
HYDRA_TEST_SSH_PID="$fixture/ssh-pid"
export HYDRA_TEST_SSH_PID
"$root/bin/hydra" fleet list --json --timeout 30 > "$fixture/cancel-result" &
coordinator=$!
attempt=0
while [ ! -f "$HYDRA_TEST_SSH_PID" ] && [ "$attempt" -lt 50 ]; do sleep 0.1; attempt=$((attempt + 1)); done
[ -f "$HYDRA_TEST_SSH_PID" ] || { kill "$coordinator"; exit 1; }
ssh_pid="$(cat "$HYDRA_TEST_SSH_PID")"
kill -TERM "$coordinator"
if wait "$coordinator"; then exit 1; fi
if kill -0 "$ssh_pid" 2>/dev/null; then kill "$ssh_pid" 2>/dev/null || :; echo 'SSH survived cancellation'; exit 1; fi
printf 'Fleet CLI transport, partial failures, and cancellation passed\n'
