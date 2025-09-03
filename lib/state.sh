#!/bin/sh
# State management functions for Hydra
# POSIX-compliant shell script

# Add a branch-session mapping
# Usage: add_mapping <branch> <session> [ai_tool]
# Returns: 0 on success, 1 on failure
add_mapping() {
    branch="$1"
    session="$2"
    ai_tool="${3:-}"
    
    if [ -z "$branch" ] || [ -z "$session" ]; then
        echo "Error: Branch and session are required" >&2
        return 1
    fi
    
    if [ -z "$HYDRA_MAP" ]; then
        echo "Error: HYDRA_MAP not set" >&2
        return 1
    fi
    
    # Remove existing mapping for this branch if any
    remove_mapping "$branch" 2>/dev/null || true
    
    # Add new mapping (optionally with AI tool)
    if [ -n "$ai_tool" ]; then
        echo "$branch $session $ai_tool" >> "$HYDRA_MAP"
    else
        echo "$branch $session" >> "$HYDRA_MAP"
    fi
    
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
    
    # Create temporary file
    tmpfile="$(mktemp)" || return 1
    trap 'rm -f "$tmpfile"' EXIT INT TERM
    
    # Filter out the branch; preserve optional AI column for others
    while IFS=' ' read -r map_branch map_session map_ai; do
        if [ "$map_branch" != "$branch" ]; then
            suffix=""
            [ -n "$map_ai" ] && suffix=" $map_ai"
            echo "$map_branch $map_session$suffix"
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
    
    if [ -z "$branch" ] || [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 1
    fi
    
    while IFS=' ' read -r map_branch map_session _map_ai; do
        if [ "$map_branch" = "$branch" ]; then
            echo "$map_session"
            return 0
        fi
    done < "$HYDRA_MAP"
    
    return 1
}

# Get branch for a session
# Usage: get_branch_for_session <session>
# Returns: Branch name on stdout, empty if not found
get_branch_for_session() {
    session="$1"
    
    if [ -z "$session" ] || [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 1
    fi
    
    while IFS=' ' read -r map_branch map_session _map_ai; do
        if [ "$map_session" = "$session" ]; then
            echo "$map_branch"
            return 0
        fi
    done < "$HYDRA_MAP"
    
    return 1
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
    
    while IFS=' ' read -r branch session _ai; do
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

# Clean up invalid mappings (preserving optional AI column)
# Usage: cleanup_mappings
# Returns: 0 on success
cleanup_mappings() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    
    # Create temporary file
    tmpfile="$(mktemp)" || return 1
    trap 'rm -f "$tmpfile"' EXIT INT TERM
    
    # Keep only valid mappings; preserve AI if present
    while IFS=' ' read -r branch session ai; do
        if git_branch_exists "$branch" && tmux_session_exists "$session"; then
            suffix=""
            [ -n "$ai" ] && suffix=" $ai"
            echo "$branch $session$suffix"
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
generate_session_name() {
    branch="$1"
    
    if [ -z "$branch" ]; then
        echo "Error: Branch is required" >&2
        return 1
    fi
    
    # Clean branch name for tmux (replace special chars)
    base_name="$(echo "$branch" | sed 's/[^a-zA-Z0-9_-]/_/g')"

    # Helper to attempt locking a candidate name (best-effort)
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

    # Helper to release a lock (best-effort)
    release_lock() {
        candidate="$1"
        if [ -n "${HYDRA_HOME:-}" ] && [ -d "$HYDRA_HOME/locks/$candidate.lock" ]; then
            rmdir "$HYDRA_HOME/locks/$candidate.lock" 2>/dev/null || true
        fi
    }

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

# Release any acquired session name lock (safe to call even if not held)
# Usage: release_session_lock <session_name>
release_session_lock() {
    name="$1"
    if [ -z "$name" ] || [ -z "${HYDRA_HOME:-}" ]; then
        return 0
    fi
    if [ -d "$HYDRA_HOME/locks/$name.lock" ]; then
        rmdir "$HYDRA_HOME/locks/$name.lock" 2>/dev/null || true
    fi
}

# Best-effort cleanup of stale session-name locks (older than 24h)
# Usage: cleanup_stale_locks
# Returns: 0 always (best-effort)
cleanup_stale_locks() {
    # Require HYDRA_HOME
    if [ -z "${HYDRA_HOME:-}" ]; then
        return 0
    fi
    locks_dir="$HYDRA_HOME/locks"
    [ -d "$locks_dir" ] || return 0

    # Prefer 'find' if available with -mtime support
    if command -v find >/dev/null 2>&1; then
        # Collect paths first to avoid invoking -exec portability concerns
        old_paths="$(find "$locks_dir" -type d -name '*.lock' -mtime +1 2>/dev/null || true)"
        if [ -n "$old_paths" ]; then
            # Iterate and remove empty lock dirs
            echo "$old_paths" | while IFS= read -r p; do
                [ -z "$p" ] && continue
                rmdir "$p" 2>/dev/null || true
            done
        fi
        return 0
    fi

    # Fallback: no portable mtime check; do nothing to avoid unsafe deletions
    return 0
}

# Get AI tool for a branch (if stored)
# Usage: get_ai_for_branch <branch>
# Returns: AI tool on stdout, empty if not set
get_ai_for_branch() {
    branch="$1"
    if [ -z "$branch" ] || [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 1
    fi
    while IFS=' ' read -r map_branch _map_session map_ai; do
        if [ "$map_branch" = "$branch" ] && [ -n "$map_ai" ]; then
            echo "$map_ai"
            return 0
        fi
    done < "$HYDRA_MAP"
    return 1
}
