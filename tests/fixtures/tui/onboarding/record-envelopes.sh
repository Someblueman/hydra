#!/bin/sh
# Records the onboarding envelopes the native TUI tests replay, from the real
# `hydra fleet discover|qualify|enroll ... --json` over the enrollment SSH
# fixture (tests/native/enrollment_ssh_fixture.c: `ssh -G` is OpenSSH's own
# answer, every connection runs `hydra fleet serve` locally as the remote).
# No network; a local HYDRA_HOME; receivers under a private directory.
#
#   make build/native-tests/enrollment-ssh-fixture build/native-tests/enrollment-receiver-fixture
#   HYDRA_FLEET_BIN=build/hydra-fleet sh tests/fixtures/tui/onboarding/record-envelopes.sh
#
# Machine paths are normalised (local HYDRA_HOME -> /Users/you/.hydra, the
# recording directory -> /Users/you/onboarding). The enrollment intent and
# its digest are the CLI's own: HOME (for the ssh -G resolution), the project,
# the package and the prefix live under /tmp/hydra-onboarding-fixture during
# recording, so the recorded intent hashes without normalisation and no real
# home path is recorded; that directory is removed afterwards.
set -eu
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
root="$(CDPATH='' cd -- "$here/../../../.." && pwd)"
fleet="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN names the hydra-fleet helper to record with}"
ssh_fixture="${HYDRA_ENROLL_SSH:-$root/build/native-tests/enrollment-ssh-fixture}"
receiver="${HYDRA_ENROLL_RECEIVER:-$root/build/native-tests/enrollment-receiver-fixture}"
out_dir="${RECORD_OUT:-$here}"
[ -x "$ssh_fixture" ] && [ -x "$receiver" ] || { echo "build the enrollment fixtures first (make test-enrollment)" >&2; exit 1; }
work="$(mktemp -d "${TMPDIR:-/tmp}/hydra-record.XXXXXX")"
work="$(cd "$work" && pwd -P)"
fixed=/tmp/hydra-onboarding-fixture
trap 'rm -rf "$work" "$fixed"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
unset CI HYDRA_NONINTERACTIVE HYDRA_ROOT
rm -rf "$fixed"
mkdir -p "$work/bin" "$work/receivers" "$fixed/home"
chmod 700 "$fixed"
ln -s "$ssh_fixture" "$work/bin/ssh"
git init -q "$fixed/project"
HYDRA_HOME="$work/home"
mkdir -p "$HYDRA_HOME"; chmod 700 "$HYDRA_HOME"
PATH="$work/bin:$PATH"
HOME="$fixed/home"
export HOME PATH HYDRA_HOME HYDRA_FLEET_BIN="$fleet" HYDRA_BIN_CMD="$receiver" ENROLL_COUNT_WRAPPER="$receiver" \
    ENROLL_REAL="$root/bin/hydra" ENROLL_COUNTER="$work/mutations" ENROLL_FINGERPRINT=SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8 \
    ENROLL_RECEIVERS="$work/receivers" ENROLL_INSTALL_COUNTER="$work/installs"
unset ENROLL_HOST_POLICY ENROLL_DROP_INIT_RESPONSE ENROLL_DROP_INSTALL_RESPONSE

normalise() {
    escaped_work="$(printf '%s' "$work" | sed 's|/|\\\\/|g')"
    escaped_root="$(printf '%s' "$root" | sed 's|/|\\\\/|g')"
    sed -e "s|$escaped_work\\\\/home|\\\\/Users\\\\/you\\\\/.hydra|g" -e "s|$escaped_work|\\\\/Users\\\\/you\\\\/onboarding|g" \
        -e "s|$escaped_root|\\\\/usr\\\\/local\\\\/hydra|g"
}
# record FILE EXPECTED ARGS...: hydra fleet ARGS --json, exit status checked.
record() {
    file=$1 expected=$2; shift 2
    code=0
    "$root/bin/hydra" fleet "$@" --json < /dev/null > "$work/out" 2> "$work/err" || code=$?
    if [ "$code" -ne "$expected" ]; then
        cat "$work/out" "$work/err" >&2
        printf 'record: fleet %s exited %s, expected %s\n' "$*" "$code" "$expected" >&2
        exit 1
    fi
    normalise < "$work/out" > "$out_dir/$file"
}
digest() { sed -n 's/.*"intent_sha256":"\([0-9a-f]*\)".*/\1/p' "$out_dir/$1" | head -n 1; }
# inventory FILE COUNT PREFIX: a schema-1 inventory of COUNT hosts.
inventory() {
    i=0
    printf '{"schema_version":1,"observed_at":1760000000,"hosts":[' > "$1"
    while [ "$i" -lt "$2" ]; do
        [ "$i" -eq 0 ] || printf ',' >> "$1"
        printf '{"name":"%s%02d","target":"%s%02d","labels":["pool:%s","rack:%d"]}' "$3" "$i" "$3" "$i" "$3" $((i % 4)) >> "$1"
        i=$((i + 1))
    done
    printf ']}\n' >> "$1"
}
selects() {
    i=0; args=""
    while [ "$i" -lt "$2" ]; do args="$args --select $(printf '%s%02d' "$1" "$i")"; i=$((i + 1)); done
    printf '%s' "$args"
}

# One prepared host: discovered, then qualified into export 1, reviewed with
# a pinned package, applied and enrolled; status before and after.
record discover-one.json 0 discover --ssh build --diff
record qualify-one.json 0 qualify --ssh build --require list --progress "$work/qualification-1.json"
"$root/bin/hydra" fleet package --source "$root" --binary "$fleet" --output "$fixed/package" --json > "$work/package.json"
package_sha="$(sed -n 's/.*"sha256":"\([0-9a-f]*\)".*/\1/p' "$work/package.json" | head -n 1)"
record review-one.json 0 enroll review --input "$work/qualification-1.json" --candidate cand_6275696c64 --project "$fixed/project" \
    --package "$fixed/package" --sha256 "$package_sha" --prefix "$fixed/prefix" --alias build --output "$fixed/intent-1.json"
record status-not-started.json 0 enroll status --input "$fixed/intent-1.json"
record apply-one.json 0 enroll apply --input "$fixed/intent-1.json" --confirm "$(digest review-one.json)"
record status-enrolled.json 0 enroll status --input "$fixed/intent-1.json"
record apply-again.json 0 enroll apply --input "$fixed/intent-1.json" --confirm "$(digest review-one.json)"
cp "$fixed/intent-1.json" "$out_dir/intent-one.json"
record status-wrong-input.json 1 enroll status --input "$work/package.json"
record apply-wrong-digest.json 1 enroll apply --input "$fixed/intent-1.json" --confirm 0000000000000000000000000000000000000000000000000000000000000000

# A revoked candidate is refused by review; a live ansible source with a
# conflicting and a revoked record, compared with its previous snapshot.
record qualify-two-fresh.json 0 qualify --ssh build --ssh other --require list --progress "$work/qualification-2.json"
"$root/bin/hydra" fleet revoke candidate other --reason "retired" --json > /dev/null
record review-revoked.json 1 enroll review --input "$work/qualification-2.json" --candidate cand_6f74686572 --project "$fixed/project" --output "$fixed/intent-x.json"
record qualify-two.json 0 qualify --ssh build --ssh other --require list --progress "$work/qualification-2-revoked.json"
rm -f "$out_dir/qualify-two-fresh.json"
printf '[web]\nweb1 ansible_host=build ansible_ssh_pass=secret\nweb2 ansible_host=other\nweb3 ansible_host=third\n' > "$work/hosts.ini"
record discover-ansible-first.json 0 discover --source "ansible-inventory:$work/hosts.ini" --diff
printf '[web]\nweb1 ansible_host=build\nweb2 ansible_host=other\nweb3 ansible_host=fourth\nweb4 ansible_host=fourth\n[db]\nweb5 ansible_host=fifth\n' > "$work/hosts.ini"
record discover-ansible.json 0 discover --source "ansible-inventory:$work/hosts.ini" --select web1 --diff

# A mixed ten-host run: seven compatible, one offline, one without the
# required capability, one revoked; apply loses one init response.
inventory "$work/mixed.json" 10 mixed
ENROLL_HOST_POLICY='{"mixed03":{"offline":true},"mixed04":{"missing_capability":true}}'
export ENROLL_HOST_POLICY
"$root/bin/hydra" fleet revoke candidate mixed05 --reason "decommissioned" --json > /dev/null
# shellcheck disable=SC2046
record qualify-mixed.json 1 qualify --inventory "$work/mixed.json" $(selects mixed 10) --require list --progress "$work/qualification-3.json"
unset ENROLL_HOST_POLICY
cand() { printf 'cand_%s' "$(printf '%s' "$1" | od -An -tx1 | tr -d ' \n')"; }
record review-mixed.json 0 enroll review --input "$work/qualification-3.json" --candidate "$(cand mixed00)" --candidate "$(cand mixed01)" \
    --candidate "$(cand mixed02)" --candidate "$(cand mixed06)" --candidate "$(cand mixed07)" --candidate "$(cand mixed08)" --candidate "$(cand mixed09)" \
    --project "$fixed/project" --output "$fixed/intent-2.json"
ENROLL_DROP_INIT_RESPONSE="$work/drop-mixed"
export ENROLL_DROP_INIT_RESPONSE
record status-mixed-reviewed.json 0 enroll status --input "$fixed/intent-2.json"
record apply-mixed.json 0 enroll apply --input "$fixed/intent-2.json" --confirm "$(digest review-mixed.json)"
unset ENROLL_DROP_INIT_RESPONSE
record status-mixed.json 0 enroll status --input "$fixed/intent-2.json"

# Fifty hosts: one qualification batch of sixteen leaves the rest pending
# (every row retained); after four batches, one apply batch of sixteen with
# one lost response leaves 34 rows not started.
inventory "$work/fifty.json" 50 fifty
# shellcheck disable=SC2046
record qualify-50.json 0 qualify --inventory "$work/fifty.json" $(selects fifty 50) --require list --progress "$work/qualification-4.json"
for pass in 2 3 4; do
    # shellcheck disable=SC2046
    "$root/bin/hydra" fleet qualify --inventory "$work/fifty.json" $(selects fifty 50) --require list --progress "$work/qualification-4.json" --json > /dev/null || :
done
# shellcheck disable=SC2046
record qualify-50-complete.json 0 qualify --inventory "$work/fifty.json" $(selects fifty 50) --require list --progress "$work/qualification-4.json"
args=""; i=0
while [ "$i" -lt 50 ]; do args="$args --candidate $(cand "$(printf 'fifty%02d' "$i")")"; i=$((i + 1)); done
# shellcheck disable=SC2086
record review-50.json 0 enroll review --input "$work/qualification-4.json" $args --project "$fixed/project" --output "$fixed/intent-3.json"
ENROLL_DROP_INIT_RESPONSE="$work/drop-fifty"
export ENROLL_DROP_INIT_RESPONSE
record status-50-reviewed.json 0 enroll status --input "$fixed/intent-3.json"
record apply-50.json 0 enroll apply --input "$fixed/intent-3.json" --confirm "$(digest review-50.json)"
unset ENROLL_DROP_INIT_RESPONSE
record status-50.json 0 enroll status --input "$fixed/intent-3.json"

# One hundred discovered candidates (no connection), for bounded rendering.
inventory "$work/hundred.json" 100 many
# shellcheck disable=SC2046
record discover-100.json 0 discover --inventory "$work/hundred.json" $(selects many 100)
printf 'recorded onboarding envelopes in %s\n' "$out_dir"
