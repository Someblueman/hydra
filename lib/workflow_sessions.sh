#!/bin/sh
# Head sessions for the statistics feed: one bounded row per head instance of
# this project, derived from each head's event log (and its unexpired archives)
# and from the observer coverage file. Hydra state is only read.
#
# A session is one instance: it starts at lifecycle.started or
# lifecycle.resumed and ends when it is torn down or superseded by a resume.
# Time inside it is active or quiet where an adapter reported running or idle,
# or where an observer was watching and inferred it from tmux output
# (activity.changed); everything else is unobserved.
#
# The feed is polled every two seconds, so what a head's older events add up
# to is kept as a checkpoint under $HYDRA_HOME/cache/sessions and only newer
# events are interpreted again. A checkpoint covers events at least
# HYDRA_SESSIONS_SETTLED_SECONDS old, whose observer coverage can no longer
# change. It is disposable derived data: without it, or when it does not
# validate, the whole history is interpreted.

# Newest sessions kept in the feed; the native reader holds as many.
HYDRA_SESSIONS_MAX_ROWS=256
# Heads whose history is read, and sessions considered before the newest are chosen.
HYDRA_SESSIONS_MAX_HEADS=512
HYDRA_SESSIONS_MAX_CANDIDATES=4096
# Event files larger than this many 512-byte blocks (8 MiB) are not read.
HYDRA_SESSIONS_MAX_LOG_BLOCKS=16384
# Events older than this are final: an observer extends its coverage only
# within HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS (45) of its last report.
HYDRA_SESSIONS_SETTLED_SECONDS=120
# Newly settled events that make a head's checkpoint worth rewriting.
HYDRA_SESSIONS_CHECKPOINT_EVENTS=64

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
    # then its archives in creation order, then its live log.
    find "$_wsn_heads" -mindepth 2 -maxdepth 4 -type f \
        \( -name branch -o -name '*.jsonl' \) ! -size +"$HYDRA_SESSIONS_MAX_LOG_BLOCKS" -print 2>/dev/null |
        LC_ALL=C sort |
        (umask 077; HYDRA_SESSION_CHECKPOINTS="$_wsn_checkpoints" LC_ALL=C awk -v root="$_wsn_heads/" -v now="$_wsn_now" \
            -v coverage="$_wsn_coverage" -v cache="$_wsn_cache" -v writer="$$" \
            -v settled="$HYDRA_SESSIONS_SETTLED_SECONDS" -v checkpoint_events="$HYDRA_SESSIONS_CHECKPOINT_EVENTS" \
            -v max_rows="$HYDRA_SESSIONS_MAX_ROWS" -v max_heads="$HYDRA_SESSIONS_MAX_HEADS" \
            -v max_candidates="$HYDRA_SESSIONS_MAX_CANDIDATES" "$_WORKFLOW_SESSIONS_AWK")
    [ -n "$_wsn_cache" ] || return 0
    for _wsn_next in "$_wsn_cache"/*.next."$$"; do
        [ -f "$_wsn_next" ] || continue
        mv -f "$_wsn_next" "${_wsn_next%.next."$$"}" 2>/dev/null || rm -f "$_wsn_next"
    done
}

# shellcheck disable=SC2016 # An awk program, not shell expansions.
_WORKFLOW_SESSIONS_AWK='
function uint(value) { return value ~ /^[0-9]+$/ && length(value) <= 10 }
function or_dash(value) { return (value == "") ? "-" : value }
function or_empty(value) { return (value == "-") ? "" : value }
function time_or_dash(value) { return value == "-" || uint(value) }

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
        print "X\tActivity coverage unreadable; observed time unknown"
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

function seconds(id, kind) { return ((id, kind) in spent) ? spent[id, kind] : 0 }

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

function begin(id, at, profile, mode) {
    if (id in started) {
        # A start found after later events of a session whose beginning was unknown.
        if (started[id] != "-") return
    } else {
        if (candidates >= max_candidates) { overflow = 1; return }
        order[++candidates] = id; head_id[++head_ids] = id
    }
    started[id] = at; cursor[id] = at; ended[id] = "-"; status[id] = "open"; done[id] = 0
    head_of[id] = head; branch_of[id] = branch
    profile_of[id] = (profile ~ /^[A-Za-z0-9._-]+$/ && length(profile) < 40) ? profile : "-"
    mode_of[id] = (mode == "headless") ? "headless" : "interactive"
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

function apply(record,    f, n, id, at, since) {
    n = split(record, f, "\t")
    id = f[2]
    if (f[1] == "F") {
        failed[id] = 1; failed_id[++head_failed] = id
        if ((id in started) && started[id] == "-") dropped[id] = 1
        return
    }
    if (f[1] == "D") {
        if (f[3] ~ /^(done|failed|blocked|abandoned|canceled)$/ && known(id)) outcome[id] = f[3]
        return
    }
    at = f[3] + 0
    if (f[1] == "S") begin(id, at, f[4], f[5])
    else if (f[1] == "M") {
        if (f[6] != "-") finish(f[6], at, "superseded")
        begin(id, at, f[4], f[5])
    } else if (f[1] == "T") { if (known(id)) finish(id, at, "ended") }
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

# A checkpoint holds what the events of a head up to one sequence number add up to:
#   HYDRA_SESSION_CHECKPOINT 1 <through-sequence>
#   I id started ended status done profile mode outcome cursor inferred reported
#     active quiet unobserved reported-seconds dropped
#   F id                         a failed spawn, never a session
#   Z <rows>
# Returns the sequence it covers after restoring it, or 0 with nothing restored.
function restore(path,    line, n, i, f, through, id) {
    if (!(path in checkpoints)) return 0
    n = 0; through = 0
    if ((getline line < path) > 0 && split(line, f, "\t") == 3 && f[1] == "HYDRA_SESSION_CHECKPOINT" && f[2] == "1" && uint(f[3])) {
        through = f[3] + 0
        while ((getline line < path) > 0) kept_line[++n] = line
    }
    close(path)
    if (!through || n < 1 || split(kept_line[n], f, "\t") != 2 || f[1] != "Z" || f[2] != n - 1) return 0
    for (i = 1; i < n; i++) {
        if (split(kept_line[i], f, "\t") == 2 && f[1] == "F" && f[2] ~ /^instance_[0-9a-f]+$/) continue
        if (split(kept_line[i], f, "\t") != 17 || f[1] != "I" || f[2] !~ /^instance_[0-9a-f]+$/ || length(f[2]) > 79 ||
            !time_or_dash(f[3]) || !time_or_dash(f[4]) || f[5] !~ /^(open|ended|superseded)$/ || f[6] !~ /^[01]$/ ||
            f[8] !~ /^(interactive|headless)$/ || !uint(f[10]) || f[11] !~ /^(active|quiet|-)$/ || f[12] !~ /^(active|quiet|-)$/ ||
            !uint(f[13]) || !uint(f[14]) || !uint(f[15]) || !uint(f[16]) || f[17] !~ /^[01]$/ || (f[2] in started)) return 0
    }
    for (i = 1; i < n; i++) {
        split(kept_line[i], f, "\t"); id = f[2]
        if (f[1] == "F") { failed[id] = 1; failed_id[++head_failed] = id; continue }
        begin(id, f[3], f[7], f[8])
        if (!(id in started)) continue
        if (f[3] != "-") started[id] += 0
        ended[id] = (f[4] == "-") ? "-" : f[4] + 0; status[id] = f[5]; done[id] = f[6] + 0
        outcome[id] = f[9]; cursor[id] = f[10] + 0; inferred[id] = or_empty(f[11]); reported[id] = or_empty(f[12])
        spent[id, "active"] = f[13] + 0; spent[id, "quiet"] = f[14] + 0; spent[id, "unobserved"] = f[15] + 0
        reported_s[id] = f[16] + 0
        if (f[17] == "1") dropped[id] = 1
    }
    return through
}

function checkpoint(path, through,    i, id, rows, row) {
    print "HYDRA_SESSION_CHECKPOINT\t1\t" through > path
    rows = 0
    for (i = 1; i <= head_ids; i++) {
        id = head_id[i]
        row = sprintf("I\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%d", id, started[id], ended[id], status[id], done[id],
            profile_of[id], mode_of[id], outcome[id], cursor[id], or_dash(inferred[id]), or_dash(reported[id]),
            seconds(id, "active"), seconds(id, "quiet"), seconds(id, "unobserved"), reported_s[id] + 0, (id in dropped) ? 1 : 0)
        print row > path
        rows++
    }
    for (i = 1; i <= head_failed; i++) { print "F\t" failed_id[i] > path; rows++ }
    print "Z\t" rows > path
    close(path)
}

# The last sequence number an archive holds, from its name
# (events-<stamp>-<pid>-<first>-<last>[-<n>].jsonl); 0 when it does not say.
function archive_last(path,    name, f, n) {
    name = path; sub(/^.*\//, "", name); sub(/\.jsonl$/, "", name)
    n = split(name, f, "-")
    return (n >= 5 && uint(f[4]) && uint(f[5])) ? f[5] + 0 : 0
}

# Collects the head events after sequence "through"; top is the newest
# sequence number its files hold.
function collect(through,    i, line, at, sequence, last) {
    pending = 0; top = 0
    for (i = 1; i <= files; i++) {
        last = file_archive[i] ? archive_last(file[i]) : 0
        if (last && last <= through) { if (last > top) top = last; continue }
        while ((getline line < file[i]) > 0) {
            at = index(line, "\"sequence\":")
            if (!at) continue
            sequence = substr(line, at + 11, 10) + 0
            if (sequence > top) top = sequence
            if (sequence <= through) continue
            if (index(line, "\"type\":\"lifecycle.") || index(line, "\"type\":\"activity.changed\"")) {
                pending++; event_line[pending] = line; event_sequence[pending] = sequence
            }
        }
        close(file[i])
    }
}

# Interprets one head: its checkpoint, then every newer event. Events that
# have settled since are folded into a new checkpoint once enough have.
function read_head(    i, path, record, through, fresh, saved, f) {
    if (head == "") return
    head_ids = 0; head_failed = 0
    path = cache "/" head ".tsv"
    through = (cache == "") ? 0 : restore(path)
    collect(through)
    if (through && top < through) {
        # The log no longer reaches the checkpoint (it was repaired): start over.
        for (i = 1; i <= head_ids; i++) forget(head_id[i])
        for (i = 1; i <= head_failed; i++) delete failed[failed_id[i]]
        candidates -= head_ids; head_ids = 0; head_failed = 0
        collect(0)
    }
    fresh = 0; saved = (cache == "" || coverage_bad)
    for (i = 1; i <= pending; i++) {
        record = record_of(event_line[i])
        if (record == "") continue
        split(record, f, "\t")
        if (!saved && f[1] != "F" && f[1] != "D" && f[3] + 0 >= now - settled) {
            if (fresh >= checkpoint_events) checkpoint(path ".next." writer, event_sequence[i] - 1)
            saved = 1
        }
        apply(record)
        fresh++
    }
    if (!saved && fresh >= checkpoint_events) checkpoint(path ".next." writer, top)
}

# Time before the coverage history begins is unknown, and a session nobody
# observed has unknown active and quiet time: neither is reported as zero.
function emit(id,    active, quiet, unobserved, reported_time) {
    active = "-"; quiet = "-"; unobserved = "-"; reported_time = "-"
    if (started[id] != "-" && started[id] >= unknown_before) {
        unobserved = seconds(id, "unobserved"); reported_time = reported_s[id] + 0
        if (seconds(id, "active") + seconds(id, "quiet") > 0) { active = seconds(id, "active"); quiet = seconds(id, "quiet") }
    }
    printf "H\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", head_of[id], id, branch_of[id], profile_of[id],
        mode_of[id], status[id], started[id], ended[id], outcome[id], active, quiet, unobserved, reported_time
}

BEGIN {
    now += 0
    load_coverage()
    count = split(ENVIRON["HYDRA_SESSION_CHECKPOINTS"], listed, "\n")
    for (i = 1; i <= count; i++) if (listed[i] != "") checkpoints[listed[i]] = 1
    head = ""
    while ((getline path) > 0) {
        if (substr(path, 1, length(root)) != root) continue
        parts = split(substr(path, length(root) + 1), part, "/")
        if (part[1] !~ /^head_[0-9a-f]+$/ || length(part[1]) > 79) continue
        if (part[1] != head) {
            read_head()
            head = ""
            if (heads >= max_heads) { if (!truncated_heads++) print "X\tMore than " max_heads " heads; sessions are a partial sample"; continue }
            heads++; head = part[1]; branch = "-"; files = 0
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
        }
    }
    read_head()
    if (overflow) print "X\tMore than " max_candidates " head sessions recorded; partial sample"
    # Keep the newest sessions: an insertion into a bounded, sorted selection.
    kept = 0
    for (i = 1; i <= candidates; i++) {
        id = order[i]
        if (id in dropped) continue
        if (!done[id]) advance(id, now)
        total++
        key = (started[id] == "-") ? 0 : started[id]
        for (at = kept; at >= 1 && sort_key[at] < key; at--) if (at < max_rows) { chosen[at + 1] = chosen[at]; sort_key[at + 1] = sort_key[at] }
        if (at < max_rows) { chosen[at + 1] = id; sort_key[at + 1] = key; if (kept < max_rows) kept++ }
    }
    if (total > max_rows) print "X\tMore than " max_rows " head sessions; showing the newest"
    for (i = 1; i <= kept; i++) emit(chosen[i])
}'
