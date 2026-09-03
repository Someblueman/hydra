#!/bin/sh
# Hydra bulk spawn operations
# POSIX-compliant shell script

# Spawn multiple sessions with same AI tool
# Usage: spawn_bulk <base_branch> <count> <layout> [ai_tool] [group]
# Note: Calls cmd_kill for rollback, which must be defined in bin/hydra
spawn_bulk() {
    base_branch="$1"
    count="$2"
    layout="${3:-default}"
    ai_tool="${4:-}"
    group="${5:-}"

    # Confirm if spawning many sessions
    if [ "$count" -gt 3 ]; then
        printf "Are you sure you want to spawn %d sessions? [y/N] " "$count"
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

    echo "Spawning $count sessions based on '$base_branch'..."

    succeeded=0
    failed=0
    created_branches=""

    i=1
    while [ "$i" -le "$count" ]; do
        branch_name="${base_branch}-${i}"
        echo ""
        echo "[$i/$count] Creating head '$branch_name'..."

        if session="$(spawn_single "$branch_name" "$layout" "$ai_tool" "$group")"; then
            succeeded=$((succeeded + 1))
            if [ -z "$created_branches" ]; then
                created_branches="$branch_name"
            else
                created_branches="$created_branches $branch_name"
            fi
            echo "[$i/$count] Successfully created session: $session"
        else
            failed=$((failed + 1))
            echo "[$i/$count] Failed to create head '$branch_name'" >&2

            # Ask whether to continue or rollback
            if [ "$i" -lt "$count" ]; then
                printf "Continue with remaining heads? [y/N] "
                read -r response
                case "$response" in
                    [yY][eE][sS]|[yY])
                        ;;
                    *)
                        echo "Rolling back created heads..."
                        for b in $created_branches; do
                            echo "Removing $b..."
                            cmd_kill "$b" >/dev/null 2>&1 || true
                        done
                        return 1
                        ;;
                esac
            fi
        fi

        i=$((i + 1))
    done

    echo ""
    echo "Bulk spawn complete:"
    echo "  Succeeded: $succeeded"
    echo "  Failed: $failed"

    # Switch to first created session if in terminal
    if [ -t 0 ] && [ -t 1 ] && [ "$succeeded" -gt 0 ]; then
        first_branch="$(echo "$created_branches" | cut -d' ' -f1)"
        first_session="$(get_session_for_branch "$first_branch" 2>/dev/null || true)"
        if [ -n "$first_session" ]; then
            echo ""
            echo "Switching to first session '$first_session'..."
            switch_to_session "$first_session"
        fi
    fi

    return 0
}

# Spawn sessions with mixed AI agents
# Usage: spawn_bulk_mixed <base_branch> <agents_spec> <layout> [group]
# agents_spec format: "claude:2,aider:1,codex:1"
# Note: Calls cmd_kill for rollback, which must be defined in bin/hydra
spawn_bulk_mixed() {
    base_branch="$1"
    agents_spec="$2"
    layout="${3:-default}"
    group="${4:-}"

    echo "Parsing agents specification: $agents_spec"

    # Parse the agents spec and create sessions
    total_count=0
    session_num=1
    succeeded=0
    failed=0
    created_branches=""

    # Process each agent:count pair
    while [ -n "$agents_spec" ]; do
        # Extract first agent:count pair
        pair="${agents_spec%%,*}"

        # Remove processed pair from spec
        if [ "$pair" = "$agents_spec" ]; then
            agents_spec=""
        else
            agents_spec="${agents_spec#*,}"
        fi

        # Check if pair contains a colon
        case "$pair" in
            *:*)
                # Parse agent and count
                agent="${pair%%:*}"
                agent_count="${pair#*:}"
                ;;
            *)
                echo "Error: Invalid agent specification: $pair" >&2
                return 1
                ;;
        esac

        # Validate
        if [ -z "$agent" ] || [ -z "$agent_count" ]; then
            echo "Error: Invalid agent specification: $pair" >&2
            return 1
        fi

        if ! echo "$agent_count" | grep -q '^[0-9]\+$' || [ "$agent_count" -lt 1 ]; then
            echo "Error: Invalid count for $agent: $agent_count" >&2
            return 1
        fi

        if ! validate_ai_command "$agent"; then
            return 1
        fi
        if [ -z "${HYDRA_SKIP_AI:-}" ] && ! ensure_ai_on_path "$agent"; then
            return 1
        fi

        total_count=$((total_count + agent_count))
    done

    # Confirm if spawning many sessions
    if [ "$total_count" -gt 3 ]; then
        printf "Are you sure you want to spawn %d sessions? [y/N] " "$total_count"
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

    # Reset agents_spec for actual processing
    agents_spec="$2"

    echo "Spawning $total_count sessions with mixed agents..."

    # Process each agent:count pair again for actual spawning
    while [ -n "$agents_spec" ]; do
        # Extract first agent:count pair
        pair="${agents_spec%%,*}"

        # Remove processed pair from spec
        if [ "$pair" = "$agents_spec" ]; then
            agents_spec=""
        else
            agents_spec="${agents_spec#*,}"
        fi

        # Parse agent and count (already validated in first loop)
        agent="${pair%%:*}"
        agent_count="${pair#*:}"

        echo ""
        echo "Creating $agent_count session(s) with $agent..."

        i=1
        while [ "$i" -le "$agent_count" ]; do
            branch_name="${base_branch}-${session_num}"
            echo ""
            echo "[$session_num/$total_count] Creating head '$branch_name' with $agent..."

            if session="$(spawn_single "$branch_name" "$layout" "$agent" "$group")"; then
                succeeded=$((succeeded + 1))
                if [ -z "$created_branches" ]; then
                    created_branches="$branch_name"
                else
                    created_branches="$created_branches $branch_name"
                fi
                echo "[$session_num/$total_count] Successfully created session: $session"
            else
                failed=$((failed + 1))
                echo "[$session_num/$total_count] Failed to create head '$branch_name'" >&2

                # Ask whether to continue or rollback
                if [ "$session_num" -lt "$total_count" ]; then
                    printf "Continue with remaining heads? [y/N] "
                    read -r response
                    case "$response" in
                        [yY][eE][sS]|[yY])
                            ;;
                        *)
                            echo "Rolling back created heads..."
                            for b in $created_branches; do
                                echo "Removing $b..."
                                cmd_kill "$b" >/dev/null 2>&1 || true
                            done
                            return 1
                            ;;
                    esac
                fi
            fi

            i=$((i + 1))
            session_num=$((session_num + 1))
        done
    done

    echo ""
    echo "Bulk spawn complete:"
    echo "  Succeeded: $succeeded"
    echo "  Failed: $failed"

    # Switch to first created session if in terminal
    if [ -t 0 ] && [ -t 1 ] && [ "$succeeded" -gt 0 ]; then
        first_branch="$(echo "$created_branches" | cut -d' ' -f1)"
        first_session="$(get_session_for_branch "$first_branch" 2>/dev/null || true)"
        if [ -n "$first_session" ]; then
            echo ""
            echo "Switching to first session '$first_session'..."
            switch_to_session "$first_session"
        fi
    fi

    return 0
}
