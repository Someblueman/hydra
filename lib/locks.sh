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

# Hydra's own state, lock and run entries are private whatever the caller's
# umask is (Ubuntu defaults to 002). Readers refuse group- or other-writable
# state, so these helpers scope umask 077 to the creation itself; worktrees,
# agents, tmux and hooks keep inheriting the user's umask. The umask alone is
# not enough: below a directory that carries a default POSIX ACL the kernel
# ignores it and a new directory takes its group bits from that ACL, so each
# directory created here is also set to 0700 explicitly.
# Usage: hydra_private_mkdir <directory>...
hydra_private_mkdir() (
    umask 077
    for _hpm_dir in "$@"; do
        hydra_private_mkdir_one "$_hpm_dir" || exit 1
    done
)

# Creates a directory and its missing parents like mkdir -p; only the
# components it creates are set to 0700, existing ones keep their mode.
# Usage: hydra_private_mkdir_one <directory>
hydra_private_mkdir_one() {
    # macOS chmod takes no "--" (a mode may start with "-"), so a relative
    # name that starts with "-" is passed as ./NAME instead.
    case "$1" in -*) set -- "./$1" ;; esac
    [ -d "$1" ] && return 0
    case "$1" in
        */*) set -- "$1" "${1%/*}" ;;
        *) set -- "$1" . ;;
    esac
    [ -n "$2" ] || set -- "$1" /
    [ "$2" = "$1" ] || hydra_private_mkdir_one "$2" || return 1
    if mkdir "$1" 2>/dev/null; then
        chmod 700 "$1"
    elif [ ! -d "$1" ]; then
        # Repeat the attempt so mkdir reports why it failed.
        mkdir "$1"
    fi
}

# Remove group and other write from your own regular files and directories
# under a Hydra-owned directory, such as outputs a step command wrote with the
# user's umask. Symbolic links are never followed; other owners' entries are
# left unchanged.
# Usage: hydra_private_tree <directory>...
hydra_private_tree() {
    _hptr_uid="$(id -u)"
    _hptr_status=0
    for _hptr_root in "$@"; do
        if [ ! -d "$_hptr_root" ] || [ -L "$_hptr_root" ]; then continue; fi
        find "$_hptr_root" \( -type f -o -type d \) -user "$_hptr_uid" \
            \( -perm -0020 -o -perm -0002 \) -exec chmod go-w {} + 2>/dev/null || _hptr_status=1
    done
    return "$_hptr_status"
}

# Create missing regular files empty and private; existing files keep their
# content and mode, so later redirections into them stay private.
# Usage: hydra_private_touch <file>...
hydra_private_touch() (
    umask 077
    for _hpt_file in "$@"; do
        [ -e "$_hpt_file" ] || [ -L "$_hpt_file" ] || : > "$_hpt_file" || exit 1
    done
)

# Try to acquire a lock for a session name candidate
# Usage: try_lock <candidate_name>
# Returns: 0 if lock acquired, 1 if failed
# Creates directory $1 for this caller alone: false when it exists or when
# another process creates it at the same moment. mkdir's exit status cannot
# decide that: the Rust coreutils Ubuntu ships from 25.10 (0.10.0 in 26.04)
# report success when the directory appears between their own existence
# check and mkdir(2), so two racing callers both "created" it. The shell's
# noclobber creates the claim file with O_EXCL, and exactly one caller gets
# it; a caller that does not must leave the directory to its owner, who
# removes the claim with the rest of it.
hydra_exclusive_mkdir() {
    mkdir "$1" 2>/dev/null || return 1
    (set -C; : > "$1/.claim") 2>/dev/null
}

# Sets _LOCK_HOST_NAME to this host's name as lock owner records carry it.
# The name is asked for once per process.
_LOCK_HOST_NAME=
lock_load_host_name() {
    [ -n "$_LOCK_HOST_NAME" ] ||
        _LOCK_HOST_NAME="$(hostname 2>/dev/null || uname -n 2>/dev/null || echo unknown)"
}

try_lock() {
    candidate="$1"
    operation="${2:-$candidate}"
    # Only lock if HYDRA_HOME is set
    if [ -z "${HYDRA_HOME:-}" ]; then
        return 0
    fi
    lock_dir="$HYDRA_HOME/locks"
    hydra_private_mkdir "$lock_dir" 2>/dev/null || true
    lock_path="$lock_dir/$candidate.lock"
    if ! (umask 077; hydra_exclusive_mkdir "$lock_path") 2>/dev/null; then
        return 1
    fi

    # The directory is the lock. Metadata is diagnostic and enables safe local
    # recovery; a partially written owner record still remains a held lock.
    lock_load_host_name
    _lock_host="$_LOCK_HOST_NAME"
    if ! (
        umask 077
        printf '%s\n' "$$" > "$lock_path/pid" &&
        printf '%s\n' "$_lock_host" > "$lock_path/host" &&
        printf '%s\n' "$(date +%s)" > "$lock_path/created_at" &&
        printf '%s\n' "$operation" > "$lock_path/operation"
    ); then
        rm -f "$lock_path/pid" "$lock_path/host" "$lock_path/created_at" "$lock_path/operation" "$lock_path/.claim"
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
        _rl_pid=
        { IFS= read -r _rl_pid < "$_rl_path/pid"; } 2>/dev/null || :
        # Empty PID supports pre-v1 metadata-free locks acquired by this process.
        if [ -z "$_rl_pid" ] || [ "$_rl_pid" = "$$" ]; then
            rm -f "$_rl_path/pid" "$_rl_path/host" \
                "$_rl_path/created_at" "$_rl_path/operation" \
                "$_rl_path/head_id" "$_rl_path/run_id" "$_rl_path/.claim" 2>/dev/null || true
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
# Usage: acquire_lock <name> [operation]
# HYDRA_LOCK_RETRIES (default 5). Sleep 0.05s between tries when supported.
# The operation is recorded in the lock for diagnostics. A lock whose recorded
# owner is a dead process on this host (a holder that was interrupted or
# killed never releases) is broken and the try repeated at once. A lock owned
# by a live process, by another host, or without a complete owner record is
# never broken, however long it has been held.
# Returns: 0 if acquired, 1 if not
acquire_lock() {
    _al_name="$1"
    _al_operation="${2:-$1}"
    _al_retries="${HYDRA_LOCK_RETRIES:-5}"
    _al_i=0
    case "$_al_retries" in
        ''|*[!0-9]*) _al_retries=5 ;;
    esac
    while [ "$_al_i" -lt "$_al_retries" ]; do
        if try_lock "$_al_name" "$_al_operation"; then
            return 0
        fi
        if [ -n "${HYDRA_HOME:-}" ] &&
            remove_stale_lock_dir "$HYDRA_HOME/locks/$_al_name.lock" 2>/dev/null &&
            try_lock "$_al_name" "$_al_operation"; then
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
# The file is private (0600) whatever the caller's umask: mktemp creates it
# with mode 0600 less the umask, and it runs here under umask 077. Callers
# therefore need no chmod before renaming it into place.
mktemp_adjacent() {
    _ma_dest="$1"
    if [ -z "$_ma_dest" ]; then
        return 1
    fi
    # The directory of an ordinary path, without a dirname process. dirname
    # still answers for the forms it treats specially.
    case "$_ma_dest" in
        */|*//*) _ma_dir="$(dirname "$_ma_dest")" ;;
        */*) _ma_dir="${_ma_dest%/*}"; [ -n "$_ma_dir" ] || _ma_dir=/ ;;
        *) _ma_dir=. ;;
    esac
    if [ ! -d "$_ma_dir" ]; then
        hydra_private_mkdir "$_ma_dir" || return 1
    fi
    (umask 077; mktemp "$_ma_dir/.hydra-tmp.XXXXXX")
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
    _ldis_pid=
    _ldis_owner_host=
    { IFS= read -r _ldis_pid < "$_ldis_path/pid"; } 2>/dev/null || :
    { IFS= read -r _ldis_owner_host < "$_ldis_path/host"; } 2>/dev/null || :
    lock_load_host_name
    _ldis_host="$_LOCK_HOST_NAME"
    case "$_ldis_pid" in ''|*[!0-9]*) return 1 ;; esac
    [ "$_ldis_owner_host" = "$_ldis_host" ] || return 1
    ! kill -0 "$_ldis_pid" 2>/dev/null
}

# True when another live process on this host has announced that it is
# breaking this lock.
# Usage: lock_breaker_active <lock-directory> <own-claim-file>
lock_breaker_active() {
    for _lba_claim in "$1".breaking.*; do
        [ "$_lba_claim" != "$2" ] || continue
        [ -f "$_lba_claim" ] || continue
        _lba_pid="${_lba_claim##*.breaking.}"
        case "$_lba_pid" in ''|*[!0-9]*) continue ;; esac
        if kill -0 "$_lba_pid" 2>/dev/null; then
            return 0
        fi
    done
    return 1
}

# Break a lock whose owner is a dead process on this host.
# Usage: remove_stale_lock_dir <lock-directory>
# Returns: 0 when this caller removed the lock, 1 otherwise
#
# Checking the owner and then deleting the directory is not enough: of two
# callers that both saw the dead owner, the slower one would delete the lock
# the faster one, or anybody else, acquired in between. As the .claim of
# hydra_exclusive_mkdir decides who created a lock, a claim decides who may
# break one:
#   1. Announce: create <lock>.breaking.<pid of this process> next to the lock.
#   2. Look for other announcements. One from a live process means that
#      process may be at step 3 or 4, so withdraw and report failure. Of two
#      breakers at least one sees the other, because each announces before it
#      looks; when both withdraw, their callers retry.
#   3. Check the owner again. No other breaker is past step 2 and a dead owner
#      releases nothing, so a lock that is stale now stays this one until
#      step 4. A lock acquired since the first look has a live owner and is
#      left alone.
#   4. Rename the directory out of the way, which frees the name in one step,
#      and delete it under its new name.
# The claim lies beside the lock, never in it, so a live owner's record and
# its release are not touched. A breaker that dies leaves its claim behind;
# a claim counts only while its process is alive, and a dead one is harmless
# and is never deleted by others (its PID may belong to a new process that
# has just announced under the same name). Only a lock whose recorded host is
# this one gets as far as a claim, so the PID in a claim's name is a local one.
#
# Besides the locks of acquire_lock this accepts the admission lock
# ($HYDRA_HOME/admission/lock, lib/admission.sh), which keeps the same owner
# record in a directory of its own.
remove_stale_lock_dir() {
    _rsld_path="$1"
    [ -n "${HYDRA_HOME:-}" ] || return 1
    case "$_rsld_path" in
        "$HYDRA_HOME"/locks/*.lock|"$HYDRA_HOME"/admission/lock) ;;
        *) return 1 ;;
    esac
    lock_dir_is_stale "$_rsld_path" || return 1
    # $$ stays the main shell's PID in a subshell; the claim has to name the
    # process that makes it, which is the parent of this command substitution.
    _rsld_self="$(exec sh -c 'echo "$PPID"' 2>/dev/null)"
    case "$_rsld_self" in ''|*[!0-9]*) return 1 ;; esac
    _rsld_claim="$_rsld_path.breaking.$_rsld_self"
    (umask 077; : > "$_rsld_claim") 2>/dev/null || return 1
    _rsld_status=1
    if ! lock_breaker_active "$_rsld_path" "$_rsld_claim" && lock_dir_is_stale "$_rsld_path"; then
        _rsld_grave="$_rsld_path.dead.$_rsld_self"
        if mv -f "$_rsld_path" "$_rsld_grave" 2>/dev/null; then
            _rsld_status=0
            # A leftover directory of that name received the lock as a
            # subdirectory; both levels are emptied of what a lock holds.
            for _rsld_dir in "$_rsld_grave/${_rsld_path##*/}" "$_rsld_grave"; do
                [ -d "$_rsld_dir" ] || continue
                rm -f "$_rsld_dir/pid" "$_rsld_dir/host" "$_rsld_dir/created_at" "$_rsld_dir/operation" \
                    "$_rsld_dir/head_id" "$_rsld_dir/run_id" "$_rsld_dir/.claim" 2>/dev/null || true
                rmdir "$_rsld_dir" 2>/dev/null || true
            done
        fi
    fi
    rm -f "$_rsld_claim"
    return "$_rsld_status"
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
