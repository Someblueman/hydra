#!/bin/sh
# GitHub integration for Hydra
# POSIX-compliant GitHub issue integration

set -eu

# Create a branch from a GitHub issue
# Usage: spawn_from_issue <issue_number>
# Returns: branch name on stdout
spawn_from_issue() {
    issue_num="$1"
    
    if [ -z "$issue_num" ]; then
        echo "Error: Issue number is required" >&2
        return 1
    fi
    
    # For now, just create a simple branch name from the issue number
    # This is a stub implementation - full GitHub API integration would go here
    branch_name="issue-$issue_num"
    
    echo "$branch_name"
    return 0
}