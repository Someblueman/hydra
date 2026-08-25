#!/bin/sh
# State management functions for Hydra
# POSIX-compliant shell script

# Load locks (required for fail-closed writers) then cache implementation
_state_lib_dir="${HYDRA_LIB_DIR:-}"
if [ -z "$_state_lib_dir" ]; then
    _state_lib_dir="$(cd "$(dirname "$0")/../lib" 2>/dev/null && pwd)" || true
fi
if [ -z "${_LOCKS_MODULE_LOADED:-}" ] && [ -n "$_state_lib_dir" ] && [ -f "$_state_lib_dir/locks.sh" ]; then
    # shellcheck disable=SC1090,SC1091
    . "$_state_lib_dir/locks.sh"
fi
if [ -z "${_STATE_CACHE_MODULE_LOADED:-}" ]; then
    if [ -n "$_state_lib_dir" ] && [ -f "$_state_lib_dir/state_cache.sh" ]; then
        # shellcheck disable=SC1090,SC1091
        . "$_state_lib_dir/state_cache.sh"
        _STATE_CACHE_MODULE_LOADED=1
    fi
fi

# Acquire the shared map lock or fail closed (no mutation).
# Usage: _lock_state_map
# Returns: 0 if held, 1 if not
_lock_state_map() {
    if ! acquire_lock "state_map"; then
        echo "Error: Failed to acquire state lock" >&2
        return 1
    fi
    return 0
}

# Format a 7-field map line
# Usage: _format_map_line <branch> <session> <ai> <group> <timestamp> <deps> <pr>
_format_map_line() {
    printf '%s %s %s %s %s %s %s\n' \
        "$1" "$2" "${3:--}" "${4:--}" "${5:--}" "${6:--}" "${7:--}"
}

# =============================================================================
# Timestamp and Duration Helpers
# =============================================================================

# Get current Unix timestamp (seconds since epoch)
# Usage: get_timestamp
# Returns: Unix timestamp on stdout
get_timestamp() {
    date +%s
}

# Format duration from seconds to human-readable string
# Usage: format_duration <seconds>
# Returns: Human-readable duration (e.g., "2h 15m", "3d 1h")
format_duration() {
    _seconds="$1"

    # Handle empty/invalid input
    if [ -z "$_seconds" ] || [ "$_seconds" = "-" ]; then
        printf '%s' "-"
        return 0
    fi

    # Validate numeric
    case "$_seconds" in
        ''|*[!0-9]*) printf '%s' "-"; return 0 ;;
    esac

    if [ "$_seconds" -lt 60 ]; then
        printf '%ds' "$_seconds"
    elif [ "$_seconds" -lt 3600 ]; then
        printf '%dm' "$((_seconds / 60))"
    elif [ "$_seconds" -lt 86400 ]; then
        _hours="$((_seconds / 3600))"
        _mins="$((_seconds % 3600 / 60))"
        printf '%dh %dm' "$_hours" "$_mins"
    else
        _days="$((_seconds / 86400))"
        _hours="$((_seconds % 86400 / 3600))"
        printf '%dd %dh' "$_days" "$_hours"
    fi
}

# Calculate duration from timestamp to now
# Usage: get_duration_since <timestamp>
# Returns: Duration in seconds on stdout
get_duration_since() {
    _ts="$1"

    # Handle empty/invalid input
    if [ -z "$_ts" ] || [ "$_ts" = "-" ]; then
        echo "0"
        return 0
    fi

    case "$_ts" in
        ''|*[!0-9]*)
            echo "0"
            return 0
            ;;
    esac

    _now="$(get_timestamp)"
    echo "$((_now - _ts))"
}
# Public API Functions
# =============================================================================

# Add a branch-session mapping
# Usage: add_mapping <branch> <session> [ai_tool] [group] [timestamp] [deps] [pr_number]
# Returns: 0 on success, 1 on failure
add_mapping() {
    branch="$1"
    session="$2"
    ai_tool="${3:-}"
    group="${4:-}"
    timestamp="${5:-$(get_timestamp)}"
    deps="${6:-}"
    pr_number="${7:-}"

    if [ -z "$branch" ] || [ -z "$session" ]; then
        echo "Error: Branch and session are required" >&2
        return 1
    fi

    if [ -z "$HYDRA_MAP" ]; then
        echo "Error: HYDRA_MAP not set" >&2
        return 1
    fi

    # Build the mapping line based on what fields are provided
    # Format: branch session [ai_tool] [group] [timestamp] [deps] [pr_number]
    # Always include timestamp for new entries
    # Use "-" as placeholder for optional fields
    ai_field="${ai_tool:-"-"}"
    group_field="${group:-"-"}"
    deps_field="${deps:-"-"}"
    pr_field="${pr_number:-"-"}"
    mapping_line="$branch $session $ai_field $group_field $timestamp $deps_field $pr_field"

    if ! _lock_state_map; then
        return 1
    fi

    _invalidate_state_cache

    tmpfile="$(mktemp_adjacent "$HYDRA_MAP")" || {
        release_lock "state_map"
        return 1
    }

    if [ -f "$HYDRA_MAP" ]; then
        while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp map_deps map_pr; do
            if [ -n "$map_branch" ] && [ "$map_branch" != "$branch" ]; then
                _format_map_line "$map_branch" "$map_session" "$map_ai" "$map_group" \
                    "$map_timestamp" "$map_deps" "$map_pr"
            fi
        done < "$HYDRA_MAP" > "$tmpfile"
    else
        : > "$tmpfile"
    fi
    printf '%s\n' "$mapping_line" >> "$tmpfile"

    if ! atomic_replace "$HYDRA_MAP" "$tmpfile"; then
        rm -f "$tmpfile"
        release_lock "state_map"
        return 1
    fi

    release_lock "state_map"
    return 0
}

# Remove a branch-session mapping
# Usage: remove_mapping <branch>
# Returns: 0 on success, 1 on failure
remove_mapping() {
    branch="$1"

    if [ -z "$branch" ]; then
        echo "Error: Branch is required" >&2
        return 1
    fi

    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0  # Nothing to remove
    fi

    if ! _lock_state_map; then
        return 1
    fi

    _invalidate_state_cache

    tmpfile="$(mktemp_adjacent "$HYDRA_MAP")" || {
        release_lock "state_map"
        return 1
    }

    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp map_deps map_pr; do
        if [ -n "$map_branch" ] && [ "$map_branch" != "$branch" ]; then
            _format_map_line "$map_branch" "$map_session" "$map_ai" "$map_group" \
                "$map_timestamp" "$map_deps" "$map_pr"
        fi
    done < "$HYDRA_MAP" > "$tmpfile"

    if ! atomic_replace "$HYDRA_MAP" "$tmpfile"; then
        rm -f "$tmpfile"
        release_lock "state_map"
        return 1
    fi

    release_lock "state_map"
    return 0
}

# Get session for a branch
# Usage: get_session_for_branch <branch>
# Returns: Session name on stdout, empty if not found
get_session_for_branch() {
    branch="$1"

    if [ -z "$branch" ]; then
        return 1
    fi

    # Use cache for O(1) lookup
    _cache_get_session "$branch"
}

# Get branch for a session
# Usage: get_branch_for_session <session>
# Returns: Branch name on stdout, empty if not found
get_branch_for_session() {
    session="$1"

    if [ -z "$session" ]; then
        return 1
    fi

    # Use cache for O(1) lookup
    _cache_get_branch "$session"
}

# List all mappings
# Usage: list_mappings
# Returns: List of "branch session" pairs on stdout
list_mappings() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    
    cat "$HYDRA_MAP"
}

# Validate mappings against actual state
# Usage: validate_mappings
# Returns: 0 if all valid, 1 if inconsistencies found
validate_mappings() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi

    errors=0

    while IFS=' ' read -r branch session _ai _group _timestamp _deps _pr; do
        # Check if branch exists
        if ! git_branch_exists "$branch"; then
            echo "Warning: Branch '$branch' no longer exists" >&2
            errors=1
        fi

        # Check if session exists
        if ! tmux_session_exists "$session"; then
            echo "Warning: Session '$session' no longer exists" >&2
            errors=1
        fi
    done < "$HYDRA_MAP"

    return $errors
}

# Clean up invalid mappings (preserving AI and group columns)
# Usage: cleanup_mappings
# Returns: 0 on success
cleanup_mappings() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi

    if ! _lock_state_map; then
        return 1
    fi

    _invalidate_state_cache

    tmpfile="$(mktemp_adjacent "$HYDRA_MAP")" || {
        release_lock "state_map"
        return 1
    }

    while IFS=' ' read -r branch session ai group timestamp deps pr; do
        if [ -n "$branch" ] && git_branch_exists "$branch" && tmux_session_exists "$session"; then
            _format_map_line "$branch" "$session" "$ai" "$group" "$timestamp" "$deps" "$pr"
        fi
    done < "$HYDRA_MAP" > "$tmpfile"

    if ! atomic_replace "$HYDRA_MAP" "$tmpfile"; then
        rm -f "$tmpfile"
        release_lock "state_map"
        return 1
    fi

    release_lock "state_map"
    return 0
}

# Generate a unique session name for a branch
# Usage: generate_session_name <branch>
# Returns: Session name on stdout
# Note: Uses try_lock/release_lock from lib/locks.sh
generate_session_name() {
    branch="$1"

    if [ -z "$branch" ]; then
        echo "Error: Branch is required" >&2
        return 1
    fi

    # Clean branch name for tmux (replace special chars)
    base_name="$(echo "$branch" | sed 's/[^a-zA-Z0-9_-]/_/g')"

    # First, try the base name with an atomic lock to avoid races
    if try_lock "$base_name"; then
        if ! tmux has-session -t "$base_name" 2>/dev/null; then
            echo "$base_name"
            return 0
        fi
        # Already exists, release and continue to numeric suffixes
        release_lock "$base_name"
    fi

    # Append a number - start from 1 and find first available with locking
    num=1
    max_attempts=100  # Prevent infinite loop in edge cases
    while [ "$num" -le "$max_attempts" ]; do
        session_name="${base_name}_${num}"
        if try_lock "$session_name"; then
            if ! tmux has-session -t "$session_name" 2>/dev/null; then
                echo "$session_name"
                return 0
            fi
            # Release and continue if exists
            release_lock "$session_name"
        fi
        num=$((num + 1))
    done

    # If we've exhausted attempts, use timestamp for uniqueness (best-effort lock)
    timestamp="$(date +%s 2>/dev/null || echo "$$")"
    final_name="${base_name}_${timestamp}"
    if try_lock "$final_name"; then
        echo "$final_name"
        return 0
    fi
    # As a last resort, return the timestamped name without lock
    echo "$final_name"
}

# Note: release_session_lock() has been moved to lib/locks.sh

# Get AI tool for a branch (if stored)
# Usage: get_ai_for_branch <branch>
# Returns: AI tool on stdout, empty if not set
get_ai_for_branch() {
    branch="$1"
    if [ -z "$branch" ]; then
        return 1
    fi

    # Use cache for O(1) lookup
    _cache_get_ai "$branch"
}

# Get group for a branch
# Usage: get_group_for_branch <branch>
# Returns: Group name on stdout, empty if not set
get_group_for_branch() {
    branch="$1"
    if [ -z "$branch" ]; then
        return 1
    fi

    # Use cache for O(1) lookup
    _cache_get_group "$branch"
}

# Set group for a branch
# Usage: set_group <branch> <group>
# Returns: 0 on success, 1 on failure
set_group() {
    branch="$1"
    group="$2"

    if [ -z "$branch" ]; then
        echo "Error: Branch is required" >&2
        return 1
    fi

    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        echo "Error: No mappings file" >&2
        return 1
    fi

    if ! _lock_state_map; then
        return 1
    fi

    _invalidate_state_cache

    tmpfile="$(mktemp_adjacent "$HYDRA_MAP")" || {
        release_lock "state_map"
        return 1
    }

    found=0
    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp map_deps map_pr; do
        if [ "$map_branch" = "$branch" ]; then
            found=1
            _format_map_line "$map_branch" "$map_session" "$map_ai" "${group:-"-"}" \
                "$map_timestamp" "$map_deps" "$map_pr"
        elif [ -n "$map_branch" ]; then
            _format_map_line "$map_branch" "$map_session" "$map_ai" "$map_group" \
                "$map_timestamp" "$map_deps" "$map_pr"
        fi
    done < "$HYDRA_MAP" > "$tmpfile"

    if [ "$found" -eq 0 ]; then
        echo "Error: Branch '$branch' not found in mappings" >&2
        rm -f "$tmpfile"
        release_lock "state_map"
        return 1
    fi

    if ! atomic_replace "$HYDRA_MAP" "$tmpfile"; then
        rm -f "$tmpfile"
        release_lock "state_map"
        return 1
    fi

    release_lock "state_map"
    return 0
}

# List all unique groups
# Usage: list_groups
# Returns: List of group names on stdout
list_groups() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    while IFS=' ' read -r _map_branch _map_session _map_ai map_group _map_timestamp; do
        if [ -n "$map_group" ] && [ "$map_group" != "-" ]; then
            echo "$map_group"
        fi
    done < "$HYDRA_MAP" | sort -u
}

# Get all mappings for a group
# Usage: list_mappings_for_group <group>
# Returns: List of "branch session ai group timestamp deps pr" entries on stdout
list_mappings_for_group() {
    group="$1"
    if [ -z "$group" ] || [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp map_deps map_pr; do
        if [ "$map_group" = "$group" ]; then
            echo "$map_branch $map_session ${map_ai:-"-"} $map_group ${map_timestamp:-"-"} ${map_deps:-"-"} ${map_pr:-"-"}"
        fi
    done < "$HYDRA_MAP"
}

# Get spawn timestamp for a branch
# Usage: get_timestamp_for_branch <branch>
# Returns: Unix timestamp on stdout, empty if not set
get_timestamp_for_branch() {
    branch="$1"
    if [ -z "$branch" ]; then
        return 1
    fi

    # Use cache for O(1) lookup
    _cache_get_timestamp "$branch"
}

# Get dependencies for a branch
# Usage: get_deps_for_branch <branch>
# Returns: Comma-separated dependency list on stdout, empty if not set
get_deps_for_branch() {
    branch="$1"
    if [ -z "$branch" ]; then
        return 1
    fi

    # Use cache for O(1) lookup
    _cache_get_deps "$branch"
}

# Get PR number for a branch
# Usage: get_pr_for_branch <branch>
# Returns: PR number on stdout, empty if not set
get_pr_for_branch() {
    branch="$1"
    if [ -z "$branch" ]; then
        return 1
    fi

    # Use cache for O(1) lookup
    _cache_get_pr "$branch"
}

# Set dependencies for a branch
# Usage: set_deps <branch> <deps>
# deps: comma-separated list of branch names (e.g., "branch1,branch2")
# Returns: 0 on success, 1 on failure
set_deps() {
    branch="$1"
    deps="$2"

    if [ -z "$branch" ]; then
        echo "Error: Branch is required" >&2
        return 1
    fi

    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        echo "Error: No mappings file" >&2
        return 1
    fi

    if ! _lock_state_map; then
        return 1
    fi

    _invalidate_state_cache

    tmpfile="$(mktemp_adjacent "$HYDRA_MAP")" || {
        release_lock "state_map"
        return 1
    }

    found=0
    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp map_deps map_pr; do
        if [ "$map_branch" = "$branch" ]; then
            found=1
            _format_map_line "$map_branch" "$map_session" "$map_ai" "$map_group" \
                "$map_timestamp" "${deps:-"-"}" "$map_pr"
        elif [ -n "$map_branch" ]; then
            _format_map_line "$map_branch" "$map_session" "$map_ai" "$map_group" \
                "$map_timestamp" "$map_deps" "$map_pr"
        fi
    done < "$HYDRA_MAP" > "$tmpfile"

    if [ "$found" -eq 0 ]; then
        echo "Error: Branch '$branch' not found in mappings" >&2
        rm -f "$tmpfile"
        release_lock "state_map"
        return 1
    fi

    if ! atomic_replace "$HYDRA_MAP" "$tmpfile"; then
        rm -f "$tmpfile"
        release_lock "state_map"
        return 1
    fi

    release_lock "state_map"
    return 0
}

# Set PR number for a branch
# Usage: set_pr_for_branch <branch> <pr_number>
# Returns: 0 on success, 1 on failure
set_pr_for_branch() {
    branch="$1"
    pr_number="$2"

    if [ -z "$branch" ]; then
        echo "Error: Branch is required" >&2
        return 1
    fi

    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        echo "Error: No mappings file" >&2
        return 1
    fi

    if ! _lock_state_map; then
        return 1
    fi

    _invalidate_state_cache

    tmpfile="$(mktemp_adjacent "$HYDRA_MAP")" || {
        release_lock "state_map"
        return 1
    }

    found=0
    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp map_deps map_pr; do
        if [ "$map_branch" = "$branch" ]; then
            found=1
            _format_map_line "$map_branch" "$map_session" "$map_ai" "$map_group" \
                "$map_timestamp" "$map_deps" "${pr_number:-"-"}"
        elif [ -n "$map_branch" ]; then
            _format_map_line "$map_branch" "$map_session" "$map_ai" "$map_group" \
                "$map_timestamp" "$map_deps" "$map_pr"
        fi
    done < "$HYDRA_MAP" > "$tmpfile"

    if [ "$found" -eq 0 ]; then
        echo "Error: Branch '$branch' not found in mappings" >&2
        rm -f "$tmpfile"
        release_lock "state_map"
        return 1
    fi

    if ! atomic_replace "$HYDRA_MAP" "$tmpfile"; then
        rm -f "$tmpfile"
        release_lock "state_map"
        return 1
    fi

    release_lock "state_map"
    return 0
}
