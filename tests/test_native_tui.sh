#!/bin/sh
# Deterministic native mission-control acceptance.

set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
tui="${HYDRA_TUI_BIN:?HYDRA_TUI_BIN is required: run via make test-tui or make test-one T=native_tui}"
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
assert_equal "Hydra TUI 2.9.0 protocol 2" "$("$tui" --version)" "native TUI version handshake"

awk 'BEGIN { FS = OFS = "\t" } $1 == "H" && !changed { $13 = "invalid"; changed = 1 } { print }' \
    "$fixture" > "$test_root/invalid-number.tsv"
"$tui" --headless-fixture "$test_root/invalid-number.tsv" --size 80x24 \
    > /dev/null 2> "$test_root/invalid-number.err"
assert_failure $? "malformed native numeric fields fail closed"
contains "invalid numeric field" "$test_root/invalid-number.err" "numeric protocol failure is explicit"

"$tui" --headless-fixture "$fixture" --size 80x24 --frames 2 > "$test_root/heads.out"
assert_success $? "headless fixture renders deterministically"
assert_equal "2" "$(grep -c '^FRAME ' "$test_root/heads.out")" "explicit frame bound is honored"
contains "running" "$test_root/heads.out" "live state is distinct"
contains "terminal gone" "$test_root/heads.out" "stale state is distinct"
contains "unknown" "$test_root/heads.out" "unavailable state is distinct"
if grep -Eq 'LIVE|STALE|UNAVAILABLE' "$test_root/heads.out"; then
    assert_success 1 "default list uses plain-language session states"
else
    assert_success 0 "default list uses plain-language session states"
fi
if grep -Eq 'instance_|source:|confidence|bounded-polling' "$test_root/heads.out"; then
    assert_success 1 "default list hides internal diagnostics"
else
    assert_success 0 "default list hides internal diagnostics"
fi

"$tui" --headless-fixture "$fixture" --size 100x28 --frames 1 --view detail > "$test_root/detail.out"
contains "Changed     4 files in the worktree" "$test_root/detail.out" "details summarize actionable work"
contains "Reported    done" "$test_root/detail.out" "task outcome is distinct from session status"
contains "Session     running" "$test_root/detail.out" "session status is labelled separately from the reported outcome"
if grep -Eq 'instance_|lifecycle source:|adapter source:' "$test_root/detail.out"; then
    assert_success 1 "default details hide identifiers and source paths"
else
    assert_success 0 "default details hide identifiers and source paths"
fi
"$tui" --headless-fixture "$fixture" --size 100x28 --diagnostics > "$test_root/diagnostics.out"
contains "lifecycle source: /tmp/hydra/state/head_live" "$test_root/diagnostics.out" "diagnostics retain inspectable sources"
contains "confidence: verified-local-help" "$test_root/diagnostics.out" "diagnostics retain evidence confidence"
"$tui" --headless-fixture "$fixture" --size 80x24 --view coordination > "$test_root/coordination.out"
contains "Approvals   1 of 2 approved" "$test_root/coordination.out" "coordination focuses on selected head"
contains "single agent session" "$test_root/coordination.out" "coordination explains what there is to coordinate"
"$tui" --headless-fixture "$fixture" --workflow-fixture "$repo_root/tests/fixtures/tui/workflow-empty.tsv" \
    --size 80x24 --view attention > "$test_root/attention-no-runs.out"
contains "no workflow runs yet, so nothing is waiting" "$test_root/attention-no-runs.out" \
    "attention in a project without workflow runs says so"
if grep -q 'unavailable' "$test_root/attention-no-runs.out"; then
    assert_success 1 "attention without workflow runs is not reported as unavailable"
else
    assert_success 0 "attention without workflow runs is not reported as unavailable"
fi
"$tui" --headless-fixture "$fixture" --size 54x12 --view recovery > "$test_root/recovery.out"
contains "RECOVERY  4 findings" "$test_root/recovery.out" "narrow recovery board renders"
contains "Terminal stopped: feature-stale" "$test_root/recovery.out" "dead sessions are explained as stopped terminals"
contains "Leftover lock" "$test_root/recovery.out" "stale locks are explained in plain language"
contains "Leftover worktree from a removed" "$test_root/recovery.out" "orphan worktrees are explained in plain language"
contains "Removal did not finish" "$test_root/recovery.out" "teardown failures are explained in plain language"
if grep -Eq 'dead-session|stale-lock|orphan-worktree|teardown-failure' "$test_root/recovery.out"; then
    assert_success 1 "recovery list keeps raw finding kinds out of the primary text"
else
    assert_success 0 "recovery list keeps raw finding kinds out of the primary text"
fi
"$tui" --headless-fixture "$fixture" --size 100x28 --view recovery --diagnostics > "$test_root/recovery-detail.out"
contains "Kind: dead-session" "$test_root/recovery-detail.out" "recovery diagnostics retain the raw finding kind"
contains "Inspect: hydra doctor" "$test_root/recovery-detail.out" "recovery diagnostics retain the inspection command"
contains "worktree and files are kept" "$test_root/recovery-detail.out" "recovery explains what is retained"
{ grep -v '^R	' "$fixture"; grep '^R	orphan-worktree	' "$fixture"; } > "$test_root/orphan-first.tsv"
"$tui" --headless-fixture "$test_root/orphan-first.tsv" --size 140x28 --view recovery --diagnostics > "$test_root/orphan-detail.out"
contains "Leftover worktree from a removed task: feature-old (/tmp/wt/head_0123456789abcdef), 1.3 GiB; no uncommitted changes" \
    "$test_root/orphan-detail.out" "leftover worktree finding names branch, path, size and change state"
contains "Inspect: hydra gc --policy orphaned --dry-run" "$test_root/orphan-detail.out" "leftover worktree check is the read-only gc review"
contains "x removes this worktree after confirmation; the branch is kept" "$test_root/orphan-detail.out" "leftover worktree offers confirmed removal"
"$tui" --headless-fixture "$test_root/orphan-first.tsv" --size 100x28 --view recovery > "$test_root/orphan-list.out"
contains "x remove" "$test_root/orphan-list.out" "recovery footer offers removal for a clean leftover worktree"
sed 's/^R	orphan-worktree	/R	orphan-worktree-dirty	/' "$test_root/orphan-first.tsv" > "$test_root/orphan-dirty.tsv"
"$tui" --headless-fixture "$test_root/orphan-dirty.tsv" --size 140x28 --view recovery --diagnostics > "$test_root/orphan-dirty.out"
contains "has uncommitted changes" "$test_root/orphan-dirty.out" "dirty leftover worktree says it has uncommitted changes"
if grep -q "x remove" "$test_root/orphan-dirty.out"; then
    assert_success 1 "dirty leftover worktree is never offered for in-app removal"
else
    assert_success 0 "dirty leftover worktree is never offered for in-app removal"
fi
for view in heads detail coordination recovery; do
    "$tui" --headless-fixture "$fixture" --size 40x10 --view "$view" > "$test_root/bounded.out"
    assert_equal "11" "$(wc -l < "$test_root/bounded.out" | tr -d ' ')" "$view fits ten rows plus frame marker"
    "$tui" --ascii --headless-fixture "$fixture" --size 40x10 --view "$view" > "$test_root/bounded-ascii.out"
    awk 'length > 39 {exit 1}' "$test_root/bounded-ascii.out"
    assert_success $? "$view respects terminal width"
    tail -n 1 "$test_root/bounded.out" > "$test_root/footer.out"
    contains "? help" "$test_root/footer.out" "$view keeps actions visible"
done

# Column separators and panel edges must be invariant across varying values.
awk 'BEGIN {FS = OFS = "\t"}
$1 == "H" && !changed++ {for (i = 0; i < 120; i++) { $2 = $2 "x"; $4 = $4 "y" }}
{print}' "$fixture" > "$test_root/columns.tsv"
"$tui" --ascii --headless-fixture "$test_root/columns.tsv" --size 100x24 > "$test_root/columns.out"
awk '
index($0, "STATUS") && index($0, "AGENT") { status = index($0, "STATUS"); agent = index($0, "AGENT"); next }
status && /^\| [ >*][ *] [^ ]/ {
    if (length != 99) exit 1
    if (substr($0, status - 1, 1) != " " || substr($0, status, 1) == " ") exit 1
    if (substr($0, agent - 1, 1) != " " || substr($0, agent, 1) == " ") exit 1
    rows++
}
END { if (rows != 3) exit 1 }
' "$test_root/columns.out"
assert_success $? "table headers and cells share fixed status and agent columns"
contains "+- Heads in this project" "$test_root/columns.out" "head list is enclosed in a named panel"
"$tui" --headless-fixture "$fixture" --size 100x24 > "$test_root/unicode.out"
contains "$(printf '\342\225\255\342\224\200 Heads in this project')" "$test_root/unicode.out" "default output draws rounded Unicode borders"
"$tui" --headless-fixture "$fixture" --size 80x24 --view heads > "$test_root/tabs.out"
contains "[Work] Details Overview Attention Recovery Flows Stats Workspace Hosts" "$test_root/tabs.out" "every tab is fully visible at 80 columns with short labels"
"$tui" --headless-fixture "$fixture" --size 80x24 --view hosts > "$test_root/hosts-tabs.out"
contains " Work Details Overview Attention Recovery Flows Stats Workspace [Hosts]" "$test_root/hosts-tabs.out" "the Hosts tab keeps the leftmost tab in view at 80 columns"
"$tui" --headless-fixture "$fixture" --size 80x24 --view coordination > "$test_root/coordination-tabs.out"
contains "Work Details [Coord] Overview Attention Recovery Flows Stats Workspace Hosts" "$test_root/coordination-tabs.out" "the coordination tab fits every tab at 80 columns"
"$tui" --fleet --headless-fixture "$fixture" --size 80x24 --view hosts > "$test_root/fleet-tabs.out"
contains "Work  Details  Overview  [Hosts]  Attention  Recovery  Stats  Workspace" "$test_root/fleet-tabs.out" "fleet tabs are fully visible at 80 columns"
"$tui" --headless-fixture "$fixture" --size 100x24 --view hosts > "$test_root/wide-tabs.out"
contains "Work  Details  Overview  Attention  Recovery  Workflows  Statistics  Workspace  [Hosts]" "$test_root/wide-tabs.out" "tabs keep full labels from 100 columns"
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v2.tsv" --size 80x24 --view statistics > "$test_root/stats-tabs.out"
contains "[Stats]" "$test_root/stats-tabs.out" "statistics keeps the shared tab bar"
contains "Work Details Overview" "$test_root/stats-tabs.out" "statistics keeps the other tabs reachable"
# Schema 4 usage rows: token totals, agent evidence and resource metrics said in full.
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v4.tsv" --size 140x40 --view statistics > "$test_root/stats-usage.out"
contains "Agent tokens" "$test_root/stats-usage.out" "statistics scope panel names agent tokens"
contains "in      3.85M" "$test_root/stats-usage.out" "input tokens are summed across the scope"
contains "cached  3.74M" "$test_root/stats-usage.out" "cached input tokens are summed across the scope"
contains "cost not reported" "$test_root/stats-usage.out" "unreported cost stays unknown instead of zero"
contains "CPU and memory: not" "$test_root/stats-usage.out" "unmeasured resources are explained, not truncated"
contains "3.85M in / 23.0k out" "$test_root/stats-usage.out" "the run table shows per-run tokens"
! grep -Fq "Tokens / cost: not m" "$test_root/stats-usage.out"
assert_success $? "the old truncated resource note is gone"
# Head sessions (schema 5): a sample without runs opens on them; inferred
# times are labelled, unobserved time is its own number, unknown is "--".
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v5-sessions.tsv" --size 140x40 --view statistics > "$test_root/stats-sessions.out"
contains "HEAD SESSIONS / newest start first" "$test_root/stats-sessions.out" "statistics opens on head sessions when there are no runs"
contains "Observed 38% of 6h00m session time; 10m00s reported by adapters, the rest inferred from terminal output" "$test_root/stats-sessions.out" "session coverage and its sources are stated"
contains "inferred, 53% of observed" "$test_root/stats-sessions.out" "the quiet share is labelled as inferred"
contains "Unobserved      3h42m" "$test_root/stats-sessions.out" "unobserved time is shown apart from quiet"
contains "docs-refresh                    opencode replaced 09-07 08:00 UTC  2h00m    active --        quiet --" "$test_root/stats-sessions.out" "an unobserved session shows unknown activity"
contains "old-experiment                  -        ended    unknown date     --" "$test_root/stats-sessions.out" "an undated session shows unknown length"
contains "SESSIONS STARTED / 7 x 24h" "$test_root/stats-sessions.out" "sessions per day are charted"
contains "s shows runs (0)" "$test_root/stats-sessions.out" "the sessions page names the runs page"
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v5.tsv" --size 140x40 --view statistics > "$test_root/stats-mixed.out"
contains "RUNS / newest creation first" "$test_root/stats-mixed.out" "a sample with runs opens on runs"
contains "s head sessions" "$test_root/stats-mixed.out" "the runs page offers head sessions"
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v5-empty.tsv" --size 140x40 --view statistics > "$test_root/stats-empty.out"
contains "No head sessions recorded yet" "$test_root/stats-empty.out" "an empty sample says nothing was recorded"
! grep -Fq "7 x 24h" "$test_root/stats-empty.out"
assert_success $? "an empty sample draws no chart"
# Samples without sessions render exactly as they did before sessions existed.
for stats_case in v2:80x24 v2:140x40 v4:80x24 v4:140x40; do
    "$tui" --ascii --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-${stats_case%%:*}.tsv" --size "${stats_case##*:}" --view statistics > "$test_root/stats-same.out"
    cmp -s "$test_root/stats-same.out" "$repo_root/tests/fixtures/tui/statistics-${stats_case%%:*}-${stats_case##*:}.txt"
    assert_success $? "statistics ${stats_case%%:*} at ${stats_case##*:} is unchanged without sessions"
done
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v4.tsv" --size 80x24 --view statistics > "$test_root/stats-usage-narrow.out"
contains "tokens in 3.85M" "$test_root/stats-usage-narrow.out" "narrow statistics keep the token total on the timing line"
"$tui" --headless-fixture "$fixture" --size 80x24 --view workspace > "$test_root/workspace-tabs.out"
contains "[Workspace]" "$test_root/workspace-tabs.out" "workspace keeps the shared tab bar"
contains "HYDRA / PLAN TOGETHER" "$test_root/workspace-tabs.out" "workspace conversation layout is titled after the reviewed mockup"
if grep -q "Tab to focus / scroll" "$test_root/workspace-tabs.out"; then
    assert_success 1 "workspace panes do not repeat focus hints"
else
    assert_success 0 "workspace panes do not repeat focus hints"
fi
printf 'HYDRA_TUI\t2\n' > "$test_root/empty.tsv"
"$tui" --headless-fixture "$test_root/empty.tsv" --size 80x24 --view heads > "$test_root/empty-heads.out"
contains "No agent work in this project yet" "$test_root/empty-heads.out" "empty head list explains the situation"
contains "Press n to start one" "$test_root/empty-heads.out" "empty head list offers a next action"
"$tui" --headless-fixture "$test_root/empty.tsv" --size 80x24 --view workspace > "$test_root/empty-workspace.out"
contains "Press n to start a task" "$test_root/empty-workspace.out" "empty workspace offers a next action"
"$tui" --headless-fixture "$test_root/empty.tsv" --size 80x24 --view overview > "$test_root/empty-overview.out"
contains "No agent work in this project yet" "$test_root/empty-overview.out" "empty overview explains instead of showing empty charts"
"$tui" --headless-fixture "$test_root/empty.tsv" --size 80x24 --view workflows > "$test_root/empty-workflows.out"
contains "A workflow is a saved sequence of steps" "$test_root/empty-workflows.out" "empty workflows view explains what a workflow is"

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
"$tui" --ascii --headless-fixture "$test_root/utf8.tsv" --size 100x14 --view detail > "$test_root/utf8-detail.out"
contains "wide-? -combining-e-invalid-?" "$test_root/utf8-detail.out" "ASCII mode keeps wide columns, drops joined marks and marks invalid bytes"
"$tui" --headless-fixture "$test_root/utf8.tsv" --size 100x14 --view detail > "$test_root/utf8-detail-unicode.out"
contains "$(printf 'wide-\344\270\255-combining-e\314\201-invalid-?')" "$test_root/utf8-detail-unicode.out" "Unicode mode preserves wide and combining characters and marks invalid bytes"

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
printf '#!/bin/sh\n[ "${1:-}" = --version ] && { echo "Hydra TUI 2.9.0 protocol 2"; exit 0; }\nexit 4\n' > "$test_root/native-transient"
chmod +x "$test_root/native-transient"
HYDRA_TUI_BIN="$test_root/native-transient" "$repo_root/bin/hydra" tui > "$test_root/unexpected.out" 2> "$test_root/unexpected.err"
assert_equal "4" "$?" "unexpected native exit status is returned unchanged"
contains "native TUI exited unexpectedly with status 4" "$test_root/unexpected.err" "unexpected native exit status is reported"
assert_equal "1" "$(wc -l < "$test_root/unexpected.err" | tr -d ' ')" "unexpected native exit is reported in one line"
assert_equal "" "$(cat "$test_root/unexpected.out")" "no other interface starts after an unexpected native exit"

for missing_args in "" "--task feature-live" "--view heads"; do
    # shellcheck disable=SC2086
    HYDRA_TUI_BIN="$test_root/missing-hydra-tui" "$repo_root/bin/hydra" tui $missing_args \
        > "$test_root/missing.out" 2> "$test_root/missing.err"
    assert_equal "1" "$?" "missing native TUI fails: tui $missing_args"
    contains "native TUI is unavailable" "$test_root/missing.err" "missing native TUI is named: tui $missing_args"
    contains "the control centre needs the hydra-tui helper" "$test_root/missing.err" "missing native TUI explains the requirement: tui $missing_args"
    contains "make build-tui" "$test_root/missing.err" "missing native TUI says how to build it: tui $missing_args"
    contains "HYDRA_TUI_ARTIFACT" "$test_root/missing.err" "missing native TUI names the installer artifact: tui $missing_args"
    contains "'hydra list' and 'hydra switch' work without it" "$test_root/missing.err" "missing native TUI names what still works: tui $missing_args"
    assert_equal "" "$(cat "$test_root/missing.out")" "no other interface starts without the native TUI: tui $missing_args"
done

for basic_tui in "$tui" "$test_root/missing-hydra-tui"; do
    HYDRA_TUI_BIN="$basic_tui" "$repo_root/bin/hydra" tui --basic > "$test_root/basic.out" 2> "$test_root/basic.err"
    assert_equal "2" "$?" "removed --basic mode is a usage error"
    contains "the basic shell TUI was removed" "$test_root/basic.err" "removed --basic mode says it was removed"
    contains "'hydra list' and 'hydra switch'" "$test_root/basic.err" "removed --basic mode names what to use instead"
    assert_equal "" "$(cat "$test_root/basic.out")" "removed --basic mode starts no interface"
done
"$repo_root/bin/hydra" help > "$test_root/help.out" 2>&1
contains "  tui  " "$test_root/help.out" "help lists the tui command"
if grep -Fq -- "--basic" "$test_root/help.out"; then
    assert_success 1 "help no longer offers --basic"
else
    assert_success 0 "help no longer offers --basic"
fi

# shellcheck disable=SC2016
printf '#!/bin/sh\n[ "${1:-}" = --version ] && { echo "Hydra TUI 2.9.0 protocol 2"; exit 0; }\nprintf "NATIVE DEFAULT\\n"\n' > "$test_root/native-success"
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
contains '"native":false' "$test_root/hanging-capabilities.json" "hung native qualification is reported as unavailable"
if [ "$_timeout_elapsed" -lt 4 ]; then
    assert_success 0 "native qualification is bounded"
else
    assert_success 1 "native qualification is bounded"
fi

printf '#!/bin/sh\necho "Hydra TUI 1.9.0 protocol 2"\n' > "$test_root/native-skew"
chmod +x "$test_root/native-skew"
HYDRA_TUI_BIN="$test_root/native-skew" "$repo_root/bin/hydra" tui > /dev/null 2> "$test_root/skew.err"
assert_equal "1" "$?" "version-skewed native TUI is refused"
contains "native TUI is unavailable" "$test_root/skew.err" "version-skewed native TUI is rejected before dispatch"

"$repo_root/bin/hydra" tui --native > /dev/null 2> "$test_root/removed-native.err"
assert_failure $? "removed --native mode fails closed"
contains "unknown TUI option '--native'" "$test_root/removed-native.err" "removed --native mode is not retained as a shim"

HYDRA_TUI_BIN="$tui" "$repo_root/bin/hydra" tui --capabilities --json > "$test_root/capabilities.json"
contains '"default_mode":"native"' "$test_root/capabilities.json" "capability diagnostics report native-first dispatch"
contains '"fallback_mode":"none","basic":false' "$test_root/capabilities.json" "capability diagnostics report that no fallback exists"
HYDRA_TUI_BIN="$test_root/missing-hydra-tui" "$repo_root/bin/hydra" tui --capabilities > "$test_root/capabilities.txt"
assert_success $? "capability diagnostics succeed without the native TUI"
contains "native TUI: unavailable" "$test_root/capabilities.txt" "capability diagnostics name a missing native TUI"
contains "hydra list and hydra switch still work" "$test_root/capabilities.txt" "capability diagnostics name what works without it"
if grep -Fqi "basic" "$test_root/capabilities.txt"; then
    assert_success 1 "capability diagnostics no longer mention a basic TUI"
else
    assert_success 0 "capability diagnostics no longer mention a basic TUI"
fi
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
assert_equal "20" "$change_index" "changing state remains a complete native snapshot"

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

# Runs group the heads they create; headless heads describe their steps.
runs_heads="$repo_root/tests/fixtures/tui/native-runs.tsv"
runs_done="$repo_root/tests/fixtures/tui/workflow-runs.tsv"
runs_live="$repo_root/tests/fixtures/tui/workflow-runs-running.tsv"
not_contains() {
    if grep -Fq "$1" "$2"; then assert_success 1 "$3"; else assert_success 0 "$3"; fi
}
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --size 120x30 --view heads > "$test_root/runs-heads.out"
contains "2 heads (+1 in runs) / 1 running / 1 need attention" "$test_root/runs-heads.out" "header counts the heads the user started and counts run heads within their run"
contains "+ plan run kill-dry-run" "$test_root/runs-heads.out" "the run is listed under its planning head with an expand affordance"
not_contains "kill-dry-run-worker" "$test_root/runs-heads.out" "run heads start collapsed under their run"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --task kill-dry-run-worker --size 120x30 --view heads > "$test_root/runs-expanded.out"
contains "  - plan run kill-dry-run" "$test_root/runs-expanded.out" "a selected run head opens its run"
contains "worker   kill-dry-run-worker" "$test_root/runs-expanded.out" "the worker is listed inside its run with its role"
contains "verifier kill-dry-run-check" "$test_root/runs-expanded.out" "a retired verifier stays visible inside its run"
contains "retired" "$test_root/runs-expanded.out" "the verifier shows that it was retired"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --task kill-dry-run-worker --size 120x40 --view detail > "$test_root/runs-worker.out"
contains "Terminal    headless (no terminal)" "$test_root/runs-worker.out" "a headless head says it has no terminal"
contains "Agent       codex / step implement / succeeded" "$test_root/runs-worker.out" "a headless head names the agent of its step"
contains "Model       configured default: gpt-6-sol (not observed)" "$test_root/runs-worker.out" "a configured model is labelled as configuration"
contains "Effort      configured default: xhigh (not observed)" "$test_root/runs-worker.out" "a configured effort is labelled as configuration"
contains "Executable  codex-cli 0.46.0" "$test_root/runs-worker.out" "the executable version is shown"
contains "Tokens      in 24,763  cached 24,448  out 1,122  cost unknown" "$test_root/runs-worker.out" "tokens are shown and an unreported cost stays unknown"
contains "Role        worker (holds the run's result)" "$test_root/runs-worker.out" "the worker role is explained"
not_contains "Session" "$test_root/runs-worker.out" "a headless head is not described by a terminal session"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_live" --task kill-dry-run-worker --size 120x40 --view detail > "$test_root/runs-worker-live.out"
contains "Tokens      reported when the step finishes" "$test_root/runs-worker-live.out" "running tokens are pending, not zero"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --size 120x40 --view detail > "$test_root/runs-planner.out"
contains "Plan        plan 60c5133478a6 approved 2026-09-26 10:21 UTC / run run_aaaaaaaaaaaaaaaaaaaa / succeeded" "$test_root/runs-planner.out" "the planning head shows its plan approval and run"
contains "Worker branch kill-dry-run-worker holds the result" "$test_root/runs-planner.out" "the planning head names what to do with the result"
contains "Approval requests  none" "$test_root/runs-planner.out" "gate approval requests are named as such"
not_contains "0 of 0" "$test_root/runs-planner.out" "no misleading zero-of-zero approval count"
contains "Full diff   everything since the branch base: press : and choose diff" "$test_root/runs-planner.out" "the full diff row has a value"
not_contains "Full diff       :" "$test_root/runs-planner.out" "the full diff row has no stray colon"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --size 140x40 --view overview > "$test_root/runs-overview.out"
contains "RUN / plan run kill-dry-run / succeeded / 2 heads / from add-kill" "$test_root/runs-overview.out" "Overview centres on the run"
contains "implement        exec/compose   succeeded         1     1m06s         kill-dry-run-worker" "$test_root/runs-overview.out" "each step shows kind, role, state, attempts, duration and head"
contains "kill-dry-run-check (retired)" "$test_root/runs-overview.out" "the run panel shows the retired verifier head"
not_contains "QUEUE DEPTH" "$test_root/runs-overview.out" "an empty queue chart is not drawn"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_live" --size 140x40 --view overview > "$test_root/runs-overview-live.out"
contains "Running step implement on kill-dry-run-worker (codex)" "$test_root/runs-overview-live.out" "a running run says what is running and where"
contains "kill-dry-run-check (pending)" "$test_root/runs-overview-live.out" "a head the run has not created yet is pending"
"$tui" --ascii --headless-fixture "$runs_heads" --size 140x40 --view overview > "$test_root/runs-overview-empty.out"
contains "No workflow run yet." "$test_root/runs-overview-empty.out" "the run panel explains when there is no run"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --task kill-dry-run-worker --size 120x40 --diagnostics > "$test_root/runs-technical.out"
for group in IDENTITY LIFECYCLE ADAPTER SOURCES; do
    contains "$group" "$test_root/runs-technical.out" "technical details group $group"
done
contains "Session     - (headless)" "$test_root/runs-technical.out" "technical details name the terminal mode"
for size in 40x10 80x24 140x40; do
    for view in heads detail overview workspace; do
        "$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_live" --task kill-dry-run-worker --size "$size" --view "$view" > "$test_root/runs-bounded.out"
        awk -v cols="${size%x*}" 'length >= cols { exit 1 }' "$test_root/runs-bounded.out"
        assert_success $? "run $view view fits $size"
    done
done
awk 'BEGIN { FS = OFS = "\t" } $1 == "E" { NF = 12 } { print }' "$runs_done" > "$test_root/runs-short.tsv"
"$tui" --headless-fixture "$runs_heads" --workflow-fixture "$test_root/runs-short.tsv" --size 80x24 > /dev/null 2>&1
assert_failure $? "a malformed agent receipt row fails closed"
awk 'BEGIN { FS = OFS = "\t" } $1 == "H" { NF = 30 } { print }' "$runs_heads" > "$test_root/runs-v30.tsv"
"$tui" --ascii --headless-fixture "$test_root/runs-v30.tsv" --workflow-fixture "$runs_done" --task kill-dry-run-worker --size 120x30 --view detail > "$test_root/runs-v30.out"
contains "headless (no terminal)" "$test_root/runs-v30.out" "rows without a terminal-mode field still recognise a headless head"

# Keys: --print-keys writes the bindings in effect as a keys file, which reads
# back unchanged. A keys file comes from $HYDRA_HOME/tui/keys.conf or
# HYDRA_TUI_KEYS; a bad one is reported and never stops the UI.
keys_home="$test_root/keys-home"
mkdir -p "$keys_home/tui"
keys_lines() { sed -e '/^#/d' -e 's/ *#.*$//' "$1"; }
HYDRA_TUI_KEYS='' HYDRA_HOME="$keys_home" "$tui" --print-keys > "$test_root/keys-default.conf" 2> "$test_root/keys-default.err"
assert_success $? "default keys print without a keys file"
assert_equal "" "$(cat "$test_root/keys-default.err")" "a missing default keys file is not reported"
cat > "$test_root/keys-expected.lines" <<'EOF'
terminal.leave = ctrl+], ctrl+\ tab
terminal.close = ctrl+\ x
terminal.next = ctrl+\ n
terminal.reconnect = ctrl+\ r
terminal.history = ctrl+\ [
terminal.live = ctrl+\ ]
app.quit = ctrl+\ q
view.statistics = ctrl+\ D
workspace.layout.conversation = ctrl+\ A
workspace.layout.plan = ctrl+\ B
workspace.layout.monitor = ctrl+\ C
workspace.zoom = ctrl+\ z
workspace.split = ctrl+\ S
terminal.literal = ctrl+\ ctrl+\, ctrl+\ ctrl+]
EOF
keys_lines "$test_root/keys-default.conf" > "$test_root/keys-default.lines"
cmp -s "$test_root/keys-expected.lines" "$test_root/keys-default.lines"
assert_success $? "default keys are Ctrl-] to leave and the Ctrl-\\ leader for the rest"
HYDRA_TUI_KEYS="$test_root/keys-default.conf" HYDRA_HOME="$keys_home" "$tui" --print-keys > "$test_root/keys-default-again.conf" 2> "$test_root/keys-again.err"
assert_success $? "printed default keys are a valid keys file"
cmp -s "$test_root/keys-default.conf" "$test_root/keys-default-again.conf"
assert_success $? "printed default keys read back unchanged"
assert_equal "" "$(cat "$test_root/keys-again.err")" "printed keys raise no report"
cat > "$keys_home/tui/keys.conf" <<'EOF'
# the tmux-style prefix again
terminal.leave = ctrl+b tab
terminal.close = Ctrl-B x
app.quit = ctrl+b q, f10
terminal.reconnect = none
terminal.literal = ctrl+b ctrl+b
EOF
HYDRA_TUI_KEYS='' HYDRA_HOME="$keys_home" "$tui" --print-keys > "$test_root/keys-custom.conf" 2> "$test_root/keys-custom.err"
assert_success $? "keys file in HYDRA_HOME/tui is read"
assert_equal "" "$(cat "$test_root/keys-custom.err")" "valid keys file raises no report"
keys_lines "$test_root/keys-custom.conf" > "$test_root/keys-custom.lines"
contains 'terminal.leave = ctrl+b tab' "$test_root/keys-custom.lines" "keys file rebinds leave to the old prefix"
contains 'terminal.close = ctrl+b x' "$test_root/keys-custom.lines" "display spelling is accepted in a keys file"
contains 'app.quit = ctrl+b q, f10' "$test_root/keys-custom.lines" "an action takes two chords"
contains 'terminal.reconnect = none' "$test_root/keys-custom.lines" "none unbinds an action"
contains 'terminal.next = ctrl+\ n' "$test_root/keys-custom.lines" "actions a keys file does not name keep their defaults"
HYDRA_TUI_KEYS="$test_root/keys-custom.conf" HYDRA_HOME="$test_root/no-such-home" "$tui" --print-keys > "$test_root/keys-custom-again.conf" 2>/dev/null
assert_success $? "HYDRA_TUI_KEYS names the keys file"
cmp -s "$test_root/keys-custom.conf" "$test_root/keys-custom-again.conf"
assert_success $? "printed custom keys read back unchanged"
HYDRA_TUI_KEYS="$test_root/keys-default.conf" HYDRA_HOME="$keys_home" "$tui" --print-keys > "$test_root/keys-override.conf" 2>/dev/null
cmp -s "$test_root/keys-default.conf" "$test_root/keys-override.conf"
assert_success $? "HYDRA_TUI_KEYS takes precedence over the file in HYDRA_HOME"
(cd "$adapter_repo" && HYDRA_HOME="$adapter_home" HYDRA_TUI_KEYS="$test_root/keys-custom.conf" HYDRA_TUI_BIN="$tui" \
    "$repo_root/bin/hydra" tui --print-keys) > "$test_root/keys-shell.conf" 2> "$test_root/keys-shell.err"
assert_success $? "hydra tui --print-keys reaches the native TUI"
cmp -s "$test_root/keys-custom.conf" "$test_root/keys-shell.conf"
assert_success $? "hydra tui passes the keys file through"
(cd "$adapter_repo" && HYDRA_HOME="$adapter_home" HYDRA_TUI_BIN="$tui" "$repo_root/bin/hydra" tui --print-keys extra) > /dev/null 2>&1
assert_equal "2" "$?" "hydra tui --print-keys takes no arguments"

printf '%s\n' 'terminal.close = ctrl+b x' 'terminal.leav = f9' 'terminal.next = ctrl+foo' '' 'app.quit ctrl+q' > "$test_root/keys-bad.conf"
HYDRA_TUI_KEYS="$test_root/keys-bad.conf" "$tui" --print-keys > "$test_root/keys-bad.out" 2> "$test_root/keys-bad.err"
assert_equal "1" "$?" "a keys file with refused lines fails --print-keys"
contains "keys-bad.conf line 2: unknown action 'terminal.leav' (and 2 more); other lines applied ($test_root/keys-bad.conf)" \
    "$test_root/keys-bad.err" "the report names the file, the line and the reason"
keys_lines "$test_root/keys-bad.out" > "$test_root/keys-bad.lines"
contains 'terminal.close = ctrl+b x' "$test_root/keys-bad.lines" "good lines of a bad keys file apply"
contains 'terminal.leave = ctrl+], ctrl+\ tab' "$test_root/keys-bad.lines" "a refused line keeps the default"
contains 'terminal.next = ctrl+\ n' "$test_root/keys-bad.lines" "an unreadable chord keeps the default"
contains 'app.quit = ctrl+\ q' "$test_root/keys-bad.lines" "a line without = keeps the default"
printf '%s\n' 'terminal.next = ctrl+\ x' > "$test_root/keys-conflict.conf"
HYDRA_TUI_KEYS="$test_root/keys-conflict.conf" "$tui" --print-keys > "$test_root/keys-conflict.out" 2> "$test_root/keys-conflict.err"
assert_equal "1" "$?" "conflicting chords fail --print-keys"
contains 'keys-conflict.conf has conflicting keys: terminal.close and terminal.next (line 1) both use Ctrl-\ x; using the default keys' \
    "$test_root/keys-conflict.err" "a conflict names both actions and the chord"
cmp -s "$test_root/keys-default.conf" "$test_root/keys-conflict.out"
assert_success $? "conflicting chords put every key back to its default"
printf '%s\n' "terminal.leave = ctrl+\\" > "$test_root/keys-prefix.conf"
HYDRA_TUI_KEYS="$test_root/keys-prefix.conf" "$tui" --print-keys > /dev/null 2> "$test_root/keys-prefix.err"
assert_equal "1" "$?" "a chord that begins another is refused"
contains 'terminal.leave (line 1) uses Ctrl-\, which begins Ctrl-\ x of terminal.close' "$test_root/keys-prefix.err" \
    "a chord that begins another is explained"
printf '%s\n' 'terminal.leave = none' 'terminal.close = f6' > "$test_root/keys-trap.conf"
HYDRA_TUI_KEYS="$test_root/keys-trap.conf" "$tui" --print-keys > "$test_root/keys-trap.out" 2> "$test_root/keys-trap.err"
assert_equal "1" "$?" "a keys file cannot unbind leaving agent input"
contains 'keys-trap.conf leaves terminal.leave unbound; its default is kept' "$test_root/keys-trap.err" "an unbound leave key is explained"
keys_lines "$test_root/keys-trap.out" > "$test_root/keys-trap.lines"
contains 'terminal.leave = ctrl+], ctrl+\ tab' "$test_root/keys-trap.lines" "leaving agent input keeps its default chords"
contains 'terminal.close = f6' "$test_root/keys-trap.lines" "the rest of that keys file applies"
HYDRA_TUI_KEYS="$test_root/keys-missing.conf" "$tui" --print-keys > "$test_root/keys-missing.out" 2> "$test_root/keys-missing.err"
assert_equal "1" "$?" "a named keys file that does not exist is reported"
contains 'keys-missing.conf does not exist; using the default keys' "$test_root/keys-missing.err" "a missing named keys file is explained"
cmp -s "$test_root/keys-default.conf" "$test_root/keys-missing.out"
assert_success $? "a missing named keys file leaves the defaults"
HYDRA_TUI_KEYS="$keys_home" "$tui" --print-keys > /dev/null 2> "$test_root/keys-directory.err"
assert_equal "1" "$?" "an unreadable keys file is reported"
contains 'cannot be read' "$test_root/keys-directory.err" "an unreadable keys file is explained"
awk 'BEGIN { for (i = 0; i < 3000; i++) print "# padding padding padding padding" }' > "$test_root/keys-large.conf"
HYDRA_TUI_KEYS="$test_root/keys-large.conf" "$tui" --print-keys > /dev/null 2> "$test_root/keys-large.err"
assert_equal "1" "$?" "an oversized keys file is refused"
contains 'is larger than 64 KiB' "$test_root/keys-large.err" "an oversized keys file is explained"
# Deterministic rendering ignores the user's own keys file and survives a bad named one.
HYDRA_TUI_KEYS='' HYDRA_HOME="$keys_home" "$tui" --ascii --headless-fixture "$fixture" --size 80x24 > "$test_root/keys-headless-home.out"
HYDRA_TUI_KEYS='' HYDRA_HOME="$test_root/no-such-home" "$tui" --ascii --headless-fixture "$fixture" --size 80x24 > "$test_root/keys-headless-none.out"
cmp -s "$test_root/keys-headless-home.out" "$test_root/keys-headless-none.out"
assert_success $? "headless rendering does not depend on a keys file in HYDRA_HOME"
HYDRA_TUI_KEYS="$test_root/keys-bad.conf" "$tui" --ascii --headless-fixture "$fixture" --size 80x24 > /dev/null 2>&1
assert_success $? "a bad keys file does not stop rendering"

printf '\nTests: %d, Passed: %d, Failed: %d\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
