#!/bin/sh
# Path utility functions for Hydra
# POSIX-compliant shell script
#
# Provides consolidated path operations to eliminate code duplication.

# Get the repository root directory
# Usage: get_repo_root
# Returns: Repository root path on stdout, exits 1 if not in a repo
get_repo_root() {
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "Error: Not in a git repository" >&2
        return 1
    fi
    git rev-parse --show-toplevel
}

# Calculate worktree path for a branch
# Usage: get_worktree_path_for_branch <branch>
# Returns: Worktree path on stdout
# Note: Does not verify the path exists
get_worktree_path_for_branch() {
    branch="$1"
    if [ -z "$branch" ]; then
        echo "Error: Branch name required" >&2
        return 1
    fi
    repo_root="$(get_repo_root)" || return 1
    echo "$repo_root/../hydra-$branch"
}

# Normalize a path to absolute form
# Usage: normalize_path <path>
# Returns: Normalized absolute path on stdout
# Falls back to original path if resolution fails
normalize_path() {
    path="$1"
    if [ -z "$path" ]; then
        return 1
    fi
    if [ -d "$path" ]; then
        # shellcheck disable=SC2164
        cd "$path" && pwd
    else
        # Return original if path doesn't exist or isn't a directory
        echo "$path"
    fi
}
