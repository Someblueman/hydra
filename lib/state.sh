#!/bin/sh
# State management functions for Hydra
# POSIX-compliant shell script

# Compute a stable repo identifier for scoping (basename + 8-char sha1 of repo root)
# Usage: get_current_repo_id
# Echoes repo id or empty if not in a git repo
get_current_repo_id() {
    if git rev-parse --git-dir >/dev/null 2>&1; then
        repo_root="$(git rev-parse --show-toplevel 2>/dev/null || true)"
        if [ -n "$repo_root" ]; then
            base="$(basename "$repo_root")"
            # sha1 via shasum or fallback to md5sum; as last resort, use path length hash
            if command -v shasum >/dev/null 2>&1; then
                h="$(printf '%s' "$repo_root" | shasum | awk '{print $1}' | cut -c1-8)"
            elif command -v sha1sum >/dev/null 2>&1; then
                h="$(printf '%s' "$repo_root" | sha1sum | awk '{print $1}' | cut -c1-8)"
            elif command -v md5 >/dev/null 2>&1; then
                h="$(printf '%s' "$repo_root" | md5 | awk '{print $1}' | cut -c1-8)"
            elif command -v md5sum >/dev/null 2>&1; then
                h="$(printf '%s' "$repo_root" | md5sum | awk '{print $1}' | cut -c1-8)"
            else
                # Very weak fallback hash
                h="$(printf '%s' "$repo_root" | wc -c | tr -d ' ')"
            fi
            # Sanitize base and compose id without spaces
            base_safe="$(printf '%s' "$base" | sed 's/[^a-zA-Z0-9_-]/_/g')"
            printf '%s\n' "repo:${base_safe}-${h}"
            return 0
        fi
    fi
    return 1
}

# Add a branch-session mapping (scoped by current repo when available)
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
    
    # Remove existing mapping for this branch in this repo (best-effort)
    remove_mapping "$branch" 2>/dev/null || true

    # Determine repo id (optional)
    repo_id=""
    if repo_id_val="$(get_current_repo_id 2>/dev/null || true)" && [ -n "$repo_id_val" ]; then
        repo_id="$repo_id_val"
    fi

    # Add new mapping (optionally with AI tool). Include repo id if available.
    if [ -n "$repo_id" ]; then
        if [ -n "$ai_tool" ]; then
            echo "$repo_id $branch $session $ai_tool" >> "$HYDRA_MAP"
        else
            echo "$repo_id $branch $session" >> "$HYDRA_MAP"
        fi
    else
        if [ -n "$ai_tool" ]; then
            echo "$branch $session $ai_tool" >> "$HYDRA_MAP"
        else
            echo "$branch $session" >> "$HYDRA_MAP"
        fi
    fi
    
    return 0
}

# Remove a branch-session mapping for current repo
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
    
    # Determine repo id (optional)
    repo_id=""
    if repo_id_val="$(get_current_repo_id 2>/dev/null || true)" && [ -n "$repo_id_val" ]; then
        repo_id="$repo_id_val"
    fi

    # Filter out the branch for this repo; preserve others unchanged (using awk for robustness)
    awk -v rid="$repo_id" -v target="$branch" '
      BEGIN { OFS=" " }
      NF==0 { next }
      $1 ~ /^repo:/ {
        if (rid != "" && $1 == rid && $2 == target) next
        print $0
        next
      }
      {
        if ($1 == target) next
        print $0
      }
    ' "$HYDRA_MAP" > "$tmpfile"
    
    # Replace original file
    mv "$tmpfile" "$HYDRA_MAP"
    trap - EXIT INT TERM
    
    return 0
}

# Get session for a branch in current repo
# Usage: get_session_for_branch <branch>
# Returns: Session name on stdout, empty if not found
get_session_for_branch() {
    branch="$1"
    
    if [ -z "$branch" ] || [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 1
    fi
    
    repo_id=""
    if repo_id_val="$(get_current_repo_id 2>/dev/null || true)" && [ -n "$repo_id_val" ]; then
        repo_id="$repo_id_val"
    fi
    # Prefer repo-scoped entries
    while IFS=' ' read -r c1 c2 c3 c4; do
        if [ -z "$c1" ]; then continue; fi
        if [ "${c1#repo:}" != "$c1" ]; then
            map_repo="$c1"; map_branch="$c2"; map_session="$c3"
        else
            map_repo=""; map_branch="$c1"; map_session="$c2"
        fi
        if [ -n "$repo_id" ] && [ "$map_repo" = "$repo_id" ] && [ "$map_branch" = "$branch" ]; then
            printf '%s\n' "$map_session"; return 0
        fi
    done < "$HYDRA_MAP"
    # Fallback to legacy entries if no repo-scoped mapping found
    if [ -z "$repo_id" ]; then
        return 1
    fi
    while IFS=' ' read -r map_branch map_session _map_ai; do
        if [ "$map_branch" = "$branch" ]; then
            printf '%s\n' "$map_session"; return 0
        fi
    done < "$HYDRA_MAP"
    
    return 1
}

# Get branch for a session in current repo
# Usage: get_branch_for_session <session>
# Returns: Branch name on stdout, empty if not found
get_branch_for_session() {
    session="$1"
    
    if [ -z "$session" ] || [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 1
    fi
    
    repo_id=""
    if repo_id_val="$(get_current_repo_id 2>/dev/null || true)" && [ -n "$repo_id_val" ]; then
        repo_id="$repo_id_val"
    fi
    while IFS=' ' read -r c1 c2 c3 c4; do
        if [ -z "$c1" ]; then continue; fi
        if [ "${c1#repo:}" != "$c1" ]; then
            map_repo="$c1"; map_branch="$c2"; map_session="$c3"
        else
            map_repo=""; map_branch="$c1"; map_session="$c2"
        fi
        if [ -n "$repo_id" ] && [ "$map_repo" = "$repo_id" ] && [ "$map_session" = "$session" ]; then
            printf '%s\n' "$map_branch"; return 0
        fi
    done < "$HYDRA_MAP"
    # Fallback to legacy lines
    if [ -z "$repo_id" ]; then
        return 1
    fi
    while IFS=' ' read -r map_branch map_session _map_ai; do
        if [ "$map_session" = "$session" ]; then
            printf '%s\n' "$map_branch"; return 0
        fi
    done < "$HYDRA_MAP"
    
    return 1
}

# List all mappings (raw; backward compatible)
# Usage: list_mappings
# Returns: Raw contents of HYDRA_MAP on stdout
list_mappings() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    
    cat "$HYDRA_MAP"
}

# List mappings for current repo only, printing: "branch session" or "branch session ai"
# Usage: list_mappings_current_repo
list_mappings_current_repo() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    repo_id=""
    if repo_id_val="$(get_current_repo_id 2>/dev/null || true)" && [ -n "$repo_id_val" ]; then
        repo_id="$repo_id_val"
    fi
    while IFS=' ' read -r c1 c2 c3 c4; do
        [ -z "$c1" ] && continue
        if [ "${c1#repo:}" != "$c1" ]; then
            map_repo="$c1"; map_branch="$c2"; map_session="$c3"; map_ai="$c4"
        else
            map_repo=""; map_branch="$c1"; map_session="$c2"; map_ai="$c3"
        fi
        if [ -n "$repo_id" ] && { [ "$map_repo" = "$repo_id" ] || [ -z "$map_repo" ]; }; then
            if [ -n "$map_ai" ]; then
                printf '%s %s %s\n' "$map_branch" "$map_session" "$map_ai"
            else
                printf '%s %s\n' "$map_branch" "$map_session"
            fi
        fi
    done < "$HYDRA_MAP"
}

# Validate mappings for current repo against actual state
# Usage: validate_mappings
# Returns: 0 if all valid, 1 if inconsistencies found
validate_mappings() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    
    errors=0
    repo_id=""
    if repo_id_val="$(get_current_repo_id 2>/dev/null || true)" && [ -n "$repo_id_val" ]; then
        repo_id="$repo_id_val"
    fi
    
    while IFS=' ' read -r c1 c2 c3 c4; do
        [ -z "$c1" ] && continue
        if [ "${c1#repo:}" != "$c1" ]; then
            map_repo="$c1"; branch="$c2"; session="$c3"
        else
            map_repo=""; branch="$c1"; session="$c2"
        fi
        # Only validate entries for current repo id (skip others and legacy)
        if [ -n "$repo_id" ] && [ "$map_repo" = "$repo_id" ]; then
            if ! git_branch_exists "$branch"; then
                echo "Warning: Branch '$branch' no longer exists" >&2
                errors=1
            fi
            if ! tmux_session_exists "$session"; then
                echo "Warning: Session '$session' no longer exists" >&2
                errors=1
            fi
        fi
    done < "$HYDRA_MAP"
    
    return $errors
}

# Clean up invalid mappings for current repo (preserving others and optional AI column)
# Usage: cleanup_mappings
# Returns: 0 on success
cleanup_mappings() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    
    # Create temporary file
    tmpfile="$(mktemp)" || return 1
    trap 'rm -f "$tmpfile"' EXIT INT TERM
    
    repo_id=""
    if repo_id_val="$(get_current_repo_id 2>/dev/null || true)" && [ -n "$repo_id_val" ]; then
        repo_id="$repo_id_val"
    fi
    
    # Re-emit entries: validate current repo entries; keep others untouched
    while IFS=' ' read -r c1 c2 c3 c4; do
        [ -z "$c1" ] && continue
        if [ "${c1#repo:}" != "$c1" ]; then
            map_repo="$c1"; branch="$c2"; session="$c3"; ai="$c4"
        else
            map_repo=""; branch="$c1"; session="$c2"; ai="$c3"
        fi
        if [ -n "$repo_id" ] && [ "$map_repo" = "$repo_id" ]; then
            if git_branch_exists "$branch" && tmux_session_exists "$session"; then
                [ -n "$ai" ] && echo "$map_repo $branch $session $ai" >> "$tmpfile" || echo "$map_repo $branch $session" >> "$tmpfile"
            fi
        else
            # Preserve non-current-repo entries as-is
            if [ -n "$map_repo" ]; then
                [ -n "$ai" ] && echo "$map_repo $branch $session $ai" >> "$tmpfile" || echo "$map_repo $branch $session" >> "$tmpfile"
            else
                [ -n "$ai" ] && echo "$branch $session $ai" >> "$tmpfile" || echo "$branch $session" >> "$tmpfile"
            fi
        fi
    done < "$HYDRA_MAP"
    
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

# Get AI tool for a branch (if stored) for current repo
# Usage: get_ai_for_branch <branch>
# Returns: AI tool on stdout, empty if not set
get_ai_for_branch() {
    branch="$1"
    if [ -z "$branch" ] || [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 1
    fi
    repo_id=""
    if repo_id_val="$(get_current_repo_id 2>/dev/null || true)" && [ -n "$repo_id_val" ]; then
        repo_id="$repo_id_val"
    fi
    # Prefer repo-scoped entries
    while IFS=' ' read -r c1 c2 c3 c4; do
        [ -z "$c1" ] && continue
        if [ "${c1#repo:}" != "$c1" ]; then
            map_repo="$c1"; map_branch="$c2"; map_ai="$c4"
        else
            map_repo=""; map_branch="$c1"; map_ai="$c3"
        fi
        if [ -n "$repo_id" ] && [ "$map_repo" = "$repo_id" ] && [ "$map_branch" = "$branch" ] && [ -n "$map_ai" ]; then
            printf '%s\n' "$map_ai"; return 0
        fi
    done < "$HYDRA_MAP"
    # Fallback to legacy
    if [ -z "$repo_id" ]; then
        return 1
    fi
    while IFS=' ' read -r map_branch _map_session map_ai; do
        if [ "$map_branch" = "$branch" ] && [ -n "$map_ai" ]; then
            printf '%s\n' "$map_ai"; return 0
        fi
    done < "$HYDRA_MAP"
    return 1
}
