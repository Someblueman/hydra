#!/bin/sh
# State management functions for Hydra
# POSIX-compliant shell script

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

    _now="$(get_timestamp)"
    echo "$((_now - _ts))"
}

# =============================================================================
# State Cache Implementation
# =============================================================================
# Uses sanitized variable names to provide O(1) lookups in POSIX shell.
# Cache is loaded once per command and invalidated on write operations.

# Cache state flag (empty = not loaded)
_STATE_CACHE_LOADED=""

# Validate state file and auto-repair if corrupted
# Usage: _validate_and_repair_state_file
# Returns: 0 on success, repairs file if malformed lines found
_validate_and_repair_state_file() {
    [ -z "$HYDRA_MAP" ] && return 0
    [ ! -f "$HYDRA_MAP" ] && return 0
    [ ! -s "$HYDRA_MAP" ] && return 0

    # Check for malformed lines (less than 2 fields = invalid)
    malformed=0
    while IFS= read -r line; do
        [ -z "$line" ] && continue
        field_count="$(echo "$line" | awk '{print NF}')"
        if [ "$field_count" -lt 2 ]; then
            malformed=$((malformed + 1))
        fi
    done < "$HYDRA_MAP"

    if [ "$malformed" -gt 0 ]; then
        # Backup corrupted file
        cp "$HYDRA_MAP" "${HYDRA_MAP}.bak" 2>/dev/null || true

        # Filter out malformed lines
        tmpfile="$(mktemp)"
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            field_count="$(echo "$line" | awk '{print NF}')"
            if [ "$field_count" -ge 2 ]; then
                echo "$line" >> "$tmpfile"
            fi
        done < "$HYDRA_MAP"
        mv "$tmpfile" "$HYDRA_MAP"

        echo "Warning: Repaired $malformed malformed line(s) in state file" >&2
        echo "  Backup saved to: ${HYDRA_MAP}.bak" >&2
    fi

    return 0
}

# Sanitize a key for use in variable names
# Converts any non-alphanumeric to underscore, adds prefix to avoid conflicts
# Usage: _sanitize_key <key>
_sanitize_key() {
    printf '%s' "$1" | sed 's/[^a-zA-Z0-9]/_/g'
}

# Load state cache from mapping file
# Creates lookup variables for branch->session, session->branch, branch->ai, branch->group, branch->timestamp
# Usage: _load_state_cache
_load_state_cache() {
    # Already loaded?
    if [ -n "$_STATE_CACHE_LOADED" ]; then
        return 0
    fi

    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        _STATE_CACHE_LOADED="empty"
        return 0
    fi

    # Validate and repair state file if needed (first time only)
    _validate_and_repair_state_file

    # Read all mappings and create lookup variables
    # Format: branch session [ai_tool] [group] [timestamp]
    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp; do
        [ -z "$map_branch" ] && continue

        # Create sanitized keys
        _key_b="$(_sanitize_key "$map_branch")"
        _key_s="$(_sanitize_key "$map_session")"

        # Store mappings using eval (safe - keys are sanitized)
        eval "_sc_b2s_${_key_b}=\"\$map_session\""
        eval "_sc_s2b_${_key_s}=\"\$map_branch\""

        # Store AI tool if present
        if [ -n "$map_ai" ] && [ "$map_ai" != "-" ]; then
            eval "_sc_b2ai_${_key_b}=\"\$map_ai\""
        fi

        # Store group if present
        if [ -n "$map_group" ] && [ "$map_group" != "-" ]; then
            eval "_sc_b2grp_${_key_b}=\"\$map_group\""
        fi

        # Store timestamp if present
        if [ -n "$map_timestamp" ] && [ "$map_timestamp" != "-" ]; then
            eval "_sc_b2ts_${_key_b}=\"\$map_timestamp\""
        fi
    done < "$HYDRA_MAP"

    _STATE_CACHE_LOADED="loaded"
    return 0
}

# Invalidate state cache (call after writes)
# Usage: _invalidate_state_cache
_invalidate_state_cache() {
    _STATE_CACHE_LOADED=""
    # Note: We don't unset cached variables - they'll be overwritten on next load
    # This is acceptable since variable count is bounded by session count
}

# Get session from cache
# Usage: _cache_get_session <branch>
# Returns: session name on stdout, 1 if not found
_cache_get_session() {
    _load_state_cache
    _key="$(_sanitize_key "$1")"
    eval "_result=\"\${_sc_b2s_${_key}:-}\""
    if [ -n "$_result" ]; then
        printf '%s\n' "$_result"
        return 0
    fi
    return 1
}

# Get branch from cache
# Usage: _cache_get_branch <session>
# Returns: branch name on stdout, 1 if not found
_cache_get_branch() {
    _load_state_cache
    _key="$(_sanitize_key "$1")"
    eval "_result=\"\${_sc_s2b_${_key}:-}\""
    if [ -n "$_result" ]; then
        printf '%s\n' "$_result"
        return 0
    fi
    return 1
}

# Get AI tool from cache
# Usage: _cache_get_ai <branch>
# Returns: AI tool on stdout, 1 if not found
_cache_get_ai() {
    _load_state_cache
    _key="$(_sanitize_key "$1")"
    eval "_result=\"\${_sc_b2ai_${_key}:-}\""
    if [ -n "$_result" ]; then
        printf '%s\n' "$_result"
        return 0
    fi
    return 1
}

# Get group from cache
# Usage: _cache_get_group <branch>
# Returns: group on stdout, 1 if not found
_cache_get_group() {
    _load_state_cache
    _key="$(_sanitize_key "$1")"
    eval "_result=\"\${_sc_b2grp_${_key}:-}\""
    if [ -n "$_result" ]; then
        printf '%s\n' "$_result"
        return 0
    fi
    return 1
}

# Get timestamp from cache
# Usage: _cache_get_timestamp <branch>
# Returns: timestamp on stdout, 1 if not found
_cache_get_timestamp() {
    _load_state_cache
    _key="$(_sanitize_key "$1")"
    eval "_result=\"\${_sc_b2ts_${_key}:-}\""
    if [ -n "$_result" ]; then
        printf '%s\n' "$_result"
        return 0
    fi
    return 1
}

# =============================================================================
# Public API Functions
# =============================================================================

# Add a branch-session mapping
# Usage: add_mapping <branch> <session> [ai_tool] [group] [timestamp]
# Returns: 0 on success, 1 on failure
add_mapping() {
    branch="$1"
    session="$2"
    ai_tool="${3:-}"
    group="${4:-}"
    timestamp="${5:-$(get_timestamp)}"

    if [ -z "$branch" ] || [ -z "$session" ]; then
        echo "Error: Branch and session are required" >&2
        return 1
    fi

    if [ -z "$HYDRA_MAP" ]; then
        echo "Error: HYDRA_MAP not set" >&2
        return 1
    fi

    # Build the mapping line based on what fields are provided
    # Format: branch session [ai_tool] [group] [timestamp]
    # Always include timestamp for new entries
    # Use "-" as placeholder for optional fields
    ai_field="${ai_tool:-"-"}"
    group_field="${group:-"-"}"
    mapping_line="$branch $session $ai_field $group_field $timestamp"

    # Invalidate cache before write
    _invalidate_state_cache

    # Use lock to make remove+add atomic
    if try_lock "state_map"; then
        # Remove existing mapping for this branch if any
        remove_mapping "$branch" 2>/dev/null || true

        echo "$mapping_line" >> "$HYDRA_MAP"

        release_lock "state_map"
        return 0
    else
        # Fallback if lock fails - still try the operation
        remove_mapping "$branch" 2>/dev/null || true
        echo "$mapping_line" >> "$HYDRA_MAP"
        return 0
    fi
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

    # Invalidate cache before write
    _invalidate_state_cache

    # Create temporary file
    tmpfile="$(mktemp)" || return 1
    trap 'rm -f "$tmpfile"' EXIT INT TERM

    # Filter out the branch; preserve AI, group, and timestamp columns for others
    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp; do
        if [ "$map_branch" != "$branch" ]; then
            # Preserve all fields that exist
            if [ -n "$map_timestamp" ]; then
                echo "$map_branch $map_session ${map_ai:-"-"} ${map_group:-"-"} $map_timestamp"
            elif [ -n "$map_group" ]; then
                echo "$map_branch $map_session ${map_ai:-"-"} $map_group"
            elif [ -n "$map_ai" ]; then
                echo "$map_branch $map_session $map_ai"
            else
                echo "$map_branch $map_session"
            fi
        fi
    done < "$HYDRA_MAP" > "$tmpfile"

    # Replace original file
    mv "$tmpfile" "$HYDRA_MAP"
    trap - EXIT INT TERM

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
    
    while IFS=' ' read -r branch session _ai _group _timestamp; do
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

    # Invalidate cache before write
    _invalidate_state_cache

    # Create temporary file
    tmpfile="$(mktemp)" || return 1
    trap 'rm -f "$tmpfile"' EXIT INT TERM

    # Keep only valid mappings; preserve AI, group, and timestamp if present
    while IFS=' ' read -r branch session ai group timestamp; do
        if git_branch_exists "$branch" && tmux_session_exists "$session"; then
            if [ -n "$timestamp" ]; then
                echo "$branch $session ${ai:-"-"} ${group:-"-"} $timestamp"
            elif [ -n "$group" ]; then
                echo "$branch $session ${ai:-"-"} $group"
            elif [ -n "$ai" ]; then
                echo "$branch $session $ai"
            else
                echo "$branch $session"
            fi
        fi
    done < "$HYDRA_MAP" > "$tmpfile"

    # Replace original file
    mv "$tmpfile" "$HYDRA_MAP"
    trap - EXIT INT TERM

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

    # Invalidate cache before write
    _invalidate_state_cache

    # Create temporary file
    tmpfile="$(mktemp)" || return 1
    trap 'rm -f "$tmpfile"' EXIT INT TERM

    found=0
    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp; do
        if [ "$map_branch" = "$branch" ]; then
            found=1
            # Preserve AI or use placeholder
            ai="${map_ai:-"-"}"
            # Set new group (or placeholder if clearing)
            new_group="${group:-"-"}"
            # Preserve timestamp or use placeholder
            ts="${map_timestamp:-"-"}"
            echo "$map_branch $map_session $ai $new_group $ts"
        else
            # Preserve existing entry with all fields
            if [ -n "$map_timestamp" ]; then
                echo "$map_branch $map_session ${map_ai:-"-"} ${map_group:-"-"} $map_timestamp"
            elif [ -n "$map_group" ]; then
                echo "$map_branch $map_session ${map_ai:-"-"} $map_group"
            elif [ -n "$map_ai" ]; then
                echo "$map_branch $map_session $map_ai"
            else
                echo "$map_branch $map_session"
            fi
        fi
    done < "$HYDRA_MAP" > "$tmpfile"

    if [ "$found" -eq 0 ]; then
        echo "Error: Branch '$branch' not found in mappings" >&2
        rm -f "$tmpfile"
        trap - EXIT INT TERM
        return 1
    fi

    mv "$tmpfile" "$HYDRA_MAP"
    trap - EXIT INT TERM
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
# Returns: List of "branch session ai group timestamp" entries on stdout
list_mappings_for_group() {
    group="$1"
    if [ -z "$group" ] || [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp; do
        if [ "$map_group" = "$group" ]; then
            echo "$map_branch $map_session ${map_ai:-"-"} $map_group ${map_timestamp:-"-"}"
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
