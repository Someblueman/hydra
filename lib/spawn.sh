#!/bin/sh
# Spawn session functions for Hydra
# POSIX-compliant shell script
#
# Provides session spawning capabilities for single and bulk operations.
# Dependencies: locks.sh, paths.sh, git.sh, tmux.sh, state.sh, hooks.sh, yaml.sh

# Roll back a partially created spawn (session, durable state, worktree)
# Usage: spawn_rollback_session <session> <branch> <worktree_path>
spawn_rollback_session() {
    _session="$1"
    _branch="$2"
    _worktree_path="$3"

    if [ -n "$_branch" ] && command -v lifecycle_load_head >/dev/null 2>&1 && \
       lifecycle_load_head "$_branch" >/dev/null 2>&1; then
        if ! lifecycle_write_head_scalar "$_branch" desired-state stopped 2>/dev/null; then
            echo "Error: Failed to retire durable head; session and worktree preserved" >&2
            return 1
        fi
        lifecycle_set_observed "$_branch" failed hydra exact 2>/dev/null || true
        event_emit "$LIFECYCLE_PROJECT_ID" "$LIFECYCLE_HEAD_ID" "$LIFECYCLE_INSTANCE_ID" \
            lifecycle.spawn-failed hydra local '{}' >/dev/null 2>&1 || true
    fi

    if [ -n "$_session" ]; then
        kill_session "$_session" 2>/dev/null || true
        release_session_lock "$_session" 2>/dev/null || true
    fi
    if [ -n "$_worktree_path" ]; then
        # Force: this is abort of a spawn we just created.
        delete_worktree "$_worktree_path" "force" 2>/dev/null || true
    fi
    return 0
}

# Queue bulk spawns with same AI tool
# Usage: queue_bulk_spawns <base_branch> <count> <ai_tool> <group> <layout>
queue_bulk_spawns() {
    _base="$1"
    _count="$2"
    _ai_tool="${3:-}"
    _group="${4:-}"
    _layout="${5:-default}"

    if [ "$_count" -eq 1 ]; then
        queue_spawn "$_base" "$_ai_tool" "$_group" "$_layout" "50" >/dev/null
        return 0
    fi

    _i=1
    while [ "$_i" -le "$_count" ]; do
        queue_spawn "${_base}-${_i}" "$_ai_tool" "$_group" "$_layout" "50" >/dev/null
        _i=$((_i + 1))
    done
}

# Queue spawns from mixed agents spec (e.g. "claude:2,aider:1")
# Usage: queue_mixed_spawns <base_branch> <agents_spec> <group> <layout>
queue_mixed_spawns() {
    _base_branch="$1"
    _agents_spec="$2"
    _group="${3:-}"
    _layout="${4:-default}"
    _session_num=1

    while [ -n "$_agents_spec" ]; do
        _pair="${_agents_spec%%,*}"
        if [ "$_pair" = "$_agents_spec" ]; then
            _agents_spec=""
        else
            _agents_spec="${_agents_spec#*,}"
        fi

        _agent="${_pair%%:*}"
        _agent_count="${_pair#*:}"

        _i=1
        while [ "$_i" -le "$_agent_count" ]; do
            _branch_name="${_base_branch}-${_session_num}"
            queue_spawn "$_branch_name" "$_agent" "$_group" "$_layout" "50" >/dev/null
            _session_num=$((_session_num + 1))
            _i=$((_i + 1))
        done
    done
}

# Confirm an allowlisted AI command exists on PATH
# Usage: ensure_ai_on_path <command>
# Returns: 0 if found, 1 if missing
ensure_ai_on_path() {
    _ai="$1"
    if [ -z "$_ai" ]; then
        echo "Error: AI command cannot be empty" >&2
        echo "Next: pass --no-agent for a shell-only head, or set HYDRA_AI_COMMAND to an installed agent" >&2
        echo "See README Quick Start." >&2
        return 1
    fi
    if ! command -v "$_ai" >/dev/null 2>&1; then
        echo "Error: AI command '$_ai' is not installed or not on PATH" >&2
        echo "Next: pass --no-agent to spawn a shell-only head, install '$_ai', or set HYDRA_AI_COMMAND" >&2
        echo "See README Quick Start for the five-minute no-agent tour." >&2
        return 1
    fi
    return 0
}

# Print a complete, non-mutating plan for one head.
# Usage: spawn_dry_run <branch> <layout> <profile> <group> <deps> <pr> <template> <task>
spawn_dry_run() {
    _sdr_branch="$1"
    _sdr_layout="$2"
    _sdr_profile="$3"
    _sdr_group="${4:--}"
    _sdr_deps="${5:--}"
    _sdr_pr="${6:--}"
    _sdr_template="${7:--}"
    _sdr_task="${8:-}"
    _sdr_policy="${9:-declared-done}"
    _sdr_scopes="${10:-}"
    _sdr_project="$(hydra_get_project_id 2>/dev/null)" || {
        echo "Error: project is not initialized" >&2
        echo "Next: hydra init --profile $_sdr_profile   or   hydra init --no-agent" >&2
        return 1
    }
    _sdr_head="$(hydra_new_id head "$_sdr_project|$_sdr_branch|dry-run")" || return 1
    _sdr_instance="$(hydra_new_id instance "$_sdr_head|dry-run")" || return 1
    _sdr_worktree="$(project_worktree_path "$_sdr_project" "$_sdr_head")" || return 1
    _sdr_head_dir="$(state_v2_head_dir "$_sdr_project" "$_sdr_head")" || return 1
    _sdr_task_file="$_sdr_head_dir/task"
    if [ "$_sdr_profile" != none ]; then
        _sdr_provider=""
        _sdr_task_arg=""
        [ -z "$_sdr_task" ] || _sdr_task_arg="$_sdr_task_file"
        [ "$(profile_field "$_sdr_profile" resume_mode)" != session-id ] || \
            _sdr_provider="$(profile_new_provider_id "$_sdr_profile" "$_sdr_instance")"
        _sdr_launch="$(profile_launch_command "$_sdr_profile" "$_sdr_task_arg" "$_sdr_provider")" || return 1
    else
        _sdr_launch="(plain shell; no agent)"
    fi

    echo "Hydra spawn plan (no changes will be made)"
    echo "  project_id: $_sdr_project"
    echo "  head_id: $_sdr_head"
    echo "  instance_id: $_sdr_instance"
    echo "  branch: $_sdr_branch"
    echo "  worktree: $_sdr_worktree"
    if git_branch_exists "$_sdr_branch"; then
        echo "  git: git worktree add -- <worktree> $_sdr_branch"
    else
        echo "  git: git worktree add -b $_sdr_branch -- <worktree>"
    fi
    echo "  layout: $_sdr_layout"
    echo "  profile: $_sdr_profile"
    echo "  launch: $_sdr_launch"
    echo "  group: ${_sdr_group:--}"
    echo "  dependencies: ${_sdr_deps:--}"
    echo "  pr: ${_sdr_pr:--}"
    echo "  template: ${_sdr_template:--}"
    echo "  task_bytes: $(printf '%s' "$_sdr_task" | LC_ALL=C wc -c | tr -d ' ') (content redacted)"
    echo "  completion_policy: $_sdr_policy"
    if [ -n "$_sdr_scopes" ]; then
        echo "  scopes:"
        printf '%s\n' "$_sdr_scopes" | sed 's/^/    - /'
    else
        echo "  scopes: (none)"
    fi
    _sdr_config="$(project_repo_config 2>/dev/null || true)"
    if [ -f "$_sdr_config" ]; then
        if project_is_trusted; then
            echo "  setup commands (trusted):"
            _sdr_setup="$(parse_setup_commands "$_sdr_config")"
            if [ -n "$_sdr_setup" ]; then
                printf '%s\n' "$_sdr_setup" | sed 's/^/    - /'
            else
                echo "    (none)"
            fi
        else
            echo "  setup commands: blocked (repository config is not trusted or changed)"
        fi
    else
        echo "  setup commands: (none)"
    fi
    echo "  state: create head and instance, then emit lifecycle.started"
}

# Helper function to spawn a single session
# Usage: spawn_single <branch> <layout> [profile] [group] [deps] [pr_number] [template] [task] [completion_policy] [scopes]
# Returns: Session name on stdout, 1 on failure
spawn_single() {
    branch="$1"
    layout="${2:-default}"
    ai_tool="${3:-}"
    group="${4:-}"
    deps="${5:-}"
    pr_number="${6:-}"
    template="${7:-}"
    task="${8:-}"
    completion_policy="${9:-declared-done}"
    scopes="${10:-}"

    # Wait for dependencies if specified
    if [ -n "$deps" ] && [ "$deps" != "-" ]; then
        _load_lib deps
        echo "Waiting for dependencies: $deps" >&2
        if ! wait_for_deps "$deps"; then
            echo "Error: Dependency wait failed or timed out" >&2
            return 1
        fi
        echo "Dependencies complete, proceeding with spawn" >&2
    fi

    # Best-effort cleanup of stale session-name locks
    cleanup_stale_locks 2>/dev/null || true

    # Check tmux availability
    if ! check_tmux_version; then
        return 1
    fi

    # Check if we're in a git repository
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "Error: Not in a git repository" >&2
        echo "Next: cd into an existing git repo, or create a throwaway repo (see README Quick Start)" >&2
        return 1
    fi

    repo_root="$(get_repo_root)" || return 1
    project_id="$(hydra_get_project_id 2>/dev/null || true)"
    if [ -z "$project_id" ]; then
        project_id="$(hydra_ensure_project_id)" || return 1
    fi
    project_activate_state_v2 "$project_id" || return 1
    if state_v2_find_head_by_branch "$project_id" "$branch" >/dev/null 2>&1; then
        echo "Error: durable head state already exists for '$branch'" >&2
        echo "Next: use 'hydra resume $branch' to create a new instance" >&2
        return 1
    fi
    head_id="$(hydra_new_id head "$project_id|$branch")" || return 1
    instance_id="$(hydra_new_id instance "$head_id|$branch")" || return 1
    worktree_path="$(project_worktree_path "$project_id" "$head_id")" || return 1
    base_ref="$(git rev-parse HEAD 2>/dev/null)" || return 1
    spawn_timestamp="$(date +%s)"
    provider_session_id=""
    if [ "$ai_tool" != none ] && [ "$(profile_field "$ai_tool" resume_mode)" = session-id ]; then
        provider_session_id="$(profile_new_provider_id "$ai_tool" "$instance_id")" || return 1
    fi
    planned_head_dir="$(state_v2_head_dir "$project_id" "$head_id")" || return 1
    planned_task_file="$planned_head_dir/task"
    launch_command=""
    if [ "$ai_tool" != none ]; then
        planned_task_arg=""
        [ -z "$task" ] || planned_task_arg="$planned_task_file"
        launch_command="$(profile_launch_command "$ai_tool" "$planned_task_arg" "$provider_session_id")" || return 1
    fi

    # Check if branch already has a session
    existing_session="$(get_session_for_branch "$branch" 2>/dev/null || true)"
    if [ -n "$existing_session" ] && tmux_session_exists "$existing_session"; then
        echo "Error: Branch '$branch' already has an active session '$existing_session'" >&2
        echo "Use 'hydra switch' to switch to it" >&2
        return 1
    fi

    # Create worktree
    project_worktree_lock="worktree_project_${project_id}"
    acquire_lock "$project_worktree_lock" "spawn worktree creation" "$head_id" || return 1
    echo "Creating worktree for branch '$branch'..." >&2
    if ! create_worktree "$branch" "$worktree_path"; then
        release_lock "$project_worktree_lock"
        return 1
    fi

    # Run environment setup commands (blocking, before session creation)
    if ! run_setup_commands "$worktree_path" "$repo_root"; then
        echo "Error: Environment setup failed" >&2
        if [ -z "${HYDRA_SETUP_CONTINUE:-}" ]; then
            # Clean up worktree and abort
            delete_worktree "$worktree_path" 2>/dev/null || true
            release_lock "$project_worktree_lock"
            return 1
        fi
        echo "Warning: Continuing despite setup failure (HYDRA_SETUP_CONTINUE set)" >&2
    fi

    # Generate session name
    session="$(generate_session_name "$branch")"

    # Run pre-spawn hook (best-effort)
    run_hook pre-spawn "$worktree_path" "$repo_root" "" "$branch"

    # Create tmux session
    echo "Creating tmux session '$session'..." >&2
    if ! create_session "$session" "$worktree_path"; then
        # Clean up worktree if session creation failed
        # Release any reserved session name lock
        release_session_lock "$session" 2>/dev/null || true
        delete_worktree "$worktree_path" 2>/dev/null || true
        release_lock "$project_worktree_lock"
        return 1
    fi
    # Release the reserved session name lock now that session is created
    release_session_lock "$session" 2>/dev/null || true

    # Commit durable identity and task before any agent process sees the task.
    committed_head="$(state_v2_create_head "$project_id" "$branch" "$session" "$ai_tool" \
        "${group:--}" "$spawn_timestamp" "${deps:--}" "${pr_number:--}" "$repo_root" \
        "$head_id" "$instance_id" "$worktree_path" "$task" "$base_ref" "$provider_session_id" \
        "$scopes" "$completion_policy")" || {
        echo "Error: Failed to commit durable head state" >&2
        spawn_rollback_session "$session" "$branch" "$worktree_path"
        release_lock "$project_worktree_lock"
        return 1
    }
    release_lock "$project_worktree_lock"
    head_id="$committed_head"
    head_dir="$(state_v2_head_dir "$project_id" "$head_id")" || return 1
    provenance_capture_head "$branch" || {
        spawn_rollback_session "$session" "$branch" "$worktree_path"
        return 1
    }
    provenance_capture_instance "$branch" launch "$launch_command" || {
        spawn_rollback_session "$session" "$branch" "$worktree_path"
        return 1
    }

    tmux set-environment -t "$session" HYDRA_PROJECT_ID "$project_id" 2>/dev/null || true
    tmux set-environment -t "$session" HYDRA_HEAD_ID "$head_id" 2>/dev/null || true
    tmux set-environment -t "$session" HYDRA_INSTANCE_ID "$instance_id" 2>/dev/null || true
    tmux set-environment -t "$session" HYDRA_BRANCH "$branch" 2>/dev/null || true
    tmux set-environment -t "$session" HYDRA_WORKTREE "$worktree_path" 2>/dev/null || true
    tmux set-environment -t "$session" HYDRA_STATE_DIR "$head_dir" 2>/dev/null || true
    tmux set-environment -t "$session" HYDRA_TASK_FILE "$head_dir/task" 2>/dev/null || true
    identity_export="export HYDRA_PROJECT_ID=$(profile_shell_quote "$project_id") HYDRA_HEAD_ID=$(profile_shell_quote "$head_id") HYDRA_INSTANCE_ID=$(profile_shell_quote "$instance_id") HYDRA_BRANCH=$(profile_shell_quote "$branch") HYDRA_WORKTREE=$(profile_shell_quote "$worktree_path") HYDRA_STATE_DIR=$(profile_shell_quote "$head_dir") HYDRA_TASK_FILE=$(profile_shell_quote "$head_dir/task")"
    send_keys_to_session "$session" "$identity_export" || {
        echo "Error: Failed to export head identity into session" >&2
        spawn_rollback_session "$session" "$branch" "$worktree_path"
        return 1
    }
    event_emit "$project_id" "$head_id" "$instance_id" lifecycle.started hydra local \
        "{\"profile\":\"$(json_escape "$ai_tool")\"}" >/dev/null || {
        echo "Error: Failed to record lifecycle event" >&2
        spawn_rollback_session "$session" "$branch" "$worktree_path"
        return 1
    }

    # Apply YAML config if present; otherwise custom/built-in layout
    # If template specified, use it as base config (merged with session config)
    if [ -n "$template" ] && [ -z "${HYDRA_DISABLE_YAML:-}" ]; then
        _load_lib template
        merged_cfg="$(apply_template "$template" "$worktree_path" "$repo_root" "$branch" "$session")" || {
            spawn_rollback_session "$session" "$branch" "$worktree_path"
            return 1
        }
        if [ -n "$merged_cfg" ] && [ -f "$merged_cfg" ]; then
            # Extract layout/ai_tool from template if not already specified by CLI
            if [ "$layout" = "default" ]; then
                tpl_layout="$(get_template_field "$merged_cfg" "layout")"
                [ -n "$tpl_layout" ] && layout="$tpl_layout"
            fi
            if [ -z "$ai_tool" ]; then
                tpl_ai="$(get_template_field "$merged_cfg" "ai_tool")"
                [ -n "$tpl_ai" ] && ai_tool="$tpl_ai"
            fi
            if ! apply_yaml_config "$merged_cfg" "$session" "$worktree_path" "$repo_root" user-template; then
                rm -f "$merged_cfg"
                spawn_rollback_session "$session" "$branch" "$worktree_path"
                return 1
            fi
            rm -f "$merged_cfg"
        fi
    elif [ -z "${HYDRA_DISABLE_YAML:-}" ] && cfgpath="$(locate_yaml_config "$worktree_path" "$repo_root" 2>/dev/null || true)" && [ -n "$cfgpath" ]; then
        if ! apply_yaml_config "$cfgpath" "$session" "$worktree_path" "$repo_root"; then
            spawn_rollback_session "$session" "$branch" "$worktree_path"
            return 1
        fi
    else
        apply_custom_layout_or_default "$layout" "$session" "$worktree_path" "$repo_root"
        # Send optional startup commands
        run_startup_commands "$session" "$worktree_path" "$repo_root"
    fi

    # Start AI tool unless explicitly skipped (e.g., demos/CI)
    if [ "$ai_tool" != none ]; then
        echo "Starting $ai_tool in session '$session'..." >&2
        if ! send_keys_to_session "$session" "$launch_command"; then
            spawn_rollback_session "$session" "$branch" "$worktree_path"
            return 1
        fi
    fi

    lifecycle_set_observed "$branch" running hydra exact || {
        spawn_rollback_session "$session" "$branch" "$worktree_path"
        return 1
    }

    # Run post-spawn hook (best-effort)
    run_hook post-spawn "$worktree_path" "$repo_root" "$session" "$branch"

    # Return session name for caller
    echo "$session"
    return 0
}
