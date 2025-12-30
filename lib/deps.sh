#!/bin/sh
# Dependency management functions for Hydra
# POSIX-compliant shell script

# =============================================================================
# Dependency Validation
# =============================================================================

# Validate a dependency specification
# Usage: validate_deps_spec <deps_spec>
# deps_spec: "branch1" or "branch1,branch2,branch3" (comma-separated)
# Returns: 0 if valid, 1 if invalid (prints error)
validate_deps_spec() {
    deps_spec="$1"

    if [ -z "$deps_spec" ]; then
        echo "Error: Empty dependency specification" >&2
        return 1
    fi

    # Check for invalid characters (only allow alphanumeric, dash, underscore, slash, comma)
    case "$deps_spec" in
        *[!a-zA-Z0-9_/,-]*)
            echo "Error: Invalid characters in dependency specification" >&2
            return 1
            ;;
    esac

    # Check each dependency branch exists (warning only, not error)
    remaining="$deps_spec"
    while [ -n "$remaining" ]; do
        dep="${remaining%%,*}"
        if [ "$dep" = "$remaining" ]; then
            remaining=""
        else
            remaining="${remaining#*,}"
        fi

        if [ -z "$dep" ]; then
            echo "Error: Empty dependency in specification" >&2
            return 1
        fi

        # Check if dependency has a session (it should exist to depend on it)
        if ! get_session_for_branch "$dep" >/dev/null 2>&1; then
            echo "Warning: Dependency '$dep' has no active session" >&2
            # Not an error - the session might be spawned later or already completed
        fi
    done

    return 0
}

# Module-level visited tracker for circular dependency detection
_DEPS_VISITED=""

# Helper for circular dependency checking (module scope)
# Usage: _check_circular_helper <deps_spec>
# Uses module-level _DEPS_VISITED variable
# Returns: 0 if no cycle, 1 if cycle detected
_check_circular_helper() {
    current_deps="$1"

    remaining="$current_deps"
    while [ -n "$remaining" ]; do
        dep="${remaining%%,*}"
        if [ "$dep" = "$remaining" ]; then
            remaining=""
        else
            remaining="${remaining#*,}"
        fi

        [ -z "$dep" ] && continue

        # Check if already visited (cycle!)
        case "$_DEPS_VISITED" in
            *",$dep,"*)
                echo "Error: Circular dependency detected: '$dep' already in chain" >&2
                return 1
                ;;
        esac

        # Add to visited
        _DEPS_VISITED="${_DEPS_VISITED}${dep},"

        # Get this branch's dependencies and recurse
        next_deps="$(get_deps_for_branch "$dep" 2>/dev/null || true)"
        if [ -n "$next_deps" ] && [ "$next_deps" != "-" ]; then
            _check_circular_helper "$next_deps" || return 1
        fi
    done

    return 0
}

# Check for circular dependencies
# Usage: check_circular_deps <branch> <deps_spec>
# Returns: 0 if no cycle, 1 if cycle detected (prints error)
check_circular_deps() {
    target="$1"
    deps_spec="$2"

    if [ -z "$deps_spec" ]; then
        return 0
    fi

    # Reset module-level visited tracker
    _DEPS_VISITED=",$target,"

    _check_circular_helper "$deps_spec"
}

# =============================================================================
# Dependency Completion
# =============================================================================

# Check if a single dependency is complete (session no longer exists)
# Usage: is_dep_complete <branch>
# Returns: 0 if complete (no session), 1 if still running
is_dep_complete() {
    dep_branch="$1"

    # Get the session for this dependency
    dep_session="$(get_session_for_branch "$dep_branch" 2>/dev/null || true)"

    # If no session found in mapping, dependency is complete
    if [ -z "$dep_session" ]; then
        return 0
    fi

    # If session exists in tmux, dependency is still running
    if tmux_session_exists "$dep_session"; then
        return 1
    fi

    # Session was in mapping but tmux session is gone - dependency complete
    return 0
}

# Check if all dependencies are complete
# Usage: check_deps_complete <deps_spec>
# Returns: 0 if all complete, 1 if any pending
check_deps_complete() {
    deps_spec="$1"

    if [ -z "$deps_spec" ] || [ "$deps_spec" = "-" ]; then
        return 0
    fi

    remaining="$deps_spec"
    while [ -n "$remaining" ]; do
        dep="${remaining%%,*}"
        if [ "$dep" = "$remaining" ]; then
            remaining=""
        else
            remaining="${remaining#*,}"
        fi

        [ -z "$dep" ] && continue

        if ! is_dep_complete "$dep"; then
            return 1
        fi
    done

    return 0
}

# Wait for all dependencies to complete
# Usage: wait_for_deps <deps_spec> [timeout_seconds] [poll_interval]
# Returns: 0 on success, 1 on timeout/failure
wait_for_deps() {
    deps_spec="$1"
    timeout="${2:-3600}"      # Default 1 hour
    poll_interval="${3:-5}"   # Default 5 seconds

    if [ -z "$deps_spec" ] || [ "$deps_spec" = "-" ]; then
        return 0
    fi

    start_time="$(date +%s)"

    while true; do
        # Check timeout
        now="$(date +%s)"
        elapsed=$((now - start_time))
        if [ "$elapsed" -ge "$timeout" ]; then
            echo "Error: Timeout waiting for dependencies after ${elapsed}s" >&2
            return 1
        fi

        # Check if all dependencies are complete
        all_complete=1
        pending=""
        remaining="$deps_spec"
        while [ -n "$remaining" ]; do
            dep="${remaining%%,*}"
            if [ "$dep" = "$remaining" ]; then
                remaining=""
            else
                remaining="${remaining#*,}"
            fi

            [ -z "$dep" ] && continue

            if ! is_dep_complete "$dep"; then
                all_complete=0
                if [ -n "$pending" ]; then
                    pending="$pending, $dep"
                else
                    pending="$dep"
                fi
            fi
        done

        if [ "$all_complete" -eq 1 ]; then
            return 0
        fi

        # Show progress
        printf "\rWaiting for: %s (%ds elapsed)..." "$pending" "$elapsed" >&2

        sleep "$poll_interval"
    done
}

# =============================================================================
# Dependency Tree Display
# =============================================================================

# Get all branches that depend on a given branch
# Usage: get_dependents <branch>
# Returns: Space-separated list of dependent branches on stdout
get_dependents() {
    target="$1"
    dependents=""

    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi

    while IFS=' ' read -r map_branch _session _ai _group _timestamp map_deps _pr; do
        [ -z "$map_deps" ] || [ "$map_deps" = "-" ] && continue

        # Check if target is in this branch's dependencies
        remaining="$map_deps"
        while [ -n "$remaining" ]; do
            dep="${remaining%%,*}"
            if [ "$dep" = "$remaining" ]; then
                remaining=""
            else
                remaining="${remaining#*,}"
            fi

            if [ "$dep" = "$target" ]; then
                if [ -n "$dependents" ]; then
                    dependents="$dependents $map_branch"
                else
                    dependents="$map_branch"
                fi
                break
            fi
        done
    done < "$HYDRA_MAP"

    echo "$dependents"
}

# Build a simple dependency tree for a branch
# Usage: build_dep_tree <branch> [indent]
# Returns: Formatted tree on stdout
build_dep_tree() {
    branch="$1"
    indent="${2:-}"

    deps="$(get_deps_for_branch "$branch" 2>/dev/null || true)"
    if [ -z "$deps" ] || [ "$deps" = "-" ]; then
        echo "${indent}$branch"
        return
    fi

    echo "${indent}$branch"
    echo "${indent}  depends on:"

    remaining="$deps"
    while [ -n "$remaining" ]; do
        dep="${remaining%%,*}"
        if [ "$dep" = "$remaining" ]; then
            remaining=""
        else
            remaining="${remaining#*,}"
        fi

        [ -z "$dep" ] && continue

        # Check if dependency is complete
        if is_dep_complete "$dep"; then
            echo "${indent}    [done] $dep"
        else
            echo "${indent}    [wait] $dep"
        fi
    done
}

# Build a full dependency tree view for all sessions with dependencies
# Usage: build_full_dep_tree
# Returns: Formatted tree on stdout
build_full_dep_tree() {
    if [ -z "$HYDRA_MAP" ] || [ ! -f "$HYDRA_MAP" ]; then
        return 0
    fi

    # Collect all branches that have dependencies
    has_deps=""

    while IFS=' ' read -r map_branch _session _ai _group _timestamp map_deps _pr; do
        [ -z "$map_branch" ] && continue

        if [ -n "$map_deps" ] && [ "$map_deps" != "-" ]; then
            if [ -n "$has_deps" ]; then
                has_deps="$has_deps $map_branch"
            else
                has_deps="$map_branch"
            fi
        fi
    done < "$HYDRA_MAP"

    # Print sessions with dependencies
    if [ -n "$has_deps" ]; then
        # shellcheck disable=SC2086
        for branch in $has_deps; do
            build_dep_tree "$branch"
            echo ""
        done
    fi
}

# Count pending dependencies for a branch
# Usage: count_pending_deps <deps_spec>
# Returns: Number of pending dependencies on stdout
count_pending_deps() {
    deps_spec="$1"
    count=0

    if [ -z "$deps_spec" ] || [ "$deps_spec" = "-" ]; then
        echo "0"
        return
    fi

    remaining="$deps_spec"
    while [ -n "$remaining" ]; do
        dep="${remaining%%,*}"
        if [ "$dep" = "$remaining" ]; then
            remaining=""
        else
            remaining="${remaining#*,}"
        fi

        [ -z "$dep" ] && continue

        if ! is_dep_complete "$dep"; then
            count=$((count + 1))
        fi
    done

    echo "$count"
}
