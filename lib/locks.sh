#!/bin/sh
# Lock management functions for Hydra
# POSIX-compliant shell script
#
# Provides atomic locking via mkdir for POSIX atomicity, plus same-filesystem
# replacement helpers for state files.
# Do not introduce PID files here; that lock protocol belongs to a later release.

if [ -n "${_LOCKS_MODULE_LOADED:-}" ]; then
    return 0
fi
_LOCKS_MODULE_LOADED=1

# Try to acquire a lock for a session name candidate
# Usage: try_lock <candidate_name>
# Returns: 0 if lock acquired, 1 if failed
try_lock() {
    candidate="$1"
    # Only lock if HYDRA_HOME is set
    if [ -z "${HYDRA_HOME:-}" ]; then
        return 0
    fi
    lock_dir="$HYDRA_HOME/locks"
    mkdir -p "$lock_dir" 2>/dev/null || true
    mkdir "$lock_dir/$candidate.lock" 2>/dev/null
}

# Release a lock for a session name candidate
# Usage: release_lock <candidate_name>
# Returns: 0 always (best-effort cleanup)
release_lock() {
    candidate="$1"
    if [ -n "${HYDRA_HOME:-}" ] && [ -d "$HYDRA_HOME/locks/$candidate.lock" ]; then
        rmdir "$HYDRA_HOME/locks/$candidate.lock" 2>/dev/null || true
    fi
}

# Release any acquired session name lock (safe to call even if not held)
# Usage: release_session_lock <session_name>
# Returns: 0 always (best-effort cleanup)
release_session_lock() {
    name="$1"
    if [ -z "$name" ] || [ -z "${HYDRA_HOME:-}" ]; then
        return 0
    fi
    if [ -d "$HYDRA_HOME/locks/$name.lock" ]; then
        rmdir "$HYDRA_HOME/locks/$name.lock" 2>/dev/null || true
    fi
}

# Acquire a lock with retries, then fail closed.
# Usage: acquire_lock <name>
# HYDRA_LOCK_RETRIES (default 5). Sleep 0.05s between tries when supported.
# Returns: 0 if acquired, 1 if not
acquire_lock() {
    _al_name="$1"
    _al_retries="${HYDRA_LOCK_RETRIES:-5}"
    _al_i=0
    case "$_al_retries" in
        ''|*[!0-9]*) _al_retries=5 ;;
    esac
    while [ "$_al_i" -lt "$_al_retries" ]; do
        if try_lock "$_al_name"; then
            return 0
        fi
        _al_i=$((_al_i + 1))
        if [ "$_al_i" -lt "$_al_retries" ]; then
            sleep 0.05 2>/dev/null || sleep 1
        fi
    done
    return 1
}

# Create a temporary file in the same directory as dest (same filesystem).
# Usage: mktemp_adjacent <dest>
# Returns: temp path on stdout
mktemp_adjacent() {
    _ma_dest="$1"
    if [ -z "$_ma_dest" ]; then
        return 1
    fi
    _ma_dir="$(dirname "$_ma_dest")"
    if [ ! -d "$_ma_dir" ]; then
        mkdir -p "$_ma_dir" || return 1
    fi
    mktemp "$_ma_dir/.hydra-tmp.XXXXXX"
}

# Atomically replace dest with tmpfile via rename. tmpfile must be same-FS.
# Usage: atomic_replace <dest> <tmpfile>
# Returns: 0 on success, 1 on failure
atomic_replace() {
    _ar_dest="$1"
    _ar_tmp="$2"
    if [ -z "$_ar_dest" ] || [ -z "$_ar_tmp" ] || [ ! -f "$_ar_tmp" ]; then
        return 1
    fi
    mv "$_ar_tmp" "$_ar_dest"
}

# Clean up stale session-name locks (older than 60 seconds / 1 minute)
# Usage: cleanup_stale_locks
# Returns: 0 always (best-effort cleanup)
cleanup_stale_locks() {
    # Early return if no HYDRA_HOME or locks directory
    if [ -z "${HYDRA_HOME:-}" ]; then
        return 0
    fi
    lock_dir="$HYDRA_HOME/locks"
    if [ ! -d "$lock_dir" ]; then
        return 0
    fi

    # Find and remove lock directories older than 1 minute
    # Uses portable find command with -mmin for minute-based age
    find "$lock_dir" -name "*.lock" -type d -mmin +1 2>/dev/null | while IFS= read -r lock; do
        rmdir "$lock" 2>/dev/null || true
    done

    return 0
}
