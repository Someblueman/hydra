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

# Get path prefix for hydra worktrees (parent/hydra-)
# Usage: get_hydra_worktree_prefix [repo_root]
# Returns: Prefix path on stdout
get_hydra_worktree_prefix() {
    repo_root="${1:-}"
    if [ -z "$repo_root" ]; then
        repo_root="$(get_repo_root)" || return 1
    fi
    parent_dir="$(dirname "$repo_root")"
    printf '%s' "$parent_dir/hydra-"
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
    prefix="$(get_hydra_worktree_prefix "$repo_root")"
    printf '%s%s' "$prefix" "$branch"
}

# Extract branch name from a hydra worktree path
# Usage: branch_from_hydra_worktree_path <path> [repo_root]
# Returns: Branch name on stdout, 1 if not a hydra worktree path
branch_from_hydra_worktree_path() {
    path="$1"
    repo_root="${2:-}"
    if [ -z "$path" ]; then
        return 1
    fi
    if [ -z "$repo_root" ]; then
        repo_root="$(get_repo_root 2>/dev/null)" || return 1
    fi
    prefix="$(get_hydra_worktree_prefix "$repo_root")"
    case "$path" in
        "$prefix"*)
            printf '%s' "${path#"$prefix"}"
            return 0
            ;;
    esac
    return 1
}

# Check if a path is a hydra worktree directory
# Usage: is_hydra_worktree_path <path> [repo_root]
# Returns: 0 if hydra worktree path, 1 otherwise
is_hydra_worktree_path() {
    branch_from_hydra_worktree_path "$1" "${2:-}" >/dev/null 2>&1
}

# List hydra worktrees from git worktree list
# Usage: list_hydra_worktrees [repo_root]
# Returns: Tab-separated branch and path lines on stdout
list_hydra_worktrees() {
    repo_root="${1:-}"
    if [ -z "$repo_root" ]; then
        repo_root="$(get_repo_root)" || return 1
    fi
    prefix="$(get_hydra_worktree_prefix "$repo_root")"

    git worktree list --porcelain 2>/dev/null | while IFS= read -r line; do
        case "$line" in
            worktree\ *)
                _path="${line#worktree }"
                case "$_path" in
                    "$prefix"*)
                        if [ -d "$_path" ]; then
                            _branch="${_path#"$prefix"}"
                            printf '%s\t%s\n' "$_branch" "$_path"
                        fi
                        ;;
                esac
                ;;
        esac
    done
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
