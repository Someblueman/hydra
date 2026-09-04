#!/bin/sh
# Deterministic native mission-control acceptance.

set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d)"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
tui="$repo_root/build/hydra-tui"
fixture="$repo_root/tests/fixtures/tui/native-v2.tsv"

# shellcheck source=helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() { rm -rf "$test_root"; }
trap cleanup EXIT HUP INT TERM

contains() {
    _contains_pattern="$1"
    _contains_file="$2"
    _contains_message="$3"
    if grep -Fq "$_contains_pattern" "$_contains_file"; then
        assert_success 0 "$_contains_message"
    else
        assert_success 1 "$_contains_message"
    fi
}

seed_state_head() (
    _ssh_home="$1"
    _ssh_project="$2"
    _ssh_repo="$3"
    _ssh_branch="$4"
    _ssh_session="$5"
    _ssh_profile="$6"
    HYDRA_HOME="$_ssh_home"
    HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
    export HYDRA_HOME HYDRA_STATE_V2_ROOT
    # shellcheck disable=SC1091
    . "$repo_root/lib/locks.sh"
    # shellcheck disable=SC1091
    . "$repo_root/lib/identity.sh"
    # shellcheck disable=SC1091
    . "$repo_root/lib/state_v2.sh"
    state_v2_create_head "$_ssh_project" "$_ssh_branch" "$_ssh_session" \
        "$_ssh_profile" - 1 - - "$_ssh_repo"
)

seed_malformed_head() {
    _smh_home="$1"
    _smh_project="$2"
    _smh_branch="$3"
    _smh_id="$4"
    _smh_dir="$_smh_home/state/v2/projects/$_smh_project/heads/$_smh_id"
    mkdir -p "$_smh_dir"
    printf '%s\n' "$_smh_id" > "$_smh_dir/head-id"
    printf '%s\n' "$_smh_branch" > "$_smh_dir/branch"
}

echo "Running native TUI tests..."
echo "==========================="

assert_equal "2" "$("$tui" --protocol-version)" "native TUI protocol handshake"
assert_equal "Hydra TUI 2.0.0 protocol 2" "$("$tui" --version)" "native TUI version handshake"

awk 'BEGIN { FS = OFS = "\t" } $1 == "H" && !changed { $13 = "invalid"; changed = 1 } { print }' \
    "$fixture" > "$test_root/invalid-number.tsv"
"$tui" --headless-fixture "$test_root/invalid-number.tsv" --size 80x24 \
    > /dev/null 2> "$test_root/invalid-number.err"
assert_failure $? "malformed native numeric fields fail closed"
contains "invalid numeric field" "$test_root/invalid-number.err" "numeric protocol failure is explicit"

"$tui" --headless-fixture "$fixture" --size 80x24 --frames 2 > "$test_root/heads.out"
assert_success $? "headless fixture renders deterministically"
assert_equal "2" "$(grep -c '^FRAME ' "$test_root/heads.out")" "explicit frame bound is honored"
contains "LIVE" "$test_root/heads.out" "live state is distinct"
contains "STALE" "$test_root/heads.out" "stale state is distinct"
contains "UNAVAILABLE" "$test_root/heads.out" "unavailable state is distinct"
contains "done" "$test_root/heads.out" "declared state is shown"
contains "reported" "$test_root/heads.out" "observed state carries confidence"

"$tui" --headless-fixture "$fixture" --size 100x28 --frames 1 --view detail > "$test_root/detail.out"
contains "events: 12   signals: 2   messages: 3   gates: 2 (1 approved)" "$test_root/detail.out" "event signal message and gate summaries render"
contains "adapter: none   confidence: verified-local-help" "$test_root/detail.out" "adapter capability and confidence are explicit"
contains "notifications: 2 configured; delivery delegated" "$test_root/detail.out" "notification configuration and responsibility are explicit"
contains "lifecycle source: /tmp/hydra/state/head_live" "$test_root/detail.out" "dashboard state links to its source record"

"$tui" --headless-fixture "$fixture" --size 80x24 --frames 1 --view coordination > "$test_root/coordination.out"
contains "COORDINATION" "$test_root/coordination.out" "coordination view renders"
contains "claims scopes queue resources diff gates" "$test_root/coordination.out" "coordination view names existing contracts"
contains "selected sources (exact):" "$test_root/coordination.out" "coordination aggregates declare source confidence"
contains "inspect claims: hydra claim list" "$test_root/coordination.out" "claims link to an inspectable source command"
contains "inspect scopes: hydra scope show feature-live" "$test_root/coordination.out" "scopes link to an inspectable source command"
contains "inspect gates: hydra gate status feature-live" "$test_root/coordination.out" "gates link to an inspectable source command"
contains "inspect queue: hydra queue" "$test_root/coordination.out" "queue links to an inspectable source command"
contains "inspect resources: hydra resource status feature-live" "$test_root/coordination.out" \
    "resources link to an inspectable source command"
contains "inspect diff: hydra diff feature-live" "$test_root/coordination.out" "diff links to an inspectable source command"

"$tui" --headless-fixture "$fixture" --size 54x12 --frames 1 --view recovery > "$test_root/recovery.out"
contains "RECOVERY BOARD" "$test_root/recovery.out" "narrow recovery board renders"
contains "dead-session" "$test_root/recovery.out" "dead sessions are recoverable findings"
contains "stale-lock" "$test_root/recovery.out" "stale locks are recoverable findings"
contains "orphan-worktree" "$test_root/recovery.out" "orphan worktrees are recoverable findings"
contains "teardown-failure" "$test_root/recovery.out" "teardown failures are recoverable findings"

printf 'WRONG\t1\n' > "$test_root/bad.tsv"
"$tui" --headless-fixture "$test_root/bad.tsv" --size 80x24 > /dev/null 2>&1
assert_failure $? "invalid fixture handshake fails closed"

printf 'HYDRA_TUI\t2\nH\tbad\033[2Jbranch\ts\t-\t-\t-\tactive\tlive\t\tidle\texact\ti\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\trunning\t/source\thead_aaaaaaaaaaaaaaaa\tnone\texact\thydra capabilities --json\t0\t/notifications\n' > "$test_root/control.tsv"
"$tui" --headless-fixture "$test_root/control.tsv" --size 80x24 > "$test_root/control.out"
if LC_ALL=C grep "$(printf '\033')" "$test_root/control.out" >/dev/null 2>&1; then
    assert_success 1 "untrusted pane/data controls cannot escape the renderer"
else
    assert_success 0 "untrusted pane/data controls cannot escape the renderer"
fi

utf8_branch="$(printf 'wide-\344\270\255-combining-e\314\201-invalid-\377')"
printf 'HYDRA_TUI\t2\nH\t%s\ts\t-\t-\t-\tactive\tlive\t\tidle\texact\ti\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\trunning\t/source\thead_aaaaaaaaaaaaaaaa\tnone\texact\thydra capabilities --json\t0\t/notifications\n' \
    "$utf8_branch" > "$test_root/utf8.tsv"
"$tui" --headless-fixture "$test_root/utf8.tsv" --size 40x10 > "$test_root/utf8.out"
assert_success $? "UTF-8, combining, wide, and invalid bytes render safely at minimum width"
"$tui" --headless-fixture "$test_root/utf8.tsv" --size 100x14 --view detail > "$test_root/utf8-detail.out"
contains "wide-???-combining-e??-invalid-?" "$test_root/utf8-detail.out" "non-ASCII input has a deterministic safe representation"

NO_COLOR=1 "$tui" --headless-fixture "$fixture" --size 54x12 --view heads > "$test_root/no-color.out"
"$tui" --no-color --headless-fixture "$fixture" --size 54x12 --view heads > "$test_root/no-color-option.out"
if cmp -s "$test_root/no-color.out" "$test_root/no-color-option.out"; then
    assert_success 0 "environment and explicit no-color modes preserve identical status language"
else
    assert_success 1 "environment and explicit no-color modes preserve identical status language"
fi
contains ">  LIVE         feature-live" "$test_root/no-color.out" "narrow head list preserves status and branch identity"

broken_pipe="$test_root/broken-pipe"
mkfifo "$broken_pipe"
"$tui" --headless-fixture "$fixture" --size 120x40 --frames 100 > "$broken_pipe" &
broken_pid=$!
head -n 1 "$broken_pipe" > /dev/null
wait "$broken_pid"
assert_equal "141" "$?" "broken output pipe exits with a bounded signal category"

TERM=dumb "$tui" > /dev/null 2>&1
assert_failure $? "TERM=dumb fails cleanly"
"$tui" > /dev/null 2>&1
assert_failure $? "non-TTY invocation fails cleanly"

# shellcheck disable=SC2016
printf '#!/bin/sh\n[ "${1:-}" = --version ] && { echo "Hydra TUI 2.0.0 protocol 2"; exit 0; }\nexit 4\n' > "$test_root/native-transient"
chmod +x "$test_root/native-transient"
HYDRA_TUI_BIN="$test_root/native-transient" "$repo_root/bin/hydra" tui > /dev/null 2> "$test_root/fallback.err"
assert_failure $? "non-TTY basic fallback still fails cleanly"
contains "starting the basic TUI" "$test_root/fallback.err" "transient native failure dispatches to basic fallback"

# shellcheck disable=SC2016
printf '#!/bin/sh\n[ "${1:-}" = --version ] && { echo "Hydra TUI 2.0.0 protocol 2"; exit 0; }\nprintf "NATIVE DEFAULT\\n"\n' > "$test_root/native-success"
chmod +x "$test_root/native-success"
HYDRA_TUI_BIN="$test_root/native-success" "$repo_root/bin/hydra" tui > "$test_root/default.out"
contains "NATIVE DEFAULT" "$test_root/default.out" "plain tui dispatches to a qualified native executable"

# shellcheck disable=SC2016
printf '#!/bin/sh\n[ "${1:-}" = --version ] && sleep 5\n' > "$test_root/native-hanging"
chmod +x "$test_root/native-hanging"
_timeout_started="$(date +%s)"
HYDRA_TUI_TIMEOUT_SECONDS=1 HYDRA_TUI_BIN="$test_root/native-hanging" \
    "$repo_root/bin/hydra" tui --capabilities --json > "$test_root/hanging-capabilities.json"
_timeout_elapsed=$(($(date +%s) - _timeout_started))
contains '"native":false' "$test_root/hanging-capabilities.json" "hung native qualification falls back visibly"
if [ "$_timeout_elapsed" -lt 4 ]; then
    assert_success 0 "native qualification is bounded"
else
    assert_success 1 "native qualification is bounded"
fi

printf '#!/bin/sh\necho "Hydra TUI 1.9.0 protocol 2"\n' > "$test_root/native-skew"
chmod +x "$test_root/native-skew"
HYDRA_TUI_BIN="$test_root/native-skew" "$repo_root/bin/hydra" tui > /dev/null 2> "$test_root/skew.err"
assert_failure $? "version-skewed native TUI falls back cleanly"
contains "native TUI is unavailable" "$test_root/skew.err" "version-skewed native TUI is rejected before dispatch"

"$repo_root/bin/hydra" tui --native > /dev/null 2> "$test_root/removed-native.err"
assert_failure $? "removed --native mode fails closed"
contains "unknown TUI option '--native'" "$test_root/removed-native.err" "removed --native mode is not retained as a shim"

HYDRA_TUI_BIN="$tui" "$repo_root/bin/hydra" tui --capabilities --json > "$test_root/capabilities.json"
contains '"default_mode":"native"' "$test_root/capabilities.json" "capability diagnostics report native-first dispatch"
contains '"fallback_mode":"basic"' "$test_root/capabilities.json" "capability diagnostics report the basic fallback"
contains '"mutation_authority":"shell-cli"' "$test_root/capabilities.json" "capability diagnostics name shell mutation authority"

adapter_home="$test_root/adapter-home"
adapter_repo="$test_root/adapter-repo"
mkdir -p "$adapter_home/profiles/custom" "$adapter_repo"
git -C "$adapter_repo" init -q
mkdir -p "$adapter_repo/.git/hydra"
printf '%s\n' project_aaaaaaaaaaaaaaaa > "$adapter_repo/.git/hydra/project-id"
printf '%s\n' \
    'lifecycle.declared terminal 60' \
    'lifecycle.observed desktop 120' > "$adapter_repo/.git/hydra/notifications"
printf '%s\n' /usr/bin/true > "$adapter_home/profiles/custom/executable"
printf '%s\n' hook-v1 > "$adapter_home/profiles/custom/adapter"
printf '%s\n' user-declared > "$adapter_home/profiles/custom/confidence"
seed_state_head "$adapter_home" project_aaaaaaaaaaaaaaaa "$adapter_repo" \
    adapter-live adapter-live custom >/dev/null
seed_malformed_head "$adapter_home" project_aaaaaaaaaaaaaaaa adapter-malformed \
    head_bbbbbbbbbbbbbbbb
(cd "$adapter_repo" && HYDRA_HOME="$adapter_home" "$repo_root/bin/hydra" tui --data) > "$test_root/adapter.tsv"
assert_equal "HYDRA_TUI	2" "$(sed -n '1p' "$test_root/adapter.tsv")" "shell adapter publishes protocol handshake"
assert_equal "1" "$(awk -F '\t' '$1 == "H" { n++ } END { print n + 0 }' "$test_root/adapter.tsv")" "shell adapter publishes valid durable heads"
contains "R	malformed-state	adapter-malformed" "$test_root/adapter.tsv" "malformed state is an explicit recovery finding"
contains "hook-v1	user-declared	$adapter_home/profiles/custom" "$test_root/adapter.tsv" \
    "live adapter exposes recorded capability and confidence"
contains "	2	" "$test_root/adapter.tsv" "live adapter exposes configured notification count"
contains "/.git/hydra/notifications" "$test_root/adapter.tsv" "live adapter links notification configuration source"
"$tui" --headless-fixture "$test_root/adapter.tsv" --size 80x24 --view recovery > /dev/null
assert_success $? "native renderer accepts the live shell adapter boundary"

parity_home="$test_root/parity-home"
parity_repo="$test_root/parity-repo"
parity_project=project_cccccccccccccccc
mkdir -p "$parity_home" "$parity_repo"
git -C "$parity_repo" init -q
mkdir -p "$parity_repo/.git/hydra"
printf '%s\n' "$parity_project" > "$parity_repo/.git/hydra/project-id"
seed_state_head "$parity_home" "$parity_project" "$parity_repo" clean session-clean none >/dev/null
seed_state_head "$parity_home" "$parity_project" "$parity_repo" stale session-stale none >/dev/null
seed_malformed_head "$parity_home" "$parity_project" malformed head_dddddddddddddddd
(cd "$parity_repo" && PATH="$repo_root/tests/fixtures/tui/fake-bin:$PATH" \
    FAKE_TMUX_SESSIONS=session-clean HYDRA_HOME="$parity_home" \
    "$repo_root/bin/hydra" list --json --no-pr-status) > "$test_root/basic-list.json"
(cd "$parity_repo" && PATH="$repo_root/tests/fixtures/tui/fake-bin:$PATH" \
    FAKE_TMUX_SESSIONS=session-clean HYDRA_HOME="$parity_home" \
    "$repo_root/bin/hydra" tui --data) > "$test_root/native-list.tsv"
assert_equal "2" "$(awk -F '\t' '$1 == "H" { n++ } END { print n + 0 }' "$test_root/native-list.tsv")" \
    "native adapter publishes the two valid durable heads"
contains '"total":2' "$test_root/basic-list.json" "basic list publishes the same valid head cardinality"
contains '"branch": "clean"' "$test_root/basic-list.json" "basic list includes clean head"
contains 'H	clean	session-clean' "$test_root/native-list.tsv" "native list includes clean head"
contains '"status": "active"' "$test_root/basic-list.json" "basic list reports live clean status"
contains 'H	clean	session-clean	none	-	-	active	live' "$test_root/native-list.tsv" "native list agrees on live clean status"
contains 'R	malformed-state	malformed' "$test_root/native-list.tsv" "native list preserves malformed-state evidence"

changing_home="$test_root/changing-home"
changing_repo="$test_root/changing-repo"
changing_project=project_eeeeeeeeeeeeeeee
mkdir -p "$changing_home" "$changing_repo"
git -C "$changing_repo" init -q
mkdir -p "$changing_repo/.git/hydra"
printf '%s\n' "$changing_project" > "$changing_repo/.git/hydra/project-id"
seed_state_head "$changing_home" "$changing_project" "$changing_repo" changing-a session-a none >/dev/null
changing_head="$(seed_state_head "$changing_home" "$changing_project" "$changing_repo" changing-b session-b none)"
changing_state="$changing_home/state/v2/projects/$changing_project/heads/$changing_head/desired-state"
(
    change_index=0
    while [ "$change_index" -lt 40 ]; do
        if [ $((change_index % 2)) -eq 0 ]; then change_value=running; else change_value=stopped; fi
        printf '%s\n' "$change_value" > "$changing_state.next"
        mv "$changing_state.next" "$changing_state"
        change_index=$((change_index + 1))
    done
) &
change_pid=$!
change_index=0
while [ "$change_index" -lt 20 ]; do
    (cd "$changing_repo" && PATH="$repo_root/tests/fixtures/tui/fake-bin:$PATH" \
        FAKE_TMUX_SESSIONS=session-a HYDRA_HOME="$changing_home" \
        "$repo_root/bin/hydra" tui --data) > "$test_root/changing.tsv"
    "$tui" --headless-fixture "$test_root/changing.tsv" --size 80x24 > /dev/null || break
    change_index=$((change_index + 1))
done
wait "$change_pid"
assert_equal "20" "$change_index" "changing state remains a complete native/basic snapshot"

printf '\nTests: %d, Passed: %d, Failed: %d\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
