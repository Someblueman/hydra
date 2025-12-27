#!/bin/sh
# Resource limit and queue management functions for Hydra
# POSIX-compliant shell script
#
# Provides session limits and spawn queue management to prevent system overload.
# Dependencies: locks.sh, state.sh

# =============================================================================
# Configuration
# =============================================================================

# Get the maximum allowed sessions (0 = unlimited)
# Usage: get_max_sessions
# Returns: Max session count on stdout
get_max_sessions() {
    printf '%s' "${HYDRA_MAX_SESSIONS:-0}"
}

# Check if session limit is enabled
# Usage: is_limit_enabled
# Returns: 0 if limit is enabled and > 0, 1 otherwise
is_limit_enabled() {
    _max="$(get_max_sessions)"
    [ "$_max" -gt 0 ] 2>/dev/null
}

# =============================================================================
# Session Counting
# =============================================================================

# Get count of active sessions (fast path using wc -l)
# Usage: get_active_session_count
# Returns: Count on stdout
get_active_session_count() {
    if [ -z "${HYDRA_MAP:-}" ] || [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        printf '%s' "0"
        return 0
    fi
    wc -l < "$HYDRA_MAP" | tr -d ' '
}

# Check if spawning N sessions would exceed limit
# Usage: would_exceed_limit <count>
# Returns: 0 if would exceed, 1 if ok
would_exceed_limit() {
    _requested="${1:-1}"
    _max="$(get_max_sessions)"

    # No limit set
    if [ "$_max" -le 0 ] 2>/dev/null; then
        return 1
    fi

    _current="$(get_active_session_count)"
    _projected=$((_current + _requested))

    [ "$_projected" -gt "$_max" ]
}

# Get available capacity (how many more sessions can be spawned)
# Usage: get_available_capacity
# Returns: Number of sessions that can be spawned, or "unlimited"
get_available_capacity() {
    _max="$(get_max_sessions)"

    if [ "$_max" -le 0 ] 2>/dev/null; then
        printf '%s' "unlimited"
        return 0
    fi

    _current="$(get_active_session_count)"
    _available=$((_max - _current))

    if [ "$_available" -lt 0 ]; then
        _available=0
    fi

    printf '%s' "$_available"
}

# =============================================================================
# Queue Directory Management
# =============================================================================

# Get queue directory path
# Usage: _get_queue_dir
# Returns: Queue directory path on stdout
_get_queue_dir() {
    printf '%s' "${HYDRA_HOME:-$HOME/.hydra}/queue"
}

# Ensure queue directory exists
# Usage: _ensure_queue_dir
# Returns: 0 on success, 1 on failure
_ensure_queue_dir() {
    _qdir="$(_get_queue_dir)"
    mkdir -p "$_qdir" 2>/dev/null || return 1
}

# =============================================================================
# Queue Operations
# =============================================================================

# Add spawn request to queue
# Usage: queue_spawn <branch> [ai_tool] [group] [layout] [priority]
# Returns: 0 on success, queue entry path on stdout
queue_spawn() {
    _branch="$1"
    _ai_tool="${2:-claude}"
    _group="${3:-}"
    _layout="${4:-default}"
    _priority="${5:-50}"

    if [ -z "$_branch" ]; then
        echo "Error: Branch name required" >&2
        return 1
    fi

    _ensure_queue_dir || return 1

    _timestamp="$(date +%s)"
    _safe_branch="$(printf '%s' "$_branch" | sed 's/[^a-zA-Z0-9_-]/_/g')"

    # Format priority as 3-digit number (001-099)
    _priority_fmt="$(printf '%03d' "$_priority")"

    # Generate unique filename with PID for uniqueness
    _seq="$$"
    _filename="${_timestamp}_${_safe_branch}_${_seq}_${_priority_fmt}.queue"
    _filepath="$(_get_queue_dir)/$_filename"

    # Atomic write with lock
    if try_lock "queue_add"; then
        cat > "$_filepath" <<EOF
branch=$_branch
ai_tool=$_ai_tool
group=$_group
layout=$_layout
priority=$_priority
requested_at=$_timestamp
EOF
        release_lock "queue_add"
        printf '%s' "$_filepath"
        return 0
    fi

    echo "Error: Failed to acquire queue lock" >&2
    return 1
}

# Get count of queued spawns
# Usage: get_queue_count
# Returns: Count on stdout
get_queue_count() {
    _ensure_queue_dir
    _qdir="$(_get_queue_dir)"
    # Use find to count .queue files, handle empty directory
    _count="$(find "$_qdir" -maxdepth 1 -name "*.queue" -type f 2>/dev/null | wc -l | tr -d ' ')"
    printf '%s' "$_count"
}

# List queued spawns in priority order
# Usage: list_queue [--json]
# Returns: Queued entries on stdout
list_queue() {
    _json_output=""
    if [ "$1" = "--json" ]; then
        _json_output="1"
    fi

    _ensure_queue_dir
    _qdir="$(_get_queue_dir)"

    if [ -n "$_json_output" ]; then
        printf '{"queue": ['
        _first=1
        for _qfile in $(find "$_qdir" -maxdepth 1 -name "*.queue" -type f 2>/dev/null | sort); do
            [ -f "$_qfile" ] || continue

            # Parse queue file
            _q_branch="" _q_ai="" _q_group="" _q_priority="" _q_requested=""
            while IFS='=' read -r _key _val; do
                case "$_key" in
                    branch) _q_branch="$_val" ;;
                    ai_tool) _q_ai="$_val" ;;
                    group) _q_group="$_val" ;;
                    priority) _q_priority="$_val" ;;
                    requested_at) _q_requested="$_val" ;;
                esac
            done < "$_qfile"

            if [ "$_first" -eq 1 ]; then
                _first=0
            else
                printf ','
            fi

            # Handle null group for JSON
            if [ -z "$_q_group" ]; then
                printf '{"branch":"%s","ai":"%s","group":null,"priority":%s,"requested_at":%s}' \
                    "$_q_branch" "$_q_ai" "$_q_priority" "$_q_requested"
            else
                printf '{"branch":"%s","ai":"%s","group":"%s","priority":%s,"requested_at":%s}' \
                    "$_q_branch" "$_q_ai" "$_q_group" "$_q_priority" "$_q_requested"
            fi
        done
        printf ']}\n'
    else
        _count=0
        for _qfile in $(find "$_qdir" -maxdepth 1 -name "*.queue" -type f 2>/dev/null | sort); do
            [ -f "$_qfile" ] || continue
            _count=$((_count + 1))

            # Parse queue file
            _q_branch="" _q_ai="" _q_group="" _q_priority="" _q_requested=""
            while IFS='=' read -r _key _val; do
                case "$_key" in
                    branch) _q_branch="$_val" ;;
                    ai_tool) _q_ai="$_val" ;;
                    group) _q_group="$_val" ;;
                    priority) _q_priority="$_val" ;;
                    requested_at) _q_requested="$_val" ;;
                esac
            done < "$_qfile"

            # Calculate wait time
            _now="$(date +%s)"
            _wait_secs=$((_now - _q_requested))
            _wait_fmt="$(format_duration "$_wait_secs")"

            _group_str=""
            [ -n "$_q_group" ] && _group_str=" [group: $_q_group]"

            printf '  [%d] %s (%s, pri=%s, waiting %s)%s\n' \
                "$_count" "$_q_branch" "$_q_ai" "$_q_priority" "$_wait_fmt" "$_group_str"
        done

        if [ "$_count" -eq 0 ]; then
            echo "No pending spawns in queue"
        else
            echo ""
            echo "Total: $_count pending spawn(s)"
        fi
    fi
}

# Remove a specific entry from the queue by branch name
# Usage: dequeue_spawn <branch>
# Returns: 0 if removed, 1 if not found
dequeue_spawn() {
    _target_branch="$1"

    if [ -z "$_target_branch" ]; then
        echo "Error: Branch name required" >&2
        return 1
    fi

    _ensure_queue_dir
    _qdir="$(_get_queue_dir)"

    for _qfile in "$_qdir"/*.queue; do
        [ -f "$_qfile" ] || continue

        if grep -q "^branch=$_target_branch$" "$_qfile" 2>/dev/null; then
            rm -f "$_qfile"
            return 0
        fi
    done

    return 1
}

# Clear entire queue
# Usage: clear_queue
# Returns: Number of entries cleared on stdout
clear_queue() {
    _ensure_queue_dir
    _qdir="$(_get_queue_dir)"

    _count="$(get_queue_count)"
    rm -f "$_qdir"/*.queue 2>/dev/null
    printf '%s' "$_count"
}

# Process queue - spawn next available entries when capacity is available
# Usage: process_spawn_queue
# Returns: Number of spawned sessions on stdout
# Note: Runs best-effort, does not fail if spawns fail
process_spawn_queue() {
    _ensure_queue_dir
    _qdir="$(_get_queue_dir)"

    # Check if any capacity available
    if is_limit_enabled; then
        _capacity="$(get_available_capacity)"
        if [ "$_capacity" = "0" ]; then
            printf '%s' "0"
            return 0
        fi
    fi

    _spawned=0

    # Process queue files in priority/timestamp order (filename sorts correctly)
    for _qfile in $(find "$_qdir" -maxdepth 1 -name "*.queue" -type f 2>/dev/null | sort); do
        [ -f "$_qfile" ] || continue

        # Check capacity again before each spawn
        if is_limit_enabled && would_exceed_limit 1; then
            break
        fi

        # Parse queue entry
        _q_branch="" _q_ai="" _q_group="" _q_layout=""
        while IFS='=' read -r _key _val; do
            case "$_key" in
                branch) _q_branch="$_val" ;;
                ai_tool) _q_ai="$_val" ;;
                group) _q_group="$_val" ;;
                layout) _q_layout="$_val" ;;
            esac
        done < "$_qfile"

        # Remove queue file before spawning (avoid double-spawn on failure)
        rm -f "$_qfile"

        # Attempt spawn (best-effort)
        echo "Processing queued spawn: $_q_branch..." >&2
        if spawn_single "$_q_branch" "$_q_layout" "$_q_ai" "$_q_group" "" "" >/dev/null 2>&1; then
            _spawned=$((_spawned + 1))
            echo "  Spawned $_q_branch successfully" >&2
        else
            echo "  Failed to spawn $_q_branch" >&2
        fi
    done

    printf '%s' "$_spawned"
}
