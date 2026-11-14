#!/bin/sh
# Captures every I4 screen state (attention queue and detail, exact review,
# details, recovery, run, result and failure screens) at 80x24 and 140x40 as
# plain text from deterministic fixtures. tests/test_native_tui_i4.sh compares
# a fresh capture with the committed docs/evidence/i4/*-after.txt files.
# usage: scripts/capture-i4.sh TUI_BIN REPO_ROOT OUT_DIR LABEL
set -u
[ "$#" -eq 4 ] || { printf 'usage: %s TUI_BIN REPO_ROOT OUT_DIR LABEL\n' "$0" >&2; exit 2; }
tui="$1"; root="$2"; out="$3"; label="$4"
f="$root/tests/fixtures/tui"
mkdir -p "$out" || exit 1
enter="$(printf '\r')"
tab="$(printf '\t')"
work="$(mktemp -d "${TMPDIR:-/tmp}/hydra-i4-capture.XXXXXX")" || exit 1
trap 'rm -rf "$work"' EXIT HUP INT TERM
# A readable local log for the full-log view: the fixture names a path that
# exists only here, so the review fixture is rewritten with this path (the
# review-log and review-reference-preview captures therefore name it).
log="$work/output.log"
i=0; while [ "$i" -lt 60 ]; do printf '[PASS] check number %d of the fixture suite\n' "$i"; i=$((i + 1)); done > "$log"
printf 'Tests: 12, Passed: 12, Failed: 0\n' >> "$log"
sed "s|/tmp/hydra-fixture/state/runs/run_2cdf8bbe1a4e/steps/verify/attempt-1/output.log|$log|" "$f/review-result.tsv" > "$work/review-result-log.tsv"
printf 'HYDRA_ATTENTION\t1\nEND\t0\t0\t0\n' > "$work/attention-empty.tsv"
printf 'HYDRA_ATTENTION\t1\nITEM\tbroken\n' > "$work/attention-broken.tsv"
printf 'HYDRA_TUI\t2\n' > "$work/empty.tsv"
# A review whose identity does not match the selected item.
sed 's/^HYDRA_REVIEW\t1\tworkflow records\tproject_7f3a/HYDRA_REVIEW\t1\tworkflow records\tproject_other/' "$f/review-result.tsv" > "$work/review-mismatch.tsv"

shot() {
    name="$1"; shift
    for size in 80x24 140x40; do
        "$tui" --headless-fixture "$f/native-runs.tsv" --size "$size" "$@" > "$out/$name-$size-$label.txt" 2>&1
    done
}
shotf() {
    name="$1"; fixture="$2"; shift 2
    for size in 80x24 140x40; do
        "$tui" --headless-fixture "$fixture" --size "$size" "$@" > "$out/$name-$size-$label.txt" 2>&1
    done
}
# The attention queue with its realistic fixture, then any keys and reviews.
shota() {
    name="$1"; shift
    shot "$name" --attention-fixture "$f/attention-v3.tsv" --view attention "$@"
}
shota attention-list
shota attention-detail-failure --keys "$enter"
shota attention-detail-result --keys "jj$enter"
shota attention-detail-unknown --keys "jjjjjj$enter"
shot attention-empty --attention-fixture "$work/attention-empty.tsv" --view attention
shot attention-stale --attention-fixture "$f/attention-v3.tsv" --attention-fixture "$work/attention-broken.tsv" --view attention
shot attention-unavailable --attention-fixture "$work/attention-broken.tsv" --view attention
shot attention-loading --view attention
shota review-failure --review-fixture "$f/review-failure.tsv"
shota review-result --keys jj --review-fixture "$f/review-result.tsv"
shota review-result-changes --keys jj --review-fixture "$f/review-result.tsv" --keys nnnnn
shota review-references --keys jj --review-fixture "$f/review-result.tsv" --keys f
shota review-reference-preview --keys jj --review-fixture "$work/review-result-log.tsv" --keys "f$enter"
shota review-identity --keys jj --review-fixture "$f/review-result.tsv" --keys i
shota review-log --keys jj --review-fixture "$work/review-result-log.tsv" --keys L
shota review-unavailable --keys jj --review-fixture "$work/review-mismatch.tsv"
shot work-list --workflow-fixture "$f/workflow-runs.tsv" --view heads
shot work-list-expanded --workflow-fixture "$f/workflow-runs.tsv" --view heads --task kill-dry-run-worker
shot details --workflow-fixture "$f/workflow-runs.tsv" --view detail
shot details-worker --workflow-fixture "$f/workflow-runs.tsv" --view detail --task kill-dry-run-worker
shot details-stopped --workflow-fixture "$f/workflow-runs.tsv" --view detail --task notes-cleanup
shot details-technical --workflow-fixture "$f/workflow-runs.tsv" --diagnostics --task kill-dry-run-worker
shotf details-remote "$f/native-v2.tsv" --remote-fixture "$f/remote-v3.tsv" --view detail --task archive/feature-index
shotf details-task "$f/native-v2.tsv" --remote-fixture "$f/remote-v3.tsv" --view detail --task task_build
shotf recovery-list "$f/native-v2.tsv" --view recovery
shotf recovery-detail "$f/native-v2.tsv" --view recovery --diagnostics
shot overview-run --workflow-fixture "$f/workflow-runs.tsv" --view overview
shot overview-run-live --workflow-fixture "$f/workflow-runs-running.tsv" --view overview
shot overview-empty-run --view overview
shot workflows --workflow-fixture "$f/workflow-runs.tsv" --view workflows
shot workspace --workflow-fixture "$f/workflow-runs.tsv" --view workspace
shot workspace-enter --workflow-fixture "$f/workflow-runs.tsv" --view workspace --keys "$tab$enter"
shot workspace-monitor --workflow-fixture "$f/workflow-runs.tsv" --view workspace --keys C
shotf statistics "$f/native-v2.tsv" --statistics-fixture "$f/statistics-v4.tsv" --view statistics
shotf empty-work "$work/empty.tsv" --view heads
shotf empty-overview "$work/empty.tsv" --view overview
