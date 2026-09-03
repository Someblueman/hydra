#!/bin/sh
# Hydra queue and regeneration command handlers
# POSIX-compliant shell script

cmd_queue() {
    # Parse arguments
    action=""
    json_output=""
    target_branch=""

    while [ $# -gt 0 ]; do
        case "$1" in
            -j|--json)
                json_output="1"
                shift
                ;;
            clear)
                action="clear"
                shift
                ;;
            remove)
                action="remove"
                shift
                if [ $# -gt 0 ]; then
                    target_branch="$1"
                    shift
                fi
                ;;
            process)
                action="process"
                shift
                ;;
            -*)
                cli_error queue invalid_input "Unknown option '$1'" "run hydra queue --json"
                return 1
                ;;
            *)
                cli_error queue invalid_input "Unknown action '$1'" "run hydra queue --json"
                return 1
                ;;
        esac
    done

    if ! hydra_get_project_id >/dev/null 2>&1; then
        if [ -n "$json_output" ]; then
            cli_error queue state_unavailable "Queue operations require an initialized Hydra project" \
                "run hydra init inside a Git repository"
        else
            echo "Error: Queue operations require an initialized Hydra project" >&2
            echo "Next: run hydra init inside a Git repository" >&2
        fi
        return 1
    fi

    case "$action" in
        clear)
            count="$(clear_queue)"
            if [ -n "$json_output" ]; then
                json_success queue "{\"action\":\"clear\",\"cleared\":$count}"
            else
                echo "Cleared $count queued spawn(s)"
            fi
            ;;
        remove)
            if [ -z "$target_branch" ]; then
                cli_error queue invalid_input "Branch name required" "run hydra queue remove <branch> --json"
                return 1
            fi
            if dequeue_spawn "$target_branch"; then
                if [ -n "$json_output" ]; then
                    json_success queue "{\"action\":\"remove\",\"branch\":\"$(json_escape "$target_branch")\"}"
                else
                    echo "Removed '$target_branch' from queue"
                fi
            else
                if [ -n "$json_output" ]; then
                    json_error queue not_found "Branch '$target_branch' not found in queue" "inspect with hydra queue --json"
                else
                    echo "Branch '$target_branch' not found in queue"
                fi
                return 1
            fi
            ;;
        process)
            spawned="$(process_spawn_queue)"
            if [ -n "$json_output" ]; then
                json_success queue "{\"action\":\"process\",\"spawned\":$spawned}"
            else
                echo "Processed queue: $spawned session(s) spawned"
            fi
            ;;
        *)
            # Default: list queue
            if [ -n "$json_output" ]; then
                list_queue --json
            else
                echo "Spawn Queue:"
                echo "============"
                echo ""
                list_queue
            fi
            ;;
    esac
}

cmd_regenerate() {
    echo "Regenerating dead heads as new lifecycle instances..."
    cleanup_stale_locks 2>/dev/null || true
    if ! state_has_heads; then
        echo "No Hydra heads to regenerate"
        return 0
    fi
    hydra_get_project_id >/dev/null 2>&1 || {
        echo "Error: project is not initialized; run hydra init first" >&2
        return 1
    }
    regenerated=0
    skipped=0
    failed=0
    while IFS=' ' read -r branch session _ai _group _timestamp _deps _pr; do
        [ -n "$branch" ] || continue
        if [ -n "$session" ] && tmux_session_exists "$session"; then
            echo "Session already exists for '$branch'; skipping"
            skipped=$((skipped + 1))
            continue
        fi
        if cmd_resume "$branch"; then
            regenerated=$((regenerated + 1))
        else
            failed=$((failed + 1))
        fi
    done <<EOF
$(state_list_heads)
EOF
    echo ""
    echo "Regeneration complete:"
    echo "  Created: $regenerated"
    echo "  Skipped: $skipped"
    echo "  Failed: $failed"
    [ "$failed" -eq 0 ]
}
