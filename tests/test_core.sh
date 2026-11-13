#!/bin/sh
# Native protocol, exact snapshot parity, and deterministic fallback tests.

set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$repo_root/bin/hydra"
HYDRA_HOME="$test_root/home"
HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
core="${HYDRA_CORE:?HYDRA_CORE is required: run via make test-parity or make test-one T=core}"
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
    _mh_instance_dir="$_mh_dir/instances/$_mh_instance"
    mkdir -p "$_mh_instance_dir" "$_mh_dir/events/archive"
    write_scalar "$_mh_dir/head-id" "$_mh_head"
    write_scalar "$_mh_dir/branch" "$_mh_branch"
    write_scalar "$_mh_dir/session" "session-$_mh_head"
    write_scalar "$_mh_dir/profile" -
    write_scalar "$_mh_dir/group" -
    write_scalar "$_mh_dir/created-at" 100
    write_scalar "$_mh_dir/dependencies" -
    write_scalar "$_mh_dir/pr" -
    write_scalar "$_mh_dir/current-instance" "$_mh_instance"
    write_scalar "$_mh_dir/desired-state" running
    write_scalar "$_mh_dir/completion-policy" declared-done
    write_scalar "$_mh_dir/worktree" "/tmp/$_mh_head"
    write_scalar "$_mh_dir/task" ""
    write_scalar "$_mh_dir/scopes" ""
    write_scalar "$_mh_dir/base-ref" main
    write_scalar "$_mh_instance_dir/instance-id" "$_mh_instance"
    write_scalar "$_mh_instance_dir/session" "session-$_mh_head"
    write_scalar "$_mh_instance_dir/started-at" 100
    write_scalar "$_mh_instance_dir/observed-status" starting
    write_scalar "$_mh_instance_dir/observed-source" hydra
    write_scalar "$_mh_instance_dir/observed-confidence" exact
    write_scalar "$_mh_instance_dir/observed-at" 100
    write_scalar "$_mh_instance_dir/observed-exit-code" ""
    write_scalar "$_mh_instance_dir/provider-session-id" ""
    : > "$_mh_dir/events/events.jsonl"
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
    '    case "${FAKE_CORE_MODE:-}" in skew) echo 99 ;; hang) sleep 3 ;; descendant) sleep 20 & echo $! > "$FAKE_CORE_CHILD_PID"; wait ;; crash) exit 9 ;;' \
    '    tree) echo $$ > "$FAKE_CORE_CHILD_PID"; sh -c '\''sleep 20 & echo $$ >> "$FAKE_CORE_CHILD_PID"; echo $! >> "$FAKE_CORE_CHILD_PID"; wait'\'' & wait ;;' \
    '    orphan) (sleep 20 & echo $! > "$FAKE_CORE_CHILD_PID"); sleep 20 ;;' \
    '    *) echo 1 ;; esac ;;' \
    '  --version)' \
    '    case "${FAKE_CORE_MODE:-}" in version-skew) echo "Hydra core 0.0.0 protocol 1" ;; *) echo "Hydra core 3.0.0 protocol 1" ;; esac ;;' \
    '  snapshot)' \
    '    case "${FAKE_CORE_MODE:-}" in malformed) echo not-json ;; shape) echo '\''{"schema_version":1,"ok":true,"command":"snapshot","data":{"state_schema":2,"projects":1,"heads":[{"project_id":false}]}}'\'' ;; crash) exit 9 ;; *) echo not-json ;; esac ;;' \
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

# Succeeds once no pid listed in the file exists any more, polling for up to the
# given seconds. A terminated descendant that was orphaned stays visible until
# init reaps it, which some inits do on a schedule rather than at once.
wait_pids_gone() {
    _wpg_file="$1"
    _wpg_polls=$(( $2 * 10 ))
    while :; do
        _wpg_left=""
        while read -r _wpg_pid; do
            [ -n "$_wpg_pid" ] || continue
            kill -0 "$_wpg_pid" 2>/dev/null && _wpg_left="$_wpg_left $_wpg_pid"
        done < "$_wpg_file"
        [ -n "$_wpg_left" ] || return 0
        if [ "$_wpg_polls" -le 0 ]; then
            echo "still present after the bound:$_wpg_left" >&2
            return 1
        fi
        _wpg_polls=$((_wpg_polls - 1))
        sleep 0.1
    done
}

# Runs a timing-out helper that records PIDs of its subtree into the file and
# asserts the fallback is bounded and every recorded level is gone. An optional
# third argument names the variant in each message.
assert_tree_terminated() {
    _att_mode="$1"
    _att_levels="$2"
    _att_label="${3:+ ($3)}"
    _att_file="$test_root/$_att_mode.pid"
    : > "$_att_file"
    FAKE_CORE_CHILD_PID="$_att_file"
    export FAKE_CORE_CHILD_PID
    _att_started="$(date +%s)"
    assert_fallback "$_att_mode" protocol-timeout "$fake_core"
    _att_elapsed=$(( $(date +%s) - _att_started ))
    assert_equal "$_att_levels" "$(wc -l < "$_att_file" | tr -d ' ')" "$_att_mode helper recorded every level of its tree$_att_label"
    echo "$_att_mode timeout returned after ${_att_elapsed}s$_att_label"
    if [ "$_att_elapsed" -le 10 ]; then
        assert_success 0 "$_att_mode timeout returns within a bound$_att_label"
    else
        assert_success 1 "$_att_mode timeout returns within a bound$_att_label"
    fi
    if wait_pids_gone "$_att_file" 10; then
        assert_success 0 "native timeout terminates every level of the $_att_mode helper tree$_att_label"
    else
        assert_success 1 "native timeout terminates every level of the $_att_mode helper tree$_att_label"
    fi
    unset FAKE_CORE_CHILD_PID
}

# Prints a directory holding links to every command on PATH except the named
# ones, to run the CLI as on a host without them (macOS has no setsid(1)).
path_without() {
    _pw_dir="$test_root/path-without"
    for _pw_tool in "$@"; do _pw_dir="$_pw_dir-$_pw_tool"; done
    mkdir -p "$_pw_dir"
    _pw_ifs="$IFS"
    IFS=:
    for _pw_entry in $PATH; do
        [ -d "$_pw_entry" ] || continue
        ln -s "$_pw_entry"/* "$_pw_dir"/ 2>/dev/null || true
    done
    IFS="$_pw_ifs"
    for _pw_tool in "$@"; do rm -f "$_pw_dir/$_pw_tool"; done
    printf '%s\n' "$_pw_dir"
}

# The orphan's parent has exited before the timeout fires, so no parent chain
# leads to it; only the helper's own process group still does. That group needs
# setsid(1), perl or python3 (see hydra_core_isolated).
assert_orphan_terminated() {
    if command -v setsid >/dev/null 2>&1 || command -v perl >/dev/null 2>&1 ||
        command -v python3 >/dev/null 2>&1; then
        assert_tree_terminated orphan 1 "${1:-}"
    else
        echo "SKIP: orphan helper tree${1:+ ($1)}: no setsid(1), perl or python3 to give the helper its own process group"
    fi
}

assert_tree_terminated descendant 1
assert_tree_terminated tree 3
assert_orphan_terminated

# The same cases on the paths a host without setsid(1) takes: perl, then
# python3, then the process-table walk alone, which still reaches a tree whose
# parent chain is intact.
full_path="$PATH"
PATH="$(path_without setsid)"
assert_tree_terminated tree 3 "without setsid"
assert_orphan_terminated "without setsid"
PATH="$(PATH="$full_path" path_without setsid perl)"
assert_orphan_terminated "without setsid and perl"
PATH="$(PATH="$full_path" path_without setsid perl python3)"
assert_tree_terminated tree 3 "without any session tool"
PATH="$full_path"
assert_fallback malformed malformed-output "$fake_core"
assert_fallback shape malformed-output "$fake_core"

printf '\nTests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
