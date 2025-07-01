#!/bin/sh
# Git helper functions for Hydra
# POSIX-compliant shell script

# Validate branch name for security
# Usage: validate_branch_name <branch_name>
# Returns: 0 if valid, 1 if invalid
validate_branch_name() {
    branch="$1"
    
    if [ -z "$branch" ]; then
        echo "Error: Branch name cannot be empty" >&2
        return 1
    fi
    
    # Check for dangerous characters and patterns
    case "$branch" in
        -*) 
            echo "Error: Branch name cannot start with '-': $branch" >&2
            return 1
            ;;
        *[";|&\`\$(){}[]<>?*'"]*) 
            echo "Error: Branch name contains invalid characters: $branch" >&2
            return 1
            ;;
        *..*) 
            echo "Error: Branch name contains dangerous path patterns: $branch" >&2
            return 1
            ;;
    esac
    
    # Git has additional restrictions on branch names
    # Check length (255 chars is reasonable limit)
    if [ ${#branch} -gt 255 ]; then
        echo "Error: Branch name too long (max 255 characters): $branch" >&2
        return 1
    fi
    
    return 0
}

# Validate worktree path for security
# Usage: validate_worktree_path <path>
# Returns: 0 if valid, 1 if invalid
validate_worktree_path() {
    path="$1"
    
    if [ -z "$path" ]; then
        echo "Error: Path cannot be empty" >&2
        return 1
    fi
    
    # Check for dangerous path patterns
    case "$path" in
        ..*) 
            echo "Error: Path contains directory traversal: $path" >&2
            return 1
            ;;
        */../*) 
            echo "Error: Path contains directory traversal: $path" >&2
            return 1
            ;;
        /*) 
            # Absolute paths are OK, but validate they're not system directories
            case "$path" in
                /bin/*|/sbin/*|/usr/bin/*|/usr/sbin/*|/etc/*|/boot/*|/sys/*|/proc/*) 
                    echo "Error: Path points to system directory: $path" >&2
                    return 1
                    ;;
            esac
            ;;
    esac
    
    return 0
}

# Check if a git branch exists
# Usage: git_branch_exists <branch_name>
# Returns: 0 if exists, 1 if not
git_branch_exists() {
    branch="$1"
    if [ -z "$branch" ]; then
        return 1
    fi
    
    # Validate branch name for security
    if ! validate_branch_name "$branch"; then
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
    
    # Validate branch name for security
    if ! validate_branch_name "$branch"; then
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
        git worktree add -- "$path" "$branch" >&2 || return 1
    else
        # Branch doesn't exist, create new branch
        git worktree add -b "$branch" -- "$path" >&2 || return 1
    fi
    
    return 0
}

# Delete a git worktree safely
# Usage: delete_worktree <path> [force]
# Returns: 0 on success, 1 on failure
delete_worktree() {
    path="$1"
    force="${2:-}"
    
    if [ -z "$path" ]; then
        echo "Error: Worktree path is required" >&2
        return 1
    fi
    
    # Validate path for security
    if ! validate_worktree_path "$path"; then
        return 1
    fi
    
    if [ ! -d "$path" ]; then
        echo "Error: Worktree path does not exist: $path" >&2
        return 1
    fi
    
    # If force is specified, skip checks
    if [ "$force" = "force" ]; then
        git worktree remove --force -- "$path" || return 1
        return 0
    fi
    
    # Check for uncommitted changes
    if ! git -C "$path" diff --quiet -- 2>/dev/null || ! git -C "$path" diff --cached --quiet -- 2>/dev/null; then
        echo "Error: Worktree has uncommitted changes" >&2
        echo "Please commit or stash your changes first" >&2
        return 1
    fi
    
    # Check for untracked files (optional warning)
    if [ -n "$(git -C "$path" ls-files --others --exclude-standard -- 2>/dev/null)" ]; then
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
    git worktree remove -- "$path" || return 1
    
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

# Find worktree path for a specific branch
# Usage: find_worktree_path <branch>
# Returns: worktree path on stdout, empty if not found
find_worktree_path() {
    branch="$1"
    
    if [ -z "$branch" ]; then
        return 1
    fi
    
    # Use temporary file to avoid pipe subshell variable scope issues
    tmpfile="$(mktemp)" || return 1
    trap 'rm -f "$tmpfile"' EXIT INT TERM
    
    git worktree list --porcelain 2>/dev/null > "$tmpfile"
    
    current_path=""
    found_path=""
    while IFS= read -r line; do
        case "$line" in
            "worktree "*)
                current_path="${line#worktree }"
                ;;
            "branch refs/heads/$branch")
                found_path="$current_path"
                break
                ;;
        esac
    done < "$tmpfile"
    
    rm -f "$tmpfile"
    trap - EXIT INT TERM
    
    if [ -n "$found_path" ]; then
        echo "$found_path"
        return 0
    fi
    
    return 1
}

# Find worktree path by matching a pattern
# Usage: find_worktree_by_pattern <pattern>
# Returns: worktree path on stdout, empty if not found
find_worktree_by_pattern() {
    pattern="$1"
    
    if [ -z "$pattern" ]; then
        return 1
    fi
    
    # Use temporary file to avoid pipe subshell variable scope issues
    tmpfile="$(mktemp)" || return 1
    trap 'rm -f "$tmpfile"' EXIT INT TERM
    
    git worktree list --porcelain 2>/dev/null > "$tmpfile"
    
    found_path=""
    while IFS= read -r line; do
        case "$line" in
            "worktree "*)
                current_path="${line#worktree }"
                # Check if path matches the pattern
                case "$current_path" in
                    *"$pattern"*)
                        found_path="$current_path"
                        break
                        ;;
                esac
                ;;
        esac
    done < "$tmpfile"
    
    rm -f "$tmpfile"
    trap - EXIT INT TERM
    
    if [ -n "$found_path" ]; then
        echo "$found_path"
        return 0
    fi
    
    return 1
}