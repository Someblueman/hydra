#!/bin/sh
# I4 visual pass: deterministic fixture renders of the attention queue and
# detail, the exact review, details, recovery, run, result and failure
# screens at 80x24 and 140x40, and snapshots of every captured state
# compared with docs/evidence/i4/*-after.txt.

set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
tui="${HYDRA_TUI_BIN:?HYDRA_TUI_BIN is required: run via make test-tui or make test-one T=native_tui_i4}"
fixture="$repo_root/tests/fixtures/tui/native-v2.tsv"
remote_fixture="$repo_root/tests/fixtures/tui/remote-v3.tsv"
runs_heads="$repo_root/tests/fixtures/tui/native-runs.tsv"
runs_done="$repo_root/tests/fixtures/tui/workflow-runs.tsv"

# shellcheck source=helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() { rm -rf "$test_root"; }
trap cleanup EXIT HUP INT TERM

contains() {
    if grep -Fq "$1" "$2"; then assert_success 0 "$3"; else assert_success 1 "$3"; fi
}
not_contains() {
    if grep -Fq "$1" "$2"; then assert_success 1 "$3"; else assert_success 0 "$3"; fi
}

# I4: Inbox (the attention list, an item's Summary, Review, References and
# Technical pages, recovery findings), run, result and failure screens render
# from fixtures at 80x24 and 140x40 (ASCII, so the captures are byte-stable);
# every row stays inside the terminal width, the three themes and no-colour
# draw the same text, and no screen repeats the footer's key hints inside its
# content.
attention_fx="$repo_root/tests/fixtures/tui/attention-v3.tsv"
review_result="$repo_root/tests/fixtures/tui/review-result.tsv"
review_failure="$repo_root/tests/fixtures/tui/review-failure.tsv"
enter_key="$(printf '\r')"
tab_key="$(printf '\t')"
printf 'HYDRA_ATTENTION\t1\nEND\t0\t0\t0\n' > "$test_root/attention-empty.tsv"
printf 'HYDRA_TUI\t2\n' > "$test_root/empty-heads.tsv"
empty_heads="$test_root/empty-heads.tsv"
printf 'HYDRA_ATTENTION\t1\nITEM\tbroken\n' > "$test_root/attention-broken.tsv"
sed 's/^HYDRA_REVIEW	1	workflow records	project_7f3a/HYDRA_REVIEW	1	workflow records	project_other/' "$review_result" > "$test_root/review-mismatch.tsv"
i4_log="$test_root/i4-output.log"
printf '[PASS] fixture check 1\n[PASS] fixture check 2\nI4-LOG-TAIL Tests: 12, Passed: 12, Failed: 0\n' > "$i4_log"
sed "s|/tmp/hydra-fixture/state/runs/run_2cdf8bbe1a4e/steps/verify/attempt-1/output.log|$i4_log|" "$review_result" > "$test_root/review-log.tsv"
fits_width() {
    awk -v cols="${2%x*}" 'NR > 1 && length >= cols { exit 1 }' "$1"
    assert_success $? "$3 fits $2"
}
hint_once() {
    assert_equal "1" "$(grep -c -F "$2" "$1")" "$3 names $2 once"
}
for size in 80x24 140x40; do
    i4="$test_root/i4-attention-$size.out"
    # Inbox lists the attention read in sections, decisions first: the
    # approval, then the failed check, then the results to review.
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" > "$i4"
    fits_width "$i4" "$size" "attention list"
    contains "Needs you" "$i4" "Inbox leads with what needs the user at $size"
    contains "Decisions 1" "$i4" "the approval is listed first, as a decision, at $size"
    contains "Failed 1" "$i4" "Inbox counts the failed check that needs a decision at $size"
    contains "x Check failed: kill-dry-run" "$i4" "the failed check is listed under Failed, named by its plan, at $size"
    contains "step repo-check / req-dry-run" "$i4" "the failure names its step and the requirements it decides at $size"
    contains "Ready for review 3" "$i4" "results to review have their own section at $size"
    contains "/ new" "$i4" "new items are marked new at $size"
    not_contains "Press r" "$i4" "Inbox rows do not repeat the review key the footer carries at $size"
    hint_once "$i4" "| r review |" "attention list"
    # Enter on the failure: its Summary page says what failed, its seen state
    # and the next actions; the Technical page keeps the exact identity.
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" --keys "j$enter_key" > "$i4"
    fits_width "$i4" "$size" "attention detail"
    contains "Summary  Review  References  Technical" "$i4" "the detail names its pages at $size"
    contains "x Check failed: kill-dry-run" "$i4" "the detail leads with the item and its subject at $size"
    contains "Status      New: not marked seen yet; listed until the run is resolved." "$i4" "the detail separates seen state from the decision at $size"
    contains "Next        r reviews the failed requirement and its log." "$i4" "the detail names the next action at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" --keys "j${enter_key}[" > "$i4"
    contains "IDENTITY" "$i4" "technical identity sits on its own page at $size"
    contains "Kind        failure / check_failed" "$i4" "the Technical page keeps the raw kind and reason at $size"
    contains "Head        head_b2c4d6e8" "$i4" "the Technical page keeps the head identity at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" --keys "jj" --review-fixture "$review_result" > "$i4"
    fits_width "$i4" "$size" "result review"
    contains "EXACT REVIEW / evidence" "$i4" "the review is labelled exact at $size"
    contains "kill-dry-run / implement" "$i4" "the review leads with the task and step at $size"
    contains "Result / run run_2cdf8bbe1a4e / attempt-1 / read-only" "$i4" "the review names its binding and that it is read-only at $size"
    contains "RESULT  kill-dry-run  -  verdict PASS  -  run succeeded" "$i4" "the result review shows the verdict at $size"
    contains "WHAT WAS PRODUCED" "$i4" "the result review groups what was produced at $size"
    hint_once "$i4" "L full log" "result review"
    hint_once "$i4" "n/N section" "result review"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" --keys "jj" --review-fixture "$review_result" --keys "]" > "$i4"
    fits_width "$i4" "$size" "review references"
    contains "REFERENCES  3 recorded" "$i4" "the References page counts the recorded references at $size"
    contains "> log         recorded preview" "$i4" "a local reference says it has a recorded preview at $size"
    contains "  transcript  inaccessible" "$i4" "an inaccessible reference says so at $size"
    contains "  pr          supplied, unopened https://github.com/example/hydra/pull/84" "$i4" "a supplied web reference is shown unopened at $size"
    contains "Reference 1/3: log / available" "$i4" "the selected reference is shown in full at $size"
    contains "Enter opens the full log (read-only)" "$i4" "the References page says what Enter does at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" --keys "jj" --review-fixture "$test_root/review-log.tsv" --keys "]$enter_key" > "$i4"
    contains "Tests: 12, Passed: 12, Failed: 0" "$i4" "Enter on a local log reference shows the log at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" --keys "jj" --review-fixture "$test_root/review-log.tsv" --keys "LG" > "$i4"
    contains "EXACT REVIEW / full log" "$i4" "L opens the full log at $size"
    contains "FULL LOG  " "$i4" "the full log is labelled with its size at $size"
    contains "I4-LOG-TAIL" "$i4" "G reaches the end of the full log at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" --keys "jj" --review-fixture "$review_result" --keys "]]" > "$i4"
    fits_width "$i4" "$size" "review identity"
    contains "IDENTITY" "$i4" "the reviewed item's identity is on the Technical page at $size"
    contains "Source      workflow records" "$i4" "the Technical page labels each field at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" --keys "j" --review-fixture "$review_failure" > "$i4"
    fits_width "$i4" "$size" "failure review"
    contains "RESULT  kill-dry-run  -  verdict FAILED  -  run failed" "$i4" "the failure review states the failed verdict at $size"
    contains "FAIL    req-dry-run-flag" "$i4" "the failure review marks the failed requirement at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size "$size" --keys "jj" --review-fixture "$test_root/review-mismatch.tsv" > "$i4"
    contains "EXACT REVIEW / evidence / UNAVAILABLE" "$i4" "a review that does not match its subject is unavailable at $size"
    contains "Review unavailable: failed, malformed or identity mismatch" "$i4" "the unavailable review says why at $size"
    not_contains "WHAT WAS PRODUCED" "$i4" "an unavailable review shows no evidence at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --attention-fixture "$test_root/attention-broken.tsv" --view attention --size "$size" > "$i4"
    contains "Showing the list from" "$i4" "a failed refresh keeps the last good list and says so at $size"
    contains "reading it failed" "$i4" "the stale banner says the read failed at $size"
    not_contains "items need you" "$i4" "a stale list counts nobody as needing the user at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$test_root/attention-broken.tsv" --view attention --size "$size" > "$i4"
    contains "Attention unavailable; no valid snapshot yet" "$i4" "attention without a valid snapshot is unavailable at $size"
    contains "Nothing is decided or acknowledged by looking" "$i4" "the unavailable state keeps seen state separate at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$test_root/attention-empty.tsv" --view attention --size "$size" > "$i4"
    not_contains "Decisions" "$i4" "an empty attention read lists no decision at $size"
    contains "Terminal stopped: notes-cleanup" "$i4" "an empty attention read still lists the heads that need the user at $size"
    "$tui" --ascii --headless-fixture "$empty_heads" --attention-fixture "$test_root/attention-empty.tsv" --view attention --size "$size" > "$i4"
    contains "Nothing needs you" "$i4" "an empty Inbox says so at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --view attention --size "$size" > "$i4"
    contains "Reading what needs you..." "$i4" "Inbox explains its loading state at $size"
    # Work: ] shows the next page of the side pane (the shared pane below
    # 120 columns), which for this planning head is its Details page.
    "$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --view workspace --size "$size" --keys "]" > "$i4"
    contains "Reported    nothing yet" "$i4" "] shows the selected head's details from the list at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --view workspace --size "$size" --keys "$tab_key]" > "$i4"
    contains "Reported    nothing yet" "$i4" "] shows the selected head's details from the next pane at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --view workspace --size "$size" > "$i4"
    contains "enter/a talk" "$i4" "the Work footer names Enter for talking to the agent at $size"
    # Recovery findings are Inbox items: the stopped terminal (after the two
    # approval gates) has a Summary in plain words and a Technical page.
    "$tui" --ascii --headless-fixture "$fixture" --view recovery --size "$size" --keys "jj$enter_key" > "$i4"
    fits_width "$i4" "$size" "recovery list"
    contains "Terminal stopped: feature-stale" "$i4" "the stopped terminal is listed by what happened at $size"
    contains "The terminal session for feature-stale is no longer running." "$i4" "the selected finding is explained in full at $size"
    contains "c Run check" "$i4" "the finding's Summary offers its check at $size"
    not_contains "hydra doctor" "$i4" "the Summary keeps the check command for the Technical page at $size"
    "$tui" --ascii --headless-fixture "$fixture" --view recovery --diagnostics --size "$size" --keys "jj$enter_key" > "$i4"
    contains "DIAGNOSTICS" "$i4" "the Technical page keeps the finding's diagnostics under their own heading at $size"
    contains "Kind        dead-session" "$i4" "the Technical page keeps the raw kind in its diagnostics group at $size"
    contains "Inspect     hydra doctor" "$i4" "the Technical page names the check c runs at $size"
    "$tui" --ascii --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --view detail --task archive/feature-index --size "$size" > "$i4"
    fits_width "$i4" "$size" "remote details"
    not_contains "not a live pr |" "$i4" "remote details wrap long values instead of cutting them at $size"
    contains "Liveness     unobserved" "$i4" "remote details keep the liveness caveat at $size"
    "$tui" --ascii --headless-fixture "$fixture" --remote-fixture "$remote_fixture" --view detail --task task_build --size "$size" > "$i4"
    contains "task task_build on builder" "$i4" "a remote task's details lead with the task at $size"
    contains "Request      " "$i4" "a remote task's request identity sits in its identity group at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --view detail --task notes-cleanup --size "$size" > "$i4"
    contains "The terminal is gone; the agent was" "$i4" "a stopped terminal's details lead with what is known at $size"
    "$tui" --ascii --headless-fixture "$runs_heads" --workflow-fixture "$runs_done" --view overview --size "$size" > "$i4"
    fits_width "$i4" "$size" "run panel"
    contains "+ succeeded / plan run kill-dry-run" "$i4" "the Run page leads with the run's state at $size"
done
contains "> + plan run kill..." "$test_root/i4-attention-80x24.out" "a run's name in the list ends in an ellipsis instead of a cut word at 80 columns"
"$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size 80x24 > "$test_root/i4-ellipsis.out"
contains "Result ready: feature/very-long-task-name-for-the-wrapping-check-of-the..." "$test_root/i4-ellipsis.out" "a title that does not fit ends in an ellipsis"
"$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size 80x24 --keys "j${enter_key}[" > "$test_root/i4-detail-wide.out"
contains "Revision   7777777777777777777777777777777777777777777777777777777777777777" "$test_root/i4-detail-wide.out" "the Technical page shows the full revision digest where it fits"
contains "Identity   8888888888888888888888888888888888888888888888888888888888888888" "$test_root/i4-detail-wide.out" "the Technical page shows the full identity digest where it fits"
"$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size 80x24 --keys "jj" --review-fixture "$review_result" --keys "]]" > "$test_root/i4-identity-wide.out"
contains "Revision   5555555555555555555555555555555555555555555555555555555555555555" "$test_root/i4-identity-wide.out" "the reviewed item's Technical page shows the full revision digest"
contains "Identity   6666666666666666666666666666666666666666666666666666666666666666" "$test_root/i4-identity-wide.out" "the reviewed item's Technical page shows the full identity digest"
"$tui" --ascii --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size 40x10 > "$test_root/i4-narrow.out"
fits_width "$test_root/i4-narrow.out" 40x10 "attention list"
contains "Check failed: kill-dry-run" "$test_root/i4-narrow.out" "Inbox items keep their two-line form at 40 columns"
for theme in terminal dark light; do
    "$tui" --theme "$theme" --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size 80x24 > "$test_root/i4-theme-$theme.out"
done
NO_COLOR=1 "$tui" --headless-fixture "$runs_heads" --attention-fixture "$attention_fx" --view attention --size 80x24 > "$test_root/i4-theme-none.out"
cmp -s "$test_root/i4-theme-terminal.out" "$test_root/i4-theme-dark.out" && cmp -s "$test_root/i4-theme-dark.out" "$test_root/i4-theme-light.out" && cmp -s "$test_root/i4-theme-light.out" "$test_root/i4-theme-none.out"
assert_success $? "the attention screen reads the same in every theme and without colour"

# Snapshots: a fresh capture of every I4 state matches the committed after
# capture byte for byte. The review-log and reference-preview captures name
# the capture's private log path and overview-run-live shows a live elapsed
# time, so those are covered by the content checks above instead. The
# captures use the box-drawing glyphs, so they need a UTF-8 locale.
LC_ALL=C.UTF-8 LANG=C.UTF-8 sh "$repo_root/scripts/capture-i4.sh" "$tui" "$repo_root" "$test_root/i4-snapshots" after
if ! grep -q "$(printf '\342\224\200')" "$test_root/i4-snapshots/attention-list-80x24-after.txt" 2>/dev/null; then
    printf '%s\n' "SKIP i4 snapshots: no UTF-8 locale for the box-drawing captures"
else
    snapshot_count=0
    for expected in "$repo_root"/docs/evidence/i4/*-after.txt; do
        name="$(basename "$expected")"
        case "$name" in review-log-*|review-reference-preview-*|overview-run-live-*) continue ;; esac
        snapshot_count=$((snapshot_count + 1))
        if cmp -s "$expected" "$test_root/i4-snapshots/$name"; then assert_success 0 "snapshot $name"
        else
            diff "$expected" "$test_root/i4-snapshots/$name" | head -20
            assert_success 1 "snapshot $name"
        fi
    done
    for actual in "$test_root"/i4-snapshots/*-after.txt; do
        if [ -f "$repo_root/docs/evidence/i4/$(basename "$actual")" ]; then check=0; else check=1; fi
        assert_success "$check" "captured state $(basename "$actual") has a committed snapshot"
    done
    [ "$snapshot_count" -ge 60 ]
    assert_success $? "at least 60 committed snapshots were compared"
fi

printf '\nTests: %d, Passed: %d, Failed: %d\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
