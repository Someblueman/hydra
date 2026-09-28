#!/bin/sh
# Remote setup over a fake SSH boundary: CLI dispatch, usage errors, private
# resumable state, host-key probe of an already trusted host, read-only
# preflight (fake uname, restricted PATH, remote umask 002, existing Hydra),
# and guided resume through verify/alias with exit codes. Every case runs
# under a local umask of 022 and again under 002.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"
fixture="$(test_mktemp_dir)"
fixture="$(cd "$fixture" && pwd -P)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
fleet="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test-fleet or make test-one T=remote_setup}"
version="$(sed -n 's/^#define F_VERSION "\(.*\)"$/\1/p' "$root/src/fleet/fleet.h")"
[ -n "$version" ]
unset CI HYDRA_NONINTERACTIVE
SETUP_REAL_SSH="$(command -v ssh)"
# This host's helper is the unpinned source for a remote of the same platform.
HYDRA_FLEET_BIN="$fleet"; export HYDRA_FLEET_BIN
local_os="$(uname -s)" local_arch="$(uname -m)"
case "$local_arch" in arm64|aarch64) darwin_other=x86_64 ;; *) darwin_other=arm64 ;; esac

# Fake SSH: `ssh -G` is answered by OpenSSH itself; anything else runs the
# remote command locally with an optional remote PATH, HOME and umask.
mkdir "$fixture/bin" "$fixture/remote-bin" "$fixture/remote-home"
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
for arg do [ "$arg" = -G ] && exec "$SETUP_REAL_SSH" "$@"; done
while [ $# -gt 1 ]; do shift; done
if [ -n "${SETUP_REMOTE_PATH:-}" ]; then PATH=$SETUP_REMOTE_PATH; fi
if [ -n "${SETUP_REMOTE_HOME:-}" ]; then HOME=$SETUP_REMOTE_HOME; fi
umask "${SETUP_REMOTE_UMASK:-022}"
exec /bin/sh -c "$1"
SSH
cat > "$fixture/remote-bin/uname" <<'UNAME'
#!/bin/sh
case "${1:-}" in
    -m) printf '%s\n' "${SETUP_REMOTE_ARCH:-x86_64}" ;;
    *) printf '%s\n' "${SETUP_REMOTE_OS:-Linux}" ;;
esac
UNAME
# A remote Hydra that answers --version and the fleet handshake. A copy with
# a sibling "version" file reports that version (an older pinned install).
cat > "$fixture/remote-hydra" <<'HYDRA'
#!/bin/sh
version_file="${0%/*}/version"
[ -f "$version_file" ] || version_file=$SETUP_TEST_VERSION_FILE
if [ "${1:-}" = --version ]; then printf 'Hydra version %s\n' "$(cat "$version_file")"; exit 0; fi
cat >/dev/null
printf '{"schema_version":1,"ok":true,"command":"fleet-handshake","data":{"hydra_version":"%s","fleet_protocol":1,"state_schema":2,"event_schema":1,"json_schema":1,"capabilities":[]}}\n' "$(cat "$version_file")"
HYDRA
chmod +x "$fixture/bin/ssh" "$fixture/remote-bin/uname" "$fixture/remote-hydra"
# find_tool NAME: first executable regular file on PATH (dash's command -v
# also reports files without the execute bit).
find_tool() {
    old_ifs=$IFS; IFS=:
    for dir in $PATH; do
        if [ -f "$dir/$1" ] && [ -x "$dir/$1" ]; then IFS=$old_ifs; printf '%s\n' "$dir/$1"; return 0; fi
    done
    IFS=$old_ifs; return 1
}
# remote_path DIR TOOL...: a restricted remote PATH holding only these tools.
remote_path() {
    target=$1; shift
    mkdir -p "$target"
    ln -sf "$fixture/remote-bin/uname" "$target/uname"
    for tool in awk df env cat "$@"; do
        if found="$(find_tool "$tool")"; then ln -sf "$found" "$target/$tool"; fi
    done
}
remote_path "$fixture/remote-full" git tmux curl mktemp head tail shasum sha256sum
remote_path "$fixture/remote-bare" mktemp head tail shasum sha256sum
cat > "$fixture/ssh_config" <<CONFIG
Host *
  UserKnownHostsFile $fixture/known_hosts
  GlobalKnownHostsFile /dev/null
CONFIG
PATH="$fixture/bin:$PATH"
SETUP_TEST_VERSION_FILE="$fixture/remote-version"
SETUP_REMOTE_HOME="$fixture/remote-home"
export PATH SETUP_TEST_VERSION_FILE SETUP_REAL_SSH SETUP_REMOTE_HOME
printf '%s' "$version" > "$SETUP_TEST_VERSION_FILE"

fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
# run EXPECTED_STATUS OUTPUT_FILE ARGS...: stdout to OUTPUT_FILE, stderr to OUTPUT_FILE.err.
run() {
    expected=$1 out=$2; shift 2
    code=0
    "$fleet" remote "$@" > "$out" 2> "$out.err" < /dev/null || code=$?
    [ "$expected" = any ] || [ "$code" -eq "$expected" ] || { cat "$out" "$out.err" >&2; fail "remote $* exited $code, expected $expected"; }
}
has() { grep -q -- "$2" "$1" || { cat "$1" >&2; [ ! -f "$1.err" ] || cat "$1.err" >&2; fail "$1 lacks $2"; }; }
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

usage_cases() {
    run 1 "$out" setup --json; has "$out" '"code":"invalid_input"'; has "$out" '"command":"remote-setup"'
    run 1 "$out" setup 'bad/name' host --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup status host --json; has "$out" '"code":"setup_not_started"'; has "$out" '"command":"remote-setup-status"'
    run 1 "$out" setup n1 host extra --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup n1 host --ssh-config relative --json; has "$out" 'absolute path'
    run 1 "$out" setup n1 host --timeout 0 --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup n1 host --binary --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup n1 host --approve 0000 --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup n1 host --remote-build --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup n1 host --project /srv/p --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" setup n1 host --unknown --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" trust-key n1 --fingerprint MD5:aa --json; has "$out" 'SHA256'
    run 1 "$out" provision n1 --approve abc --json; has "$out" 'plan_sha256'
    run 1 "$out" agents n1 --record claude=relative --json; has "$out" 'EXECUTABLE='
    run 1 "$out" install-agent n1 --json; has "$out" '--agent is required'
    run 1 "$out" sign-in n1 --agent 'a b' --json; has "$out" '"code":"invalid_input"'
    run 1 "$out" preflight n1 --json; has "$out" '"code":"setup_not_started"'; has "$out" '"command":"remote-preflight"'
    test ! -e "$HYDRA_HOME/fleet/setup/n1.json" || fail "usage errors created state"
    run 0 "$out" setup --help --json; has "$out" '"command":"remote-setup-help"'; has "$out" 'install-agent'
    ! grep -Eq -- '--remote-build|--project' "$out" || fail "setup help lists removed options"
}

guided_cases() {
    # A new setup records its binding, trusts the already known host and
    # passes preflight before later steps take over.
    SETUP_REMOTE_PATH="$fixture/remote-full" SETUP_REMOTE_ARCH=aarch64 run any "$out" setup n1 user@host1 --ssh-config "$fixture/ssh_config" --json
    has "$out" '"setup_schema":1'
    has "$out" '"id":"host_key","status":"done","detail":"verified by ssh'
    has "$out" '"id":"preflight","status":"done","detail":"Linux aarch64'
    lacks "$out" '"step":"host_key"'
    exact_mode "$HYDRA_HOME/fleet/setup" 700 || fail "setup directory is not private"
    exact_mode "$(state n1)" 600 || fail "setup state is not private"
    exact_mode "$HYDRA_HOME/fleet/setup/n1.lock" 600 || fail "setup lock is not private"
    test ! -e "$fixture/known_hosts" || fail "a known host was written to known_hosts"
    has "$(state n1)" '"destination":"user@host1"'
    run 1 "$out" setup n1 user@host2 --json; has "$out" '"code":"setup_binding_changed"'
    run 1 "$out" setup n1 user@host1 --ssh-config /etc/other --json; has "$out" '"code":"setup_binding_changed"'
    run 0 "$out" trust-key n1 --json; has "$out" '"result":"already trusted"'; has "$out" '"command":"remote-trust-key"'
    # Every other step command dispatches against the same state.
    for step in provision agents; do
        run any "$out" "$step" n1 --json
        has "$out" "\"command\":\"remote-$step\""; has "$out" '"steps":'
    done
    run any "$out" install-agent n1 --agent claude --json; has "$out" '"command":"remote-install-agent"'
    run any "$out" sign-in n1 --agent claude --json; has "$out" '"command":"remote-sign-in"'
    run 0 "$out" setup status n1 --json; has "$out" '"ok":true'; has "$out" '"destination":"user@host1"'
    # Human mode keeps stdout empty and prints steps plus the next command.
    run 0 "$out" setup status n1
    [ ! -s "$out" ] || fail "human status output reached stdout"
    has "$out.err" 'host key'; has "$out.err" 'next: hydra remote'
}

preflight_cases() {
    write_state p1 fixture-host hydra '{}'
    SETUP_REMOTE_PATH="$fixture/remote-full" SETUP_REMOTE_ARCH=aarch64 run 0 "$out" preflight p1 --json
    has "$out" '"schema":"remote-preflight"'; has "$out" '"os":"Linux"'; has "$out" '"arch":"aarch64"'
    has "$out" '"name":"platform","status":"ok"'; has "$out" '"name":"git","status":"ok"'
    has "$out" '"hydra":{"path":null,"version":null,"reusable":false}'; has "$out" '"pins":\[\]'
    has "$out" '"name":"umask","status":"ok"'
    has "$(state p1)" '"preflight":{"status":"done"'
    # A Darwin remote without a pinned asset needs this host's helper for the
    # identical platform (an unpinned warning); any other Darwin platform and
    # an unknown one block with platform_unsupported and name --binary.
    SETUP_REMOTE_PATH="$fixture/remote-full" SETUP_REMOTE_OS=Darwin SETUP_REMOTE_ARCH="$darwin_other" run 1 "$out" preflight p1 --json
    has "$out" '"code":"platform_unsupported"'; has "$out" '"missing":\["platform"\]'; has "$out" '--binary FILE'
    has "$(state p1)" '"preflight":{"status":"blocked"'
    SETUP_REMOTE_PATH="$fixture/remote-full" SETUP_REMOTE_OS=Linux SETUP_REMOTE_ARCH=riscv64 run 1 "$out" preflight p1 --json
    has "$out" '"code":"platform_unsupported"'; has "$out" '"name":"platform","status":"missing"'
    if [ "$local_os" = Darwin ]; then
        SETUP_REMOTE_PATH="$fixture/remote-full" SETUP_REMOTE_OS=Darwin SETUP_REMOTE_ARCH="$local_arch" run 0 "$out" preflight p1 --json
        has "$out" '"name":"platform","status":"warning","blocking":false'; has "$out" 'installed unpinned'
        has "$(state p1)" '"preflight":{"status":"done"'
        # Without this host's helper the same platform blocks too.
        HYDRA_FLEET_BIN="$fixture/no-helper" SETUP_REMOTE_PATH="$fixture/remote-full" SETUP_REMOTE_OS=Darwin SETUP_REMOTE_ARCH="$local_arch" \
            run 1 "$out" preflight p1 --json
        has "$out" '"code":"platform_unsupported"'
    fi
    # Missing tools block; tmux and curl only warn.
    SETUP_REMOTE_PATH="$fixture/remote-bare" run 1 "$out" preflight p1
    has "$out.err" 'git: install git'; lacks "$out.err" 'tmux:'
    SETUP_REMOTE_PATH="$fixture/remote-bare" run 1 "$out" preflight p1 --json
    has "$out" '"missing":\["git"\]'; has "$out" '"name":"tmux","status":"warning"'
    has "$out" '"name":"curl","status":"warning"'; has "$out" '"git":null'
    # A group-writable remote umask is reported for R1.
    SETUP_REMOTE_PATH="$fixture/remote-full" SETUP_REMOTE_UMASK=002 run 0 "$out" preflight p1 --json
    has "$out" '"umask":"0002"'; has "$out" '"name":"umask","status":"warning"'
    # A same-version Hydra that completes the handshake is reusable; others are kept.
    ln -sf "$fixture/remote-hydra" "$fixture/remote-full/hydra"
    mkdir -p "$fixture/remote-home/.local/share/hydra/fleet/pin1/bin"
    ln -sf "$fixture/remote-hydra" "$fixture/remote-home/.local/share/hydra/fleet/pin1/bin/hydra"
    SETUP_REMOTE_PATH="$fixture/remote-full" run 0 "$out" preflight p1 --json
    has "$out" "\"version\":\"Hydra version $version\",\"reusable\":true"; has "$out" 'pin1"'
    printf '0.0.1' > "$SETUP_TEST_VERSION_FILE"
    SETUP_REMOTE_PATH="$fixture/remote-full" run 0 "$out" preflight p1 --json
    has "$out" '"reusable":false'; has "$out" '"name":"hydra","status":"warning"'
    printf '%s' "$version" > "$SETUP_TEST_VERSION_FILE"
    rm -rf "$fixture/remote-full/hydra" "$fixture/remote-home/.local"
    # Agents in search directories are reported with their source, including
    # the pi installer's ~/.pi/agent/bin.
    mkdir -p "$fixture/remote-home/.local/bin" "$fixture/remote-home/.pi/agent/bin"
    printf '#!/bin/sh\n' > "$fixture/remote-home/.local/bin/claude"; chmod +x "$fixture/remote-home/.local/bin/claude"
    printf '#!/bin/sh\n' > "$fixture/remote-home/.pi/agent/bin/pi"; chmod +x "$fixture/remote-home/.pi/agent/bin/pi"
    SETUP_REMOTE_PATH="$fixture/remote-full" run 0 "$out" preflight p1 --json
    has "$out" '"executable":"claude","path":"[^"]*remote-home[^"]*claude","source":"[^"]*","on_path":false'
    has "$out" '"executable":"pi","path":"[^"]*remote-home\\/.pi\\/agent\\/bin\\/pi"'
    # The native search list (preflight) is exactly the shell's (agent locate).
    native_dirs="$(sed -n 's/.*"search_dirs":\[\([^]]*\)\].*/\1/p' "$out" | sed 's#\\/#/#g')"
    shell_dirs="$(sh -c '. "$1/lib/cmd_lifecycle.sh" && agent_locate_search_dirs' sh "$root" | awk '{ printf "%s\"%s\"", (NR > 1 ? "," : ""), $0 }')"
    [ -n "$native_dirs" ] && [ "$native_dirs" = "$shell_dirs" ] || fail "search dirs differ: native [$native_dirs] shell [$shell_dirs]"
    rm -rf "$fixture/remote-home/.pi"
    rm -rf "$fixture/remote-home/.local"
}

state_cases() {
    # Unsafe or corrupt state is preserved and refused.
    cp "$(state n1)" "$fixture/n1.saved"
    chmod 644 "$(state n1)"
    run 1 "$out" setup status n1 --json; has "$out" '"code":"state_invalid"'
    printf 'not json' > "$(state n1)"; chmod 600 "$(state n1)"
    run 1 "$out" setup n1 --json; has "$out" '"code":"state_invalid"'
    [ "$(cat "$(state n1)")" = 'not json' ] || fail "invalid state was rewritten"
    cp "$fixture/n1.saved" "$(state n1)"; chmod 600 "$(state n1)"
}

finish_cases() {
    # Guided resume skips done steps, verifies the exact version, then
    # publishes the alias last and reports completion.
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
    run any "$out" setup n4 --json
    has "$out" '"command":"remote-setup"'; has "$out" '"id":"install_agent:claude"'; has "$out" '"id":"sign_in:claude"'
    has "$out" '"detail":"claude found"'
    lacks "$out" '"id":"verify","status":"done"'
}

# write_upgrade_state NAME DEST HYDRA ALIAS_HYDRA HOST_KEY STEPS_JSON: an upgrade
# whose provisioning (another step's work) produced HYDRA.
write_upgrade_state() {
    printf '{"schema_version":1,"kind":"remote-setup","name":"%s","destination":"%s","ssh_config":"","steps":%s,"upgrade":{"hydra":"%s","target":"%s"},"remote":{"target":"%s","ssh_config":"","hydra":"%s","home":"","principal":"","project":"","accepted_host_key":"%s","multiplex":false}}' \
        "$1" "$2" "$6" "$4" "$2" "$2" "$3" "$5" > "$(state "$1")"
    chmod 600 "$(state "$1")"
}
same_alias() { cmp -s "$HYDRA_HOME/fleet/remotes/$1.json" "$2" || fail "alias $1 changed"; }

upgrade_cases() {
    old="$fixture/old-pin-$mask" new="$fixture/new-pin-$mask"
    mkdir -p "$old/bin" "$new/bin"
    cp "$fixture/remote-hydra" "$old/bin/hydra"; printf '2.7.0' > "$old/bin/version"
    cp "$fixture/remote-hydra" "$new/bin/hydra"
    cp "$old/bin/hydra" "$fixture/old-pin.bytes"
    run 0 "$out" add up1 up-host --hydra "$old/bin/hydra" --home /remote/state
    cp "$HYDRA_HOME/fleet/remotes/up1.json" "$fixture/up1.alias"
    # An enrolled alias is never re-pointed.
    run 1 "$out" setup up1 other-host --json; has "$out" '"code":"alias_conflict"'; has "$out" 'never re-points'
    test ! -e "$(state up1)" || fail "a refused upgrade created state"
    same_alias up1 "$fixture/up1.alias"
    # Upgrade mode binds to the alias and checks the install it uses today.
    SETUP_REMOTE_PATH="$fixture/remote-full" run any "$out" setup up1 --json
    has "$out" '"destination":"up-host"'; has "$out" '"id":"host_key","status":"done"'; has "$out" '"id":"preflight","status":"done"'
    has "$(state up1)" '"upgrade":{"hydra":"[^"]*old-pin[^"]*","target":"up-host"}'
    has "$(state up1)" '"alias_hydra":{"path":"[^"]*old-pin[^"]*","version":"2.7.0","reusable":false}'
    has "$(state up1)" '"home":"\\/remote\\/state"'
    lacks "$out" '"id":"provision","status":"skipped"'
    same_alias up1 "$fixture/up1.alias"
    # After provisioning a new pin, the alias update needs approval of old -> new.
    write_upgrade_state up1 up-host "$new/bin/hydra" "$old/bin/hydra" SHA256:fixture \
        "{\"host_key\":{\"status\":\"done\"},\"preflight\":{\"status\":\"done\",\"detail\":{\"alias_hydra\":{\"path\":\"$old/bin/hydra\",\"version\":\"2.7.0\",\"reusable\":false}}},\"provision\":{\"status\":\"done\"},\"agents\":{\"status\":\"done\"}}"
    run 3 "$out" setup up1 --json
    has "$out" '"code":"approval_required"'; has "$out" '"step":"alias"'
    has "$out" '"old_hydra":"[^"]*old-pin'; has "$out" '"new_hydra":"[^"]*new-pin'; has "$out" '"old_version":"2.7.0"'
    has "$out" "\"new_version\":\"$version\""; has "$out" '"previous_install":"stays installed'
    has "$out" '"argv":\["hydra","remote","setup","up1","--approve","[0-9a-f]*","--json"\]'
    hash="$(sed -n 's/.*"plan_sha256":"\([0-9a-f]*\)".*/\1/p' "$out")"
    same_alias up1 "$fixture/up1.alias"
    run 1 "$out" setup up1 --approve 0000000000000000000000000000000000000000000000000000000000000000 --json
    has "$out" '"code":"approval_mismatch"'
    same_alias up1 "$fixture/up1.alias"
    run 0 "$out" setup up1 --approve "$hash" --json
    has "$out" '"complete":true'; has "$out" '"id":"alias","status":"done","detail":"alias updated'
    has "$HYDRA_HOME/fleet/remotes/up1.json" '"hydra":"[^"]*new-pin[^"]*"'
    has "$HYDRA_HOME/fleet/remotes/up1.json" '"target":"up-host"'
    has "$HYDRA_HOME/fleet/remotes/up1.json" '"home":"\\/remote\\/state"'
    has "$HYDRA_HOME/fleet/remotes/up1.json" '"accepted_host_key":"SHA256:fixture"'
    exact_mode "$HYDRA_HOME/fleet/remotes/up1.json" 600 || fail "updated alias is not private"
    cmp -s "$old/bin/hydra" "$fixture/old-pin.bytes" && [ "$(cat "$old/bin/version")" = 2.7.0 ] || fail "old pin changed"
    run 0 "$out" setup up1 --json; has "$out" '"complete":true'
    # A same-version install behind the alias is reused: no provisioning, no alias change.
    run 0 "$out" add up2 up-host2 --hydra "$new/bin/hydra"
    cp "$HYDRA_HOME/fleet/remotes/up2.json" "$fixture/up2.alias"
    SETUP_REMOTE_PATH="$fixture/remote-full" run any "$out" setup up2 --json
    has "$out" '"id":"provision","status":"skipped","detail":"existing Hydra'
    write_upgrade_state up2 up-host2 "$new/bin/hydra" "$new/bin/hydra" "" \
        '{"host_key":{"status":"done"},"preflight":{"status":"done"},"provision":{"status":"skipped"},"agents":{"status":"done"}}'
    run 0 "$out" setup up2 --json
    has "$out" '"id":"alias","status":"skipped","detail":"alias already uses this install"'
    same_alias up2 "$fixture/up2.alias"
}

for mask in 022 002; do
    (
        umask "$mask"
        HYDRA_HOME="$fixture/home-$mask"; export HYDRA_HOME
        out="$fixture/out"
        usage_cases
        guided_cases
        preflight_cases
        state_cases
        finish_cases
        upgrade_cases
    )
done
printf 'Remote setup acceptance passed\n'
