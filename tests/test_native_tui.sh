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
assert_equal "Hydra TUI 3.0.0 protocol 2" "$("$tui" --version)" "native TUI version handshake"

awk 'BEGIN { FS = OFS = "\t" } $1 == "H" && !changed { $13 = "invalid"; changed = 1 } { print }' \
    "$fixture" > "$test_root/invalid-number.tsv"
"$tui" --headless-fixture "$test_root/invalid-number.tsv" --size 80x24 \
    > /dev/null 2> "$test_root/invalid-number.err"
assert_failure $? "malformed native numeric fields fail closed"
contains "invalid numeric field" "$test_root/invalid-number.err" "numeric protocol failure is explicit"

"$tui" --headless-fixture "$fixture" --size 80x24 --frames 2 > "$test_root/heads.out"
assert_success $? "headless fixture renders deterministically"
assert_equal "2" "$(grep -c '^FRAME ' "$test_root/heads.out")" "explicit frame bound is honored"
# Work's list marks each state with its own glyph; the one-pane list says it in words.
contains "$(printf '\342\226\262 feature-stale')" "$test_root/heads.out" "stale state has its own glyph"
contains "? feature-unavailable" "$test_root/heads.out" "unavailable state has its own glyph"
"$tui" --headless-fixture "$fixture" --size 60x24 > "$test_root/heads-words.out"
contains "running" "$test_root/heads-words.out" "live state is distinct"
contains "terminal gone" "$test_root/heads-words.out" "stale state is distinct"
contains "unknown" "$test_root/heads-words.out" "unavailable state is distinct"
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
"$tui" --headless-fixture "$fixture" --size 80x40 --view coordination > "$test_root/coordination.out"
contains "Approvals   1 of 2 approved" "$test_root/coordination.out" "coordination focuses on selected head"
contains "Coordinate  1 claims" "$test_root/coordination.out" "coordination counters are one Details row"
contains "flags overlap" "$test_root/coordination.out" "coordination explains what there is to coordinate"
"$tui" --headless-fixture "$fixture" --workflow-fixture "$repo_root/tests/fixtures/tui/workflow-empty.tsv" \
    --size 80x24 --view attention > "$test_root/attention-no-runs.out"
contains "No workflow runs are recorded in this project, so only heads are watched." "$test_root/attention-no-runs.out" \
    "Inbox in a project without workflow runs says so"
contains "Terminal stopped: feature-stale" "$test_root/attention-no-runs.out" "Inbox still lists the heads that need the user"
if grep -qi 'attention unavailable' "$test_root/attention-no-runs.out"; then
    assert_success 1 "Inbox without workflow runs is not reported as unavailable"
else
    assert_success 0 "Inbox without workflow runs is not reported as unavailable"
fi
printf 'HYDRA_TUI\t2\n' > "$test_root/nothing.tsv"
"$tui" --headless-fixture "$test_root/nothing.tsv" --workflow-fixture "$repo_root/tests/fixtures/tui/workflow-empty.tsv" \
    --size 80x24 --view inbox > "$test_root/inbox-empty.out"
contains "Nothing needs you" "$test_root/inbox-empty.out" "an empty Inbox says nothing needs the user"
contains "stopped terminals" "$test_root/inbox-empty.out" "an empty Inbox says what would appear there"
contains "nothing waiting" "$test_root/inbox-empty.out" "the status line says nothing is waiting"
# A plan run that succeeded with its worker head still listed is ready to land (L).
"$tui" --headless-fixture "$repo_root/tests/fixtures/tui/native-runs.tsv" --workflow-fixture "$repo_root/tests/fixtures/tui/workflow-runs.tsv" \
    --attention-fixture "$repo_root/tests/fixtures/tui/inbox-v1.tsv" --size 140x40 --view inbox --keys jj > "$test_root/inbox-land.out"
contains "Ready to land: kill-dry-run-worker" "$test_root/inbox-land.out" "a finished plan run's worker is ready to land"
contains "L lands it: hydra land merges the commit" "$test_root/inbox-land.out" "the land item says what landing does"
contains "L Land" "$test_root/inbox-land.out" "the land item offers L"
contains "L land" "$test_root/inbox-land.out" "the footer names L for the land item"
# Registered hosts without the fleet helper: only the user can fix that, so
# the finding is under Your actions, not Repairs.
{ cat "$fixture"; printf 'R\tfleet-unavailable\t2 registered remote hosts\t/home/u/.hydra/fleet/remotes\texact\tmake build-fleet\n'; } > "$test_root/fleet-unavailable.tsv"
"$tui" --headless-fixture "$test_root/fleet-unavailable.tsv" --attention-fixture "$repo_root/tests/fixtures/tui/inbox-v1.tsv" \
    --size 140x40 --view inbox --keys jjjjjjjjjjj > "$test_root/fleet-unavailable.out"
contains "Your actions 1" "$test_root/fleet-unavailable.out" "the missing fleet helper is one of the user's actions"
contains "Repairs 4" "$test_root/fleet-unavailable.out" "the missing fleet helper is not counted as a repair"
contains "Remote hosts cannot be observed: 2 registered remote hosts" "$test_root/fleet-unavailable.out" "the finding names the registered hosts"
contains "make build-fleet" "$test_root/fleet-unavailable.out" "the finding says what to run"
if grep -q "c Run check" "$test_root/fleet-unavailable.out"; then
    assert_success 1 "the missing fleet helper offers no in-app check"
else
    assert_success 0 "the missing fleet helper offers no in-app check"
fi
# A remote approval is a decision naming its host.
printf 'HYDRA_ATTENTION\t3\nITEM\tfleet overview\tapproval\tapproval_pending\t/work/project\tbuilder\ttask_build\trun_1\timplement\tattempt-1\thead_r\tinstance_r\treq-1\t-\t%s\t%s\tfresh\ttask-observe\t1\tbuild-plan\t-\nEND\t1\t0\t0\n' \
    1111111111111111111111111111111111111111111111111111111111111111 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa > "$test_root/remote-approval.tsv"
"$tui" --headless-fixture "$fixture" --remote-fixture "$repo_root/tests/fixtures/tui/remote-v3.tsv" --attention-fixture "$test_root/remote-approval.tsv" \
    --size 140x40 --view inbox > "$test_root/remote-approval.out"
contains "Approve implement: build-plan" "$test_root/remote-approval.out" "a remote approval is listed as a decision"
contains "on builder" "$test_root/remote-approval.out" "the remote decision names its host"
contains "Host        builder" "$test_root/remote-approval.out" "the remote decision's Summary names its host"
contains "rejects it on its host" "$test_root/remote-approval.out" "the remote decision says where it is decided"
# The recovery findings are Inbox's Repairs; --view recovery opens Inbox.
"$tui" --headless-fixture "$fixture" --size 54x30 --view recovery > "$test_root/recovery.out"
contains " 2 Inbox " "$test_root/recovery.out" "the recovery view name opens Inbox"
contains "Repairs 3" "$test_root/recovery.out" "recovery findings are listed under Repairs"
contains "Terminal stopped: feature-stale" "$test_root/recovery.out" "dead sessions are explained as stopped terminals"
contains "Agents 2" "$test_root/recovery.out" "a stopped terminal is one Agents item, not also a repair"
contains "Leftover lock" "$test_root/recovery.out" "stale locks are explained in plain language"
contains "Leftover worktree from a" "$test_root/recovery.out" "orphan worktrees are explained in plain language"
contains "Removal did not finish" "$test_root/recovery.out" "teardown failures are explained in plain language"
if grep -Eq 'dead-session|stale-lock|orphan-worktree|teardown-failure' "$test_root/recovery.out"; then
    assert_success 1 "Inbox keeps raw finding kinds out of the primary text"
else
    assert_success 0 "Inbox keeps raw finding kinds out of the primary text"
fi
# A finding's Technical page keeps the raw kind and the inspection command.
{ grep -v '^R	' "$fixture"; grep '^R	stale-lock	' "$fixture"; grep '^R	orphan-worktree	' "$fixture"; } > "$test_root/lock-first.tsv"
"$tui" --headless-fixture "$test_root/lock-first.tsv" --size 140x28 --view inbox > "$test_root/recovery-detail.out"
contains "Approval gates: feature-live" "$test_root/recovery-detail.out" "a head's pending approval gates are a decision"
contains "1 of 2 approved" "$test_root/recovery-detail.out" "the gate item says how many are approved"
awk 'BEGIN {FS = OFS = "\t"} $1 == "H" {$21 = 0; $22 = 0} $1 != "H" || $2 == "feature-stale" {print}' "$fixture" > "$test_root/stopped-only.tsv"
"$tui" --headless-fixture "$test_root/stopped-only.tsv" --size 140x28 --view inbox --diagnostics > "$test_root/stopped-technical.out"
contains "Summary  Review  References  Technical" "$test_root/stopped-technical.out" "diagnostics open the Technical page"
contains "Kind        dead-session" "$test_root/stopped-technical.out" "the Technical page keeps the raw finding kind"
contains "Inspect     hydra doctor" "$test_root/stopped-technical.out" "the Technical page keeps the inspection command"
"$tui" --headless-fixture "$test_root/stopped-only.tsv" --size 140x28 --view inbox > "$test_root/stopped.out"
contains "and files are kept; nothing was removed" "$test_root/stopped.out" "a stopped terminal's Summary says what is kept"
contains "R Restart terminal" "$test_root/stopped.out" "a stopped terminal offers R to restart it"
sed 's/	dead	stopped	/	running	live	/' "$test_root/stopped-only.tsv" > "$test_root/repairs-only.tsv"
"$tui" --headless-fixture "$test_root/repairs-only.tsv" --size 140x28 --view inbox > "$test_root/repairs.out"
contains "Terminal stopped: feature-stale" "$test_root/repairs.out" "a dead-session finding of a live head stays a repair"
{ grep -v '^R	' "$test_root/repairs-only.tsv"; grep '^R	orphan-worktree	' "$fixture"; } > "$test_root/orphan-only.tsv"
"$tui" --headless-fixture "$test_root/orphan-only.tsv" --size 140x28 --view inbox > "$test_root/orphan-detail.out"
contains "Leftover worktree from a removed task: feature-old" "$test_root/orphan-detail.out" "leftover worktree finding names its branch"
contains "(/tmp/wt/head_0123456789abcdef), 1.3 GiB; no uncommitted changes" "$test_root/orphan-detail.out" \
    "leftover worktree finding names path, size and change state"
contains "Check       hydra gc --policy orphaned --dry-run" "$test_root/orphan-detail.out" "leftover worktree check is the read-only gc review"
contains "x removes only this" "$test_root/orphan-detail.out" "leftover worktree offers confirmed removal"
contains "x remove" "$test_root/orphan-detail.out" "Inbox footer offers removal for a clean leftover worktree"
contains "c run check" "$test_root/orphan-detail.out" "Inbox footer offers the finding's check"
sed 's/^R	orphan-worktree	/R	orphan-worktree-dirty	/' "$test_root/orphan-only.tsv" > "$test_root/orphan-dirty.tsv"
"$tui" --headless-fixture "$test_root/orphan-dirty.tsv" --size 140x28 --view inbox > "$test_root/orphan-dirty.out"
contains "has uncommitted changes" "$test_root/orphan-dirty.out" "dirty leftover worktree says it has uncommitted changes"
if grep -q "x remove" "$test_root/orphan-dirty.out"; then
    assert_success 1 "dirty leftover worktree is never offered for in-app removal"
else
    assert_success 0 "dirty leftover worktree is never offered for in-app removal"
fi
# Attention items: sections in order (decisions first, repairs last), the
# selected item's Summary beside the list, and the badge on the tab.
"$tui" --headless-fixture "$fixture" --workflow-fixture "$repo_root/tests/fixtures/tui/workflow-v1.tsv" \
    --attention-fixture "$repo_root/tests/fixtures/tui/inbox-v1.tsv" --size 140x40 --view inbox > "$test_root/inbox.out"
awk '/^ Decisions /{d=NR} /^ Failed /{f=NR} /^ Ready for review /{r=NR} /^ Agents /{a=NR} /^ Repairs /{p=NR}
END { exit !(d && d < f && f < r && r < a && a < p) }' "$test_root/inbox.out"
assert_success $? "Inbox sections run decisions, failures, results, agents, then repairs"
contains "Approve publish: docs-sweep" "$test_root/inbox.out" "an approval names its step and plan"
contains "Check failed: cache-plan" "$test_root/inbox.out" "a failed check names its plan"
contains "requirement R2" "$test_root/inbox.out" "a failed check names the requirement it decides"
contains "Result ready: docs sweep" "$test_root/inbox.out" "a sealed result is ready for review"
contains "Needs inspection: T-4" "$test_root/inbox.out" "an item Hydra cannot classify is a repair to inspect"
contains "Request     req_7f3a2c" "$test_root/inbox.out" "the Summary names the exact request"
contains "Y Approve    N Reject    r Review    s Seen" "$test_root/inbox.out" "the Summary lists the keys that act on the item"
contains "Summary  Review  References  Technical" "$test_root/inbox.out" "the detail names its pages"
contains " 2 Inbox 11" "$test_root/inbox.out" "the Inbox tab carries the count of waiting items"
contains "11 items need you" "$test_root/inbox.out" "the status line counts the waiting items"
"$tui" --headless-fixture "$fixture" --size 100x30 --view inbox > "$test_root/inbox-loading.out"
contains "Reading what needs you" "$test_root/inbox-loading.out" "Inbox says it is still reading attention"
contains "$(printf ' 2 Inbox \302\267')" "$test_root/inbox-loading.out" "the badge is a dot until the first read"
for view in heads detail coordination inbox; do
    "$tui" --headless-fixture "$fixture" --size 40x10 --view "$view" > "$test_root/bounded.out"
    assert_equal "11" "$(wc -l < "$test_root/bounded.out" | tr -d ' ')" "$view fits ten rows plus frame marker"
    "$tui" --ascii --headless-fixture "$fixture" --size 40x10 --view "$view" > "$test_root/bounded-ascii.out"
    awk 'length > 39 {exit 1}' "$test_root/bounded-ascii.out"
    assert_success $? "$view respects terminal width"
    tail -n 1 "$test_root/bounded.out" > "$test_root/footer.out"
    contains "? help" "$test_root/footer.out" "$view keeps actions visible"
done

# The help dialog's keys-file sentence wraps at 80 columns rather than being cut.
"$tui" --headless-fixture "$fixture" --size 80x24 --view heads --keys '?' > "$test_root/help-narrow.out"
contains 'your own.' "$test_root/help-narrow.out" "the help dialog's keys-file sentence is whole at 80 columns"

# Column separators and panel edges must be invariant across varying values.
awk 'BEGIN {FS = OFS = "\t"}
$1 == "H" && !changed++ {for (i = 0; i < 120; i++) { $2 = $2 "x"; $4 = $4 "y" }}
{print}' "$fixture" > "$test_root/columns.tsv"
"$tui" --ascii --headless-fixture "$test_root/columns.tsv" --size 140x24 > "$test_root/columns.out"
# A long name ends in an ellipsis inside its column; the agent column starts
# at the same place on every row (read inside the list's 34 columns).
awk '
/^ [ >][*!?~x+-] feature-/ { row = substr($0, 1, 34); agent = index(row, "codex") + index(row, "claude") + index(row, "shell")
    if (!agent || (first && agent != first)) exit 1
    first = agent; rows++ }
END { if (rows != 3) exit 1 }
' "$test_root/columns.out"
assert_success $? "list rows share a fixed agent column"
contains "Heads ---" "$test_root/columns.out" "head list is titled by a named rule (ASCII)"
"$tui" --headless-fixture "$fixture" --size 100x24 > "$test_root/unicode.out"
contains "$(printf 'Heads \342\224\200')" "$test_root/unicode.out" "default output titles panes with a Unicode rule"
if grep -q "$(printf '\342\225\255')" "$test_root/unicode.out"; then
    assert_success 1 "panes are not boxed"
else
    assert_success 0 "panes are not boxed"
fi
"$tui" --headless-fixture "$fixture" --size 80x24 --view heads > "$test_root/tabs.out"
# The top bar: the brand, the four screens as numbered tabs (the active one
# a pill) and the project summary at the right.
contains " 1 Work  2 Inbox " "$test_root/tabs.out" "the four screens are numbered tabs and the active one is a pill"
contains "3 Hosts  4 Stats" "$test_root/tabs.out" "every screen's tab is visible at 80 columns"
contains "1 running" "$test_root/tabs.out" "the summary keeps its running count at 80 columns"
"$tui" --headless-fixture "$fixture" --size 80x24 --view hosts > "$test_root/hosts-tabs.out"
contains " 3 Hosts " "$test_root/hosts-tabs.out" "the Hosts tab stays in view at 80 columns"
"$tui" --headless-fixture "$fixture" --size 80x24 --view coordination > "$test_root/coordination-tabs.out"
contains " 1 Work " "$test_root/coordination-tabs.out" "the coordination view name opens Work"
contains "Conversation  Details" "$test_root/coordination-tabs.out" "the coordination view name opens Work on its Details page"
"$tui" --fleet --headless-fixture "$fixture" --size 80x24 --view hosts > "$test_root/fleet-tabs.out"
contains "1 Work  2 Inbox" "$test_root/fleet-tabs.out" "the fleet entry keeps the four screens"
contains " 3 Hosts  4 Stats" "$test_root/fleet-tabs.out" "the fleet entry keeps Hosts in view at 80 columns"
"$tui" --headless-fixture "$fixture" --size 100x24 --view hosts > "$test_root/wide-tabs.out"
contains " 3 Hosts " "$test_root/wide-tabs.out" "the active tab is a pill at 100 columns"
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v2.tsv" --size 80x24 --view statistics > "$test_root/stats-tabs.out"
contains " 4 Stats " "$test_root/stats-tabs.out" "statistics keeps the shared tab bar"
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v2.tsv" --size 80x24 --view stats > "$test_root/stats-name.out"
cmp -s "$test_root/stats-tabs.out" "$test_root/stats-name.out"
assert_success $? "the Stats screen opens by its name and by the earlier one"
# Schema 4 usage rows: token totals, agent evidence and resource metrics said in full.
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v4.tsv" --size 140x40 --view statistics > "$test_root/stats-usage.out"
contains "Agent tokens" "$test_root/stats-usage.out" "statistics notes name agent tokens"
contains "Agent tokens in 3.85M" "$test_root/stats-usage.out" "input tokens are summed across the scope"
contains "cached 3.74M" "$test_root/stats-usage.out" "cached input tokens are summed across the scope"
contains "cost not reported" "$test_root/stats-usage.out" "unreported cost stays unknown instead of zero"
contains "CPU and memory: not measured." "$test_root/stats-usage.out" "unmeasured resources are explained, not truncated"
contains "3.85M in / 23.0k out" "$test_root/stats-usage.out" "the run table shows per-run tokens"
! grep -Fq "Tokens / cost: not m" "$test_root/stats-usage.out"
assert_success $? "the old truncated resource note is gone"
! grep -Fq "Sessions  Runs" "$test_root/stats-usage.out"
assert_success $? "a sample older than head sessions has no sessions page"
# Head sessions (schema 6): a sample without runs opens on them; inferred
# times are labelled, unobserved time is its own number, unknown is "--".
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v6-sessions.tsv" --size 140x40 --view statistics > "$test_root/stats-sessions.out"
contains "Head sessions ───" "$test_root/stats-sessions.out" "statistics opens on head sessions when there are no runs"
contains "Observed 38% of 6h00m session time; 10m00s reported by adapters, the rest inferred from terminal output" "$test_root/stats-sessions.out" "session coverage and its sources are stated"
contains "inferred, 53% of observed" "$test_root/stats-sessions.out" "the quiet share is labelled as inferred"
contains "Unobserved" "$test_root/stats-sessions.out" "unobserved time has its own tile"
contains "3h42m" "$test_root/stats-sessions.out" "unobserved time is shown apart from quiet"
contains "docs-refresh         opencode  ↻ replaced  09-07 08:00  2h00m   --      --" "$test_root/stats-sessions.out" "an unobserved session shows unknown activity"
contains "old-experiment       -         ✓ ended     --           --      --" "$test_root/stats-sessions.out" "an undated session shows unknown length"
contains "Sessions started, last 7 days" "$test_root/stats-sessions.out" "sessions per day are charted"
contains "By agent" "$test_root/stats-sessions.out" "sessions are counted by agent"
contains " Sessions  Runs ─" "$test_root/stats-sessions.out" "the sessions page names the runs page in its strip"
contains "▌ fix-login-timeout" "$test_root/stats-sessions.out" "the selected session carries the bar"
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v6.tsv" --size 140x40 --view statistics > "$test_root/stats-mixed.out"
contains "Workflow runs ───" "$test_root/stats-mixed.out" "a sample with runs opens on runs"
contains "[/] sessions/runs" "$test_root/stats-mixed.out" "the runs page offers head sessions"
contains "w workflow" "$test_root/stats-mixed.out" "the runs page offers the workflow filter"
# Nothing recorded is told apart from filtered out (the PTY suite types a
# filter); an empty sample draws no tile and no chart.
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v6-empty.tsv" --size 140x40 --view statistics > "$test_root/stats-empty.out"
contains "No sessions recorded yet" "$test_root/stats-empty.out" "an empty sample says nothing was recorded"
! grep -Fq "Nothing matches the filters" "$test_root/stats-empty.out"
assert_success $? "an empty sample is not called filtered out"
! grep -Fq "last 7 days" "$test_root/stats-empty.out"
assert_success $? "an empty sample draws no chart"
! grep -Fq "Unobserved" "$test_root/stats-empty.out"
assert_success $? "an empty sample draws no tiles"
# Golden frames of every screen at 140x40 and 80x24 (Work also at 100x30
# and 60x20, where its panes share one), in Unicode and ASCII: the frame (top
# bar, titled rules, status chip, generated footer) is pinned. The earlier
# view names detail and overview open Work on its Details and Run pages.
# Regenerate with the same command when a change to a screen is intended.
for golden_case in work:140x40 work:100x30 work:80x24 work:60x20 detail:140x40 detail:80x24 overview:140x40 overview:80x24 \
    hosts:140x40 hosts:80x24 inbox:140x40 inbox:80x24 stats:140x40 stats:80x24; do
    golden_view="${golden_case%%:*}"; golden_size="${golden_case#*:}"
    for golden_mode in plain ascii; do
        golden_flag=
        [ "$golden_mode" = ascii ] && golden_flag=--ascii
        "$tui" $golden_flag --headless-fixture "$fixture" --workflow-fixture "$repo_root/tests/fixtures/tui/workflow-v1.tsv" \
            --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v6.tsv" \
            --attention-fixture "$repo_root/tests/fixtures/tui/inbox-v1.tsv" --size "$golden_size" --view "$golden_view" \
            > "$test_root/golden.out"
        cmp -s "$test_root/golden.out" "$repo_root/tests/fixtures/tui/screens/$golden_view-$golden_size-$golden_mode.txt"
        assert_success $? "golden frame $golden_view at $golden_size ($golden_mode)"
    done
done
# Golden Stats frames: head sessions at every layout (tiles and charts,
# tiles, summary line, list only), an empty sample, and the fleet's hosts.
for golden_case in sessions:v6-sessions:140x40 sessions:v6-sessions:100x30 sessions:v6-sessions:80x24 \
    sessions:v6-sessions:60x20 empty:v6-empty:80x24; do
    golden_name="${golden_case%%:*}"; golden_rest="${golden_case#*:}"
    for golden_mode in plain ascii; do
        golden_flag=
        [ "$golden_mode" = ascii ] && golden_flag=--ascii
        "$tui" $golden_flag --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-${golden_rest%%:*}.tsv" \
            --size "${golden_rest##*:}" --view statistics > "$test_root/golden.out"
        cmp -s "$test_root/golden.out" "$repo_root/tests/fixtures/tui/screens/statistics-$golden_name-${golden_rest##*:}-$golden_mode.txt"
        assert_success $? "golden Stats frame $golden_name at ${golden_rest##*:} ($golden_mode)"
    done
done
# Golden Stats frames of a sample with sessions, runs and remote steps: the
# three pages ([ from Runs is Sessions, C is Cohorts) and the location chip.
for golden_case in remote-runs:140x40: remote-runs:80x24: remote-sessions:140x40:[ remote-cohorts:140x40:C \
    remote-cohorts:80x24:C remote-location:140x40:CLL remote-location:80x24:CLL; do
    golden_name="${golden_case%%:*}"; golden_rest="${golden_case#*:}"; golden_size="${golden_rest%%:*}"
    for golden_mode in plain ascii; do
        golden_flag=
        [ "$golden_mode" = ascii ] && golden_flag=--ascii
        "$tui" $golden_flag --keys "${golden_rest#*:}" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v6-remote.tsv" \
            --size "$golden_size" --view statistics > "$test_root/golden.out"
        cmp -s "$test_root/golden.out" "$repo_root/tests/fixtures/tui/screens/statistics-$golden_name-$golden_size-$golden_mode.txt"
        assert_success $? "golden Stats frame $golden_name at $golden_size ($golden_mode)"
    done
done
# Golden Hosts frames: This machine beside the observed hosts at every layout
# (side by side from 90 columns, stacked from 70, the list alone below), a
# disconnected host selected (j moves down the list), the add-host form as a
# modal (A), the Onboarding page and the fleet entry's hosts.
for golden_case in remote:140x40: remote:100x30: remote:80x24: remote:60x20: remote-offline:140x40:jjj \
    remote-offline:80x24:jjj add-host:140x40:A add-host:80x24:A; do
    golden_name="${golden_case%%:*}"; golden_rest="${golden_case#*:}"; golden_size="${golden_rest%%:*}"
    for golden_mode in plain ascii; do
        golden_flag=
        [ "$golden_mode" = ascii ] && golden_flag=--ascii
        "$tui" $golden_flag --keys "${golden_rest#*:}" --headless-fixture "$fixture" --remote-fixture "$repo_root/tests/fixtures/tui/remote-v3.tsv" \
            --size "$golden_size" --view hosts > "$test_root/golden.out"
        cmp -s "$test_root/golden.out" "$repo_root/tests/fixtures/tui/screens/hosts-$golden_name-$golden_size-$golden_mode.txt"
        assert_success $? "golden Hosts frame $golden_name at $golden_size ($golden_mode)"
    done
done
for golden_size in 140x40 80x24; do
    "$tui" --headless-fixture "$fixture" --onboarding-fixture "$repo_root/tests/fixtures/tui/onboarding/qualify-two.json" \
        --size "$golden_size" --view hosts > "$test_root/golden.out"
    cmp -s "$test_root/golden.out" "$repo_root/tests/fixtures/tui/screens/hosts-onboarding-$golden_size-plain.txt"
    assert_success $? "golden Hosts frame of the Onboarding page at $golden_size"
done
"$tui" --fleet --headless-fixture "$repo_root/tests/fixtures/tui/fleet-v2.tsv" --size 140x40 --view hosts > "$test_root/golden.out"
cmp -s "$test_root/golden.out" "$repo_root/tests/fixtures/tui/screens/hosts-fleet-140x40-plain.txt"
assert_success $? "golden Hosts frame of the fleet entry"
"$tui" --fleet --headless-fixture "$repo_root/tests/fixtures/tui/fleet-v2.tsv" --size 140x40 --view statistics > "$test_root/golden.out"
cmp -s "$test_root/golden.out" "$repo_root/tests/fixtures/tui/screens/statistics-fleet-140x40-plain.txt"
assert_success $? "golden Stats frame of the fleet's hosts"
# Samples without sessions (schemas 2 and 4) keep the runs page alone.
for stats_case in v2:80x24 v2:140x40 v4:80x24 v4:140x40; do
    "$tui" --ascii --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-${stats_case%%:*}.tsv" --size "${stats_case##*:}" --view statistics > "$test_root/stats-same.out"
    cmp -s "$test_root/stats-same.out" "$repo_root/tests/fixtures/tui/statistics-${stats_case%%:*}-${stats_case##*:}.txt"
    assert_success $? "statistics ${stats_case%%:*} at ${stats_case##*:} matches its golden frame"
done
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v4.tsv" --size 80x24 --view statistics > "$test_root/stats-usage-narrow.out"
contains "tokens in 3.85M" "$test_root/stats-usage-narrow.out" "narrow statistics keep the token total on the summary line"
# Schema 5 remote rows: the runs page reports what remote task receipts
# measured and keeps local steps unmeasured; the strip offers the Cohorts
# page, and no location chip is shown while every location is.
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v5.tsv" --size 140x40 --view statistics > "$test_root/stats-remote.out"
contains "Remote CPU 5.5s" "$test_root/stats-remote.out" "remote receipts' CPU is reported with its coverage"
contains " Runs  Cohorts " "$test_root/stats-remote.out" "the strip offers the cohorts page"
contains "C cohorts" "$test_root/stats-remote.out" "the cohorts page is announced"
! grep -Fq "location: " "$test_root/stats-remote.out"
assert_success $? "no location chip while every location is shown"
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v6.tsv" --size 140x40 --view statistics > "$test_root/stats-local.out"
! grep -Fq "Cohorts" "$test_root/stats-local.out"
assert_success $? "a sample without remote steps has no cohorts page"
"$tui" --headless-fixture "$fixture" --size 80x24 --view workspace > "$test_root/workspace-tabs.out"
contains " 1 Work " "$test_root/workspace-tabs.out" "the workspace view name opens Work"
"$tui" --headless-fixture "$fixture" --size 80x24 > "$test_root/work-default.out"
cmp -s "$test_root/workspace-tabs.out" "$test_root/work-default.out"
assert_success $? "the workspace view is Work: no layout is named any more"
if grep -q "Tab to focus / scroll" "$test_root/workspace-tabs.out"; then
    assert_success 1 "workspace panes do not repeat focus hints"
else
    assert_success 0 "workspace panes do not repeat focus hints"
fi
printf 'HYDRA_TUI\t2\n' > "$test_root/empty.tsv"
"$tui" --headless-fixture "$test_root/empty.tsv" --size 80x24 --view heads > "$test_root/empty-heads.out"
contains "No agent work in this project yet" "$test_root/empty-heads.out" "empty head list explains the situation"
contains "n start a task" "$test_root/empty-heads.out" "empty head list offers a next action"
contains "A head is one task: its own branch, worktree, terminal and agent." "$test_root/empty-heads.out" "empty Work says what a head is"
contains "3 add a remote host" "$test_root/empty-heads.out" "empty Work offers adding a host"
"$tui" --headless-fixture "$test_root/empty.tsv" --size 80x24 --view overview > "$test_root/empty-overview.out"
contains "No agent work in this project yet" "$test_root/empty-overview.out" "empty overview explains instead of showing empty charts"
"$tui" --headless-fixture "$test_root/empty.tsv" --size 140x40 --view workflows > "$test_root/empty-workflows.out"
contains "No agent work in this project yet" "$test_root/empty-workflows.out" "the workflows view name opens the same empty Work"

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
printf '#!/bin/sh\n[ "${1:-}" = --version ] && { echo "Hydra TUI 3.0.0 protocol 2"; exit 0; }\nexit 4\n' > "$test_root/native-transient"
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
printf '#!/bin/sh\n[ "${1:-}" = --version ] && { echo "Hydra TUI 3.0.0 protocol 2"; exit 0; }\nprintf "NATIVE DEFAULT\\n"\n' > "$test_root/native-success"
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
"$tui" --fleet --headless-fixture "$repo_root/tests/fixtures/tui/fleet-v1.tsv" --size 100x24 --view detail > "$test_root/fleet-detail.txt"
grep -Eq '^ .. ovh/feature +ovh ' "$test_root/fleet.txt"
assert_success $? 'the one Work list names where each head runs'
contains 'ovh/feature' "$test_root/fleet.txt" 'a remote head is named with its host and branch'
contains 'desired running' "$test_root/fleet-detail.txt" 'a remote head reports its durable desired state explicitly'
contains 'unknown; no host observation' "$test_root/fleet-detail.txt" 'a version-1 stream without host rows is an unknown observation, not a live one'
contains 'x interrupt' "$test_root/fleet.txt" 'a remote head offers interrupt in place of removal'
contains 'Showing remote work' "$test_root/fleet.txt" 'hydra fleet tui opens filtered to remote work'

printf 'HYDRA_FLEET_TUI\t3\nT\tbuild\tresponded\t0\t-\tunreachable\tstale\t123\t4\nO\tbuild\ttask_abc\trun_xyz\tstep\tattempt-1\t/work\tnone\trecorded\tcancelled\tnone\t-\tinspect dependency\t123\t123\tfresh\t1\tready\tintegrity_verified\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\tconfirmed_stopped\tmanaged_commands\t123\treq-1\n' > "$test_root/fleet-v3.tsv"
"$tui" --fleet --headless-fixture "$test_root/fleet-v3.tsv" --view hosts --ascii --size 140x30 > "$test_root/fleet-v3-hosts.txt"
contains 'build' "$test_root/fleet-v3-hosts.txt" 'fleet v3 preserves host identity'
contains 'stale' "$test_root/fleet-v3-hosts.txt" 'fleet v3 renders stale freshness'
contains 'task_abc' "$test_root/fleet-v3-hosts.txt" 'fleet v3 renders task identity'
contains 'dependency' "$test_root/fleet-v3-hosts.txt" 'fleet v3 renders waiting reason'
"$tui" --fleet --headless-fixture "$test_root/fleet-v3.tsv" --view overview --ascii --size 140x30 > "$test_root/fleet-v3-overview.txt"
contains '>- task_abc' "$test_root/fleet-v3-overview.txt" 'a remote task is a row of the one Work list'
contains 'inspect dependency' "$test_root/fleet-v3-overview.txt" 'the selected task shows the next action'
contains 'confirmed_stopped' "$test_root/fleet-v3-overview.txt" 'the selected task shows the receiver cancellation acknowledgment'
contains 'task task_abc on build' "$test_root/fleet-v3-overview.txt" 'a selected remote task gets the Details page'
contains 'Request      req-1' "$test_root/fleet-v3-overview.txt" 'the Details page carries the binding the controls act on'

# One workspace (U11): a local and a remote observation compose one list.
remote_fixture="$repo_root/tests/fixtures/tui/remote-v3.tsv"
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 140x40 --view heads > "$test_root/unified.txt"
contains 'feature-live' "$test_root/unified.txt" 'local heads stay in the unified list'
contains 'builder/feature' "$test_root/unified.txt" 'a remote head is listed beside local heads'
contains 'task_build' "$test_root/unified.txt" 'a remote task is listed beside local heads'
contains '~ task_build' "$test_root/unified.txt" 'a waiting task is marked as waiting on the user'
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 60x40 --view heads > "$test_root/unified-words.txt"
contains 'needs approval' "$test_root/unified-words.txt" 'a waiting task says what it needs'
contains '! archive/feature...' "$test_root/unified.txt" 'a stale host keeps its head listed, marked'
contains '3 remote / 1 of 3 hosts disconnected' "$test_root/unified.txt" 'the header counts remote work and disconnected hosts'
contains '3 local' "$test_root/unified.txt" 'the header keeps the local counts'
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 80x24 --view heads > "$test_root/unified-narrow.txt"
contains 'builder/feature-b' "$test_root/unified-narrow.txt" 'the unified list fits 80 columns'
contains '? help' "$test_root/unified-narrow.txt" 'the unified list keeps its hints at 80 columns'
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 100x24 --view heads --location builder > "$test_root/location-host.txt"
contains 'Heads / host builder' "$test_root/location-host.txt" 'a host filter names the host on the list rule'
contains 'builder/feature-build' "$test_root/location-host.txt" 'a host filter keeps that host''s head'
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 100x24 --view detail --location builder > "$test_root/location-host-detail.txt"
contains 'fresh, 3s old' "$test_root/location-host-detail.txt" 'a fresh remote head shows the observation age'
if grep -Fq 'feature-live' "$test_root/location-host.txt"; then assert_success 1 'a host filter hides local heads'; else assert_success 0 'a host filter hides local heads'; fi
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 100x24 --view heads --location local > "$test_root/location-local.txt"
contains 'Heads / local work' "$test_root/location-local.txt" 'the local filter is named'
if grep -Fq 'builder/' "$test_root/location-local.txt"; then assert_success 1 'the local filter hides remote heads'; else assert_success 0 'the local filter hides remote heads'; fi
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 110x28 --view detail --task archive/feature-index > "$test_root/stale-detail.txt"
contains 'stale, 300s old' "$test_root/stale-detail.txt" 'a stale remote head shows its observation age'
contains 'not its current state' "$test_root/stale-detail.txt" 'a stale observation is never presented as current'
contains 'nothing here completes or stops it' "$test_root/stale-detail.txt" 'a stale observation does not complete or stop work'
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 140x40 --view hosts --keys jjj > "$test_root/unified-hosts.txt"
contains 'offline' "$test_root/unified-hosts.txt" 'a host that did not answer is listed'
contains 'disconnected' "$test_root/unified-hosts.txt" 'a host that did not answer is disconnected'
contains 'never observed' "$test_root/unified-hosts.txt" 'a never-observed host says so instead of claiming an age'
contains 'r retries' "$test_root/unified-hosts.txt" 'hosts offer an explicit retry'
contains 'r observe again' "$test_root/unified-hosts.txt" 'the footer offers observing the hosts again'
contains 'Observed 3' "$test_root/unified-hosts.txt" 'observed hosts are listed in their own section'
contains '* This machine' "$test_root/unified-hosts.txt" 'This machine leads the list'
# Enter on an observed host opens Work filtered to that host; Esc comes back
# to Hosts with the host still selected and Work's own filter as it was.
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 100x24 --view hosts --keys "j$(printf '\r')" > "$test_root/hosts-follow.txt"
contains 'Heads / host builder' "$test_root/hosts-follow.txt" 'Enter on an observed host lands in Work filtered to it'
if grep -Fq 'feature-live' "$test_root/hosts-follow.txt"; then assert_success 1 'the followed host shows only its work'; else assert_success 0 'the followed host shows only its work'; fi
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 100x24 --view hosts --keys "j$(printf '\r\033')" > "$test_root/hosts-back.txt"
contains 'builder --------' "$test_root/hosts-back.txt" 'Esc in Work returns to Hosts on the host it followed'
contains 'Observed 3' "$test_root/hosts-back.txt" 'Esc returns to the Hosts list'
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 100x24 --view hosts --keys "j$(printf '\r\033')1" > "$test_root/hosts-back-work.txt"
if grep -Fq 'host builder' "$test_root/hosts-back-work.txt"; then assert_success 1 'returning to Hosts restores the location filter Work had'; else assert_success 0 'returning to Hosts restores the location filter Work had'; fi
# The screen letters of earlier releases still open their screens for one
# release, as a jump (Esc comes back), with a notice naming the key now.
for alias_case in "I:2:Inbox:Needs you" "H:3:Hosts:Observed 3" "D:4:Stats:Sessions  Runs ---" "W:1:Work:Heads"; do
    alias_key="${alias_case%%:*}"; alias_rest="${alias_case#*:}"; alias_now="${alias_rest%%:*}"; alias_rest="${alias_rest#*:}"
    alias_name="${alias_rest%%:*}"; alias_marker="${alias_rest#*:}"
    alias_from=inbox
    [ "$alias_key" = W ] || alias_from=work
    "$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v6.tsv" \
        --ascii --size 140x40 --view "$alias_from" --keys "$alias_key" > "$test_root/alias.txt"
    contains "$alias_name is $alias_now now; $alias_key is its old key" "$test_root/alias.txt" "the old key $alias_key names $alias_now, the key of $alias_name now"
    contains "$alias_marker" "$test_root/alias.txt" "the old key $alias_key opens $alias_name"
done
"$tui" --headless-fixture "$fixture" --ascii --size 140x40 --view work --keys "I$(printf '\033')" > "$test_root/alias-back.txt"
contains 'Heads ---' "$test_root/alias-back.txt" 'Esc after an old screen key returns to the screen it left'
"$tui" --headless-fixture "$fixture" --statistics-fixture "$repo_root/tests/fixtures/tui/statistics-v6.tsv" --ascii --size 140x40 --view work --keys DD > "$test_root/alias-toggle.txt"
contains 'Heads ---' "$test_root/alias-toggle.txt" 'D on Stats still goes back, as the toggle it was'
# A notice about the selection's pages goes when the selection moves.
"$tui" --headless-fixture "$fixture" --workflow-fixture "$repo_root/tests/fixtures/tui/workflow-v1.tsv" --ascii --size 140x40 --view work \
    --keys "$(printf '\034C')" > "$test_root/page-notice.txt"
contains 'No Run page for this selection' "$test_root/page-notice.txt" 'ctrl+\ C on a head without a run says there is no Run page'
"$tui" --headless-fixture "$fixture" --workflow-fixture "$repo_root/tests/fixtures/tui/workflow-v1.tsv" --ascii --size 140x40 --view work \
    --keys "$(printf '\034C')j" > "$test_root/page-notice-moved.txt"
if grep -Fq 'No Run page' "$test_root/page-notice-moved.txt"; then assert_success 1 'the page notice goes when the selection moves'; else assert_success 0 'the page notice goes when the selection moves'; fi
"$tui" --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --ascii --size 140x40 --view overview > "$test_root/unified-overview.txt"
contains '3 local / 3 remote' "$test_root/unified-overview.txt" 'the top bar counts remote work apart'
contains '1 of 3 hosts disconnected' "$test_root/unified-overview.txt" 'the top bar names disconnected hosts'

# Runs group the heads they create; headless heads describe their steps.
runs_heads="$repo_root/tests/fixtures/tui/native-runs.tsv"
runs_done="$repo_root/tests/fixtures/tui/workflow-runs.tsv"
runs_live="$repo_root/tests/fixtures/tui/workflow-runs-running.tsv"
not_contains() {
    if grep -Fq "$1" "$2"; then assert_success 1 "$3"; else assert_success 0 "$3"; fi
}
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --size 60x30 --view heads > "$test_root/runs-heads.out"
# The summary follows the view title at the right of the top bar where the tabs leave room.
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --size 240x30 --view heads > "$test_root/runs-heads-wide.out"
contains "2 heads / +1 in runs / 1 running" "$test_root/runs-heads-wide.out" "header counts the heads the user started and counts run heads within their run"
contains "> + plan run kill-dry-run" "$test_root/runs-heads.out" "the run is listed under its planning head with an expand affordance"
not_contains "kill-dry-run-worker" "$test_root/runs-heads.out" "run heads start collapsed under their run"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --task kill-dry-run-worker --size 60x30 --view heads > "$test_root/runs-expanded.out"
contains "  v + plan run kill-dry-run" "$test_root/runs-expanded.out" "a selected run head opens its run"
contains "+ worker kill-dry-run-worker" "$test_root/runs-expanded.out" "the worker is listed inside its run with its role"
contains "verifier kill-dry-run-check" "$test_root/runs-expanded.out" "a retired verifier stays visible inside its run"
contains "retired" "$test_root/runs-expanded.out" "the verifier shows that it was retired"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --task kill-dry-run-worker --size 100x40 --view detail > "$test_root/runs-worker.out"
contains "Terminal    headless (no terminal)" "$test_root/runs-worker.out" "a headless head says it has no terminal"
contains "Agent       codex / step implement / succeeded" "$test_root/runs-worker.out" "a headless head names the agent of its step"
contains "Model       configured default: gpt-6-sol (not observed)" "$test_root/runs-worker.out" "a configured model is labelled as configuration"
contains "Effort      configured default: xhigh (not observed)" "$test_root/runs-worker.out" "a configured effort is labelled as configuration"
contains "Executable  codex-cli 0.46.0" "$test_root/runs-worker.out" "the executable version is shown"
contains "Tokens      in 24,763  cached 24,448  out 1,122  cost unknown" "$test_root/runs-worker.out" "tokens are shown and an unreported cost stays unknown"
contains "Role        worker (holds the run's result)" "$test_root/runs-worker.out" "the worker role is explained"
not_contains "Session" "$test_root/runs-worker.out" "a headless head is not described by a terminal session"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_live" --task kill-dry-run-worker --size 100x40 --view detail > "$test_root/runs-worker-live.out"
contains "Tokens      reported when the step finishes" "$test_root/runs-worker-live.out" "running tokens are pending, not zero"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --size 100x40 --view detail > "$test_root/runs-planner.out"
contains "Plan        plan 60c5133478a6 approved 2026-09-26 10:21 UTC / run" "$test_root/runs-planner.out" "the planning head shows its plan approval and run"
contains "run_aaaaaaaaaaaaaaaaaaaa / succeeded" "$test_root/runs-planner.out" "the planning head names its run and its state"
contains "Worker branch kill-dry-run-worker holds the result" "$test_root/runs-planner.out" "the planning head names what to do with the result"
contains "Approval requests  none" "$test_root/runs-planner.out" "gate approval requests are named as such"
not_contains "0 of 0" "$test_root/runs-planner.out" "no misleading zero-of-zero approval count"
contains "Full diff   : diff (everything since the branch base)" "$test_root/runs-planner.out" "the full diff row has a value"
not_contains "Full diff       :" "$test_root/runs-planner.out" "the full diff row has no stray colon"
# --view overview opens Work on the Run page of the run it used to centre on.
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --size 100x40 --view overview > "$test_root/runs-overview.out"
contains "+ succeeded / plan run kill-dry-run" "$test_root/runs-overview.out" "the Run page leads with the run's state and names it"
contains "plan run kill-dry-run / succeeded / 2 heads" "$test_root/runs-overview.out" "the status line names the selected run"
contains "Conversation  Details  Run" "$test_root/runs-overview.out" "the run's pages are named in the strip"
contains " implement     + succeeded         1m06s" "$test_root/runs-overview.out" "each step shows its state and duration"
contains "Head    kill-dry-run-worker" "$test_root/runs-overview.out" "the selected step names the head it ran on"
contains "kill-dry-run-check (retired)" "$test_root/runs-overview.out" "the Run page shows the retired verifier head"
not_contains "QUEUE DEPTH" "$test_root/runs-overview.out" "an empty queue chart is not drawn"
"$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_live" --size 100x40 --view overview > "$test_root/runs-overview-live.out"
contains "Running step implement on kill-dry-run-worker (codex)" "$test_root/runs-overview-live.out" "a running run says what is running and where"
contains "kill-dry-run-check (pending)" "$test_root/runs-overview-live.out" "a head the run has not created yet is pending"
"$tui" --ascii --headless-fixture "$runs_heads" --size 140x40 --view overview > "$test_root/runs-overview-empty.out"
not_contains "Details  Run" "$test_root/runs-overview-empty.out" "without a run there is no Run page"
contains " Details " "$test_root/runs-overview-empty.out" "without a run the side pane shows the head's details"
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
# The agent-layer chords come first and keep their names and defaults; every
# other key of the control centre follows, one action per line.
keys_lines "$test_root/keys-default.conf" | head -n 14 > "$test_root/keys-default.lines"
cmp -s "$test_root/keys-expected.lines" "$test_root/keys-default.lines"
assert_success $? "default keys are Ctrl-] to leave and the Ctrl-\\ leader for the rest"
for keys_action in heads.down task.new head.remove app.help app.exit view.attention statistics.range plan.execute; do
    grep -q "^$keys_action = " "$test_root/keys-default.conf"
    assert_success $? "the keys file lists $keys_action"
done
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
