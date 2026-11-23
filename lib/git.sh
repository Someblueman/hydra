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
    
    # Reject whitespace or control characters
    if printf '%s' "$branch" | grep -q '[[:space:]]'; then
        echo "Error: Branch name cannot contain whitespace: $branch" >&2
        return 1
    fi
    if printf '%s' "$branch" | grep -q '[[:cntrl:]]'; then
        echo "Error: Branch name contains control characters: $branch" >&2
        return 1
    fi
    
    # Check for dangerous characters and patterns
    case "$branch" in
        -*)
            echo "Error: Branch name cannot start with '-': $branch" >&2
            return 1
            ;;
        */|/*)
            echo "Error: Branch name cannot start or end with '/': $branch" >&2
            return 1
            ;;
        *//*)
            echo "Error: Branch name cannot contain consecutive '/': $branch" >&2
            return 1
            ;;
        */./*|*/.)
            echo "Error: Branch name contains self-reference '.': $branch" >&2
            return 1
            ;;
        *..*)
            echo "Error: Branch name cannot contain '..': $branch" >&2
            return 1
            ;;
        *.)
            echo "Error: Branch name cannot end with '.': $branch" >&2
            return 1
            ;;
        *'@{'*)
            echo "Error: Branch name cannot contain '@{': $branch" >&2
            return 1
            ;;
        *.lock)
            echo "Error: Branch name cannot end with '.lock': $branch" >&2
            return 1
            ;;
    esac
    
    # Restrict to a conservative safe character set (unless advanced refs allowed)
    if [ -z "${HYDRA_ALLOW_ADVANCED_REFS:-}" ]; then
        case "$branch" in
            *[!A-Za-z0-9._/-]* )
                echo "Error: Branch name contains unsupported characters: $branch" >&2
                return 1
                ;;
        esac
    fi
    
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
    
    # Disallow control characters. Spaces are valid when arguments stay quoted.
    if printf '%s' "$path" | grep -q '[[:cntrl:]]'; then
        echo "Error: Path contains control characters: $path" >&2
        return 1
    fi
    
    # Check for dangerous path patterns
    case "$path" in
        /)
            echo "Error: Path cannot be root '/'" >&2
            return 1
            ;;
        */../*|../*|*/..|..)
            echo "Error: Path contains directory traversal '..': $path" >&2
            return 1
            ;;
        */./*|./*|*/.)
            echo "Error: Path contains self-reference '.': $path" >&2
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
    
    # Restrict to a conservative safe character set (unless advanced refs allowed)
    if [ -z "${HYDRA_ALLOW_ADVANCED_REFS:-}" ]; then
        case "$path" in
            *[!A-Za-z0-9._/\ -]* )
                echo "Error: Path contains unsupported characters: $path" >&2
                return 1
                ;;
        esac
    fi
    
    return 0
}

# Removes Git configuration injected through the environment (GIT_CONFIG_COUNT
# with its GIT_CONFIG_KEY_<n> and GIT_CONFIG_VALUE_<n> pairs, and
# GIT_CONFIG_PARAMETERS) from this shell and every command it starts, so a
# result review or a landing reads only the repository's, the user's and the
# system's Git configuration. The native review helper does the same.
# Usage: git_isolate_config
git_isolate_config() {
    _gic_count="${GIT_CONFIG_COUNT:-0}"
    case "$_gic_count" in ''|*[!0-9]*) _gic_count=0 ;; esac
    [ "${#_gic_count}" -le 4 ] && [ "$_gic_count" -le 4096 ] || _gic_count=4096
    _gic_index=0
    while [ "$_gic_index" -lt "$_gic_count" ]; do
        unset "GIT_CONFIG_KEY_$_gic_index" "GIT_CONFIG_VALUE_$_gic_index"
        _gic_index=$((_gic_index + 1))
    done
    unset GIT_CONFIG_COUNT GIT_CONFIG_PARAMETERS
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

# Check whether a worktree is safe to remove (dirty / untracked policy).
# Usage: check_worktree_removable <path> [protect-untracked: true|false]
# Returns: 0 if removal may proceed, 1 if dirty or the user aborted
check_worktree_removable() {
    path="$1"

    if [ -z "$path" ] || [ ! -d "$path" ]; then
        return 0
    fi

    if ! git -C "$path" diff --quiet -- 2>/dev/null || ! git -C "$path" diff --cached --quiet -- 2>/dev/null; then
        echo "Error: Worktree has uncommitted changes" >&2
        echo "Please commit or stash your changes first" >&2
        return 1
    fi

    if [ -n "$(git -C "$path" ls-files --others --exclude-standard -- 2>/dev/null)" ]; then
        if [ "${2:-false}" = true ]; then
            echo "Error: Worktree has untracked files; commit, stash or remove them first" >&2
            return 1
        fi
        if [ -z "${HYDRA_NONINTERACTIVE:-}" ] && [ -z "${CI:-}" ]; then
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
        else
            echo "Warning: Worktree has untracked files (proceeding in non-interactive mode)" >&2
        fi
    fi

    return 0
}

# Delete a git worktree safely
# Usage: delete_worktree <path> [force|skip_preflight]
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

    if [ "$force" != "skip_preflight" ]; then
        if ! check_worktree_removable "$path"; then
            return 1
        fi
    fi
    
    # Remove the worktree
    git worktree remove -- "$path" || return 1

    return 0
}
