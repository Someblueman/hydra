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
        echo "Next: cd into an existing git repo, or create a throwaway repo (see README Quick Start)" >&2
        return 1
    fi
    git rev-parse --show-toplevel
}

# Get parent directory used for hydra worktrees (sibling to repo root)
# Usage: get_hydra_worktree_parent [repo_root]
# Returns: Parent directory path on stdout
get_hydra_worktree_parent() {
    repo_root="${1:-}"
    if [ -z "$repo_root" ]; then
        repo_root="$(get_repo_root)" || return 1
    fi
    dirname "$repo_root"
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
