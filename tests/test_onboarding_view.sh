#!/bin/sh
# Onboarding operator view (H5): the CLI's JSON and the native TUI's model
# agree on identity and typed state. Real `hydra fleet discover` output over a
# fake `ssh -G` (no connection, as tests/test_revocation.sh) and a real
# `fleet enroll review|status` over a recorded qualification export are read
# by `hydra-tui --onboarding-dump`, and every candidate ID, target, status,
# typed state and enrollment row is compared. Also: the onboarding directory
# the CLI creates for the view is private, `enroll status` is read-only and
# typed, and `enroll apply` with a wrong digest changes nothing.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"
fixture="$(test_mktemp_dir)"
fixture="$(cd "$fixture" && pwd -P)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test or make test-one T=onboarding_view}"
tui="${HYDRA_TUI_BIN:?HYDRA_TUI_BIN is required: run via make test or make test-one T=onboarding_view}"
export HYDRA_FLEET_BIN HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1
test_count=0; pass_count=0; fail_count=0
hydra="$root/bin/hydra"
recorded="$root/tests/fixtures/tui/onboarding"
empty="$root/tests/fixtures/tui/setup/empty.tsv"
real_ssh="$(command -v ssh)"
mkdir -p "$fixture/bin"
cat > "$fixture/bin/ssh" <<SSH
#!/bin/sh
for arg do [ "\$arg" = -G ] && exec "$real_ssh" "\$@"; done
echo "unexpected SSH connection: \$*" >> "$fixture/connections"
exit 255
SSH
chmod +x "$fixture/bin/ssh"
PATH="$fixture/bin:$PATH"; export PATH
HYDRA_HOME="$fixture/home"; export HYDRA_HOME
(umask 077 && mkdir -p "$HYDRA_HOME")
printf 'Host build other third\n HostName %%h.invalid\n User me\n' > "$fixture/ssh.conf"
chmod 600 "$fixture/ssh.conf"
printf '[web]\nweb1 ansible_host=build\nweb2 ansible_host=other\nweb3 ansible_host=third\nweb4 ansible_host=third\n' > "$fixture/hosts.ini"
json_field() { sed -n "s/.*\"$1\":\"\([^\"]*\)\".*/\1/p" | head -n 1; }
# candidate rows of a discovery envelope: id<TAB>target<TAB>status, one per line, in order.
json_candidates() {
    tr -d '\n' | sed 's/{"candidate_id":"/\
CANDIDATE /g' | sed 's/\("status":"[a-z_]*"\).*$/\1/' |
        sed -n 's/^CANDIDATE \([^"]*\)","target":"\([^"]*\)".*"status":"\([a-z_]*\)"$/\1	\2	\3/p'
}
# model rows of the TUI dump: id<TAB>target<TAB>status.
dump_candidates() { awk -F '\t' '$1 == "C" { print $2 "\t" $3 "\t" $4 }'; }
dump_typed() { awk -F '\t' '$1 == "C" { print $3 "\t" $5 }'; }

# The CLI creates the view's private directory; the TUI never does.
dir="$("$hydra" tui --onboarding-home)" && status=0 || status=$?
assert_success "$status" "tui --onboarding-home names the onboarding directory"
assert_equal "$HYDRA_HOME/fleet/onboarding" "$dir" "the onboarding directory is under the fleet state"
assert_equal 700 "$(stat -c %a "$dir")" "the onboarding directory is private"
assert_equal 700 "$(stat -c %a "$HYDRA_HOME/fleet")" "its parent is private"

# Discovery with a revoked candidate: identity and typed state agree.
"$hydra" fleet revoke candidate other --reason "retired" --json > /dev/null
"$hydra" fleet discover --ssh build --ssh other --ssh-config "$fixture/ssh.conf" --json > "$fixture/discover.json" && status=0 || status=$?
assert_success "$status" "discovery over the fake ssh -G succeeds"
"$tui" --headless-fixture "$empty" --onboarding-fixture "$fixture/discover.json" --onboarding-dump > "$fixture/discover.dump" && status=0 || status=$?
assert_success "$status" "the TUI reads the discovery envelope"
json_candidates < "$fixture/discover.json" > "$fixture/discover.expected"
dump_candidates < "$fixture/discover.dump" > "$fixture/discover.actual"
assert_equal "$(cat "$fixture/discover.expected")" "$(cat "$fixture/discover.actual")" "every candidate ID, target and status match the CLI JSON"
assert_equal 2 "$(grep -c . "$fixture/discover.expected")" "two candidates were compared"
assert_equal "$(printf 'build\tnew\nother\trevoked')" "$(dump_typed < "$fixture/discover.dump")" "typed states follow the CLI statuses (discovered is new, revoked is revoked)"
[ -f "$fixture/connections" ] && connected=yes || connected=no
assert_equal no "$connected" "no SSH connection was attempted"

# A live source with conflicting and revoked records: reported, never imported.
"$hydra" fleet discover --source "ansible-inventory:$fixture/hosts.ini" --ssh-config "$fixture/ssh.conf" --select web1 --diff --json > "$fixture/ansible.json" && status=0 || status=$?
assert_success "$status" "ansible source discovery succeeds"
"$tui" --headless-fixture "$empty" --onboarding-fixture "$fixture/ansible.json" --onboarding-dump > "$fixture/ansible.dump"
assert_equal "$(json_candidates < "$fixture/ansible.json")" "$(dump_candidates < "$fixture/ansible.dump")" "the imported candidate matches the CLI JSON"
assert_equal "$(printf 'web3\tconflicting\nweb4\tconflicting\nweb2\trevoked')" "$(awk -F '\t' '$1 == "N" { print $2 "\t" $5 }' "$fixture/ansible.dump")" \
    "conflicting and revoked records are rows typed as such, in the CLI's order"
case "$(cat "$fixture/ansible.json")" in *'"conflicting_count":2'*) counted=yes ;; *) counted=no ;; esac
assert_equal yes "$counted" "the CLI reports the same two conflicts"
"$tui" --ascii --headless-fixture "$empty" --onboarding-fixture "$fixture/ansible.json" --onboarding-screen changes --size 100x30 --view hosts > "$fixture/changes.txt"
case "$(cat "$fixture/changes.txt")" in *"2 conflicting, 1 revoked"*) shown=yes ;; *) shown=no ;; esac
assert_equal yes "$shown" "the changes screen shows the conflict and revocation counts"

# Review and status over the recorded export: the intent rows agree.
export_file="$recorded/qualify-one.json"
"$hydra" fleet enroll review --input "$export_file" --candidate cand_6275696c64 --project /srv/project --alias build --output "$fixture/intent.json" --json > "$fixture/review.json" && status=0 || status=$?
assert_success "$status" "review over the recorded export succeeds"
digest="$(json_field intent_sha256 < "$fixture/review.json")"
assert_equal 64 "${#digest}" "the review carries its intent digest"
"$hydra" fleet enroll status --input "$fixture/intent.json" --json > "$fixture/status.json" && status=0 || status=$?
assert_success "$status" "enroll status reads the intent"
assert_equal fleet-enrollment-status "$(json_field command < "$fixture/status.json")" "status envelope command"
assert_equal not_started "$(json_field status < "$fixture/status.json")" "a reviewed host is not started"
case "$(cat "$fixture/status.json")" in *'"progress_recorded":false'*'"attempted":false'*) recorded_flag=yes ;; *) recorded_flag=no ;; esac
assert_equal yes "$recorded_flag" "status says no progress is recorded and nothing was attempted"
[ -d "$HYDRA_HOME/fleet/enrollment" ] && wrote=yes || wrote=no
assert_equal no "$wrote" "status writes no enrollment state"
"$tui" --headless-fixture "$empty" --onboarding-fixture "$export_file" --onboarding-intent "$fixture/status.json" --onboarding-dump > "$fixture/status.dump"
assert_equal "$(printf 'C\tcand_6275696c64\tbuild\tcompatible\treviewed\tssh\tlinux-x86_64\tbuild\tnot_started')" "$(grep '^C' "$fixture/status.dump")" \
    "the TUI row carries the CLI's alias and apply status and types it reviewed"
"$tui" --ascii --headless-fixture "$empty" --onboarding-fixture "$export_file" --onboarding-intent "$fixture/status.json" --onboarding-screen intent --size 140x40 --view hosts > "$fixture/intent.txt"
case "$(cat "$fixture/intent.txt")" in *"Intent digest"*"$digest"*"Apply command"*"hydra fleet enroll apply --input"*"--confirm"*) command_shown=yes ;; *) command_shown=no ;; esac
assert_equal yes "$command_shown" "the intent screen shows the digest and the apply command"
assert_equal 2 "$(grep -c "$digest" "$fixture/intent.txt")" "the digest appears as the binding and in the command"
"$hydra" fleet enroll apply --input "$fixture/intent.json" --confirm 0000000000000000000000000000000000000000000000000000000000000000 --json > "$fixture/apply.json" && status=0 || status=$?
assert_failure "$status" "apply with a wrong digest fails"
assert_equal invalid_intent "$(json_field code < "$fixture/apply.json")" "a wrong digest is typed invalid_intent"
[ -d "$HYDRA_HOME/fleet/enrollment" ] && wrote=yes || wrote=no
assert_equal no "$wrote" "a refused apply records nothing"
"$hydra" fleet enroll status --input "$export_file" --json > "$fixture/bad-status.json" && status=0 || status=$?
assert_failure "$status" "status over a non-intent fails"
assert_equal invalid_intent "$(json_field code < "$fixture/bad-status.json")" "a non-intent is typed invalid_intent"
[ -f "$fixture/connections" ] && connected=yes || connected=no
assert_equal no "$connected" "review, status and the refused apply made no SSH connection"

# Recorded mixed and fifty-host runs: every row keeps its outcome in the model.
"$tui" --headless-fixture "$empty" --onboarding-fixture "$recorded/qualify-mixed.json" --onboarding-intent "$recorded/status-mixed.json" --onboarding-dump > "$fixture/mixed.dump"
assert_equal 10 "$(grep -c '^C' "$fixture/mixed.dump")" "the mixed run lists all ten rows"
assert_equal "$(printf '1 revoked\n1 unknown\n2 failed\n6 enrolled')" "$(awk -F '\t' '$1 == "C" { print $5 }' "$fixture/mixed.dump" | sort | uniq -c | awk '{ print $1 " " $2 }' | sort)" \
    "mixed outcomes: enrolled, failed, revoked and unknown are each typed"
"$tui" --headless-fixture "$empty" --onboarding-fixture "$recorded/qualify-50-complete.json" --onboarding-intent "$recorded/status-50.json" --onboarding-dump > "$fixture/fifty.dump"
assert_equal 50 "$(grep -c '^C' "$fixture/fifty.dump")" "the interrupted fifty-host apply lists all fifty rows"
assert_equal "$(printf '1 unknown\n15 enrolled\n34 reviewed')" "$(awk -F '\t' '$1 == "C" { print $5 }' "$fixture/fifty.dump" | sort | uniq -c | awk '{ print $1 " " $2 }' | sort)" \
    "fifty outcomes: not started, enrolled and unknown rows are each typed"
assert_equal "$(grep -o '"status":"[a-z_]*"' "$recorded/status-50.json" | sed 's/"status":"//; s/"//' | sort | uniq -c | awk '{ print $1 " " $2 }')" \
    "$(awk -F '\t' '$1 == "C" { print $9 }' "$fixture/fifty.dump" | sort | uniq -c | awk '{ print $1 " " $2 }')" "the fifty apply statuses match the CLI JSON row for row"
for size in 80x24 140x40; do
    "$tui" --ascii --headless-fixture "$empty" --onboarding-fixture "$recorded/discover-100.json" --size "$size" --view hosts > "$fixture/hundred.txt"
    awk -v cols="${size%x*}" 'length >= cols { exit 1 }' "$fixture/hundred.txt"
    assert_success $? "the 100-candidate list fits $size"
    case "$(cat "$fixture/hundred.txt")" in *"100 candidates"*"of 100"*) bounded=yes ;; *) bounded=no ;; esac
    assert_equal yes "$bounded" "the 100-candidate list is paged at $size"
done

printf '\nTests: %d, Passed: %d, Failed: %d\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
