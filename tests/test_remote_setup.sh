#!/bin/sh
# Remote setup foundation: CLI dispatch, usage errors, private resumable state,
# guided resume through verify/alias over a fake SSH boundary, and exit codes.
# Every case runs under umask 022 and again under umask 002.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
fixture="$(cd "$fixture" && pwd -P)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
fleet="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
version="$(sed -n 's/^#define F_VERSION "\(.*\)"$/\1/p' "$root/src/fleet/fleet.h")"
[ -n "$version" ]
unset CI HYDRA_NONINTERACTIVE

mkdir "$fixture/bin"
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
while [ $# -gt 1 ]; do shift; done
exec /bin/sh -c "$1"
SSH
chmod +x "$fixture/bin/ssh"
# A remote Hydra that answers only the fleet handshake with a chosen version.
cat > "$fixture/remote-hydra" <<'HYDRA'
#!/bin/sh
cat >/dev/null
printf '{"schema_version":1,"ok":true,"command":"fleet-handshake","data":{"hydra_version":"%s","fleet_protocol":1,"state_schema":2,"event_schema":1,"json_schema":1,"capabilities":[]}}\n' "$(cat "$SETUP_TEST_VERSION_FILE")"
HYDRA
chmod +x "$fixture/remote-hydra"
PATH="$fixture/bin:$PATH"
SETUP_TEST_VERSION_FILE="$fixture/remote-version"
export PATH SETUP_TEST_VERSION_FILE

fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
# run EXPECTED_STATUS OUTPUT_FILE ARGS...: stdout to OUTPUT_FILE, stderr to OUTPUT_FILE.err.
run() {
    expected=$1 out=$2; shift 2
    code=0
    "$fleet" remote "$@" > "$out" 2> "$out.err" < /dev/null || code=$?
    [ "$code" -eq "$expected" ] || { cat "$out" "$out.err" >&2; fail "remote $* exited $code, expected $expected"; }
}
has() { grep -q -- "$2" "$1" || { cat "$1" >&2; fail "$1 lacks $2"; }; }
lacks() { if grep -q -- "$2" "$1"; then cat "$1" >&2; fail "$1 unexpectedly has $2"; fi; }
# exact_mode PATH OCTAL: permission bits equal OCTAL exactly.
exact_mode() { [ -n "$(find "$1" -prune -perm "$2")" ]; }
state() { printf '%s/fleet/setup/%s.json' "$HYDRA_HOME" "$1"; }
# write_state NAME DEST HYDRA STEPS_JSON: a record the guided flow can resume.
write_state() {
    printf '{"schema_version":1,"kind":"remote-setup","name":"%s","destination":"%s","ssh_config":"","steps":%s,"remote":{"target":"%s","ssh_config":"","hydra":"%s","home":"","principal":"","project":"","accepted_host_key":"SHA256:fixture","multiplex":false}}' \
        "$1" "$2" "$4" "$2" "$3" > "$(state "$1")"
    chmod 600 "$(state "$1")"
}
done_through_agents='"host_key":{"status":"done"},"preflight":{"status":"done"},"provision":{"status":"done"},"agents":{"status":"done"}'

cases() {
    out="$fixture/out"
    # Usage and parsing errors never create state.
    run 1 "$out" setup --json; has "$out" '"code":"invalid_input"'; has "$out" '"command":"remote-setup"'
    run 1 "$out" setup 'bad/name' host --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup status host --json; has "$out" '"code":"setup_not_started"'; has "$out" '"command":"remote-setup-status"'
    run 1 "$out" setup n1 host extra --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup n1 host --ssh-config relative --json; has "$out" 'absolute path'
    run 1 "$out" setup n1 host --timeout 0 --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup n1 host --binary --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup n1 host --approve 0000 --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" trust-key n1 --fingerprint MD5:aa --json; has "$out" 'SHA256'
    run 1 "$out" provision n1 --approve abc --json; has "$out" 'plan_sha256'
    run 1 "$out" agents n1 --record claude=relative --json; has "$out" 'EXECUTABLE='
    run 1 "$out" install-agent n1 --json; has "$out" '--agent is required'
    run 1 "$out" sign-in n1 --agent 'a b' --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" preflight n1 --json; has "$out" '"code":"setup_not_started"'; has "$out" '"command":"remote-preflight"'
    test ! -e "$HYDRA_HOME/fleet/setup/n1.json" || fail "usage errors created state"
    run 0 "$out" setup --help --json; has "$out" '"command":"remote-setup-help"'; has "$out" 'install-agent'

    # A new setup records its binding, then stops at the first unimplemented step.
    run 1 "$out" setup n1 user@host1 --json
    has "$out" '"code":"not_implemented"'; has "$out" '"setup_schema":1'
    has "$out" '"steps":\[{"id":"host_key","status":"pending"'
    has "$out" '"next":{"step":"host_key","argv":\["hydra","remote","trust-key","n1","--json"\],"approval_sha256":null}'
    exact_mode "$HYDRA_HOME/fleet/setup" 700 || fail "setup directory is not private"
    exact_mode "$(state n1)" 600 || fail "setup state is not private"
    exact_mode "$HYDRA_HOME/fleet/setup/n1.lock" 600 || fail "setup lock is not private"
    has "$(state n1)" '"destination":"user@host1"'
    run 1 "$out" setup n1 user@host2 --json; has "$out" '"code":"setup_binding_changed"'
    run 1 "$out" setup n1 user@host1 --ssh-config /etc/other --json; has "$out" '"code":"setup_binding_changed"'
    has "$(state n1)" '"destination":"user@host1"'
    run 1 "$out" setup n1 --json; has "$out" '"code":"not_implemented"'
    # Every step command dispatches against the same state.
    for step in trust-key preflight agents; do
        run 1 "$out" "$step" n1 --json
        has "$out" '"code":"not_implemented"'; has "$out" "\"command\":\"remote-$step\""; has "$out" '"steps":'
    done
    # Provisioning is implemented (tests/test_remote_provision.sh) and needs preflight first.
    run 1 "$out" provision n1 --json
    has "$out" '"code":"prerequisite_missing"'; has "$out" '"command":"remote-provision"'; has "$out" '"steps":'
    run 1 "$out" install-agent n1 --agent claude --json; has "$out" '"command":"remote-install-agent"'
    run 1 "$out" sign-in n1 --agent claude --json; has "$out" '"command":"remote-sign-in"'
    run 0 "$out" setup status n1 --json; has "$out" '"ok":true'; has "$out" '"destination":"user@host1"'

    # Human mode keeps stdout empty and prints steps plus the next command.
    run 1 "$out" setup n1
    [ ! -s "$out" ] || fail "human setup output reached stdout"
    has "$out.err" 'host key'; has "$out.err" 'not_implemented'; has "$out.err" 'next: hydra remote trust-key n1'
    run 0 "$out" setup status n1
    [ ! -s "$out" ] || fail "human status output reached stdout"
    has "$out.err" 'next: hydra remote trust-key n1'

    # Unsafe or corrupt state is preserved and refused.
    cp "$(state n1)" "$fixture/n1.saved"
    chmod 644 "$(state n1)"
    run 1 "$out" setup status n1 --json; has "$out" '"code":"state_invalid"'
    printf 'not json' > "$(state n1)"; chmod 600 "$(state n1)"
    run 1 "$out" setup n1 --json; has "$out" '"code":"state_invalid"'
    [ "$(cat "$(state n1)")" = 'not json' ] || fail "invalid state was rewritten"
    cp "$fixture/n1.saved" "$(state n1)"; chmod 600 "$(state n1)"

    # Guided resume skips done steps, verifies the exact version, then
    # publishes the alias last and reports completion.
    printf '%s' "$version" > "$SETUP_TEST_VERSION_FILE"
    write_state n2 fixture-host "$fixture/remote-hydra" "{$done_through_agents}"
    printf '0.0.1' > "$SETUP_TEST_VERSION_FILE"
    run 1 "$out" setup n2 --json; has "$out" '"code":"version_mismatch"'; has "$out" '"id":"verify","status":"failed"'
    test ! -e "$HYDRA_HOME/fleet/remotes/n2.json" || fail "alias published before verification"
    printf '%s' "$version" > "$SETUP_TEST_VERSION_FILE"
    run 0 "$out" setup n2 --json
    has "$out" '"complete":true'; has "$out" '"next":null'
    has "$out" '"id":"verify","status":"done"'; has "$out" '"id":"alias","status":"done"'
    has "$HYDRA_HOME/fleet/remotes/n2.json" '"hydra":"[^"]*remote-hydra"'
    has "$HYDRA_HOME/fleet/remotes/n2.json" '"accepted_host_key":"SHA256:fixture"'
    run 0 "$out" setup n2; has "$out.err" 'remote setup complete'
    run 0 "$out" list; has "$out" '"n2"'

    # An existing different alias is never overwritten.
    run 0 "$out" add n3 other-host
    write_state n3 fixture-host "$fixture/remote-hydra" "{$done_through_agents}"
    run 1 "$out" setup n3 --json; has "$out" '"code":"alias_conflict"'; has "$out" '"id":"alias","status":"blocked"'
    has "$HYDRA_HOME/fleet/remotes/n3.json" '"target":"other-host"'

    # Selected agents add per-agent steps before verification.
    write_state n4 fixture-host "$fixture/remote-hydra" \
        "{$done_through_agents,\"agents\":{\"status\":\"done\",\"detail\":{\"selected\":[\"claude\"],\"summary\":\"claude found\"}}}"
    run 1 "$out" setup n4 --json
    has "$out" '"command":"remote-setup"'; has "$out" '"id":"install_agent:claude"'; has "$out" '"id":"sign_in:claude"'
    has "$out" '"detail":"claude found"'
    has "$out" '"argv":\["hydra","remote","install-agent","n4","--agent","claude","--json"\]'
    lacks "$out" '"id":"verify","status":"done"'
}

for mask in 022 002; do
    (
        umask "$mask"
        HYDRA_HOME="$fixture/home-$mask"; export HYDRA_HOME
        cases
    )
done
printf 'Remote setup foundation acceptance passed\n'
