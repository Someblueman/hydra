#!/bin/sh
# Scoped revocation and live discovery sources through the public CLI:
# usage, JSON schemas, private record modes under umask 022 and 002, blocked
# alias reuse and remote operations without any SSH connection, re-import of
# a revoked identity, and the ssh-config/ansible/dns-sd source reports.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"
fixture="$(test_mktemp_dir)"
fixture="$(cd "$fixture" && pwd -P)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test or make test-one T=revocation}"
export HYDRA_FLEET_BIN HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1
test_count=0; pass_count=0; fail_count=0
hydra="$root/bin/hydra"
real_ssh="$(command -v ssh)"
# ssh -G is answered by OpenSSH itself; any connection attempt is a failure.
mkdir -p "$fixture/bin"
cat > "$fixture/bin/ssh" <<SSH
#!/bin/sh
for arg do [ "\$arg" = -G ] && exec "$real_ssh" "\$@"; done
echo "unexpected SSH connection: \$*" >> "$fixture/connections"
exit 255
SSH
chmod +x "$fixture/bin/ssh"
PATH="$fixture/bin:$PATH"; export PATH
printf 'Host build other\n HostName build.invalid\n User me\nHost pat*\n HostName x\n' > "$fixture/ssh.conf"
chmod 600 "$fixture/ssh.conf"
printf '[web]\nweb1 ansible_host=build ansible_ssh_pass=not-in-output\nweb2 ansible_host=other\n' > "$fixture/hosts.ini"
json_field() { sed -n "s/.*\"$1\":\"\([^\"]*\)\".*/\1/p"; }

run_cases() {
    HYDRA_HOME="$fixture/home-$1"; export HYDRA_HOME
    # Hydra creates its own home privately; a fixture home must be as private.
    (umask 077 && mkdir -p "$HYDRA_HOME/fleet/remotes")
    printf '{"schema_version":1,"target":"build","hydra":"hydra","home":"","accepted_host_key":"SHA256:oldkey","multiplex":false}\n' > "$HYDRA_HOME/fleet/remotes/build.json"
    cp "$HYDRA_HOME/fleet/remotes/build.json" "$fixture/alias-before"

    out="$("$hydra" fleet revoke --json 2>&1)" && status=0 || status=$?
    assert_failure "$status" "umask $1: revoke without scope fails"
    assert_equal invalid_input "$(printf '%s' "$out" | json_field code)" "umask $1: usage error is typed"
    out="$("$hydra" fleet revoke transport build 2>&1)" && status=0 || status=$?
    assert_failure "$status" "umask $1: revoke without --reason fails"
    out="$("$hydra" fleet revoke bogus build --reason x 2>&1)" && status=0 || status=$?
    assert_failure "$status" "umask $1: unknown scope fails"

    out="$("$hydra" fleet revoke transport build --reason "key rotated" --json)" && status=0 || status=$?
    assert_success "$status" "umask $1: transport revocation succeeds"
    assert_equal fleet-revoke "$(printf '%s' "$out" | json_field command)" "umask $1: revoke envelope command"
    assert_equal transport "$(printf '%s' "$out" | json_field scope)" "umask $1: revoke envelope scope"
    assert_equal "key rotated" "$(printf '%s' "$out" | json_field reason)" "umask $1: revoke retains the reason"
    assert_equal 'SHA256:oldkey' "$(printf '%s' "$out" | json_field accepted_host_key)" "umask $1: superseded key retained"
    case "$out" in *'"revoked_at":'*'"revoked_by":{"user":'*) revoked_by=yes ;; *) revoked_by=no ;; esac
    assert_equal yes "$revoked_by" "umask $1: who and when are recorded"
    out="$("$hydra" fleet revoke transport build --reason again --json 2>&1)" && status=0 || status=$?
    assert_failure "$status" "umask $1: second revocation fails"
    assert_equal already_revoked "$(printf '%s' "$out" | json_field code)" "umask $1: second revocation is typed"

    record="$(find "$HYDRA_HOME/fleet/revocations/transport" -name '*.json' | head -n 1)"
    assert_equal 600 "$(stat -c %a "$record")" "umask $1: revocation record is private"
    assert_equal 700 "$(stat -c %a "$HYDRA_HOME/fleet/revocations")" "umask $1: revocation store is private"
    assert_equal 700 "$(stat -c %a "$HYDRA_HOME/fleet/revocations/transport")" "umask $1: scope directory is private"
    cmp -s "$fixture/alias-before" "$HYDRA_HOME/fleet/remotes/build.json" && same=yes || same=no
    assert_equal yes "$same" "umask $1: alias record is retained byte for byte"

    out="$("$hydra" fleet list --json)" && status=0 || status=$?
    case "$out" in *'"host":"build"'*'"code":"revoked"'*|*'"code":"revoked"'*'"host":"build"'*) row=yes ;; *) row=no ;; esac
    assert_equal yes "$row" "umask $1: fleet list reports the alias as revoked"
    out="$("$hydra" fleet list build --json 2>&1)" && status=0 || status=$?
    assert_failure "$status" "umask $1: operation on a revoked alias fails"
    assert_equal revoked "$(printf '%s' "$out" | json_field code)" "umask $1: operation failure is typed revoked"
    out="$("$hydra" remote add build other-host 2>&1)" && status=0 || status=$?
    assert_failure "$status" "umask $1: alias reuse is blocked"
    assert_equal revoked "$(printf '%s' "$out" | json_field code)" "umask $1: alias reuse is typed revoked"
    out="$("$hydra" remote list)"
    case "$out" in *'"build"'*) listed=yes ;; *) listed=no ;; esac
    assert_equal yes "$listed" "umask $1: revoked alias remains listed for inspection"
    [ -f "$fixture/connections" ] && connected=yes || connected=no
    assert_equal no "$connected" "umask $1: no SSH connection was attempted"

    out="$("$hydra" fleet revoke candidate build --reason retired --json)" && status=0 || status=$?
    assert_success "$status" "umask $1: candidate revocation by target"
    assert_equal cand_6275696c64 "$(printf '%s' "$out" | json_field id)" "umask $1: candidate target normalizes to its ID"
    out="$("$hydra" fleet revoke list --json)"
    assert_equal fleet-revoke-list "$(printf '%s' "$out" | json_field command)" "umask $1: list envelope"
    case "$out" in *'"count":2'*) count=yes ;; *) count=no ;; esac
    assert_equal yes "$count" "umask $1: list counts both records"
    out="$("$hydra" fleet revoke list --scope candidate --json)"
    case "$out" in *'"count":1'*'"scope":"candidate"'*) scoped=yes ;; *) scoped=no ;; esac
    assert_equal yes "$scoped" "umask $1: list filters by scope"
    out="$("$hydra" fleet revoke project /srv/project --reason "decommissioned" --json)" && status=0 || status=$?
    assert_success "$status" "umask $1: project revocation"
    out="$("$hydra" fleet spawn other --project /srv/project -- x 2>&1)" && status=0 || status=$?
    assert_equal revoked "$(printf '%s' "$out" | json_field code)" "umask $1: revoked project blocks remote operations"

    out="$("$hydra" fleet discover --ssh build --ssh other --ssh-config "$fixture/ssh.conf" --json)" && status=0 || status=$?
    assert_success "$status" "umask $1: discovery with a revoked candidate still succeeds"
    case "$out" in *'"target":"build"'*'"status":"revoked"'*'"revocation":{'*) revoked=yes ;; *) revoked=no ;; esac
    assert_equal yes "$revoked" "umask $1: revoked candidate is shown as revoked"
    case "$out" in *'"revoked_count":1'*) counted=yes ;; *) counted=no ;; esac
    assert_equal yes "$counted" "umask $1: revoked count is reported"

    out="$("$hydra" fleet discover --source "ansible-inventory:$fixture/hosts.ini" --ssh-config "$fixture/ssh.conf" --diff --json)" && status=0 || status=$?
    assert_success "$status" "umask $1: ansible inventory source acquires"
    case "$out" in *not-in-output*) leaked=yes ;; *) leaked=no ;; esac
    assert_equal no "$leaked" "umask $1: inventory credentials never reach the output"
    case "$out" in *'"credentials_omitted":1'*) omitted=yes ;; *) omitted=no ;; esac
    assert_equal yes "$omitted" "umask $1: omitted credentials are counted"
    case "$out" in *'"kind":"ansible-inventory"'*'"status":"acquired"'*'"diff":{'*'"added":["web1","web2"]'*) report=yes ;; *) report=no ;; esac
    assert_equal yes "$report" "umask $1: source report carries the first-observation diff"
    case "$out" in *'"name":"web1"'*'"revocation":{'*) revoked_record=yes ;; *) revoked_record=no ;; esac
    assert_equal yes "$revoked_record" "umask $1: a revoked target is reported, not imported"
    case "$out" in *'"target":"other"'*'"freshness":"acquired_now"'*) imported=yes ;; *) imported=no ;; esac
    assert_equal yes "$imported" "umask $1: the other record becomes a candidate"
    cache="$(find "$HYDRA_HOME/fleet/discovery/sources" -name '*.json' | head -n 1)"
    assert_equal 600 "$(stat -c %a "$cache")" "umask $1: snapshot cache is private"
    out="$("$hydra" fleet discover --source "ssh-config:$fixture/ssh.conf" --ssh-config "$fixture/ssh.conf" --json)" && status=0 || status=$?
    assert_success "$status" "umask $1: ssh-config scan source"
    case "$out" in *'"kind":"ssh-config"'*'"name":"pat*"'*'"reason":"pattern"'*) skipped=yes ;; *) skipped=no ;; esac
    assert_equal yes "$skipped" "umask $1: host patterns are skipped, never expanded"
    out="$("$hydra" fleet discover --source dns-sd --json 2>&1)" && status=0 || status=$?
    assert_failure "$status" "umask $1: dns-sd without a client fails"
    assert_equal source_unavailable "$(printf '%s' "$out" | json_field code)" "umask $1: missing client is typed"
    missing=client_missing
    command -v dns-sd >/dev/null 2>&1 && missing=client_unsupported
    case "$out" in *'"status":"unavailable"'*"\"reason\":\"$missing\""*) unavailable=yes ;; *) unavailable=no ;; esac
    assert_equal yes "$unavailable" "umask $1: dns-sd adapter reports unavailable"
    out="$("$hydra" fleet qualify --source "ssh-config:$fixture/ssh.conf" --json 2>&1)" && status=0 || status=$?
    assert_equal invalid_input "$(printf '%s' "$out" | json_field code)" "umask $1: qualify over a source needs --select"
    out="$("$hydra" fleet revoke source "ansible-inventory:$fixture/hosts.ini" --reason "moved" --json)" && status=0 || status=$?
    assert_success "$status" "umask $1: source revocation"
    out="$("$hydra" fleet discover --source "ansible-inventory:$fixture/hosts.ini" --ssh-config "$fixture/ssh.conf" --json 2>&1)" && status=0 || status=$?
    assert_equal revoked "$(printf '%s' "$out" | json_field code)" "umask $1: a revoked source is not acquired"
    [ -f "$fixture/connections" ] && connected=yes || connected=no
    assert_equal no "$connected" "umask $1: discovery made no SSH connection"
}
umask 022
run_cases 022
umask 002
run_cases 002
umask 022

out="$("$hydra" fleet help)"
case "$out" in *"fleet revoke candidate|source|transport|package|project ID --reason TEXT"*) help=yes ;; *) help=no ;; esac
assert_equal yes "$help" "fleet help documents revoke"
case "$out" in *"--source ssh-config[:FILE]|ansible-inventory:FILE|dns-sd[:DOMAIN]"*) help=yes ;; *) help=no ;; esac
assert_equal yes "$help" "fleet help documents live sources"
out="$("$hydra" help)"
case "$out" in *"revoke"*"discover"*|*"discover"*"revoke"*) help=yes ;; *) help=no ;; esac
assert_equal yes "$help" "hydra help lists discover and revoke"
for shell in bash zsh fish; do
    script="$(cat "$root/lib/completion_$shell.sh")"
    case "$shell" in fish) reason='-l reason' source='-l source' diff='-l diff' ;; *) reason='--reason' source='--source' diff='--diff' ;; esac
    case "$script" in *"revoke"*"$reason"*|*"$reason"*"revoke"*) completed=yes ;; *) completed=no ;; esac
    assert_equal yes "$completed" "$shell completion covers revoke"
    case "$script" in *"$source"*"$diff"*|*"$diff"*"$source"*) completed=yes ;; *) completed=no ;; esac
    assert_equal yes "$completed" "$shell completion covers --source and --diff"
done

echo "Tests: $test_count, Passed: $pass_count, Failed: $fail_count"
[ "$fail_count" -eq 0 ]
