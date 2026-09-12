#!/bin/sh
# Deterministic native mission-control acceptance.

set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d)"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
tui="${HYDRA_TUI_BIN:-$repo_root/build/hydra-tui}"
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
assert_equal "Hydra TUI 2.4.0 protocol 2" "$("$tui" --version)" "native TUI version handshake"

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
if grep -Eq 'instance_|source:|confidence|bounded-polling' "$test_root/heads.out"; then
    assert_success 1 "default list hides internal diagnostics"
else
    assert_success 0 "default list hides internal diagnostics"
fi

"$tui" --headless-fixture "$fixture" --size 100x28 --frames 1 --view detail > "$test_root/detail.out"
contains "Changes     4" "$test_root/detail.out" "details summarize actionable work"
contains "Reported outcome: done" "$test_root/detail.out" "task outcome is distinct from session status"
if grep -Eq 'instance_|lifecycle source:|adapter source:' "$test_root/detail.out"; then
    assert_success 1 "default details hide identifiers and source paths"
else
    assert_success 0 "default details hide identifiers and source paths"
fi
"$tui" --headless-fixture "$fixture" --size 100x28 --diagnostics > "$test_root/diagnostics.out"
contains "lifecycle source: /tmp/hydra/state/head_live" "$test_root/diagnostics.out" "diagnostics retain inspectable sources"
contains "confidence: verified-local-help" "$test_root/diagnostics.out" "diagnostics retain evidence confidence"
"$tui" --headless-fixture "$fixture" --size 80x24 --view coordination > "$test_root/coordination.out"
contains "Gates       1 of 2 approved" "$test_root/coordination.out" "coordination focuses on selected head"
"$tui" --headless-fixture "$fixture" --size 54x12 --view recovery > "$test_root/recovery.out"
contains "RECOVERY BOARD" "$test_root/recovery.out" "narrow recovery board renders"
contains "dead-session" "$test_root/recovery.out" "dead sessions are recoverable findings"
contains "stale-lock" "$test_root/recovery.out" "stale locks are recoverable findings"
contains "orphan-worktree" "$test_root/recovery.out" "orphan worktrees are recoverable findings"
contains "teardown-failure" "$test_root/recovery.out" "teardown failures are recoverable findings"
for view in heads detail coordination recovery; do
    "$tui" --headless-fixture "$fixture" --size 40x10 --view "$view" > "$test_root/bounded.out"
    assert_equal "11" "$(wc -l < "$test_root/bounded.out" | tr -d ' ')" "$view fits ten rows plus frame marker"
    awk 'length > 39 {exit 1}' "$test_root/bounded.out"
    assert_success $? "$view respects terminal width"
    tail -n 1 "$test_root/bounded.out" > "$test_root/footer.out"
    contains "? help" "$test_root/footer.out" "$view keeps actions visible"
done

# Column separators and panel edges must be invariant across varying values.
awk 'BEGIN {FS = OFS = "\t"}
$1 == "H" && !changed++ {for (i = 0; i < 120; i++) { $2 = $2 "x"; $4 = $4 "y" }}
{print}' "$fixture" > "$test_root/columns.tsv"
"$tui" --headless-fixture "$test_root/columns.tsv" --size 100x24 > "$test_root/columns.out"
awk '
/^\| [ >*][ *] [^ ]/ {
    if (length != 99) exit 1
    positions = ""
    for (i = 1; i <= length; i++) if (substr($0, i, 1) == "|") positions = positions ":" i
    if (rows++ && positions != expected) exit 1
    expected = positions
}
END { if (rows != 4) exit 1 }
' "$test_root/columns.out"
assert_success $? "table headers, cells, and borders have identical fixed columns"
contains "+- Heads" "$test_root/columns.out" "head list is enclosed in a named panel"

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
contains "feature-live" "$test_root/no-color.out" "narrow head list preserves status and branch identity"

HYDRA_TUI_THEME=dark "$tui" --headless-fixture "$fixture" --size 54x12 > "$test_root/theme.out"
cmp -s "$test_root/theme.out" "$test_root/no-color.out"
assert_success $? "themes preserve headless layout and status text"
HYDRA_TUI_THEME=invalid "$tui" --theme light --headless-fixture "$fixture" > /dev/null
assert_success $? "explicit theme overrides the environment"
"$tui" --theme invalid --headless-fixture "$fixture" > /dev/null 2> "$test_root/theme-error"
assert_equal "2" "$?" "unknown theme fails before entering terminal mode"
contains "terminal, dark, or light" "$test_root/theme-error" "invalid theme names explain supported choices"

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
printf '#!/bin/sh\n[ "${1:-}" = --version ] && { echo "Hydra TUI 2.4.0 protocol 2"; exit 0; }\nexit 4\n' > "$test_root/native-transient"
chmod +x "$test_root/native-transient"
HYDRA_TUI_BIN="$test_root/native-transient" "$repo_root/bin/hydra" tui > /dev/null 2> "$test_root/fallback.err"
assert_failure $? "non-TTY basic fallback still fails cleanly"
contains "starting the basic TUI" "$test_root/fallback.err" "transient native failure dispatches to basic fallback"

# shellcheck disable=SC2016
printf '#!/bin/sh\n[ "${1:-}" = --version ] && { echo "Hydra TUI 2.4.0 protocol 2"; exit 0; }\nprintf "NATIVE DEFAULT\\n"\n' > "$test_root/native-success"
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
# Hydra reports the physical home path, including macOS /var -> /private/var.
adapter_home="$(cd "$adapter_home" && pwd -P)"
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

# A branch lookup includes stopped records and returns the first directory in
# glob order. Do not replace that identity with the active row's directory.
boundary_home="$test_root/boundary-home"
boundary_repo="$test_root/boundary-repo"
boundary_project=project_ffffffffffffffff
mkdir -p "$boundary_home" "$boundary_repo/.git/hydra"
git -C "$boundary_repo" init -q
printf '%s\n' "$boundary_project" > "$boundary_repo/.git/hydra/project-id"
boundary_heads="$boundary_home/state/v2/projects/$boundary_project/heads"
duplicate_head="$(seed_state_head "$boundary_home" "$boundary_project" "$boundary_repo" duplicate session-duplicate none)"
first_head=head_0000000000000000
cp -R "$boundary_heads/$duplicate_head" "$boundary_heads/$first_head"
printf '%s\n' "$first_head" > "$boundary_heads/$first_head/head-id"
printf '%s\n' stopped > "$boundary_heads/$first_head/desired-state"
first_instance="$(cat "$boundary_heads/$first_head/current-instance")"
for boundary_dir in "$boundary_heads/$first_head" "$boundary_heads/$first_head/instances/$first_instance"; do
    printf '%s\n' headless > "$boundary_dir/terminal-mode"
    printf '%s\n' - > "$boundary_dir/session"
done
legacy_head="$(seed_state_head "$boundary_home" "$boundary_project" "$boundary_repo" legacy session-legacy none)"
legacy_instance="$(cat "$boundary_heads/$legacy_head/current-instance")"
rm "$boundary_heads/$legacy_head/terminal-mode" "$boundary_heads/$legacy_head/instances/$legacy_instance/terminal-mode"
invalid_head="$(seed_state_head "$boundary_home" "$boundary_project" "$boundary_repo" invalid-binding session-invalid none)"
invalid_instance="$(cat "$boundary_heads/$invalid_head/current-instance")"
printf '%s\n' head_bad > "$boundary_heads/$invalid_head/head-id"
printf '%s\n' instance_bad > "$boundary_heads/$invalid_head/instances/$invalid_instance/instance-id"
printf '%s\n' broken > "$boundary_heads/$invalid_head/terminal-mode"
(cd "$boundary_repo" && PATH="$repo_root/tests/fixtures/tui/fake-bin:$PATH" \
    FAKE_TMUX_SESSIONS="session-duplicate
session-legacy
session-invalid" HYDRA_HOME="$boundary_home" \
    "$repo_root/bin/hydra" tui --data) > "$test_root/boundaries.tsv"
assert_equal "$first_head stopped unavailable" \
    "$(awk -F '\t' '$1 == "H" && $2 == "duplicate" { print $25, $23, $8 }' "$test_root/boundaries.tsv")" \
    "native lookup preserves the stopped first-match identity and mode"
assert_equal 'active live' \
    "$(awk -F '\t' '$1 == "H" && $2 == "legacy" { print $7, $8 }' "$test_root/boundaries.tsv")" \
    "missing terminal mode retains legacy interactive observation"
assert_equal 'active live' \
    "$(awk -F '\t' '$1 == "H" && $2 == "invalid-binding" { print $7, $8 }' "$test_root/boundaries.tsv")" \
    "invalid terminal mode retains the observation fallback"
contains 'R	malformed-state	invalid-binding' "$test_root/boundaries.tsv" \
    "invalid head and instance bindings retain recovery evidence"
"$tui" --headless-fixture "$test_root/boundaries.tsv" --size 80x24 > /dev/null
assert_success $? "native renderer accepts the preserved identity boundary snapshot"

# An invalid first directory must not make lookup fall through to a later valid
# identity. The malformed name also exercises literal whitespace and backslashes.
bad_name="head_000 bad\\name"
bad_later="$(seed_state_head "$boundary_home" "$boundary_project" "$boundary_repo" bad-name session-bad none)"
cp -R "$boundary_heads/$duplicate_head" "$boundary_heads/$bad_name"
printf '%s\n' bad-name > "$boundary_heads/$bad_name/branch"
printf '%s\n' "$bad_name" > "$boundary_heads/$bad_name/head-id"
(cd "$boundary_repo" && PATH="$repo_root/tests/fixtures/tui/fake-bin:$PATH" \
    HYDRA_HOME="$boundary_home" "$repo_root/bin/hydra" tui --data) > "$test_root/bad-name.tsv"
contains 'R	malformed-state	bad-name' "$test_root/bad-name.tsv" "invalid directory identity remains explicit"
if grep -Fq "$bad_later" "$test_root/bad-name.tsv"; then
    assert_success 1 "invalid first identity does not select a later valid head"
else
    assert_success 0 "invalid first identity does not select a later valid head"
fi

# Change a later branch after rows are enumerated, when observing the first
# worktree. A remembered directory must not become a stale identity binding.
vanish_home="$test_root/vanish-home"
vanish_repo="$test_root/vanish-repo"
vanish_project=project_1111111111111111
mkdir -p "$vanish_home" "$vanish_repo" "$test_root/vanish-bin"
git -C "$vanish_repo" init -q
mkdir -p "$vanish_repo/.git/hydra"
printf '%s\n' "$vanish_project" > "$vanish_repo/.git/hydra/project-id"
vanish_heads="$vanish_home/state/v2/projects/$vanish_project/heads"
for vanish_branch in first vanishing; do
    vanish_old="$(seed_state_head "$vanish_home" "$vanish_project" "$vanish_repo" "$vanish_branch" "session-$vanish_branch" none)"
    case "$vanish_branch" in first) vanish_id=head_1111 ;; *) vanish_id=head_2222 ;; esac
    mv "$vanish_heads/$vanish_old" "$vanish_heads/$vanish_id"
    printf '%s\n' "$vanish_id" > "$vanish_heads/$vanish_id/head-id"
    printf '%s\n' "$vanish_repo" > "$vanish_heads/$vanish_id/worktree"
done
cat > "$test_root/vanish-bin/git" <<'GIT'
#!/bin/sh
if [ "${1:-}" = -C ] && [ "${3:-}" = status ] && [ -f "$HYDRA_TEST_CHANGE_MARKER" ]; then
    rm "$HYDRA_TEST_CHANGE_MARKER"
    case "$HYDRA_TEST_CHANGE_ACTION" in
        remove) rm "$HYDRA_TEST_CHANGE_BRANCH" ;;
        rename) printf '%s\n' renamed > "$HYDRA_TEST_CHANGE_BRANCH" ;;
    esac
fi
exec "$HYDRA_TEST_REAL_GIT" "$@"
GIT
chmod +x "$test_root/vanish-bin/git"
vanish_real_git="$(command -v git)"
for vanish_action in remove rename; do
    printf '%s\n' vanishing > "$vanish_heads/head_2222/branch"
    : > "$test_root/change-marker"
    (cd "$vanish_repo" && PATH="$test_root/vanish-bin:$repo_root/tests/fixtures/tui/fake-bin:$PATH" \
        HYDRA_TEST_REAL_GIT="$vanish_real_git" HYDRA_TEST_CHANGE_MARKER="$test_root/change-marker" \
        HYDRA_TEST_CHANGE_BRANCH="$vanish_heads/head_2222/branch" HYDRA_TEST_CHANGE_ACTION="$vanish_action" \
        HYDRA_HOME="$vanish_home" "$repo_root/bin/hydra" tui --data) > "$test_root/vanish-$vanish_action.tsv"
    if [ -f "$test_root/change-marker" ]; then vanish_changed=1; else vanish_changed=0; fi
    assert_success "$vanish_changed" "$vanish_action fixture changes the record during row observation"
    awk -F '\t' '$1 == "H" && $2 == "vanishing" {
        seen++; if ($25 != "" || $12 != "" || $23 != "unavailable") bad=1
    } END { exit !(seen == 1 && !bad) }' "$test_root/vanish-$vanish_action.tsv"
    assert_success $? "$vanish_action after enumeration cannot reuse a stale head identity"
    "$tui" --headless-fixture "$test_root/vanish-$vanish_action.tsv" --size 80x24 > /dev/null
    assert_success $? "$vanish_action during observation retains a complete native snapshot"
done

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

"$tui" --fleet --headless-fixture "$repo_root/tests/fixtures/tui/fleet-v1.tsv" --size 100x24 > "$test_root/fleet.txt"
contains 'DESIRED' "$test_root/fleet.txt" 'fleet labels durable desired state explicitly'
contains 'ovh' "$test_root/fleet.txt" 'fleet identifies remote hosts in their own column'
contains 'feature' "$test_root/fleet.txt" 'fleet identifies the remote branch'
contains 'a attach  c interrupt' "$test_root/fleet.txt" 'fleet advertises only remote-safe actions'

printf 'HYDRA_FLEET_TUI\t3\nT\tbuild\tresponded\t0\t-\tunreachable\tstale\t123\t4\nO\tbuild\ttask_abc\trun_xyz\tstep\tattempt-1\t/work\tnone\trecorded\tcancelled\tnone\t-\tinspect dependency\t123\t123\tfresh\t1\tready\tintegrity_verified\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\tconfirmed_stopped\tmanaged_commands\t123\treq-1\n' > "$test_root/fleet-v3.tsv"
"$tui" --fleet --headless-fixture "$test_root/fleet-v3.tsv" --view hosts --ascii --size 140x30 > "$test_root/fleet-v3-hosts.txt"
contains 'build' "$test_root/fleet-v3-hosts.txt" 'fleet v3 preserves host identity'
contains 'stale' "$test_root/fleet-v3-hosts.txt" 'fleet v3 renders stale freshness'
contains 'task_abc' "$test_root/fleet-v3-hosts.txt" 'fleet v3 renders task identity'
contains 'dependency' "$test_root/fleet-v3-hosts.txt" 'fleet v3 renders waiting reason'
"$tui" --fleet --headless-fixture "$test_root/fleet-v3.tsv" --view overview --ascii --size 140x30 > "$test_root/fleet-v3-overview.txt"
contains 'REMOTE TASKS' "$test_root/fleet-v3-overview.txt" 'fleet overview reserves a task observation panel'
contains 'inspect dependency' "$test_root/fleet-v3-overview.txt" 'fleet overview renders the next action'
contains 'confirmed_stopped' "$test_root/fleet-v3-overview.txt" 'fleet overview renders receiver cancellation acknowledgment'

printf '\nTests: %d, Passed: %d, Failed: %d\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
