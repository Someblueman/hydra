#!/bin/sh
# State management functions for Hydra
# POSIX-compliant shell script

# Add a branch-session mapping
# Usage: add_mapping <branch> <session>
# Returns: 0 on success, 1 on failure
add_mapping() {
    branch="$1"
    session="$2"
    
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
    
    # Add new mapping
    echo "$branch $session" >> "$HYDRA_MAP"
    
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
    
    # Filter out the branch
    while IFS=' ' read -r map_branch map_session; do
        if [ "$map_branch" != "$branch" ]; then
            echo "$map_branch $map_session"
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
    
    while IFS=' ' read -r map_branch map_session; do
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
    
    while IFS=' ' read -r map_branch map_session; do
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
    
    while IFS=' ' read -r branch session; do
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

# Clean up invalid mappings
# Usage: cleanup_mappings
# Returns: 0 on success
cleanup_mappings() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi
    
    # Create temporary file
    tmpfile="$(mktemp)" || return 1
    trap 'rm -f "$tmpfile"' EXIT INT TERM
    
    # Keep only valid mappings
    while IFS=' ' read -r branch session; do
        if git_branch_exists "$branch" && tmux_session_exists "$session"; then
            echo "$branch $session"
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
    
    # If session doesn't exist, use base name
    if ! tmux has-session -t "$base_name" 2>/dev/null; then
        echo "$base_name"
        return 0
    fi
    
    # Otherwise, append a number - start from 1 and find first available
    num=1
    max_attempts=100  # Prevent infinite loop in edge cases
    
    while [ "$num" -le "$max_attempts" ]; do
        session_name="${base_name}_${num}"
        if ! tmux has-session -t "$session_name" 2>/dev/null; then
            echo "$session_name"
            return 0
        fi
        num=$((num + 1))
    done
    
    # If we've exhausted attempts, use timestamp for uniqueness
    timestamp="$(date +%s 2>/dev/null || echo "$$")"
    echo "${base_name}_${timestamp}"
}