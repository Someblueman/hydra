#!/bin/sh
# Snapshot dispatch: the native helper by default, exact parity with the shell
# implementation, and a silent, deterministic fallback to it.

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

assert_same_file() {
    if cmp -s "$1" "$2"; then
        assert_success 0 "$3"
    else
        assert_success 1 "$3"
    fi
}

# Runs the default path with a helper that cannot be used and asserts that the
# shell implementation answers: the same bytes, and nothing on stderr.
assert_silent_fallback() {
    _asf_mode="$1"
    _asf_core="$2"
    FAKE_CORE_MODE="$_asf_mode" HYDRA_CORE="$_asf_core" HYDRA_CORE_TIMEOUT_SECONDS=1 \
        "$HYDRA_BIN" snapshot > "$test_root/fallback.out" 2> "$test_root/fallback.err"
    assert_success $? "$_asf_mode: the snapshot succeeds through the shell implementation"
    assert_same_file "$test_root/shell.out" "$test_root/fallback.out" "$_asf_mode: the fallback is byte-identical"
    assert_equal "" "$(cat "$test_root/fallback.err")" "$_asf_mode: the fallback is silent"
}

# The same, then with --native, which also names why the helper was not used.
assert_fallback() {
    _af_mode="$1"
    _af_reason="$2"
    _af_core="$3"
    assert_silent_fallback "$_af_mode" "$_af_core"
    FAKE_CORE_MODE="$_af_mode" HYDRA_CORE="$_af_core" HYDRA_CORE_TIMEOUT_SECONDS=1 \
        "$HYDRA_BIN" snapshot --native > "$test_root/fallback.out" 2> "$test_root/fallback.err"
    assert_success $? "$_af_mode: --native succeeds through the shell implementation"
    assert_same_file "$test_root/shell.out" "$test_root/fallback.out" "$_af_mode: the --native fallback is byte-identical"
    assert_equal "hydra: native snapshot fallback: $_af_reason" "$(cat "$test_root/fallback.err")" \
        "$_af_mode: --native names the reason"
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

"$core" snapshot "$HYDRA_STATE_V2_ROOT" > "$test_root/validation.out"
assert_success $? "native core accepts state v2"
make_head "$project_a" head_bbbbbbbbbbbbbbbb instance_cccccccccccccccc first
"$core" snapshot "$HYDRA_STATE_V2_ROOT" > "$test_root/validation.out" 2> /dev/null
assert_failure $? "native core rejects duplicate branch identity like shell verification"
assert_equal "" "$(cat "$test_root/validation.out")" "native core prints nothing for a state it rejects"
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

# The three ways to ask: forced shell, the default (native when the helper
# qualifies) and --native. One snapshot, byte for byte.
canonical='{"schema_version":1,"ok":true,"command":"snapshot","data":{"state_schema":2,"projects":2,"heads":[{"project_id":"project_aaaaaaaaaaaaaaaa","head_id":"head_cccccccccccccccc","branch":"first","desired_state":"running","current_instance":"instance_dddddddddddddddd"},{"project_id":"project_aaaaaaaaaaaaaaaa","head_id":"head_dddddddddddddddd","branch":"feature/\"quoted\"\\path","desired_state":"running","current_instance":"instance_eeeeeeeeeeeeeeee"},{"project_id":"project_bbbbbbbbbbbbbbbb","head_id":"head_eeeeeeeeeeeeeeee","branch":"later","desired_state":"running","current_instance":"instance_ffffffffffffffff"}]}}'
HYDRA_CORE="$core" "$HYDRA_BIN" snapshot --shell > "$test_root/shell.out" 2> "$test_root/shell.err"
assert_success $? "shell implementation emits the snapshot"
assert_equal "$canonical" "$(cat "$test_root/shell.out")" "shell snapshot is the canonical document"
assert_equal "" "$(cat "$test_root/shell.err")" "shell implementation has no diagnostic"
HYDRA_CORE="$core" "$HYDRA_BIN" snapshot > "$test_root/default.out" 2> "$test_root/default.err"
assert_success $? "default snapshot succeeds"
assert_same_file "$test_root/shell.out" "$test_root/default.out" "default snapshot is byte-identical to shell"
assert_equal "" "$(cat "$test_root/default.err")" "default snapshot has no diagnostic"
HYDRA_CORE="$core" "$HYDRA_BIN" snapshot --native > "$test_root/native.out" 2> "$test_root/native.err"
assert_success $? "explicit native snapshot succeeds"
assert_same_file "$test_root/shell.out" "$test_root/native.out" "native snapshot is byte-identical to shell"
assert_equal "" "$(cat "$test_root/native.err")" "successful native dispatch has no diagnostic"
HYDRA_CORE="$core" "$HYDRA_BIN" snapshot --json > "$test_root/json.out" 2>&1
assert_success $? "snapshot accepts --json"
assert_same_file "$test_root/shell.out" "$test_root/json.out" "snapshot --json is the same document"

"$HYDRA_BIN" snapshot --native --shell > "$test_root/conflict.out" 2> "$test_root/conflict.err"
assert_failure $? "--native with --shell is refused"
assert_equal "" "$(cat "$test_root/conflict.out")" "refused options print no snapshot"
"$HYDRA_BIN" snapshot --shell --native > /dev/null 2>&1
assert_failure $? "--shell with --native is refused"
"$HYDRA_BIN" snapshot --fast > /dev/null 2>&1
assert_failure $? "an unknown snapshot option is refused"

# A stand-in helper: FAKE_CORE_MODE selects how it misbehaves; without one it
# passes the handshake and answers FAKE_CORE_ANSWER, a well-formed snapshot
# that no real store produces, so its use is visible in the output.
FAKE_CORE_VERSION="$("$core" --version)"
# shellcheck disable=SC2089,SC2090 # A JSON document, not a command line.
FAKE_CORE_ANSWER='{"schema_version":1,"ok":true,"command":"snapshot","data":{"state_schema":2,"projects":7,"heads":[]}}'
# shellcheck disable=SC2090
export FAKE_CORE_VERSION FAKE_CORE_ANSWER
fake_core="$test_root/fake-core"
# shellcheck disable=SC2016
printf '%s\n' \
    '#!/bin/sh' \
    '[ -z "${FAKE_CORE_LOG:-}" ] || echo "${1:-}" >> "$FAKE_CORE_LOG"' \
    'case "${1:-}" in' \
    '  --protocol-version)' \
    '    case "${FAKE_CORE_MODE:-}" in skew) echo 99 ;; hang) sleep 3 ;; descendant) sleep 20 & echo $! > "$FAKE_CORE_CHILD_PID"; wait ;; crash) exit 9 ;;' \
    '    tree) echo $$ > "$FAKE_CORE_CHILD_PID"; sh -c '\''sleep 20 & echo $$ >> "$FAKE_CORE_CHILD_PID"; echo $! >> "$FAKE_CORE_CHILD_PID"; wait'\'' & wait ;;' \
    '    orphan) (sleep 20 & echo $! > "$FAKE_CORE_CHILD_PID"); sleep 20 ;;' \
    '    *) echo 1 ;; esac ;;' \
    '  --version)' \
    '    case "${FAKE_CORE_MODE:-}" in version-skew) echo "Hydra core 0.0.0 protocol 1" ;; version-crash) exit 9 ;; *) echo "$FAKE_CORE_VERSION" ;; esac ;;' \
    '  snapshot)' \
    '    case "${FAKE_CORE_MODE:-}" in malformed) echo not-json ;; shape) echo '\''{"schema_version":1,"ok":true,"command":"snapshot","data":{"state_schema":2,"projects":1,"heads":[{"project_id":false}]}}'\'' ;;' \
    '    snapshot-crash) echo "$FAKE_CORE_ANSWER"; exit 9 ;; snapshot-hang) echo "$FAKE_CORE_ANSWER"; sleep 3 ;; empty) : ;;' \
    '    unterminated) printf "%s" "$FAKE_CORE_ANSWER" ;; trailing) echo "$FAKE_CORE_ANSWER"; echo "$FAKE_CORE_ANSWER" ;;' \
    '    *) echo "$FAKE_CORE_ANSWER" ;; esac ;;' \
    'esac' > "$fake_core"
chmod +x "$fake_core"

FAKE_CORE_LOG="$test_root/fake-core.log"
export FAKE_CORE_LOG
HYDRA_CORE="$fake_core" "$HYDRA_BIN" snapshot > "$test_root/answer.out" 2>&1
assert_success $? "default snapshot succeeds with a qualifying helper"
assert_equal "$FAKE_CORE_ANSWER" "$(cat "$test_root/answer.out")" "default snapshot is the qualifying helper's answer"
assert_equal "--protocol-version --version snapshot" "$(tr '\n' ' ' < "$FAKE_CORE_LOG" | sed 's/ $//')" \
    "default snapshot asks the helper for its protocol, its version, then the snapshot"
: > "$FAKE_CORE_LOG"
HYDRA_CORE="$fake_core" "$HYDRA_BIN" snapshot --shell > "$test_root/forced.out" 2>&1
assert_success $? "--shell succeeds beside a qualifying helper"
assert_same_file "$test_root/shell.out" "$test_root/forced.out" "--shell prints the shell implementation's snapshot"
assert_equal "" "$(cat "$FAKE_CORE_LOG")" "--shell never runs the helper"
unset FAKE_CORE_LOG

# An install without the helper, and the same install once it has one: the
# helper is found beside the CLI, with no HYDRA_CORE to point at it.
shell_only="$test_root/shell-only"
mkdir -p "$shell_only"
cp -R "$repo_root/bin" "$repo_root/lib" "$shell_only/"
(unset HYDRA_CORE; "$shell_only/bin/hydra" snapshot > "$test_root/shell-only.out" 2> "$test_root/shell-only.err")
assert_success $? "an install without the helper answers"
assert_same_file "$test_root/shell.out" "$test_root/shell-only.out" "an install without the helper prints the same snapshot"
assert_equal "" "$(cat "$test_root/shell-only.err")" "an install without the helper is silent about it"
mkdir -p "$shell_only/libexec/hydra"
cp "$fake_core" "$shell_only/libexec/hydra/hydra-core"
(unset HYDRA_CORE; "$shell_only/bin/hydra" snapshot > "$test_root/installed.out" 2>&1)
assert_success $? "an install with the helper answers"
assert_equal "$FAKE_CORE_ANSWER" "$(cat "$test_root/installed.out")" "the installed helper is used without HYDRA_CORE"

assert_fallback absent helper-unavailable "$test_root/missing-core"
permission_core="$test_root/permission-core"
write_scalar "$permission_core" blocked
assert_fallback permission helper-not-executable "$permission_core"
assert_fallback skew protocol-skew "$fake_core"
assert_fallback version-skew version-skew "$fake_core"
assert_fallback crash protocol-failure "$fake_core"
assert_fallback version-crash version-failure "$fake_core"
assert_fallback snapshot-crash snapshot-failure "$fake_core"
assert_fallback hang protocol-timeout "$fake_core"
assert_fallback snapshot-hang snapshot-timeout "$fake_core"

# Without a directory for the helper's output the helper is not tried.
saved_tmpdir="${TMPDIR:-}"
TMPDIR="$test_root/no-such-directory"
export TMPDIR
assert_fallback scratch scratch-unavailable "$fake_core"
if [ -n "$saved_tmpdir" ]; then TMPDIR="$saved_tmpdir"; else unset TMPDIR; fi

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

# Runs the default path with a timing-out helper that records PIDs of its
# subtree into the file and asserts the fallback is bounded and every recorded
# level is gone. An optional third argument names the variant in each message.
assert_tree_terminated() {
    _att_mode="$1"
    _att_levels="$2"
    _att_label="${3:+ ($3)}"
    _att_file="$test_root/$_att_mode.pid"
    : > "$_att_file"
    FAKE_CORE_CHILD_PID="$_att_file"
    export FAKE_CORE_CHILD_PID
    _att_started="$(date +%s)"
    assert_silent_fallback "$_att_mode" "$fake_core"
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
assert_fallback empty malformed-output "$fake_core"
assert_fallback unterminated malformed-output "$fake_core"
assert_fallback trailing malformed-output "$fake_core"

if [ -c /dev/full ]; then
    HYDRA_CORE="$core" "$HYDRA_BIN" snapshot > /dev/full 2> /dev/null
    assert_failure $? "a snapshot that cannot be written fails (native)"
    HYDRA_CORE="$core" "$HYDRA_BIN" snapshot --shell > /dev/full 2> /dev/null
    assert_failure $? "a snapshot that cannot be written fails (shell)"
else
    echo "SKIP: output errors: no /dev/full"
fi

# One verdict on every store: the helper prints a snapshot only for a store the
# shell implementation accepts, and whenever it refuses, the shell decides.
# Each case makes one change to the valid store above.
pristine="$test_root/pristine"
cp -R "$HYDRA_STATE_V2_ROOT" "$pristine"
head_c="projects/$project_a/heads/head_cccccccccccccccc"
instance_c="$head_c/instances/instance_dddddddddddddddd"

restore_state() {
    rm -rf "$HYDRA_STATE_V2_ROOT"
    cp -R "$pristine" "$HYDRA_STATE_V2_ROOT"
}

# Usage: record <path in the store> <printf format>
record() {
    # shellcheck disable=SC2059 # The format is the record's bytes, escapes included.
    printf "$2" > "$HYDRA_STATE_V2_ROOT/$1"
}

record_line() {
    printf '%s\n' "$2" > "$HYDRA_STATE_V2_ROOT/$1"
}

remove_record() {
    rm -f "$HYDRA_STATE_V2_ROOT/$1"
}

carriage_return() {
    _cr_value="$(sed -n '1p' "$HYDRA_STATE_V2_ROOT/$1")"
    printf '%s\r\n' "$_cr_value" > "$HYDRA_STATE_V2_ROOT/$1"
}

make_headless() {
    record_line "$head_c/terminal-mode" headless
    record_line "$instance_c/terminal-mode" headless
    record_line "$head_c/session" -
    record_line "$instance_c/session" -
    record_line "$head_c/desired-state" headless
}

make_half_headless() {
    record_line "$head_c/terminal-mode" headless
    record_line "$head_c/session" -
}

make_bare_project() {
    mkdir "$HYDRA_STATE_V2_ROOT/projects/project_cccccccccccccccc"
    record_line projects/project_cccccccccccccccc/project-id project_cccccccccccccccc
    record_line projects/project_cccccccccccccccc/repo-root /tmp/bare
}

make_misnamed_project() {
    mkdir "$HYDRA_STATE_V2_ROOT/projects/project_nothex"
    record_line projects/project_nothex/project-id project_nothex
    record_line projects/project_nothex/repo-root /tmp/nothex
}

# The verifier accepts any kind of opaque ID for the current instance; the
# helper insists on an instance ID, so the shell implementation answers.
rename_instance() {
    mv "$HYDRA_STATE_V2_ROOT/$instance_c" "$HYDRA_STATE_V2_ROOT/$head_c/instances/run_0123456789abcdef"
    record_line "$head_c/instances/run_0123456789abcdef/instance-id" run_0123456789abcdef
    record_line "$head_c/current-instance" run_0123456789abcdef
}

# Usage: assert_same_verdict <label> <accepted|rejected|same> <change...>
# The default path and the forced shell path agree on exit status, stdout and
# stderr; "accepted" and "rejected" also say which verdict that is.
assert_same_verdict() {
    _asv_label="$1"
    _asv_expected="$2"
    shift 2
    "$@"
    HYDRA_CORE="$core" "$HYDRA_BIN" snapshot --shell > "$test_root/verdict-shell.out" 2> "$test_root/verdict-shell.err"
    _asv_shell=$?
    HYDRA_CORE="$core" "$HYDRA_BIN" snapshot > "$test_root/verdict.out" 2> "$test_root/verdict.err"
    _asv_default=$?
    _asv_result=0
    [ "$_asv_default" -eq "$_asv_shell" ] || _asv_result=1
    cmp -s "$test_root/verdict-shell.out" "$test_root/verdict.out" || _asv_result=1
    cmp -s "$test_root/verdict-shell.err" "$test_root/verdict.err" || _asv_result=1
    case "$_asv_expected" in
        accepted) [ "$_asv_shell" -eq 0 ] || _asv_result=1 ;;
        rejected) if [ "$_asv_shell" -eq 0 ] || [ -s "$test_root/verdict.out" ]; then _asv_result=1; fi ;;
    esac
    if [ "$_asv_result" -ne 0 ]; then
        echo "shell: status $_asv_shell"; cat "$test_root/verdict-shell.out" "$test_root/verdict-shell.err"
        echo "default: status $_asv_default"; cat "$test_root/verdict.out" "$test_root/verdict.err"
    fi
    assert_success "$_asv_result" "$_asv_label: default path and shell implementation agree ($_asv_expected)"
    restore_state
}

# Usage: assert_core_refuses <label> <change...>
assert_core_refuses() {
    _acr_label="$1"
    shift
    "$@"
    "$core" snapshot "$HYDRA_STATE_V2_ROOT" > "$test_root/refused.out" 2> /dev/null
    _acr_status=$?
    if [ "$_acr_status" -ne 0 ] && [ ! -s "$test_root/refused.out" ]; then
        assert_success 0 "$_acr_label: the helper refuses and prints nothing"
    else
        assert_success 1 "$_acr_label: the helper refuses and prints nothing"
    fi
    restore_state
}

assert_same_verdict "record without its final newline" accepted record "$head_c/branch" 'first'
assert_same_verdict "headless head" accepted make_headless
assert_same_verdict "file among the heads" accepted record "projects/$project_a/heads/head_ffffffffffffffff" 'not a head\n'
assert_same_verdict "project without heads" accepted make_bare_project
assert_same_verdict "current instance named by another kind of ID" accepted rename_instance
assert_same_verdict "event log that is not events" same record "$head_c/events/events.jsonl" 'not an event\n'
assert_same_verdict "missing schema-version" rejected remove_record schema-version
assert_same_verdict "another schema version" rejected record_line schema-version 3
assert_same_verdict "project-id naming another project" rejected record_line "projects/$project_a/project-id" "$project_b"
assert_same_verdict "empty repo-root" rejected record_line "projects/$project_a/repo-root" ""
assert_same_verdict "project directory that is not an ID" rejected make_misnamed_project
assert_same_verdict "head-id naming another head" rejected record_line "$head_c/head-id" head_dddddddddddddddd
assert_same_verdict "two heads on one branch" rejected record_line "$head_c/branch" 'feature/"quoted"\path'
assert_same_verdict "empty branch" rejected record_line "$head_c/branch" ""
assert_same_verdict "branch of two lines" rejected record "$head_c/branch" 'first\nsecond\n'
assert_same_verdict "branch with a carriage return" rejected carriage_return "$head_c/branch"
assert_same_verdict "interactive head without a session" rejected record_line "$head_c/session" -
assert_same_verdict "unknown desired state" rejected record_line "$head_c/desired-state" paused
assert_same_verdict "unknown completion policy" rejected record_line "$head_c/completion-policy" never
assert_same_verdict "unknown terminal mode" rejected record_line "$head_c/terminal-mode" windowed
assert_same_verdict "headless head with an interactive instance" rejected make_half_headless
assert_same_verdict "current instance without its directory" rejected record_line "$head_c/current-instance" instance_0000000000000000
assert_same_verdict "instance-id naming another instance" rejected record_line "$instance_c/instance-id" instance_eeeeeeeeeeeeeeee
assert_same_verdict "empty instance record file" rejected record "$instance_c/observed-exit-code" ''

# Every record the verifier requires, by name: absent, then with a carriage
# return. The helper alone is asked, which is quick; whatever it refuses goes
# to the shell implementation, as the cases above show.
"$core" snapshot "$HYDRA_STATE_V2_ROOT" > /dev/null 2>&1
assert_success $? "the helper accepts the restored store"
for record_name in head-id branch session profile group created-at dependencies pr current-instance \
    desired-state completion-policy worktree base-ref; do
    assert_core_refuses "head without $record_name" remove_record "$head_c/$record_name"
    assert_core_refuses "head $record_name with a carriage return" carriage_return "$head_c/$record_name"
done
for record_name in task scopes; do
    assert_core_refuses "head without $record_name" remove_record "$head_c/$record_name"
done
for record_name in instance-id session started-at observed-status observed-source observed-confidence \
    observed-at observed-exit-code provider-session-id; do
    assert_core_refuses "instance without $record_name" remove_record "$instance_c/$record_name"
    assert_core_refuses "instance $record_name with a carriage return" carriage_return "$instance_c/$record_name"
done

printf '\nTests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
