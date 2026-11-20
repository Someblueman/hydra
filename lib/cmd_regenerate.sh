#!/bin/sh
# Hydra regenerate command handler
# POSIX-compliant shell script

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
        if [ "$(get_terminal_mode_for_branch "$branch" 2>/dev/null || echo interactive)" = headless ] || [ "$session" = - ]; then
            echo "Headless head '$branch' has no terminal to regenerate; keeping its durable instance"
            skipped=$((skipped + 1))
            continue
        fi
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
