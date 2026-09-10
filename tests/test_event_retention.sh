#!/bin/sh
set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d)"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home" HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
mkdir -p "$repo" "$HYDRA_HOME"
# shellcheck source=/dev/null
. "$root/tests/helpers.sh"
# shellcheck source=/dev/null
. "$root/lib/output.sh"
# shellcheck source=/dev/null
. "$root/lib/locks.sh"
# shellcheck source=/dev/null
. "$root/lib/identity.sh"
# shellcheck source=/dev/null
. "$root/lib/state_v2.sh"
# shellcheck source=/dev/null
. "$root/lib/events.sh"

cleanup() { rm -rf "$test_root"; }
trap cleanup EXIT HUP INT TERM

git -C "$repo" init -q
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name Test
printf 'fixture\n' > "$repo/file"
git -C "$repo" add file
git -C "$repo" commit -qm fixture
cd "$repo" || exit 1
project="$(hydra_ensure_project_id)"
head="$(state_v2_create_head "$project" retention-test retention-test - - 1 - - "$repo")"
head_dir="$(state_v2_head_dir "$project" "$head")"
instance="$(sed -n '1p' "$head_dir/current-instance")"
event_file="$(event_file_for_head "$project" "$head")"
event_lock="events_${project}_${head}"

event_emit "$project" "$head" "$instance" lifecycle.started >/dev/null || exit 1
event_emit "$project" "$head" "$instance" lifecycle.progress >/dev/null || exit 1
event_emit "$project" "$head" "$instance" lifecycle.declared >/dev/null || exit 1

output="$(HYDRA_HOME="$HYDRA_HOME" "$root/bin/hydra" events retain --project "$project" --head "$head" --max-events 1 --archive-keep-seconds 60 --dry-run 2>&1)"
assert_success $? "dry-run retention succeeds"
case "$output" in *would-archive*) assert_success 0 "dry-run reports an archive" ;; *) assert_success 1 "dry-run reports an archive" ;; esac
assert_equal 3 "$(wc -l < "$event_file" | tr -d ' ')" "dry-run preserves current stream"
assert_equal 0 "$(find "$(dirname "$event_file")/archive" -name '*.jsonl' 2>/dev/null | wc -l | tr -d ' ')" "dry-run creates no archive"

head -n 2 "$event_file" > "$test_root/expected-prefix"
"$root/bin/hydra" events retain --project "$project" --head "$head" --max-events 1 --archive-keep-seconds 1 --apply >/dev/null
assert_success $? "apply archives the removed prefix"
assert_equal 1 "$(wc -l < "$event_file" | tr -d ' ')" "apply preserves the requested current tail"
assert_equal 3 "$(sed -n 's/.*"sequence":\([0-9][0-9]*\).*/\1/p' "$event_file")" "retained tail keeps its sequence"

prefix_archive="$(find "$(dirname "$event_file")/archive" -name 'events-*.jsonl' -type f | head -n 1)"
cmp "$test_root/expected-prefix" "$prefix_archive"
assert_success $? "archive contains exactly the removed prefix"
assert_equal 4 "$(cat "$event_file.next-sequence")" "cursor describes the full original stream"

printf 'untouched\n' > "$test_root/summary-victim"
ln -s "$test_root/summary-victim" "$(dirname "$event_file")/archive/expiry-summary.jsonl.tmp"
sleep 2
event_emit "$project" "$head" "$instance" lifecycle.finished >/dev/null
"$root/bin/hydra" events retain --project "$project" --head "$head" --max-events 0 --archive-keep-seconds 3600 --apply >/dev/null
assert_success $? "zero-tail retention succeeds"
event_emit "$project" "$head" "$instance" lifecycle.reopened >/dev/null
assert_equal 5 "$(sed -n 's/.*"sequence":\([0-9][0-9]*\).*/\1/p' "$event_file")" "zero-tail retention preserves monotonic sequence"
summary="$(dirname "$event_file")/archive/expiry-summary.jsonl"
grep -q '"status":"expired"' "$summary"
assert_success $? "expired archive is recorded in bounded summary"
assert_equal untouched "$(cat "$test_root/summary-victim")" "expiry cannot follow a predictable temporary symlink"

event_emit "$project" "$head" "$instance" lifecycle.extra >/dev/null
try_lock "$event_lock" "test held event writer"
"$root/bin/hydra" events retain --project "$project" --head "$head" --max-events 0 --dry-run >/dev/null 2>&1
assert_failure $? "busy event stream refuses retention"
release_lock "$event_lock"

"$root/bin/hydra" events retain --project "$project" --head "$head" --max-events 0 --archive-max-count 1 --archive-keep-seconds 60 --apply >/dev/null 2>&1
assert_failure $? "protected archive capacity refuses a new archive"
assert_equal 2 "$(wc -l < "$event_file" | tr -d ' ')" "capacity refusal preserves current stream"

archive_dir="$(dirname "$event_file")/archive"
cp -R "$archive_dir" "$test_root/valid-archive"
cp "$event_file" "$test_root/valid-events"
cp "$event_file.next-sequence" "$test_root/valid-cursor"
restore_archive() {
    rm -rf "$archive_dir"
    cp -R "$test_root/valid-archive" "$archive_dir"
    cp "$test_root/valid-events" "$event_file"
    cp "$test_root/valid-cursor" "$event_file.next-sequence"
    valid_archive="$(find "$archive_dir" -name 'events-*.jsonl' -type f | head -n 1)"
}
retain_refused() {
    "$root/bin/hydra" events retain --project "$project" --head "$head" --max-events 0 --archive-max-count 32 --apply >/dev/null 2>&1
    assert_failure $? "$1"
    [ ! -e "$HYDRA_HOME/locks/$event_lock.lock" ]
    assert_success $? "failed public retention releases its lock"
    cmp "$test_root/valid-events" "$event_file"
    assert_success $? "refusal preserves current events"
}
restore_archive
printf 'tampered\n' >> "$valid_archive"
retain_refused "tampered archive hash fails closed"
restore_archive
cp "$event_file" "$archive_dir/legacy.jsonl"
retain_refused "legacy archive consumes protected quota"
restore_archive
printf 'corrupt\n' > "$archive_dir/corrupt.jsonl"
printf 'expires_at=9999999999\nbytes=7\n' > "$archive_dir/corrupt.jsonl.meta"
retain_refused "corrupt metadata fails closed"
restore_archive
ln -s "$event_file" "$archive_dir/symlink.jsonl"
retain_refused "symlink archive fails closed"
for field in schema_version stream_id created_at; do
    restore_archive
    sed "s/^$field=.*/$field=invalid/" "$valid_archive.meta" > "$test_root/meta"
    mv "$test_root/meta" "$valid_archive.meta"
    retain_refused "invalid $field metadata is protected"
done
restore_archive
: > "$event_file"
printf 'not-a-sequence\n' > "$event_file.next-sequence"
event_emit "$project" "$head" "$instance" lifecycle.invalid-cursor >/dev/null 2>&1
assert_failure $? "invalid empty-stream cursor refuses append"
rm "$event_file.next-sequence"
event_emit "$project" "$head" "$instance" lifecycle.missing-cursor >/dev/null 2>&1
assert_failure $? "missing rotation cursor does not restart sequence numbering"
restore_archive
sed 's/"sequence":[0-9]*,//' "$event_file" > "$test_root/missing-sequence"
event_verify_file "$test_root/missing-sequence" >/dev/null 2>&1
assert_failure $? "events without numeric sequences fail verification"
printf 'corrupt\n' >> "$event_file"
cp "$event_file" "$test_root/corrupt-events"
"$root/bin/hydra" events retain --project "$project" --head "$head" --max-events 0 --apply >/dev/null 2>&1
assert_failure $? "corrupt current stream cannot be archived as valid history"
cmp "$test_root/corrupt-events" "$event_file"
assert_success $? "corrupt stream preserved for explicit repair"
restore_archive
: > "$event_file"
rm "$event_file.next-sequence"
rm -rf "$archive_dir"
printf 'not a directory\n' > "$archive_dir"
event_emit "$project" "$head" "$instance" lifecycle.invalid-archive >/dev/null 2>&1
assert_failure $? "regular archive path cannot reset an empty stream"
restore_archive
sed "s/\"project_id\":\"$project\"/\"project_id\":\"project_aaaaaaaaaaaaaaaaaaaa\"/" "$event_file" > "$test_root/foreign-events"
cp "$test_root/foreign-events" "$event_file"
"$root/bin/hydra" events retain --project "$project" --head "$head" --max-events 0 --apply >/dev/null 2>&1
assert_failure $? "foreign current stream cannot be archived with a local identity"
cmp "$test_root/foreign-events" "$event_file"
assert_success $? "foreign stream remains available for reconciliation"
if [ ! -e "$HYDRA_HOME/locks/$event_lock.lock" ]; then
    assert_success 0 "foreign stream rejection releases the event lock"
else
    assert_success 1 "foreign stream rejection releases the event lock"
fi
restore_archive
event_emit "$project" "$head" "$instance" lifecycle.nested hydra local '{"sequence":999}' >/dev/null
assert_success $? "nested payload sequence is permitted"
event_emit "$project" "$head" "$instance" lifecycle.after-nested >/dev/null
assert_success $? "next envelope sequence ignores the payload sequence"
assert_equal 8 "$(tail -n 1 "$event_file" | _event_sequence)" "envelope sequence remains monotonic"

printf 'Event archive retention: dry-run/apply, expiry summary, zero-tail sequence, busy lock, and protected capacity passed\n'
printf 'Total: %s  Passed: %s  Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
