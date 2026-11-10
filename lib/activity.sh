#!/bin/sh
# Inferred head activity and the observer coverage that qualifies it.
#
# An observer (the control centre) infers from tmux window output whether an
# interactive head is producing output ("active") or silent ("quiet") and
# records each change as an activity.changed event in the head's event log.
# While it watches it also keeps a per-project coverage file current, so time
# nobody watched stays distinguishable from quiet time. Neither state is a
# statement about what the agent is waiting for.

# An observer that has not reported for this long has stopped watching.
HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS=45
# Watch intervals kept per project. Dropping older ones makes the time before
# them unknown, which the first line of the file records.
HYDRA_ACTIVITY_COVERAGE_MAX_INTERVALS=512

activity_coverage_file() {
    _acf_dir="$(state_v2_project_dir "$1")" || return 1
    printf '%s/activity/coverage\n' "$_acf_dir"
}

# Rewrites the coverage intervals with the observer seen at <now>: the newest
# interval is extended while the observer keeps reporting, and a new one starts
# after a gap. Output is the whole file; exit 1 means the input is malformed.
_activity_coverage_next() {
    awk -v now="$2" -v stale="$HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS" \
        -v max="$HYDRA_ACTIVITY_COVERAGE_MAX_INTERVALS" '
        BEGIN { FS = OFS = "\t"; unknown_before = 0; n = 0 }
        function uint(value) { return value ~ /^[0-9]+$/ && length(value) <= 10 }
        NR == 1 {
            if (NF != 3 || $1 != "HYDRA_ACTIVITY_COVERAGE" || $2 != "1" || !uint($3)) bad = 1
            unknown_before = $3 + 0
            next
        }
        {
            if (NF != 2 || !uint($1) || !uint($2) || $2 + 0 < $1 + 0 || (n && $1 + 0 < last[n])) bad = 1
            n++; first[n] = $1 + 0; last[n] = $2 + 0
        }
        END {
            if (bad) exit 1
            now += 0
            if (n && now >= last[n] && now - last[n] <= stale) last[n] = now
            else if (!n || now > last[n]) { n++; first[n] = now; last[n] = now }
            from = 1
            if (n > max) { from = n - max + 1; unknown_before = last[from - 1] }
            print "HYDRA_ACTIVITY_COVERAGE", 1, unknown_before
            for (i = from; i <= n; i++) print first[i], last[i]
        }' "$1"
}

# Records that an observer was watching this project's interactive heads now.
# Usage: activity_observer_beat <project-id> [now]
activity_observer_beat() {
    _aob_project="$1"
    _aob_now="${2:-$(date +%s)}"
    hydra_valid_id "$_aob_project" || return 1
    _event_uint "$_aob_now" 4294967294 || return 1
    _aob_file="$(activity_coverage_file "$_aob_project")" || return 1
    _aob_dir="$(dirname "$_aob_file")"
    [ -d "$(dirname "$_aob_dir")" ] || return 1
    if [ -L "$_aob_dir" ] || [ -L "$_aob_file" ]; then
        echo "Error: activity coverage must not be a symbolic link" >&2
        return 1
    fi
    hydra_private_mkdir "$_aob_dir" || return 1
    _aob_lock="activity_${_aob_project}"
    acquire_lock "$_aob_lock" "activity coverage" || return 1
    _aob_tmp="$(mktemp_adjacent "$_aob_file")" || { release_lock "$_aob_lock"; return 1; }
    chmod 600 "$_aob_tmp" 2>/dev/null || true
    if [ -f "$_aob_file" ] && ! _activity_coverage_next "$_aob_file" "$_aob_now" > "$_aob_tmp"; then
        # History that cannot be read is unknown from here back, never zero.
        mv "$_aob_file" "$_aob_file.corrupt.$_aob_now" 2>/dev/null || rm -f "$_aob_file"
    fi
    if [ ! -f "$_aob_file" ]; then
        _aob_before=0
        [ -z "$(find "$_aob_dir" -name 'coverage.corrupt.*' -print 2>/dev/null | head -n 1)" ] || _aob_before="$_aob_now"
        printf 'HYDRA_ACTIVITY_COVERAGE\t1\t%s\n%s\t%s\n' "$_aob_before" "$_aob_now" "$_aob_now" > "$_aob_tmp"
    fi
    if ! atomic_replace "$_aob_file" "$_aob_tmp"; then
        rm -f "$_aob_tmp"
        release_lock "$_aob_lock"
        return 1
    fi
    release_lock "$_aob_lock"
}

# Records one inferred activity change of an interactive head's current
# instance. A change equal to the last recorded state is dropped under the
# head's activity lock, so several observers never record it twice.
# Usage: activity_record <branch> <instance-id> <active|quiet> <since-epoch>
# Prints: recorded | unchanged
activity_record() {
    _arc_branch="$1"
    _arc_instance="$2"
    _arc_state="$3"
    _arc_since="$4"
    case "$_arc_state" in active|quiet) ;; *) return 1 ;; esac
    _event_uint "$_arc_since" 4294967294 && [ "$_arc_since" -gt 0 ] || return 1
    lifecycle_load_head "$_arc_branch" || return 1
    if [ "$_arc_instance" != "$LIFECYCLE_INSTANCE_ID" ]; then
        echo "Error: stale activity for non-current instance" >&2
        return 1
    fi
    _arc_mode="$( [ -s "$LIFECYCLE_HEAD_DIR/terminal-mode" ] && sed -n '1p' "$LIFECYCLE_HEAD_DIR/terminal-mode" || echo interactive )"
    if [ "$_arc_mode" != interactive ] || [ "$(lifecycle_read session)" = - ]; then
        echo "Error: head '$_arc_branch' has no terminal to observe; its owner reports its status" >&2
        return 1
    fi
    case "$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/desired-state" 2>/dev/null || true)" in
        running|stopping) ;;
        *) echo "Error: head '$_arc_branch' is not running" >&2; return 1 ;;
    esac
    _arc_now="$(date +%s)"
    if [ "$_arc_since" -gt "$_arc_now" ]; then
        echo "Error: activity cannot begin in the future" >&2
        return 1
    fi
    # A state cannot have begun before the instance it describes.
    _arc_started="$(lifecycle_read started-at)"
    if _event_uint "$_arc_started" 4294967294 && [ "$_arc_since" -lt "$_arc_started" ]; then
        _arc_since="$_arc_started"
    fi
    _arc_lock="activity_${LIFECYCLE_PROJECT_ID}_${LIFECYCLE_HEAD_ID}"
    acquire_lock "$_arc_lock" "record activity" || return 1
    if [ "$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/current-instance" 2>/dev/null || true)" != "$_arc_instance" ]; then
        release_lock "$_arc_lock"
        echo "Error: stale activity for non-current instance" >&2
        return 1
    fi
    if [ "$(lifecycle_read activity-state)" = "$_arc_state" ]; then
        release_lock "$_arc_lock"
        activity_observer_beat "$LIFECYCLE_PROJECT_ID" "$_arc_now" || true
        printf 'unchanged\n'
        return 0
    fi
    if ! event_emit "$LIFECYCLE_PROJECT_ID" "$LIFECYCLE_HEAD_ID" "$LIFECYCLE_INSTANCE_ID" \
            activity.changed observer tmux \
            "{\"state\":\"$_arc_state\",\"since\":$_arc_since,\"source\":\"tmux\",\"confidence\":\"inferred\"}" >/dev/null ||
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/activity-state" "$_arc_state" ||
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/activity-since" "$_arc_since"; then
        release_lock "$_arc_lock"
        return 1
    fi
    release_lock "$_arc_lock"
    activity_observer_beat "$LIFECYCLE_PROJECT_ID" "$_arc_now" || true
    printf 'recorded\n'
}
