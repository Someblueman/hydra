#!/bin/sh
# Head sessions: one bounded statistics row per head instance of this project,
# and the records that keep those rows after the events behind them are gone.
#
# A session is one instance: it starts at lifecycle.started or
# lifecycle.resumed and ends when it is torn down or superseded by a resume.
# Time inside it is active or quiet where an adapter reported running or idle,
# or where an observer was watching and inferred it from tmux output
# (activity.changed); everything else is unobserved. Lifecycle events come
# from the head's event log (and its unexpired archives); activity events from
# each instance's activity log, placed among them by head_next_sequence (older
# heads have them in the head log itself); watching from the coverage file.
#
# Three records stand in for events:
# - A summary, projects/<project>/sessions/<instance>, is what a finished
#   session adds up to, written once the observer coverage of its time can no
#   longer change. The feed prints it instead of interpreting the session's
#   events, and its activity log is then removed.
# - A session checkpoint, heads/<head>/instances/<instance>/session-checkpoint,
#   is what an open session's events up to a point add up to. Activity older
#   than HYDRA_SESSIONS_ACTIVITY_HORIZON_SECONDS is folded into it and removed
#   from the activity log.
# - The feed's own checkpoint per head under $HYDRA_HOME/cache/sessions is
#   disposable: the feed is polled every two seconds, so what settled events
#   (at least HYDRA_SESSIONS_SETTLED_SECONDS old) add up to is kept there and
#   only newer events are interpreted again.
# Summaries and session checkpoints are written by session_maintain_head under
# the head's activity and event locks, at teardown, resume, reconciliation,
# event retention and at most every HYDRA_SESSIONS_MAINTAIN_SECONDS from the
# observer's recorder. The feed only reads Hydra state.

# Newest sessions kept in the feed; the native reader holds as many.
HYDRA_SESSIONS_MAX_ROWS=256
# Heads whose history is read, and sessions considered before the newest are chosen.
HYDRA_SESSIONS_MAX_HEADS=512
HYDRA_SESSIONS_MAX_CANDIDATES=4096
# Summaries of finished sessions read, most recently written first.
HYDRA_SESSIONS_MAX_SUMMARIES=4096
# Event files larger than this many 512-byte blocks (8 MiB) are not read.
HYDRA_SESSIONS_MAX_LOG_BLOCKS=16384
# Events older than this are final: an observer extends its coverage only
# within HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS (45) of its last report.
HYDRA_SESSIONS_SETTLED_SECONDS=120
# Newly settled events that make a head's feed checkpoint worth rewriting.
HYDRA_SESSIONS_CHECKPOINT_EVENTS=64
# An open session's activity events older than this are folded into its
# session checkpoint and removed from its activity log.
HYDRA_SESSIONS_ACTIVITY_HORIZON_SECONDS=3600
# The observer's recorder maintains a project's sessions at most this often,
# at most this many heads at a time, and starts no head after this many seconds.
HYDRA_SESSIONS_MAINTAIN_SECONDS=600
HYDRA_SESSIONS_MAINTAIN_HEADS=64
HYDRA_SESSIONS_MAINTAIN_BUDGET_SECONDS=5

# Prints the directory for this project's checkpoints, or nothing when they
# cannot be kept privately (the whole history is then interpreted every time).
# HYDRA_SESSIONS_NO_CACHE turns checkpoints off; tests use it to compare.
_workflow_sessions_cache_dir() {
    [ -z "${HYDRA_SESSIONS_NO_CACHE:-}" ] || return 0
    _wsc_dir="$HYDRA_HOME/cache/sessions/$1"
    [ ! -L "$HYDRA_HOME/cache" ] && [ ! -L "$HYDRA_HOME/cache/sessions" ] && [ ! -L "$_wsc_dir" ] || return 0
    hydra_private_mkdir "$_wsc_dir" 2>/dev/null || return 0
    [ -d "$_wsc_dir" ] && [ -w "$_wsc_dir" ] || return 0
    printf '%s\n' "$_wsc_dir"
}

# Lists the summaries in <dir>, most recently written first, as
# "summary <instance>" lines. ls -F marks links, directories and executables,
# so only plain regular files keep a bare instance ID.
_workflow_sessions_summary_names() {
    [ -n "$1" ] && [ -d "$1" ] && [ ! -L "$1" ] || return 0
    # find cannot order by modification time portably; only names that are
    # exactly an instance ID pass the filter.
    # shellcheck disable=SC2012
    ls -tF "$1" 2>/dev/null | sed -n 's/^\(instance_[0-9a-f]*\)$/summary \1/p'
}

# The files of the heads under <dir> that sessions are derived from, at most
# <blocks> 512-byte blocks each.
_workflow_sessions_files() {
    find "$1" -mindepth "$2" -maxdepth "$3" -type f \
        \( -name branch -o -name '*.jsonl' -o -name session-checkpoint \) \
        ! -size +"$4" -print 2>/dev/null | LC_ALL=C sort
}

# Prints H rows (newest first) and X warning rows.
# Usage: workflow_sessions_rows <now-epoch>
workflow_sessions_rows() {
    _wsn_now="$1"
    _wsn_project="$(hydra_get_project_id 2>/dev/null)" || return 0
    _wsn_dir="$(state_v2_project_dir "$_wsn_project" 2>/dev/null)" || return 0
    _wsn_heads="$_wsn_dir/heads"
    [ -d "$_wsn_heads" ] || return 0
    if [ -L "$_wsn_dir" ] || [ -L "$_wsn_heads" ]; then
        printf 'X\tSkipped symlinked head state\n'
        return 0
    fi
    # Coverage is read only as a regular file in a real directory.
    _wsn_coverage="$_wsn_dir/activity/coverage"
    if [ -L "$_wsn_dir/activity" ] || [ -L "$_wsn_coverage" ] || [ ! -f "$_wsn_coverage" ]; then
        [ ! -L "$_wsn_dir/activity" ] && [ ! -L "$_wsn_coverage" ] || printf 'X\tSkipped symlinked activity coverage\n'
        _wsn_coverage=""
    fi
    _wsn_summaries="$_wsn_dir/sessions"
    if [ -L "$_wsn_summaries" ]; then
        printf 'X\tSkipped symlinked session summaries\n'
        _wsn_summaries=""
    fi
    if [ -n "$(find "$_wsn_heads" -mindepth 3 -maxdepth 4 -type f -name '*.jsonl' \
        -size +"$HYDRA_SESSIONS_MAX_LOG_BLOCKS" -print 2>/dev/null | sed -n '1p')" ]; then
        printf 'X\tSkipped an event log over 8 MiB; its sessions are partial\n'
    fi
    _wsn_cache="$(_workflow_sessions_cache_dir "$_wsn_project")"
    _wsn_checkpoints=""
    if [ -n "$_wsn_cache" ]; then
        rm -f "$_wsn_cache"/*.next."$$" 2>/dev/null
        # Only regular checkpoint files are read back.
        _wsn_checkpoints="$(find "$_wsn_cache" -type f -name 'head_*.tsv' -print 2>/dev/null)"
    fi
    # find does not follow symbolic links, so only regular files reached
    # through real directories are listed. Sorted, a head's branch comes first,
    # then its archives in creation order, its live log, then its instances.
    { _workflow_sessions_summary_names "$_wsn_summaries"; _workflow_sessions_files "$_wsn_heads" 2 4 "$HYDRA_SESSIONS_MAX_LOG_BLOCKS"; } |
        (umask 077; HYDRA_SESSION_CHECKPOINTS="$_wsn_checkpoints" LC_ALL=C awk -v mode=feed -v root="$_wsn_heads/" \
            -v now="$_wsn_now" -v coverage="$_wsn_coverage" -v summaries="$_wsn_summaries" -v cache="$_wsn_cache" \
            -v writer="$$" -v settled="$HYDRA_SESSIONS_SETTLED_SECONDS" -v stale="${HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS:-45}" \
            -v horizon="$HYDRA_SESSIONS_ACTIVITY_HORIZON_SECONDS" -v checkpoint_events="$HYDRA_SESSIONS_CHECKPOINT_EVENTS" \
            -v max_rows="$HYDRA_SESSIONS_MAX_ROWS" -v max_heads="$HYDRA_SESSIONS_MAX_HEADS" \
            -v max_candidates="$HYDRA_SESSIONS_MAX_CANDIDATES" -v max_summaries="$HYDRA_SESSIONS_MAX_SUMMARIES" \
            "$_WORKFLOW_SESSIONS_AWK")
    [ -n "$_wsn_cache" ] || return 0
    for _wsn_next in "$_wsn_cache"/*.next."$$"; do
        [ -f "$_wsn_next" ] || continue
        mv -f "$_wsn_next" "${_wsn_next%.next."$$"}" 2>/dev/null || rm -f "$_wsn_next"
    done
}

# Writes the summary of each finished session of a head whose observer
# coverage can no longer change (sessions that ended before summaries existed
# included), folds activity older than the horizon of a session that has not
# finished into its session checkpoint, and removes the activity events these
# records cover from the instance activity logs. The head's activity lock is
# held, then its event lock, so nothing is appended to either log meanwhile.
# Usage: session_maintain_head <project-id> <head-id> [now]
session_maintain_head() {
    _smh_project="$1" _smh_head="$2" _smh_now="${3:-$(date +%s)}"
    hydra_valid_id "$_smh_project" && hydra_valid_id "$_smh_head" || return 1
    _event_uint "$_smh_now" 4294967294 || return 1
    _smh_dir="$(state_v2_project_dir "$_smh_project")" || return 1
    _smh_heads="$_smh_dir/heads"
    _smh_head_dir="$_smh_heads/$_smh_head"
    [ -d "$_smh_head_dir" ] || return 0
    for _smh_path in "$_smh_dir" "$_smh_heads" "$_smh_head_dir" "$_smh_head_dir/instances" \
        "$_smh_dir/activity" "$_smh_dir/activity/coverage" "$_smh_dir/sessions"; do
        [ ! -L "$_smh_path" ] || return 1
    done
    _smh_coverage="$_smh_dir/activity/coverage"
    [ -f "$_smh_coverage" ] || _smh_coverage=""
    _smh_summaries="$_smh_dir/sessions"
    hydra_private_mkdir "$_smh_summaries" || return 1
    _smh_activity_lock="activity_${_smh_project}_${_smh_head}"
    _smh_events_lock="events_${_smh_project}_${_smh_head}"
    acquire_lock "$_smh_activity_lock" "summarise sessions" || return 1
    if ! acquire_lock "$_smh_events_lock" "summarise sessions"; then
        release_lock "$_smh_activity_lock"
        return 1
    fi
    _smh_status=0
    # Every file is read, however large: a summary or checkpoint made from
    # all of a log is what brings an oversized one back within the bounds.
    _smh_manifest="$( { _workflow_sessions_summary_names "$_smh_summaries"; _workflow_sessions_files "$_smh_head_dir" 1 3 4294967294; } |
        (umask 077; LC_ALL=C awk -v mode=maintain -v root="$_smh_heads/" -v now="$_smh_now" \
            -v coverage="$_smh_coverage" -v summaries="$_smh_summaries" -v cache="" -v writer="$$" \
            -v settled="$HYDRA_SESSIONS_SETTLED_SECONDS" -v stale="${HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS:-45}" \
            -v horizon="$HYDRA_SESSIONS_ACTIVITY_HORIZON_SECONDS" -v checkpoint_events=0 \
            -v max_rows=0 -v max_heads=1 -v max_candidates="$HYDRA_SESSIONS_MAX_CANDIDATES" -v max_summaries=0 \
            "$_WORKFLOW_SESSIONS_AWK"))" || _smh_status=1
    [ "$_smh_status" -ne 0 ] || _session_apply_manifest || _smh_status=1
    rm -f "$_smh_summaries"/*.next."$$" "$_smh_head_dir"/instances/*/session-checkpoint.next."$$" 2>/dev/null
    release_lock "$_smh_events_lock"
    release_lock "$_smh_activity_lock"
    return "$_smh_status"
}

# Applies what the maintain pass decided: a summary or checkpoint it wrote
# beside its final name replaces that file, and only then are the activity
# events it covers removed. Runs inside session_maintain_head's locks.
_session_apply_manifest() {
    _sam_tab="$(printf '\t')"
    _sam_status=0
    while IFS="$_sam_tab" read -r _sam_kind _sam_id _sam_through; do
        case "$_sam_kind" in summary|checkpoint|drop) ;; *) continue ;; esac
        case "$_sam_id" in instance_*) hydra_valid_id "$_sam_id" || continue ;; *) continue ;; esac
        _sam_dir="$_smh_head_dir/instances/$_sam_id"
        _sam_through_kept=4294967294
        case "$_sam_kind" in
            summary)
                mv -f "$_smh_summaries/$_sam_id.next.$$" "$_smh_summaries/$_sam_id" || { _sam_status=1; continue; }
                rm -f "$_sam_dir/session-checkpoint"
                ;;
            checkpoint)
                _event_uint "$_sam_through" 4294967294 || continue
                mv -f "$_sam_dir/session-checkpoint.next.$$" "$_sam_dir/session-checkpoint" || { _sam_status=1; continue; }
                _sam_through_kept="$_sam_through"
                ;;
            drop) rm -f "$_sam_dir/session-checkpoint" ;;
        esac
        event_drop_prefix_locked "$_sam_dir/activity.jsonl" "$_sam_through_kept" || _sam_status=1
    done <<EOF
$_smh_manifest
EOF
    return "$_sam_status"
}

# Maintains the sessions of a project's heads that keep activity logs or
# session checkpoints, at most once per HYDRA_SESSIONS_MAINTAIN_SECONDS, for at
# most HYDRA_SESSIONS_MAINTAIN_HEADS heads and within a time budget. The stamp
# file holds when the last pass began and where in the sorted heads the next
# one starts, so heads a pass did not reach come first in the next.
# Usage: session_maintain_due <project-id> [now]
session_maintain_due() {
    _smd_project="$1" _smd_now="${2:-$(date +%s)}"
    hydra_valid_id "$_smd_project" || return 1
    _event_uint "$_smd_now" 4294967294 || return 1
    _smd_dir="$(state_v2_project_dir "$_smd_project")" || return 1
    _smd_stamp="$_smd_dir/activity/maintained"
    [ -d "$_smd_dir/activity" ] && [ ! -L "$_smd_dir/activity" ] && [ ! -L "$_smd_stamp" ] || return 0
    _smd_last="$(sed -n '1p' "$_smd_stamp" 2>/dev/null || true)"
    _smd_next="$(sed -n '2p' "$_smd_stamp" 2>/dev/null || true)"
    _event_uint "$_smd_next" 4294967294 || _smd_next=0
    if _event_uint "$_smd_last" 4294967294 && [ "$_smd_last" -le "$_smd_now" ] &&
        [ $((_smd_now - _smd_last)) -lt "$HYDRA_SESSIONS_MAINTAIN_SECONDS" ]; then
        return 0
    fi
    _session_write_stamp "$_smd_stamp" "$_smd_now" "$_smd_next" || return 1
    _smd_heads="$(find "$_smd_dir/heads" -mindepth 4 -maxdepth 4 -type f \
        \( -name activity.jsonl -o -name session-checkpoint \) -print 2>/dev/null | LC_ALL=C sort |
        LC_ALL=C awk -v root="$_smd_dir/heads/" -v next_head="$_smd_next" -v most="$HYDRA_SESSIONS_MAINTAIN_HEADS" '
            index($0, root) == 1 {
                head = substr($0, length(root) + 1); sub(/\/.*/, "", head)
                if (head ~ /^head_[0-9a-f]+$/ && !(head in seen)) { seen[head] = 1; heads[++n] = head }
            }
            END {
                start = n ? next_head % n : 0
                for (i = 0; i < n && i < most; i++) print heads[(start + i) % n + 1]
            }')"
    # The observer stops a recorder after ten seconds; start no head after five.
    _smd_started="$(date +%s)" _smd_done=0
    for _smd_head in $_smd_heads; do
        [ $(($(date +%s) - _smd_started)) -lt "$HYDRA_SESSIONS_MAINTAIN_BUDGET_SECONDS" ] || break
        session_maintain_head "$_smd_project" "$_smd_head" "$_smd_now" || true
        _smd_done=$((_smd_done + 1))
    done
    _session_write_stamp "$_smd_stamp" "$_smd_now" $(((_smd_next + _smd_done) % 4294967294))
}

_session_write_stamp() {
    _sws_tmp="$(mktemp_adjacent "$1")" || return 1
    if ! printf '%s\n%s\n' "$2" "$3" > "$_sws_tmp" || ! atomic_replace "$1" "$_sws_tmp"; then
        rm -f "$_sws_tmp"
        return 1
    fi
}

# Keeps a finished session's summary in step with an outcome declared after
# the session ended.
# Usage: session_summary_set_outcome <project-id> <head-id> <instance-id> <outcome>
session_summary_set_outcome() {
    _ssso_project="$1" _ssso_head="$2" _ssso_instance="$3" _ssso_outcome="$4"
    hydra_valid_id "$_ssso_project" && hydra_valid_id "$_ssso_head" && hydra_valid_id "$_ssso_instance" || return 1
    case "$_ssso_outcome" in done|failed|blocked|abandoned|canceled) ;; *) return 1 ;; esac
    _ssso_file="$(state_v2_project_dir "$_ssso_project")/sessions/$_ssso_instance" || return 1
    [ -f "$_ssso_file" ] && [ ! -L "$_ssso_file" ] && [ ! -L "${_ssso_file%/*}" ] || return 0
    _ssso_lock="activity_${_ssso_project}_${_ssso_head}"
    acquire_lock "$_ssso_lock" "update session summary" || return 1
    _ssso_tmp="$(mktemp_adjacent "$_ssso_file")" || { release_lock "$_ssso_lock"; return 1; }
    if ! awk -v outcome="$_ssso_outcome" 'BEGIN { FS = OFS = "\t" } $1 == "outcome" && NF == 2 { $2 = outcome } { print }' \
            "$_ssso_file" > "$_ssso_tmp" || ! atomic_replace "$_ssso_file" "$_ssso_tmp"; then
        rm -f "$_ssso_tmp"
        release_lock "$_ssso_lock"
        return 1
    fi
    release_lock "$_ssso_lock"
}

# shellcheck disable=SC2016 # An awk program, not shell expansions.
_WORKFLOW_SESSIONS_AWK='
function uint(value) { return value ~ /^[0-9]+$/ && length(value) <= 10 }
function or_dash(value) { return (value == "") ? "-" : value }
function or_empty(value) { return (value == "-") ? "" : value }
function time_or_dash(value) { return value == "-" || uint(value) }
function larger(a, b) { return (a + 0 > b + 0) ? a + 0 : b + 0 }

# "2026-11-10T14:02:59Z" as seconds since the epoch; 0 when malformed.
function epoch(iso,    y, m, d) {
    if (iso !~ /^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z$/) return 0
    y = substr(iso, 1, 4) + 0; m = substr(iso, 6, 2) + 0; d = substr(iso, 9, 2) + 0
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return 0
    if (m <= 2) { y--; m += 12 }
    return (365 * y + int(y / 4) - int(y / 100) + int(y / 400) + int((153 * (m - 3) + 2) / 5) + d - 719469) * 86400 \
        + substr(iso, 12, 2) * 3600 + substr(iso, 15, 2) * 60 + substr(iso, 18, 2)
}

# The string value of "key" in text; empty when absent or escaped.
function text_field(text, key,    at, rest, close_at) {
    at = index(text, "\"" key "\":\"")
    if (!at) return ""
    rest = substr(text, at + length(key) + 4, 300)
    close_at = index(rest, "\"")
    if (!close_at || index(substr(rest, 1, close_at), "\\")) return ""
    return substr(rest, 1, close_at - 1)
}

function number_field(text, key,    at, rest) {
    at = index(text, "\"" key "\":")
    if (!at) return ""
    rest = substr(text, at + length(key) + 3, 12)
    if (!match(rest, /^[0-9]+/)) return ""
    return substr(rest, 1, RLENGTH)
}

function load_coverage(    line, parts, count, ok) {
    intervals = 0; unknown_before = 0
    if (coverage == "") return
    ok = (getline line < coverage) > 0 && split(line, parts, "\t") == 3 &&
        parts[1] == "HYDRA_ACTIVITY_COVERAGE" && parts[2] == "1" && uint(parts[3])
    if (ok) unknown_before = parts[3] + 0
    while (ok && (getline line < coverage) > 0) {
        count = split(line, parts, "\t")
        if (count != 2 || !uint(parts[1]) || !uint(parts[2]) || parts[2] + 0 < parts[1] + 0 ||
            (intervals && parts[1] + 0 < watch_end[intervals]) || intervals >= 4096) { ok = 0; break }
        intervals++; watch_start[intervals] = parts[1] + 0; watch_end[intervals] = parts[2] + 0
    }
    close(coverage)
    if (!ok) {
        # Coverage that cannot be trusted makes observed time unknown.
        coverage_bad = 1
        intervals = 0; unknown_before = now + 1
        if (mode == "feed") print "X\tActivity coverage unreadable; observed time unknown"
    }
}

# Seconds of [from, to) during which an observer was watching.
function watched(from, to,    low, high, mid, total, a, b) {
    low = 1; high = intervals + 1
    while (low < high) {
        mid = int((low + high) / 2)
        if (watch_end[mid] <= from) low = mid + 1; else high = mid
    }
    total = 0
    for (; low <= intervals && watch_start[low] < to; low++) {
        a = (watch_start[low] > from) ? watch_start[low] : from
        b = (watch_end[low] < to) ? watch_end[low] : to
        if (b > a) total += b - a
    }
    return total
}

# Whether the coverage of every moment up to "at" is final: an observer
# extends its newest interval only by reporting within stale seconds of its end.
function coverage_final(at) {
    return !intervals || watch_end[intervals] >= at || now >= watch_end[intervals] + stale
}

function seconds(id, kind) { return ((id, kind) in spent) ? spent[id, kind] : 0 }
function cursor_e(id) { return (id in cur_e) ? cur_e[id] : 0 }
function cursor_a(id) { return (id in cur_a) ? cur_a[id] : 0 }

# Accounts the time up to "to" under the state in force, then moves on.
function advance(id, to,    length_s, seen) {
    if (!(id in started) || done[id] || started[id] == "-" || to <= cursor[id]) return
    length_s = to - cursor[id]
    if (reported[id] != "") {
        spent[id, reported[id]] += length_s; reported_s[id] += length_s
    } else if (inferred[id] != "") {
        seen = watched(cursor[id], to)
        spent[id, inferred[id]] += seen; spent[id, "unobserved"] += length_s - seen
    } else spent[id, "unobserved"] += length_s
    cursor[id] = to
}

function enroll(id) {
    if (candidates >= max_candidates) { overflow = 1; return 0 }
    order[++candidates] = id; head_id[++head_ids] = id
    return 1
}

function identity(id, profile, mode_name) {
    head_of[id] = head; branch_of[id] = branch
    profile_of[id] = (profile ~ /^[A-Za-z0-9._-]+$/ && length(profile) < 40) ? profile : "-"
    mode_of[id] = (mode_name == "headless") ? "headless" : "interactive"
}

function begin(id, at, profile, mode_name) {
    if (id in started) {
        # A start found after later events of a session whose beginning was unknown.
        if (started[id] != "-") return
    } else if (!enroll(id)) return
    started[id] = at; cursor[id] = at; ended[id] = "-"; status[id] = "open"; done[id] = 0
    identity(id, profile, mode_name)
    if (!(id in outcome)) outcome[id] = "-"
    inferred[id] = ""; reported[id] = ""
}

# A session whose start is no longer in the log is still a session.
function known(id) {
    if (id in started) return 1
    if (id in failed) return 0
    begin(id, "-", "-", "")
    return id in started
}

function finish(id, at, how) {
    if (!(id in started) || done[id]) return
    advance(id, at)
    ended[id] = at; status[id] = how; done[id] = 1
}

function forget(id) {
    delete started[id]; delete done[id]; delete outcome[id]; delete dropped[id]; delete reported_s[id]
    delete spent[id, "active"]; delete spent[id, "quiet"]; delete spent[id, "unobserved"]
    delete cur_e[id]; delete cur_a[id]; delete damaged[id]
}

# One event as a record: the kind, the instance, then what the kind needs.
#   S id at profile mode            started
#   M id at profile mode previous   resumed, superseding previous
#   F id                            spawn failed: never a session
#   T id at                         torn down
#   D id outcome                    declared outcome
#   O id at state                   observation; active or quiet when an adapter reported it
#   A id at since state             inferred activity that began at since
# Empty for anything else, including a torn or oversized line.
function record_of(line,    type, id, at, payload, source, state) {
    if (length(line) > 32768 || substr(line, 1, 20) != "{\"schema_version\":1," || substr(line, length(line) - 1) != "}}") return ""
    type = text_field(line, "type")
    id = text_field(line, "instance_id")
    at = epoch(text_field(line, "occurred_at"))
    if (id !~ /^instance_[0-9a-f]+$/ || length(id) > 79 || !at) return ""
    payload = substr(line, index(line, "\"payload\":{"))
    if (type == "lifecycle.started")
        return "S\t" id "\t" at "\t" or_dash(text_field(payload, "profile")) "\t" or_dash(text_field(payload, "terminal_mode"))
    if (type == "lifecycle.resumed")
        return "M\t" id "\t" at "\t" or_dash(text_field(payload, "profile")) "\t" or_dash(text_field(payload, "terminal_mode")) "\t" or_dash(text_field(payload, "previous_instance_id"))
    if (type == "lifecycle.spawn-failed") return "F\t" id
    if (type == "lifecycle.torn-down") return "T\t" id "\t" at
    if (type == "lifecycle.declared") return "D\t" id "\t" or_dash(text_field(payload, "outcome"))
    if (type == "lifecycle.observed") {
        source = text_field(substr(line, index(line, "\"actor\":{"), 120), "id")
        state = text_field(payload, "status")
        if (source == "adapter" && state == "running") return "O\t" id "\t" at "\tactive"
        if (source == "adapter" && state == "idle") return "O\t" id "\t" at "\tquiet"
        return "O\t" id "\t" at "\t-"
    }
    if (type == "activity.changed")
        return "A\t" id "\t" at "\t" or_dash(number_field(payload, "since")) "\t" or_dash(text_field(payload, "state"))
    return ""
}

# Whether a record of an instance is already accounted for: by its summary, or
# by the checkpoint it was restored from, through head event cur_e and
# activity event cur_a.
function covered(id, stream, position) {
    if (id in summarised) return 1
    return position <= ((stream == "h") ? cursor_e(id) : cursor_a(id))
}

function apply(record, stream, position,    f, n, id, at, since) {
    n = split(record, f, "\t")
    id = f[2]
    if (f[1] == "M") {
        at = f[3] + 0
        if (f[6] != "-" && !covered(f[6], stream, position)) finish(f[6], at, "superseded")
        if (!covered(id, stream, position)) begin(id, at, f[4], f[5])
        return
    }
    if (covered(id, stream, position)) return
    if (f[1] == "F") {
        if (!(id in failed)) { failed[id] = 1; failed_id[++head_failed] = id }
        if ((id in started) && started[id] == "-") dropped[id] = 1
        return
    }
    if (f[1] == "D") {
        if (f[3] ~ /^(done|failed|blocked|abandoned|canceled)$/ && known(id)) outcome[id] = f[3]
        return
    }
    at = f[3] + 0
    if (f[1] == "S") begin(id, at, f[4], f[5])
    else if (f[1] == "T") { if (known(id)) finish(id, at, "ended") }
    else if (f[1] == "O") {
        if (!known(id)) return
        # An adapter report holds until the next observation of the instance.
        advance(id, at)
        reported[id] = or_empty(f[4])
    } else if (f[1] == "A") {
        if (!uint(f[4]) || (f[5] != "active" && f[5] != "quiet") || !known(id)) return
        # The state began at "since", not when it was noticed.
        since = (f[4] + 0 > at) ? at : f[4] + 0
        advance(id, since)
        inferred[id] = f[5]
    }
}

# A checkpoint row: what an instance s records add up to, through head event
# e and activity event a:
#   I id started ended status done profile mode outcome cursor inferred
#     reported active quiet unobserved reported-seconds dropped e a
function row(id, e, a) {
    return sprintf("I\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d", id, started[id], ended[id],
        status[id], done[id], profile_of[id], mode_of[id], outcome[id], cursor[id], or_dash(inferred[id]), or_dash(reported[id]),
        seconds(id, "active"), seconds(id, "quiet"), seconds(id, "unobserved"), reported_s[id] + 0, (id in dropped) ? 1 : 0, e, a)
}

function row_valid(f, n) {
    return n == 19 && f[1] == "I" && f[2] ~ /^instance_[0-9a-f]+$/ && length(f[2]) <= 79 && time_or_dash(f[3]) &&
        time_or_dash(f[4]) && f[5] ~ /^(open|ended|superseded)$/ && f[6] ~ /^[01]$/ && f[8] ~ /^(interactive|headless)$/ &&
        f[9] ~ /^(done|failed|blocked|abandoned|canceled|-)$/ && uint(f[10]) && f[11] ~ /^(active|quiet|-)$/ &&
        f[12] ~ /^(active|quiet|-)$/ && uint(f[13]) && uint(f[14]) && uint(f[15]) && uint(f[16]) && f[17] ~ /^[01]$/ &&
        uint(f[18]) && uint(f[19])
}

function load_row(f,    id) {
    id = f[2]
    if (!(id in started) && !enroll(id)) return
    identity(id, f[7], f[8])
    started[id] = (f[3] == "-") ? "-" : f[3] + 0
    ended[id] = (f[4] == "-") ? "-" : f[4] + 0; status[id] = f[5]; done[id] = f[6] + 0
    outcome[id] = f[9]; cursor[id] = f[10] + 0; inferred[id] = or_empty(f[11]); reported[id] = or_empty(f[12])
    spent[id, "active"] = f[13] + 0; spent[id, "quiet"] = f[14] + 0; spent[id, "unobserved"] = f[15] + 0
    reported_s[id] = f[16] + 0
    if (f[17] == "1") dropped[id] = 1; else delete dropped[id]
    cur_e[id] = f[18] + 0; cur_a[id] = f[19] + 0
}

# The feed checkpoint of a head:
#   HYDRA_SESSION_CHECKPOINT 2 <through-sequence>
#   I rows, and F id for a failed spawn (never a session)
#   Z <rows>
# A row is used unless a session checkpoint already restored for the
# instance covers more. Returns the head sequence covered, or 0.
function restore(path,    line, n, i, f, count, through, id, seen) {
    if (!(path in checkpoints)) return 0
    n = 0; through = 0
    if ((getline line < path) > 0 && split(line, f, "\t") == 3 && f[1] == "HYDRA_SESSION_CHECKPOINT" && f[2] == "2" && uint(f[3])) {
        through = f[3] + 0
        while ((getline line < path) > 0) kept_line[++n] = line
    }
    close(path)
    if (!through || n < 1 || split(kept_line[n], f, "\t") != 2 || f[1] != "Z" || f[2] != n - 1) return 0
    for (i = 1; i < n; i++) {
        count = split(kept_line[i], f, "\t")
        if (count == 2 && f[1] == "F" && f[2] ~ /^instance_[0-9a-f]+$/) continue
        if (!row_valid(f, count) || (f[2] in seen)) return 0
        seen[f[2]] = 1
    }
    for (i = 1; i < n; i++) {
        split(kept_line[i], f, "\t"); id = f[2]
        if (f[1] == "F") { if (!(id in failed)) { failed[id] = 1; failed_id[++head_failed] = id }; continue }
        if (id in summarised) continue
        if ((id in started) && (f[18] + 0 < cur_e[id] || f[19] + 0 < cur_a[id])) continue
        load_row(f)
    }
    return through
}

# Session checkpoints of the head s open sessions:
#   HYDRA_SESSION_CHECKPOINT 2 <e>, one I row, Z 1
# Activity events they cover may be gone, so an unreadable one leaves its
# session s times unknown rather than counting what remains.
function read_sessions(    k, id, path, line, n, f) {
    for (k = 1; k <= durables; k++) {
        id = durable_id[k]; path = durable_file[k]; durable_row[k] = ""
        n = 0
        if ((getline line < path) > 0 && split(line, f, "\t") == 3 && f[1] == "HYDRA_SESSION_CHECKPOINT" && f[2] == "2" && uint(f[3]))
            while (n < 3 && (getline line < path) > 0) kept_line[++n] = line
        close(path)
        if (n == 2 && kept_line[2] == "Z\t1") durable_row[k] = kept_line[1]
    }
}

function load_sessions(    k, id, f, count) {
    for (k = 1; k <= durables; k++) {
        id = durable_id[k]
        if (id in summarised) continue
        count = split(durable_row[k], f, "\t")
        if (!row_valid(f, count) || f[2] != id || (id in started)) { damaged[id] = 1; continue }
        load_row(f)
    }
}

# Activity events missing between an instance s checkpoint and its log were
# removed without a record of them, so the session s times are unknown.
function check_gaps(    k) {
    for (k = 1; k <= afiles; k++)
        if (alog_first[k] <= alog_last[k] && aseq_of[alog_first[k]] > cursor_a(aid[k]) + 1) damaged[aid[k]] = 1
}

# A summary written after the head s files were listed replaces its session.
function probe_summaries(    k) {
    if (summaries == "") return
    for (k = 1; k <= afiles; k++) probe_summary(aid[k])
    for (k = 1; k <= durables; k++) probe_summary(durable_id[k])
}

function probe_summary(id,    line) {
    if ((id in summarised) || (id in probed)) return
    probed[id] = 1
    if ((getline line < (summaries "/" id)) > 0) {
        summarised[id] = 1
        if (mode == "feed") read_summary(id)
    }
    close(summaries "/" id)
}

# The last sequence number an archive holds, from its name
# (events-<stamp>-<pid>-<first>-<last>[-<n>].jsonl); 0 when it does not say.
function archive_last(path,    name, f, n) {
    name = path; sub(/^.*\//, "", name); sub(/\.jsonl$/, "", name)
    n = split(name, f, "-")
    return (n >= 5 && uint(f[4]) && uint(f[5])) ? f[5] + 0 : 0
}

# Collects the head s activity logs: every activity event with its sequence
# number and the head event it precedes.
function collect_activity(    k, line, sequence, position) {
    apending = 0
    for (k = 1; k <= afiles; k++) {
        alog_first[k] = apending + 1
        while ((getline line < afile[k]) > 0) {
            if (!index(line, "\"type\":\"activity.changed\"")) continue
            sequence = number_field(line, "sequence"); position = number_field(line, "head_next_sequence")
            if (!uint(sequence) || !uint(position)) continue
            apending++; aline[apending] = line; aseq_of[apending] = sequence + 0; ahpos[apending] = position + 0; alog[apending] = k
        }
        close(afile[k])
        alog_last[k] = apending
    }
}

# Collects the head events after sequence "lower"; top is the newest
# sequence number its files hold.
function collect(lower,    i, line, at, sequence, last) {
    pending = 0; top = 0
    for (i = 1; i <= files; i++) {
        last = file_archive[i] ? archive_last(file[i]) : 0
        if (last && last <= lower) { if (last > top) top = last; continue }
        while ((getline line < file[i]) > 0) {
            at = index(line, "\"sequence\":")
            if (!at) continue
            sequence = substr(line, at + 11, 10) + 0
            if (sequence > top) top = sequence
            if (sequence <= lower) continue
            if (index(line, "\"type\":\"lifecycle.") || index(line, "\"type\":\"activity.changed\"")) {
                pending++; event_line[pending] = line; event_sequence[pending] = sequence
            }
        }
        close(file[i])
    }
}

# The activity event that comes next: the one preceding the lowest head event.
function next_activity(    k, best) {
    best = 0
    for (k = 1; k <= afiles; k++)
        if (afront[k] <= alog_last[k] && (!best || ahpos[afront[k]] < ahpos[afront[best]])) best = k
    return best ? afront[best]++ : 0
}

# Orders head and activity events as they were written: an activity event
# before the head event numbered head_next_sequence. Negative entries are
# activity events.
function merge(    i, j, k, n) {
    n = 0
    for (k = 1; k <= afiles; k++) afront[k] = alog_first[k]
    j = next_activity()
    for (i = 1; i <= pending; i++) {
        while (j && ahpos[j] <= event_sequence[i]) { merged[++n] = -j; j = next_activity() }
        merged[++n] = i
    }
    for (; j; j = next_activity()) merged[++n] = -j
    merged_count = n
}

function checkpoint(path, through,    i, id, rows) {
    print "HYDRA_SESSION_CHECKPOINT\t2\t" through > path
    rows = 0
    for (i = 1; i <= head_ids; i++) {
        id = head_id[i]
        print row(id, larger(cursor_e(id), through), larger(cursor_a(id), passed[id])) > path
        rows++
    }
    for (i = 1; i <= head_failed; i++) { print "F\t" failed_id[i] > path; rows++ }
    print "Z\t" rows > path
    close(path)
}

# What every instance adds up to at this point of the merged events. The feed
# keeps it in its cache; the maintain pass may keep it as session checkpoints.
function snapshot(fresh,    i, id) {
    if (mode == "feed") {
        if (fresh >= checkpoint_events) checkpoint(cache "/" head ".tsv.next." writer, at_head)
        return
    }
    for (i = 1; i <= head_ids; i++) {
        id = head_id[i]
        snap_e[id] = larger(cursor_e(id), at_head); snap_a[id] = larger(cursor_a(id), passed[id])
        snap_row[id] = row(id, snap_e[id], snap_a[id])
    }
}

# Applies the merged events in order. Events from "settle_from" on can still
# change meaning (or, when maintaining, are within the activity horizon), so
# the snapshot is taken before the first of them.
function walk(    m, x, j, id, record, f, stream, position, settle_from, fresh, done_snapshot, k) {
    split("", passed); at_head = lower
    fresh = 0
    done_snapshot = (mode == "feed") ? (cache == "" || coverage_bad) : coverage_bad
    settle_from = (mode == "feed") ? now - settled : now - horizon
    for (m = 1; m <= merged_count; m++) {
        x = merged[m]
        if (x < 0) {
            j = -x; stream = "a"; position = aseq_of[j]; id = aid[alog[j]]
            record = record_of(aline[j])
            if (record != "" && (split(record, f, "\t") < 2 || f[1] != "A" || f[2] != id)) record = ""
        } else {
            stream = "h"; position = event_sequence[x]
            record = record_of(event_line[x])
            if (record != "") split(record, f, "\t")
        }
        if (record != "" && !done_snapshot && f[1] != "F" && f[1] != "D" && f[3] + 0 >= settle_from) {
            snapshot(fresh); done_snapshot = 1
        }
        if (stream == "a") passed[id] = position; else at_head = position
        if (record == "") continue
        apply(record, stream, position)
        fresh++
    }
    if (!done_snapshot) {
        if (top > at_head) at_head = top
        for (k = 1; k <= afiles; k++) if (alog_last[k] >= alog_first[k]) passed[aid[k]] = aseq_of[alog_last[k]]
        snapshot(fresh)
    }
}

# Interprets one head: its session checkpoints and the feed s checkpoint,
# then every newer event. Activity logs are read before the session
# checkpoints, and those before a late summary is looked for: a maintain pass
# writes a checkpoint before it trims the log, and a summary before it removes
# both, so whatever was read stays consistent.
function read_head(use_cache,    i, through) {
    if (head == "") return
    head_ids = 0; head_failed = 0
    split("", snap_a)
    collect_activity()
    read_sessions()
    probe_summaries()
    load_sessions()
    through = (use_cache && cache != "") ? restore(cache "/" head ".tsv") : 0
    check_gaps()
    lower = through
    for (i = 1; i <= head_ids && lower; i++) if (cursor_e(head_id[i]) < lower) lower = cursor_e(head_id[i])
    collect(lower)
    if (through && top < through) {
        # The log no longer reaches the checkpoint (it was repaired): start over.
        for (i = 1; i <= head_ids; i++) forget(head_id[i])
        for (i = 1; i <= head_failed; i++) delete failed[failed_id[i]]
        candidates -= head_ids
        read_head(0)
        return
    }
    merge()
    walk()
    if (mode == "maintain") maintain()
}

# Time before the coverage history begins is unknown, and a session nobody
# observed has unknown active and quiet time: neither is reported as zero.
function emit_values(id) {
    v_active = "-"; v_quiet = "-"; v_unobserved = "-"; v_reported = "-"; v_coverage = "unknown"
    if (started[id] != "-" && started[id] >= unknown_before && !(id in damaged)) {
        v_unobserved = seconds(id, "unobserved"); v_reported = reported_s[id] + 0; v_coverage = "unobserved"
        if (seconds(id, "active") + seconds(id, "quiet") > 0) {
            v_active = seconds(id, "active"); v_quiet = seconds(id, "quiet"); v_coverage = "observed"
        }
    }
}

function emit(id) {
    emit_values(id)
    printf "H\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", head_of[id], id, branch_of[id], profile_of[id],
        mode_of[id], status[id], started[id], ended[id], outcome[id], v_active, v_quiet, v_unobserved, v_reported
}

# A summary is what the feed shows for a finished session:
#   HYDRA_SESSION_SUMMARY 1, then one "key<TAB>value" line per field.
function write_summary(id, path) {
    emit_values(id)
    print "HYDRA_SESSION_SUMMARY\t1" > path
    print "head\t" head_of[id] > path
    print "instance\t" id > path
    print "branch\t" branch_of[id] > path
    print "profile\t" profile_of[id] > path
    print "terminal\t" mode_of[id] > path
    print "state\t" status[id] > path
    print "started\t" started[id] > path
    print "ended\t" ended[id] > path
    print "outcome\t" outcome[id] > path
    print "active\t" v_active > path
    print "quiet\t" v_quiet > path
    print "unobserved\t" v_unobserved > path
    print "reported\t" v_reported > path
    print "coverage\t" v_coverage > path
    print "summarised\t" now > path
    close(path)
}

function read_summary(id,    path, line, n, f, kv, ok) {
    path = summaries "/" id
    split("", kv); n = 0; ok = 0
    if ((getline line < path) > 0 && line == "HYDRA_SESSION_SUMMARY\t1") {
        ok = 1
        while ((getline line < path) > 0)
            if (++n > 32 || split(line, f, "\t") != 2 || (f[1] in kv)) { ok = 0; break } else kv[f[1]] = f[2]
    }
    close(path)
    if (!ok || kv["instance"] != id || kv["head"] !~ /^head_[0-9a-f]+$/ || length(kv["head"]) > 79 ||
        kv["branch"] == "" || length(kv["branch"]) > 120 || kv["branch"] ~ /[\001-\037\177]/ ||
        kv["profile"] !~ /^[A-Za-z0-9._-]+$/ || length(kv["profile"]) >= 40 ||
        kv["terminal"] !~ /^(interactive|headless)$/ || kv["state"] !~ /^(ended|superseded)$/ ||
        !time_or_dash(kv["started"]) || !uint(kv["ended"]) || kv["outcome"] !~ /^(done|failed|blocked|abandoned|canceled|-)$/ ||
        !time_or_dash(kv["active"]) || !time_or_dash(kv["quiet"]) || !time_or_dash(kv["unobserved"]) ||
        !time_or_dash(kv["reported"]) || kv["coverage"] !~ /^(observed|unobserved|unknown)$/ || !uint(kv["summarised"])) {
        bad_summaries++
        return
    }
    summary_row[id] = "H\t" kv["head"] "\t" id "\t" kv["branch"] "\t" kv["profile"] "\t" kv["terminal"] "\t" kv["state"] "\t" \
        kv["started"] "\t" kv["ended"] "\t" kv["outcome"] "\t" kv["active"] "\t" kv["quiet"] "\t" kv["unobserved"] "\t" kv["reported"]
    summary_key[id] = (kv["started"] == "-") ? 0 : kv["started"] + 0
    summary_id[++summaries_read] = id
}

# Decides, for the one head being maintained, which sessions are summarised
# and which session checkpoints are rewritten. Files are written beside their
# final names; the shell renames them and trims the activity logs.
function maintain(    i, id, k, path) {
    if (coverage_bad) return
    for (i = 1; i <= head_ids; i++) {
        id = head_id[i]
        if (id in dropped) continue
        if (done[id] && (started[id] == "-" || coverage_final(ended[id]))) {
            write_summary(id, summaries "/" id ".next." writer)
            print "summary\t" id
        } else if (!(id in damaged) && (id in snap_a) && snap_a[id] > cursor_a(id)) {
            path = root head "/instances/" id "/session-checkpoint.next." writer
            print "HYDRA_SESSION_CHECKPOINT\t2\t" snap_e[id] > path
            print snap_row[id] > path
            print "Z\t1" > path
            close(path)
            print "checkpoint\t" id "\t" snap_a[id]
        }
    }
    for (k = 1; k <= afiles; k++) if (aid[k] in summarised) print "drop\t" aid[k]
    for (k = 1; k <= durables; k++) if (durable_id[k] in summarised) print "drop\t" durable_id[k]
}

# Keeps the newest sessions: an insertion into a bounded, sorted selection,
# newest start first and, for equal starts, by instance.
function consider(key, id,    at) {
    total++
    for (at = kept; at >= 1 && (sort_key[at] < key || (sort_key[at] == key && chosen[at] < id)); at--)
        if (at < max_rows) { chosen[at + 1] = chosen[at]; sort_key[at + 1] = sort_key[at] }
    if (at < max_rows) { chosen[at + 1] = id; sort_key[at + 1] = key; if (kept < max_rows) kept++ }
}

BEGIN {
    now += 0
    load_coverage()
    count = split(ENVIRON["HYDRA_SESSION_CHECKPOINTS"], listed, "\n")
    for (i = 1; i <= count; i++) if (listed[i] != "") checkpoints[listed[i]] = 1
    head = ""
    while ((getline path) > 0) {
        if (substr(path, 1, 8) == "summary ") {
            # Listed newest first; all are finished sessions, the first are read.
            id = substr(path, 9)
            if (id !~ /^instance_[0-9a-f]+$/ || length(id) > 79 || (id in summarised)) continue
            summarised[id] = 1
            if (mode != "feed") continue
            if (listed_summaries++ < max_summaries) read_summary(id); else unread_summaries++
            continue
        }
        if (substr(path, 1, length(root)) != root) continue
        parts = split(substr(path, length(root) + 1), part, "/")
        if (part[1] !~ /^head_[0-9a-f]+$/ || length(part[1]) > 79) continue
        if (part[1] != head) {
            read_head(1)
            head = ""
            if (heads >= max_heads) { if (!truncated_heads++ && mode == "feed") print "X\tMore than " max_heads " heads; sessions are a partial sample"; continue }
            heads++; head = part[1]; branch = "-"; files = 0; afiles = 0; durables = 0
        }
        if (parts == 2 && part[2] == "branch") {
            if ((getline branch < path) <= 0) branch = "-"
            close(path)
            gsub(/[\001-\037\177]/, "?", branch)
            if (length(branch) > 120) branch = substr(branch, 1, 117) "..."
            if (branch == "") branch = "-"
        } else if ((parts == 3 && part[2] == "events" && part[3] == "events.jsonl") ||
                   (parts == 4 && part[2] == "events" && part[3] == "archive" && part[4] ~ /^events-.*\.jsonl$/)) {
            file[++files] = path; file_archive[files] = (parts == 4)
        } else if (parts == 4 && part[2] == "instances" && part[3] ~ /^instance_[0-9a-f]+$/ && length(part[3]) <= 79) {
            if (part[4] == "activity.jsonl") { afile[++afiles] = path; aid[afiles] = part[3] }
            else if (part[4] == "session-checkpoint") { durable_file[++durables] = path; durable_id[durables] = part[3] }
        }
    }
    read_head(1)
    if (mode != "feed") exit 0
    if (overflow) print "X\tMore than " max_candidates " head sessions recorded; partial sample"
    if (unread_summaries) print "X\tMore than " max_summaries " finished sessions summarised; reading the most recent"
    if (bad_summaries) print "X\tSkipped an unreadable session summary"
    for (id in damaged) if (!(id in summarised)) { print "X\tA session checkpoint is missing or unreadable; its times are unknown"; break }
    kept = 0
    for (i = 1; i <= summaries_read; i++) consider(summary_key[summary_id[i]], summary_id[i])
    for (i = 1; i <= candidates; i++) {
        id = order[i]
        if (id in dropped) continue
        if (!done[id]) advance(id, now)
        consider((started[id] == "-") ? 0 : started[id], id)
    }
    if (total > max_rows) print "X\tMore than " max_rows " head sessions; showing the newest"
    for (i = 1; i <= kept; i++) if (chosen[i] in summary_row) print summary_row[chosen[i]]; else emit(chosen[i])
}'
