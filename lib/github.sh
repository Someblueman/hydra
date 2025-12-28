#!/bin/sh
# GitHub integration helper functions for Hydra
# POSIX-compliant shell script

# Check if GitHub CLI is available
# Usage: check_gh_cli
# Returns: 0 if available, 1 if not
check_gh_cli() {
    if ! command -v gh >/dev/null 2>&1; then
        echo "Error: GitHub CLI (gh) is not installed" >&2
        echo "Install it from: https://cli.github.com/" >&2
        return 1
    fi
    
    # Check if authenticated
    if ! gh auth status >/dev/null 2>&1; then
        echo "Error: GitHub CLI is not authenticated" >&2
        echo "Run: gh auth login" >&2
        return 1
    fi
    
    return 0
}

# Validate issue number
# Usage: validate_issue_number <number>
# Returns: 0 if valid, 1 if invalid
validate_issue_number() {
    issue_num="$1"
    
    if [ -z "$issue_num" ]; then
        echo "Error: Issue number is required" >&2
        return 1
    fi
    
    # Check if it's a positive integer
    case "$issue_num" in
        ''|*[!0-9]*)
            echo "Error: Issue number must be a positive integer" >&2
            return 1
            ;;
        0)
            echo "Error: Issue number must be greater than 0" >&2
            return 1
            ;;
    esac
    
    return 0
}

# Fetch issue details from GitHub
# Usage: fetch_issue_details <issue_number>
# Returns: JSON data on stdout, empty on error
fetch_issue_details() {
    issue_num="$1"
    
    if ! validate_issue_number "$issue_num"; then
        return 1
    fi
    
    # Fetch issue data
    if ! gh issue view "$issue_num" --json number,title,state 2>/dev/null; then
        echo "Error: Failed to fetch issue #$issue_num" >&2
        echo "Make sure you're in a GitHub repository and the issue exists" >&2
        return 1
    fi
}

# Sanitize text for use in branch names
# Usage: sanitize_branch_name <text>
# Returns: Sanitized text on stdout
sanitize_branch_name() {
    text="$1"
    
    # Convert to lowercase, replace spaces with hyphens
    # Remove special characters, keep only alphanumeric and hyphens
    # Truncate to reasonable length (50 chars)
    echo "$text" | \
        tr '[:upper:]' '[:lower:]' | \
        tr ' ' '-' | \
        sed 's/[^a-z0-9-]//g' | \
        sed 's/--*/-/g' | \
        sed 's/^-//' | \
        sed 's/-$//' | \
        cut -c1-50
}

# Generate branch name from issue
# Usage: generate_branch_from_issue <issue_number> <issue_title>
# Returns: Branch name on stdout
generate_branch_from_issue() {
    issue_num="$1"
    issue_title="$2"
    
    if [ -z "$issue_num" ] || [ -z "$issue_title" ]; then
        echo "Error: Issue number and title are required" >&2
        return 1
    fi
    
    # Sanitize the title
    sanitized_title="$(sanitize_branch_name "$issue_title")"
    
    # Create branch name in format: issue-<number>-<title>
    branch_name="issue-${issue_num}-${sanitized_title}"
    
    # Ensure it doesn't end with a hyphen
    branch_name="$(echo "$branch_name" | sed 's/-$//')"
    
    echo "$branch_name"
}

# Parse JSON value using portable method
# Usage: parse_json_value <json> <key>
# Returns: Value on stdout
parse_json_value() {
    json="$1"
    key="$2"
    
    # Extract value for given key
    # This is a simple parser that works for flat JSON
    # First try quoted strings, then unquoted values
    result="$(echo "$json" | sed -n "s/.*\"$key\":\"\([^\"]*\)\".*/\1/p" | head -1)"
    
    if [ -z "$result" ]; then
        # Try for unquoted values (numbers, booleans)
        result="$(echo "$json" | sed -n "s/.*\"$key\":\([^,}]*\).*/\1/p" | head -1)"
    fi
    
    echo "$result"
}

# =============================================================================
# Pull Request Functions
# =============================================================================

# Validate PR number
# Usage: validate_pr_number <number>
# Returns: 0 if valid, 1 if invalid
validate_pr_number() {
    pr_num="$1"

    if [ -z "$pr_num" ]; then
        echo "Error: PR number is required" >&2
        return 1
    fi

    # Check if it's a positive integer
    case "$pr_num" in
        ''|*[!0-9]*)
            echo "Error: PR number must be a positive integer" >&2
            return 1
            ;;
        0)
            echo "Error: PR number must be greater than 0" >&2
            return 1
            ;;
    esac

    return 0
}

# Fetch PR details from GitHub
# Usage: fetch_pr_details <pr_number>
# Returns: JSON data on stdout, 1 on error
fetch_pr_details() {
    pr_num="$1"

    if ! validate_pr_number "$pr_num"; then
        return 1
    fi

    # Fetch PR data
    if ! gh pr view "$pr_num" --json number,title,headRefName,state,isDraft 2>/dev/null; then
        echo "Error: Failed to fetch PR #$pr_num" >&2
        echo "Make sure you're in a GitHub repository and the PR exists" >&2
        return 1
    fi
}

# Get branch name from PR
# Usage: get_pr_branch <pr_number>
# Returns: Branch name on stdout, 1 on error
get_pr_branch() {
    pr_num="$1"

    if ! validate_pr_number "$pr_num"; then
        return 1
    fi

    # Use gh CLI to get PR head ref
    pr_json="$(gh pr view "$pr_num" --json headRefName 2>/dev/null)" || {
        echo "Error: Failed to fetch PR #$pr_num" >&2
        return 1
    }

    branch="$(parse_json_value "$pr_json" "headRefName")"
    if [ -z "$branch" ]; then
        echo "Error: Could not get branch name from PR" >&2
        return 1
    fi

    echo "$branch"
}

# Get PR status
# Usage: get_pr_status <pr_number>
# Returns: OPEN|MERGED|CLOSED|DRAFT on stdout, 1 on error
get_pr_status() {
    pr_num="$1"

    if ! validate_pr_number "$pr_num"; then
        return 1
    fi

    pr_json="$(gh pr view "$pr_num" --json state,isDraft 2>/dev/null)" || {
        echo "Error: Failed to fetch PR #$pr_num" >&2
        return 1
    }

    state="$(parse_json_value "$pr_json" "state")"
    is_draft="$(parse_json_value "$pr_json" "isDraft")"

    if [ "$is_draft" = "true" ]; then
        echo "DRAFT"
    else
        echo "$state"
    fi
}

# =============================================================================
# PR Status Caching
# =============================================================================

# Get path to PR status cache file
# Usage: _get_pr_cache_file
# Returns: Path on stdout
_get_pr_cache_file() {
    printf '%s' "${HYDRA_HOME:-$HOME/.hydra}/pr_status_cache"
}

# Get cached PR status if still valid
# Usage: get_cached_pr_status <pr_number>
# Returns: Status on stdout if cached and valid, 1 if stale/missing
get_cached_pr_status() {
    _pr_num="$1"
    _cache_file="$(_get_pr_cache_file)"
    _ttl="${HYDRA_PR_CACHE_TTL:-300}"

    [ -f "$_cache_file" ] || return 1

    # Look up PR in cache
    _cached_line="$(grep "^${_pr_num} " "$_cache_file" 2>/dev/null | head -1)"
    [ -n "$_cached_line" ] || return 1

    # Parse: pr_num status timestamp
    _cached_ts="$(echo "$_cached_line" | cut -d' ' -f3)"
    _now="$(date +%s)"
    _age=$((_now - _cached_ts))

    if [ "$_age" -lt "$_ttl" ]; then
        echo "$_cached_line" | cut -d' ' -f2
        return 0
    fi
    return 1
}

# Update PR status cache
# Usage: cache_pr_status <pr_number> <status>
cache_pr_status() {
    _pr_num="$1"
    _status="$2"
    _cache_file="$(_get_pr_cache_file)"
    _now="$(date +%s)"

    mkdir -p "$(dirname "$_cache_file")"

    # Remove old entry for this PR (atomic update via temp file)
    if [ -f "$_cache_file" ]; then
        _tmpfile="$(mktemp)"
        grep -v "^${_pr_num} " "$_cache_file" > "$_tmpfile" 2>/dev/null || true
        mv "$_tmpfile" "$_cache_file"
    fi

    # Add new entry
    echo "$_pr_num $_status $_now" >> "$_cache_file"
}

# Get PR status with caching
# Usage: get_pr_status_cached <pr_number> [force_refresh]
# Returns: OPEN|MERGED|CLOSED|DRAFT|UNKNOWN on stdout
get_pr_status_cached() {
    _pr_num="$1"
    _force_refresh="${2:-}"

    # Try cache first (unless force refresh)
    if [ -z "$_force_refresh" ]; then
        _cached="$(get_cached_pr_status "$_pr_num" 2>/dev/null)" && {
            echo "$_cached"
            return 0
        }
    fi

    # Check if gh is available (silent check)
    if ! command -v gh >/dev/null 2>&1; then
        echo "UNKNOWN"
        return 0
    fi

    # Check if authenticated (silent check)
    if ! gh auth status >/dev/null 2>&1; then
        echo "UNKNOWN"
        return 0
    fi

    # Fetch from API
    _status="$(get_pr_status "$_pr_num" 2>/dev/null)" || {
        # API failed - try returning stale cache
        if [ -f "$(_get_pr_cache_file)" ]; then
            _stale="$(grep "^${_pr_num} " "$(_get_pr_cache_file)" 2>/dev/null | cut -d' ' -f2)"
            [ -n "$_stale" ] && echo "$_stale" && return 0
        fi
        echo "UNKNOWN"
        return 0
    }

    # Cache the result
    cache_pr_status "$_pr_num" "$_status"
    echo "$_status"
}

# Create a new PR for branch
# Usage: create_pr_for_branch <branch> [--draft]
# Returns: PR number on stdout, 1 on error
create_pr_for_branch() {
    branch="$1"
    is_draft=""

    if [ "$2" = "--draft" ]; then
        is_draft="--draft"
    fi

    if [ -z "$branch" ]; then
        echo "Error: Branch is required" >&2
        return 1
    fi

    # Check if gh is available
    if ! check_gh_cli; then
        return 1
    fi

    # Push branch first if not pushed
    if ! git ls-remote --heads origin "$branch" 2>/dev/null | grep -q "$branch"; then
        echo "Pushing branch to origin..." >&2
        git push -u origin "$branch" || {
            echo "Error: Failed to push branch to origin" >&2
            return 1
        }
    fi

    # Create PR using --fill to auto-fill from commit
    # shellcheck disable=SC2086
    pr_url="$(gh pr create --head "$branch" $is_draft --fill 2>&1)" || {
        echo "Error: Failed to create PR: $pr_url" >&2
        return 1
    }

    # Extract PR number from URL
    pr_num="$(echo "$pr_url" | grep -o '/pull/[0-9]*' | sed 's|/pull/||' | head -1)"

    if [ -z "$pr_num" ]; then
        echo "Error: Could not extract PR number from response" >&2
        return 1
    fi

    echo "$pr_num"
}

# Spawn head from GitHub PR
# Usage: spawn_from_pr <pr_number>
# Returns: Branch name on stdout, 1 on error
spawn_from_pr() {
    pr_num="$1"

    # Check dependencies
    if ! check_gh_cli; then
        return 1
    fi

    # Validate PR number
    if ! validate_pr_number "$pr_num"; then
        return 1
    fi

    echo "Fetching PR #$pr_num from GitHub..." >&2

    # Get branch name from PR
    branch="$(get_pr_branch "$pr_num")" || return 1

    echo "PR branch: $branch" >&2
    echo "$branch"
}

# =============================================================================
# Issue Functions
# =============================================================================

# Spawn head from GitHub issue
# Usage: spawn_from_issue <issue_number>
# Returns: Branch name on stdout, empty on error
spawn_from_issue() {
    issue_num="$1"
    
    # Check dependencies
    if ! check_gh_cli; then
        return 1
    fi
    
    # Validate issue number
    if ! validate_issue_number "$issue_num"; then
        return 1
    fi
    
    echo "Fetching issue #$issue_num from GitHub..." >&2
    
    # Fetch issue details
    issue_json="$(fetch_issue_details "$issue_num")" || return 1
    
    # Parse JSON to get title and state
    issue_title="$(parse_json_value "$issue_json" "title")"
    issue_state="$(parse_json_value "$issue_json" "state")"
    
    if [ -z "$issue_title" ]; then
        echo "Error: Could not parse issue title" >&2
        return 1
    fi
    
    # Check if issue is closed
    if [ "$issue_state" = "CLOSED" ]; then
        echo "Warning: Issue #$issue_num is closed" >&2
        printf "Continue anyway? [y/N] " >&2
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
    
    # Generate branch name
    branch_name="$(generate_branch_from_issue "$issue_num" "$issue_title")"
    
    if [ -z "$branch_name" ]; then
        echo "Error: Failed to generate branch name" >&2
        return 1
    fi
    
    echo "Generated branch name: $branch_name" >&2
    echo "$branch_name"
}