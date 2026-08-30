#!/bin/sh
# Native protocol, exact snapshot parity, and deterministic fallback tests.

set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d)"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$repo_root/bin/hydra"
HYDRA_HOME="$test_root/home"
HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
core="$repo_root/build/hydra-core"
export HYDRA_HOME HYDRA_STATE_V2_ROOT

# shellcheck source=helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

write_scalar() {
    printf '%s\n' "$2" > "$1"
}

make_head() {
    _mh_project="$1"
    _mh_head="$2"
    _mh_instance="$3"
    _mh_branch="$4"
    _mh_dir="$HYDRA_STATE_V2_ROOT/projects/$_mh_project/heads/$_mh_head"
    mkdir -p "$_mh_dir/instances/$_mh_instance"
    write_scalar "$_mh_dir/head-id" "$_mh_head"
    write_scalar "$_mh_dir/branch" "$_mh_branch"
    write_scalar "$_mh_dir/session" "session-$_mh_head"
    write_scalar "$_mh_dir/current-instance" "$_mh_instance"
    write_scalar "$_mh_dir/desired-state" running
    write_scalar "$_mh_dir/instances/$_mh_instance/instance-id" "$_mh_instance"
}

make_project() {
    _mp_project="$1"
    mkdir -p "$HYDRA_STATE_V2_ROOT/projects/$_mp_project/heads"
    write_scalar "$HYDRA_STATE_V2_ROOT/projects/$_mp_project/project-id" "$_mp_project"
    write_scalar "$HYDRA_STATE_V2_ROOT/projects/$_mp_project/repo-root" "/tmp/$_mp_project"
}

assert_fallback() {
    _af_mode="$1"
    _af_reason="$2"
    _af_core="$3"
    FAKE_CORE_MODE="$_af_mode" HYDRA_CORE="$_af_core" HYDRA_CORE_TIMEOUT_SECONDS=1 \
        "$HYDRA_BIN" snapshot --native > "$test_root/fallback.out" 2> "$test_root/fallback.err"
    _af_status=$?
    assert_success "$_af_status" "$_af_mode fallback succeeds through shell"
    if cmp -s "$test_root/shell.out" "$test_root/fallback.out"; then
        assert_success 0 "$_af_mode fallback is byte-identical"
    else
        assert_success 1 "$_af_mode fallback is byte-identical"
    fi
    if grep -Fq "native snapshot fallback: $_af_reason" "$test_root/fallback.err"; then
        assert_success 0 "$_af_mode fallback has a named reason"
    else
        assert_success 1 "$_af_mode fallback has a named reason"
    fi
}

mkdir -p "$HYDRA_STATE_V2_ROOT/projects"
write_scalar "$HYDRA_STATE_V2_ROOT/schema-version" 2
project_a=project_aaaaaaaaaaaaaaaa
project_b=project_bbbbbbbbbbbbbbbb
make_project "$project_b"
make_project "$project_a"
make_head "$project_b" head_eeeeeeeeeeeeeeee instance_ffffffffffffffff later
make_head "$project_a" head_dddddddddddddddd instance_eeeeeeeeeeeeeeee 'feature/"quoted"\path'
make_head "$project_a" head_cccccccccccccccc instance_dddddddddddddddd first
mkdir -p "$HYDRA_STATE_V2_ROOT/projects/ignored-directory"
write_scalar "$HYDRA_STATE_V2_ROOT/projects/README" ignored

echo "Running native core tests..."
echo "============================"

"$core" validate-state "$HYDRA_STATE_V2_ROOT" > "$test_root/validation.out"
assert_success $? "native core validates state v2"
make_head "$project_a" head_bbbbbbbbbbbbbbbb instance_cccccccccccccccc first
"$core" validate-state "$HYDRA_STATE_V2_ROOT" > /dev/null 2>&1
assert_failure $? "native core rejects duplicate branch identity like shell verification"
rm -rf "$HYDRA_STATE_V2_ROOT/projects/$project_a/heads/head_bbbbbbbbbbbbbbbb"

events="$test_root/events.jsonl"
printf '%s\n' \
    '{"schema_version":1,"event_id":"evt_aaaaaaaaaaaaaaaa","sequence":7,"project_id":"project_aaaaaaaaaaaaaaaa","head_id":"head_cccccccccccccccc","instance_id":"instance_dddddddddddddddd"}' \
    '{"schema_version":1,"event_id":"evt_bbbbbbbbbbbbbbbb","sequence":8,"project_id":"project_aaaaaaaaaaaaaaaa","head_id":"head_cccccccccccccccc","instance_id":"instance_dddddddddddddddd"}' > "$events"
"$core" validate-events "$events" > "$test_root/events.out"
assert_success $? "native core validates sequential events v1"
printf '%s\n' \
    '{"schema_version":1,"event_id":"evt_aaaaaaaaaaaaaaaa","sequence":1,"project_id":"project_aaaaaaaaaaaaaaaa","head_id":"head_cccccccccccccccc","instance_id":"instance_dddddddddddddddd"}' \
    '{"schema_version":1,"event_id":"evt_bbbbbbbbbbbbbbbb","sequence":3,"project_id":"project_aaaaaaaaaaaaaaaa","head_id":"head_cccccccccccccccc","instance_id":"instance_dddddddddddddddd"}' > "$events"
"$core" validate-events "$events" > /dev/null 2>&1
assert_failure $? "native core rejects broken event sequence"

"$HYDRA_BIN" snapshot > "$test_root/shell.out"
assert_success $? "shell emits canonical snapshot"
HYDRA_CORE="$core" "$HYDRA_BIN" snapshot --native > "$test_root/native.out" 2> "$test_root/native.err"
assert_success $? "explicit native snapshot succeeds"
if cmp -s "$test_root/shell.out" "$test_root/native.out"; then
    assert_success 0 "native snapshot is byte-identical to shell"
else
    assert_success 1 "native snapshot is byte-identical to shell"
fi
assert_equal "" "$(sed -n '1p' "$test_root/native.err")" "successful native dispatch has no diagnostic"

fake_core="$test_root/fake-core"
# shellcheck disable=SC2016
printf '%s\n' \
    '#!/bin/sh' \
    'case "${1:-}" in' \
    '  --protocol-version)' \
    '    case "${FAKE_CORE_MODE:-}" in skew) echo 99 ;; hang) sleep 3 ;; crash) exit 9 ;; *) echo 1 ;; esac ;;' \
    '  --version)' \
    '    case "${FAKE_CORE_MODE:-}" in version-skew) echo "Hydra core 0.0.0 protocol 1" ;; *) echo "Hydra core 1.7.0 protocol 1" ;; esac ;;' \
    '  snapshot)' \
    '    case "${FAKE_CORE_MODE:-}" in malformed) echo not-json ;; crash) exit 9 ;; *) echo not-json ;; esac ;;' \
    'esac' > "$fake_core"
chmod +x "$fake_core"

assert_fallback absent helper-unavailable "$test_root/missing-core"
permission_core="$test_root/permission-core"
write_scalar "$permission_core" blocked
assert_fallback permission helper-not-executable "$permission_core"
assert_fallback skew protocol-skew "$fake_core"
assert_fallback version-skew version-skew "$fake_core"
assert_fallback crash protocol-failure "$fake_core"
assert_fallback hang protocol-timeout "$fake_core"
assert_fallback malformed malformed-output "$fake_core"

printf '\nTests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
