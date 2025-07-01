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