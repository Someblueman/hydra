#!/bin/sh
# Lock management functions for Hydra
# POSIX-compliant shell script
#
# Provides the shared v1 lock protocol: atomic mkdir acquisition, owner metadata,
# evidence-based local stale-lock cleanup, and same-filesystem replacement helpers.

if [ -n "${_LOCKS_MODULE_LOADED:-}" ]; then
    return 0
fi
_LOCKS_MODULE_LOADED=1

# Try to acquire a lock for a session name candidate
# Usage: try_lock <candidate_name>
# Returns: 0 if lock acquired, 1 if failed
try_lock() {
    candidate="$1"
    operation="${2:-$candidate}"
    # Only lock if HYDRA_HOME is set
    if [ -z "${HYDRA_HOME:-}" ]; then
        return 0
    fi
    lock_dir="$HYDRA_HOME/locks"
    mkdir -p "$lock_dir" 2>/dev/null || true
    lock_path="$lock_dir/$candidate.lock"
    if ! mkdir "$lock_path" 2>/dev/null; then
        return 1
    fi

    # The directory is the lock. Metadata is diagnostic and enables safe local
    # recovery; a partially written owner record still remains a held lock.
    _lock_host="$(hostname 2>/dev/null || uname -n 2>/dev/null || echo unknown)"
    if ! (
        umask 077
        printf '%s\n' "$$" > "$lock_path/pid" &&
        printf '%s\n' "$_lock_host" > "$lock_path/host" &&
        printf '%s\n' "$(date +%s)" > "$lock_path/created_at" &&
        printf '%s\n' "$operation" > "$lock_path/operation"
    ); then
        rm -f "$lock_path/pid" "$lock_path/host" "$lock_path/created_at" "$lock_path/operation"
        rmdir "$lock_path" 2>/dev/null || true
        return 1
    fi
    return 0
}

# Release a lock for a session name candidate
# Usage: release_lock <candidate_name>
# Returns: 0 always (best-effort cleanup)
release_lock() {
    candidate="$1"
    if [ -n "${HYDRA_HOME:-}" ] && [ -d "$HYDRA_HOME/locks/$candidate.lock" ]; then
        _rl_path="$HYDRA_HOME/locks/$candidate.lock"
        _rl_pid="$(sed -n '1p' "$_rl_path/pid" 2>/dev/null || true)"
        # Empty PID supports pre-v1 metadata-free locks acquired by this process.
        if [ -z "$_rl_pid" ] || [ "$_rl_pid" = "$$" ]; then
            rm -f "$_rl_path/pid" "$_rl_path/host" \
                "$_rl_path/created_at" "$_rl_path/operation" \
                "$_rl_path/head_id" "$_rl_path/run_id" 2>/dev/null || true
            rmdir "$_rl_path" 2>/dev/null || true
        fi
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
        release_lock "$name"
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

# Return success only for a lock owned by a dead process on this host.
# Usage: lock_dir_is_stale <lock-directory>
lock_dir_is_stale() {
    _ldis_path="$1"
    [ -d "$_ldis_path" ] || return 1
    _ldis_pid="$(sed -n '1p' "$_ldis_path/pid" 2>/dev/null || true)"
    _ldis_owner_host="$(sed -n '1p' "$_ldis_path/host" 2>/dev/null || true)"
    _ldis_host="$(hostname 2>/dev/null || uname -n 2>/dev/null || echo unknown)"
    case "$_ldis_pid" in ''|*[!0-9]*) return 1 ;; esac
    [ "$_ldis_owner_host" = "$_ldis_host" ] || return 1
    ! kill -0 "$_ldis_pid" 2>/dev/null
}

# Remove a lock only after rechecking stale-owner evidence.
# Usage: remove_stale_lock_dir <lock-directory>
remove_stale_lock_dir() {
    _rsld_path="$1"
    case "$_rsld_path" in
        "$HYDRA_HOME"/locks/*.lock) ;;
        *) return 1 ;;
    esac
    lock_dir_is_stale "$_rsld_path" || return 1
    rm -f "$_rsld_path/pid" "$_rsld_path/host" "$_rsld_path/created_at" \
        "$_rsld_path/operation" "$_rsld_path/head_id" "$_rsld_path/run_id" 2>/dev/null || true
    rmdir "$_rsld_path" 2>/dev/null
}

# Clean up locks provably owned by a dead process on this host.
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

    find "$lock_dir" -name "*.lock" -type d 2>/dev/null | while IFS= read -r lock; do
        remove_stale_lock_dir "$lock" 2>/dev/null || true
    done

    return 0
}
