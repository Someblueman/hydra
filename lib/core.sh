#!/bin/sh
# Canonical shell snapshot plus bounded dispatch to the optional read-only core.

HYDRA_CORE_PROTOCOL=1
HYDRA_CORE_TIMEOUT_SECONDS="${HYDRA_CORE_TIMEOUT_SECONDS:-2}"

hydra_snapshot_shell() {
    state_v2_verify >/dev/null || return 1
    _hss_projects=0
    _hss_first=1
    for _hss_project_dir in "$HYDRA_STATE_V2_ROOT"/projects/project_*; do
        [ -d "$_hss_project_dir" ] || continue
        _hss_projects=$((_hss_projects + 1))
    done
    printf '{"schema_version":1,"ok":true,"command":"snapshot","data":{"state_schema":2,"projects":%s,"heads":[' "$_hss_projects"
    for _hss_project_dir in "$HYDRA_STATE_V2_ROOT"/projects/project_*; do
        [ -d "$_hss_project_dir" ] || continue
        _hss_project_id="$(basename "$_hss_project_dir")"
        for _hss_head_dir in "$_hss_project_dir"/heads/head_*; do
            [ -d "$_hss_head_dir" ] || continue
            _hss_head_id="$(basename "$_hss_head_dir")"
            _hss_branch="$(sed -n '1p' "$_hss_head_dir/branch")" || return 1
            _hss_desired="$(sed -n '1p' "$_hss_head_dir/desired-state")" || return 1
            _hss_instance="$(sed -n '1p' "$_hss_head_dir/current-instance")" || return 1
            if [ "$_hss_first" -eq 0 ]; then printf ','; fi
            _hss_first=0
            printf '{"project_id":"%s","head_id":"%s","branch":"%s","desired_state":"%s","current_instance":"%s"}' \
                "$(json_escape "$_hss_project_id")" "$(json_escape "$_hss_head_id")" \
                "$(json_escape "$_hss_branch")" "$(json_escape "$_hss_desired")" \
                "$(json_escape "$_hss_instance")"
        done
    done
    printf ']}}\n'
}

hydra_core_path() {
    if [ -n "${HYDRA_CORE:-}" ]; then
        printf '%s\n' "$HYDRA_CORE"
        return 0
    fi
    for _hcp_candidate in \
        "$HYDRA_BIN_DIR/../build/hydra-core" \
        "$HYDRA_BIN_DIR/../libexec/hydra/hydra-core"; do
        [ -f "$_hcp_candidate" ] || continue
        printf '%s\n' "$_hcp_candidate"
        return 0
    done
    return 1
}

# One snapshot of the process table: pid, parent, process group and state per
# line. The state column (first letter Z for an exited, not yet reaped process)
# is left out where ps does not know it; members are then treated as running.
hydra_core_process_table() {
    ps -eo pid=,ppid=,pgid=,state= 2>/dev/null || ps -eo pid=,ppid=,pgid= 2>/dev/null
}

# Prints the helper's own process group when it leads one, else nothing. A group
# the helper shares with this shell is never signalled as a whole.
hydra_core_tree_group() {
    _hctg_group="$(ps -o pgid= -p "$1" 2>/dev/null | tr -d ' ')"
    [ -n "$_hctg_group" ] && [ "$_hctg_group" = "$1" ] || return 1
    printf '%s\n' "$_hctg_group"
}

# Prints the pids of the helper subtree from one snapshot: every descendant by
# parent chain (deepest first, the helper itself last) plus every member of the
# helper's own group, so a descendant that was reparented or that left the group
# is still found. Usage: hydra_core_tree_pids PID [GROUP] [running]; with
# "running" exited-but-unreaped processes are left out.
hydra_core_tree_pids() {
    hydra_core_process_table | awk -v root="$1" -v group="${2:-}" -v running="${3:-}" '
        {
            parent[$1] = $2
            if (group != "" && $3 == group) member[$1] = 1
            if (running != "" && substr($4, 1, 1) == "Z") exited[$1] = 1
        }
        END {
            if (root in parent) { count = 1; queue[1] = root; seen[root] = 1 }
            for (position = 1; position <= count; position++)
                for (pid in parent)
                    if (parent[pid] == queue[position] && !(pid in seen)) {
                        seen[pid] = 1
                        queue[++count] = pid
                    }
            for (position = count; position >= 1; position--)
                if (!(queue[position] in exited)) print queue[position]
            for (pid in member)
                if (!(pid in seen) && !(pid in exited)) print pid
        }'
}

# Sends a signal to the helper subtree: its own process group as one unit when
# it has one (which also reaches a descendant forked after the snapshot), then
# every process of the tree by pid.
hydra_core_signal_tree() {
    _hcst_pid="$1"
    _hcst_signal="$2"
    _hcst_group="${3:-}"
    [ -z "$_hcst_group" ] || kill "-$_hcst_signal" "-$_hcst_group" 2>/dev/null || true
    for _hcst_member in $(hydra_core_tree_pids "$_hcst_pid" "$_hcst_group"); do
        kill "-$_hcst_signal" "$_hcst_member" 2>/dev/null || true
    done
}

# Waits, polling, until no process of the helper subtree is still running or the
# given number of polls is used up; succeeds when the tree has stopped. Exited
# descendants that only await their reaper count as stopped: once orphaned they
# belong to init, which collects them on its own schedule.
hydra_core_tree_settle() {
    _hcts_pid="$1"
    _hcts_group="$2"
    _hcts_polls="$3"
    _hcts_pause="$4"
    while [ -n "$(hydra_core_tree_pids "$_hcts_pid" "$_hcts_group" running)" ]; do
        [ "$_hcts_polls" -gt 0 ] || return 1
        _hcts_polls=$((_hcts_polls - 1))
        sleep "$_hcts_pause"
    done
    return 0
}

# Terminates the helper subtree after a timeout: TERM, up to a second for it to
# stop, then KILL, then a bounded wait until nothing of it still runs.
hydra_core_stop_tree() {
    _hcsk_pid="$1"
    _hcsk_group="$(hydra_core_tree_group "$_hcsk_pid" || true)"
    if sleep 0.05 2>/dev/null; then _hcsk_polls=20; _hcsk_pause=0.05; else _hcsk_polls=1; _hcsk_pause=1; fi
    hydra_core_signal_tree "$_hcsk_pid" TERM "$_hcsk_group"
    hydra_core_tree_settle "$_hcsk_pid" "$_hcsk_group" "$_hcsk_polls" "$_hcsk_pause" && return 0
    hydra_core_signal_tree "$_hcsk_pid" KILL "$_hcsk_group"
    hydra_core_tree_settle "$_hcsk_pid" "$_hcsk_group" "$_hcsk_polls" "$_hcsk_pause" || true
}

# Runs the helper with stdout and stderr captured into the supplied directory and
# returns 124 when it exceeds HYDRA_CORE_TIMEOUT_SECONDS. The helper runs in its
# own session where setsid exists, so a timeout signals its whole subtree as one
# process group; elsewhere the subtree is found from a process table snapshot.
# A watchdog subshell applies the timeout. Once it has fired, the parent waits
# for the TERM, then KILL pass to finish instead of stopping the watchdog mid-way,
# so no descendant outlives the call; after a normal exit the watchdog is stopped
# with USR1 and reaped. A HUP, INT or TERM that reaches the watchdog while the
# helper still runs is forwarded to the helper, whose own session would not see it.
hydra_core_run_bounded() {
    _hcr_dir="$1"
    shift
    case "$HYDRA_CORE_TIMEOUT_SECONDS" in
        ''|*[!0-9]*|0) HYDRA_CORE_TIMEOUT_SECONDS=2 ;;
    esac
    if command -v setsid >/dev/null 2>&1; then
        # The background child of a script never leads its process group, so
        # setsid replaces it in place and $! stays the helper's pid.
        setsid "$@" > "$_hcr_dir/stdout" 2> "$_hcr_dir/stderr" &
    else
        "$@" > "$_hcr_dir/stdout" 2> "$_hcr_dir/stderr" &
    fi
    _hcr_pid=$!
    (
        _hcr_sleep=""
        trap '[ -z "$_hcr_sleep" ] || kill "$_hcr_sleep" 2>/dev/null; exit 0' USR1
        trap '[ -z "$_hcr_sleep" ] || kill "$_hcr_sleep" 2>/dev/null
            if kill -0 "$_hcr_pid" 2>/dev/null; then hydra_core_signal_tree "$_hcr_pid" TERM "$(hydra_core_tree_group "$_hcr_pid" || true)"; fi
            exit 0' HUP INT TERM
        sleep "$HYDRA_CORE_TIMEOUT_SECONDS" &
        _hcr_sleep=$!
        wait "$_hcr_sleep" || exit 0
        kill -0 "$_hcr_pid" 2>/dev/null || exit 0
        trap '' USR1 HUP INT TERM
        : > "$_hcr_dir/timed-out"
        hydra_core_stop_tree "$_hcr_pid"
    ) >/dev/null 2>&1 &
    _hcr_watchdog=$!
    if wait "$_hcr_pid"; then _hcr_status=0; else _hcr_status=$?; fi
    [ -f "$_hcr_dir/timed-out" ] || kill -USR1 "$_hcr_watchdog" 2>/dev/null || true
    wait "$_hcr_watchdog" 2>/dev/null || true
    if [ -f "$_hcr_dir/timed-out" ]; then return 124; fi
    return "$_hcr_status"
}

hydra_core_snapshot_valid() {
    _hcsv_file="$1"
    [ "$(wc -l < "$_hcsv_file" | tr -d ' ')" = "1" ] || return 1
    LC_ALL=C awk '
        function take(value) {
            if (substr(line, position, length(value)) != value) return 0
            position += length(value)
            return 1
        }
        function number( digit) {
            digit = substr(line, position, 1)
            if (digit !~ /[0-9]/) return 0
            if (digit == "0") {
                position++
                return substr(line, position, 1) !~ /[0-9]/
            }
            while (substr(line, position, 1) ~ /[0-9]/) position++
            return 1
        }
        function json_string(escape, char, offset) {
            if (substr(line, position, 1) != "\"") return 0
            position++
            while (position <= length(line)) {
                char = substr(line, position, 1)
                if (char == "\"") {
                    position++
                    return 1
                }
                if (char == "\\") {
                    position++
                    escape = substr(line, position, 1)
                    if (escape == "u") {
                        for (offset = 1; offset <= 4; offset++) {
                            position++
                            if (substr(line, position, 1) !~ /[0-9A-Fa-f]/) return 0
                        }
                    } else if (escape !~ /^["\\\/bfnrt]$/) {
                        return 0
                    }
                } else if (char ~ /[[:cntrl:]]/) {
                    return 0
                }
                position++
            }
            return 0
        }
        function head() {
            return take("{\"project_id\":") && json_string() &&
                take(",\"head_id\":") && json_string() &&
                take(",\"branch\":") && json_string() &&
                take(",\"desired_state\":") && json_string() &&
                take(",\"current_instance\":") && json_string() && take("}")
        }
        BEGIN { valid = 1 }
        NR != 1 { valid = 0; exit }
        {
            line = $0
            position = 1
            if (!take("{\"schema_version\":1,\"ok\":true,\"command\":\"snapshot\",\"data\":{\"state_schema\":2,\"projects\":")) valid = 0
            if (valid && !number()) valid = 0
            if (valid && !take(",\"heads\":[")) valid = 0
            if (valid && substr(line, position, 1) != "]") {
                if (!head()) valid = 0
                while (valid && substr(line, position, 1) == ",") {
                    position++
                    if (!head()) valid = 0
                }
            }
            if (valid && !take("]}}")) valid = 0
            if (valid && position != length(line) + 1) valid = 0
        }
        END { exit valid && NR == 1 ? 0 : 1 }
    ' "$_hcsv_file"
}

hydra_snapshot_native() {
    _hsn_core="$(hydra_core_path 2>/dev/null || true)"
    if [ -z "$_hsn_core" ] || [ ! -e "$_hsn_core" ]; then
        echo "hydra: native snapshot fallback: helper-unavailable" >&2
        LC_ALL=C hydra_snapshot_shell
        return
    fi
    if [ ! -x "$_hsn_core" ]; then
        echo "hydra: native snapshot fallback: helper-not-executable" >&2
        LC_ALL=C hydra_snapshot_shell
        return
    fi
    _hsn_dir="$(mktemp -d "${TMPDIR:-/tmp}/hydra-core.XXXXXX")" || return 1
    if hydra_core_run_bounded "$_hsn_dir" "$_hsn_core" --protocol-version; then
        _hsn_status=0
    else
        _hsn_status=$?
        if [ "$_hsn_status" -eq 124 ]; then _hsn_reason=protocol-timeout; else _hsn_reason=protocol-failure; fi
    fi
    if [ "$_hsn_status" -ne 0 ]; then
        :
    elif [ "$(sed -n '1p' "$_hsn_dir/stdout")" != "$HYDRA_CORE_PROTOCOL" ] || \
         [ "$(wc -l < "$_hsn_dir/stdout" | tr -d ' ')" != "1" ]; then
        _hsn_reason=protocol-skew
    else
        rm -f "$_hsn_dir/stdout" "$_hsn_dir/stderr" "$_hsn_dir/timed-out"
        if hydra_core_run_bounded "$_hsn_dir" "$_hsn_core" --version; then
            _hsn_status=0
        else
            _hsn_status=$?
            if [ "$_hsn_status" -eq 124 ]; then _hsn_reason=version-timeout; else _hsn_reason=version-failure; fi
        fi
        if [ "$_hsn_status" -ne 0 ]; then
            :
        elif [ "$(sed -n '1p' "$_hsn_dir/stdout")" != "Hydra core $HYDRA_VERSION protocol $HYDRA_CORE_PROTOCOL" ] || \
             [ "$(wc -l < "$_hsn_dir/stdout" | tr -d ' ')" != 1 ]; then
            _hsn_reason=version-skew
        else
            rm -f "$_hsn_dir/stdout" "$_hsn_dir/stderr" "$_hsn_dir/timed-out"
            if hydra_core_run_bounded "$_hsn_dir" "$_hsn_core" snapshot "$HYDRA_STATE_V2_ROOT"; then
                _hsn_status=0
            else
                _hsn_status=$?
                if [ "$_hsn_status" -eq 124 ]; then _hsn_reason=snapshot-timeout; else _hsn_reason=snapshot-failure; fi
            fi
            if [ "$_hsn_status" -ne 0 ]; then
                :
            elif ! hydra_core_snapshot_valid "$_hsn_dir/stdout"; then
                _hsn_reason=malformed-output
            else
                cat "$_hsn_dir/stdout"
                rm -rf "$_hsn_dir"
                return 0
            fi
        fi
    fi
    echo "hydra: native snapshot fallback: $_hsn_reason" >&2
    rm -rf "$_hsn_dir"
    LC_ALL=C hydra_snapshot_shell
}
