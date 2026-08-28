#!/bin/sh
# State cache implementation for Hydra (private API)
# POSIX-compliant shell script
#
# Loaded by lib/state.sh — do not source directly.

# =============================================================================
# State Cache Implementation
# =============================================================================
# Uses byte-encoded variable names to provide O(1) lookups in POSIX shell.
# Cache is loaded once per command and invalidated on write operations.

# Cache state flag (empty = not loaded)
_STATE_CACHE_LOADED=""
_STATE_CACHE_VARIABLES=""

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
        # Count fields using POSIX word splitting (perf: avoid awk)
        # shellcheck disable=SC2086
        set -- $line
        if [ $# -lt 2 ]; then
            malformed=$((malformed + 1))
        fi
    done < "$HYDRA_MAP"

    if [ "$malformed" -gt 0 ]; then
        if ! acquire_lock "state_map"; then
            echo "Warning: State file has $malformed malformed line(s); lock busy, skipping repair" >&2
            return 0
        fi

        # Backup corrupted file
        cp "$HYDRA_MAP" "${HYDRA_MAP}.bak" 2>/dev/null || true

        tmpfile="$(mktemp_adjacent "$HYDRA_MAP")" || {
            release_lock "state_map"
            return 1
        }
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            # Count fields using POSIX word splitting (perf: avoid awk)
            # shellcheck disable=SC2086
            set -- $line
            if [ $# -ge 2 ]; then
                echo "$line" >> "$tmpfile"
            fi
        done < "$HYDRA_MAP"
        if ! atomic_replace "$HYDRA_MAP" "$tmpfile"; then
            rm -f "$tmpfile"
            release_lock "state_map"
            return 1
        fi
        release_lock "state_map"

        echo "Warning: Repaired $malformed malformed line(s) in state file" >&2
        echo "  Backup saved to: ${HYDRA_MAP}.bak" >&2
    fi

    return 0
}

# Encode a key for use in variable names without collapsing distinct names.
# POSIX od emits one hexadecimal byte per input byte; command substitution removes
# the trailing newline and tr removes formatting spaces.
# Usage: _encode_cache_key <key>
_encode_cache_key() {
    printf '%s' "$1" | od -An -tx1 | tr -d ' \n'
}

# Store a cache variable and remember it so invalidation can remove stale values.
# Usage: _store_cache_variable <variable_name> <value>
_store_cache_variable() {
    _scv_name="$1"
    _scv_value="$2"
    eval "$_scv_name=\$_scv_value"
    _STATE_CACHE_VARIABLES="${_STATE_CACHE_VARIABLES}${_STATE_CACHE_VARIABLES:+ }${_scv_name}"
}

_clear_cache_variables() {
    for _ccv_name in $_STATE_CACHE_VARIABLES; do
        unset "$_ccv_name"
    done
    _STATE_CACHE_VARIABLES=""
}

# Load state cache from mapping file
# Creates lookup variables for branch->session, session->branch, branch->ai, branch->group, branch->timestamp, branch->deps, branch->pr
# Usage: _load_state_cache
_load_state_cache() {
    # Already loaded?
    if [ -n "$_STATE_CACHE_LOADED" ]; then
        return 0
    fi

    # A caller may explicitly request a reload. Remove values for records or
    # optional fields that may no longer exist before rebuilding the cache.
    _clear_cache_variables

    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        _STATE_CACHE_LOADED="empty"
        return 0
    fi

    # Validate and repair state file if needed (first time only)
    _validate_and_repair_state_file

    # Read all mappings and create lookup variables
    # Format: branch session [ai_tool] [group] [timestamp] [deps] [pr_number]
    while IFS=' ' read -r map_branch map_session map_ai map_group map_timestamp map_deps map_pr; do
        [ -z "$map_branch" ] && continue

        # Create collision-free keys from the original branch and session bytes.
        _key_b="$(_encode_cache_key "$map_branch")"
        _key_s="$(_encode_cache_key "$map_session")"

        # Store mappings using eval only with byte-encoded variable names.
        _store_cache_variable "_sc_b2s_${_key_b}" "$map_session"
        _store_cache_variable "_sc_s2b_${_key_s}" "$map_branch"

        # Store AI tool if present
        if [ -n "$map_ai" ] && [ "$map_ai" != "-" ]; then
            _store_cache_variable "_sc_b2ai_${_key_b}" "$map_ai"
        fi

        # Store group if present
        if [ -n "$map_group" ] && [ "$map_group" != "-" ]; then
            _store_cache_variable "_sc_b2grp_${_key_b}" "$map_group"
        fi

        # Store timestamp if present
        if [ -n "$map_timestamp" ] && [ "$map_timestamp" != "-" ]; then
            _store_cache_variable "_sc_b2ts_${_key_b}" "$map_timestamp"
        fi

        # Store dependencies if present
        if [ -n "$map_deps" ] && [ "$map_deps" != "-" ]; then
            _store_cache_variable "_sc_b2deps_${_key_b}" "$map_deps"
        fi

        # Store PR number if present
        if [ -n "$map_pr" ] && [ "$map_pr" != "-" ]; then
            _store_cache_variable "_sc_b2pr_${_key_b}" "$map_pr"
        fi
    done < "$HYDRA_MAP"

    _STATE_CACHE_LOADED="loaded"
    return 0
}

# Invalidate state cache (call after writes)
# Usage: _invalidate_state_cache
_invalidate_state_cache() {
    _STATE_CACHE_LOADED=""
    _clear_cache_variables
}

# Get session from cache
# Usage: _cache_get_session <branch>
# Returns: session name on stdout, 1 if not found
_cache_get_session() {
    _load_state_cache
    _key="$(_encode_cache_key "$1")"
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
    _key="$(_encode_cache_key "$1")"
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
    _key="$(_encode_cache_key "$1")"
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
    _key="$(_encode_cache_key "$1")"
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
    _key="$(_encode_cache_key "$1")"
    eval "_result=\"\${_sc_b2ts_${_key}:-}\""
    if [ -n "$_result" ]; then
        printf '%s\n' "$_result"
        return 0
    fi
    return 1
}

# Get dependencies from cache
# Usage: _cache_get_deps <branch>
# Returns: comma-separated deps on stdout, 1 if not found
_cache_get_deps() {
    _load_state_cache
    _key="$(_encode_cache_key "$1")"
    eval "_result=\"\${_sc_b2deps_${_key}:-}\""
    if [ -n "$_result" ]; then
        printf '%s\n' "$_result"
        return 0
    fi
    return 1
}

# Get PR number from cache
# Usage: _cache_get_pr <branch>
# Returns: PR number on stdout, 1 if not found
_cache_get_pr() {
    _load_state_cache
    _key="$(_encode_cache_key "$1")"
    eval "_result=\"\${_sc_b2pr_${_key}:-}\""
    if [ -n "$_result" ]; then
        printf '%s\n' "$_result"
        return 0
    fi
    return 1
}
