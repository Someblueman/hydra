#!/bin/sh
# Git helper functions for Hydra
# POSIX-compliant shell script

# Check if a git branch exists
# Usage: git_branch_exists <branch_name>
# Returns: 0 if exists, 1 if not
git_branch_exists() {
    branch="$1"
    if [ -z "$branch" ]; then
        return 1
    fi
    
    # Check both local and remote branches
    if git rev-parse --verify --quiet "refs/heads/$branch" >/dev/null 2>&1 || \
       git rev-parse --verify --quiet "refs/remotes/origin/$branch" >/dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

# Create a new git worktree
# Usage: create_worktree <branch> <path>
# Returns: 0 on success, 1 on failure
create_worktree() {
    branch="$1"
    path="$2"
    
    if [ -z "$branch" ] || [ -z "$path" ]; then
        echo "Error: Branch and path are required" >&2
        return 1
    fi
    
    # Check if worktree already exists
    if [ -d "$path" ]; then
        echo "Error: Worktree path already exists: $path" >&2
        return 1
    fi
    
    # Create parent directory if needed
    parent_dir="$(dirname "$path")"
    if [ ! -d "$parent_dir" ]; then
        mkdir -p "$parent_dir" || return 1
    fi
    
    # Create worktree
    if git_branch_exists "$branch"; then
        # Branch exists, create worktree from it
        git worktree add "$path" "$branch" || return 1
    else
        # Branch doesn't exist, create new branch
        git worktree add -b "$branch" "$path" || return 1
    fi
    
    return 0
}

# Delete a git worktree safely
# Usage: delete_worktree <path>
# Returns: 0 on success, 1 on failure
delete_worktree() {
    path="$1"
    
    if [ -z "$path" ]; then
        echo "Error: Worktree path is required" >&2
        return 1
    fi
    
    if [ ! -d "$path" ]; then
        echo "Error: Worktree path does not exist: $path" >&2
        return 1
    fi
    
    # Check for uncommitted changes
    if ! git -C "$path" diff --quiet || ! git -C "$path" diff --cached --quiet; then
        echo "Error: Worktree has uncommitted changes" >&2
        echo "Please commit or stash your changes first" >&2
        return 1
    fi
    
    # Check for untracked files (optional warning)
    if [ -n "$(git -C "$path" ls-files --others --exclude-standard)" ]; then
        echo "Warning: Worktree has untracked files" >&2
        printf "Continue anyway? [y/N] "
        read -r response
        case "$response" in
            [yY][eE][sS]|[yY])
                ;;
            *)
                echo "Aborted" >&2
                return 1
                ;;
        esac
    fi
    
    # Remove the worktree
    git worktree remove "$path" || return 1
    
    return 0
}

# Get the branch name for a worktree path
# Usage: get_worktree_branch <path>
# Returns: Branch name on stdout, empty if not found
get_worktree_branch() {
    path="$1"
    
    if [ -z "$path" ] || [ ! -d "$path" ]; then
        return 1
    fi
    
    # Get absolute path
    abs_path="$(cd "$path" 2>/dev/null && pwd)" || return 1
    
    # Parse git worktree list output
    git worktree list --porcelain | while IFS= read -r line; do
        case "$line" in
            "worktree $abs_path")
                # Found our worktree, next line should have the branch
                IFS= read -r branch_line
                case "$branch_line" in
                    "branch refs/heads/"*)
                        # Extract branch name
                        echo "${branch_line#branch refs/heads/}"
                        return 0
                        ;;
                esac
                ;;
        esac
    done
}

# List all worktrees
# Usage: list_worktrees
# Returns: List of "path branch" pairs on stdout
list_worktrees() {
    git worktree list --porcelain | while IFS= read -r line; do
        case "$line" in
            "worktree "*)
                worktree_path="${line#worktree }"
                # Read next lines to find branch
                while IFS= read -r next_line && [ -n "$next_line" ]; do
                    case "$next_line" in
                        "branch refs/heads/"*)
                            branch="${next_line#branch refs/heads/}"
                            echo "$worktree_path $branch"
                            break
                            ;;
                    esac
                done
                ;;
        esac
    done
}