#!/bin/sh
# Lock management functions for Hydra
# POSIX-compliant shell script
#
# Provides atomic locking for session name generation to prevent race conditions.
# Locks are implemented using mkdir for POSIX atomicity.

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

# Clean up stale session-name locks (older than 60 seconds)
# Usage: cleanup_stale_locks
# Returns: 0 always (best-effort cleanup)
# Note: This function was previously undefined but called in bin/hydra
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
