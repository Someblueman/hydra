#!/bin/sh
# Directory-backed, scalar-record state v2.

HYDRA_STATE_V2_ROOT="${HYDRA_STATE_V2_ROOT:-${HYDRA_HOME:?HYDRA_HOME is required}/state/v2}"

state_v2_init() {
    mkdir -p "$HYDRA_STATE_V2_ROOT/projects" || return 1
    chmod 700 "$HYDRA_STATE_V2_ROOT" "$HYDRA_STATE_V2_ROOT/projects" 2>/dev/null || true
    if [ ! -f "$HYDRA_STATE_V2_ROOT/schema-version" ]; then
        printf '2\n' > "$HYDRA_STATE_V2_ROOT/schema-version" || return 1
        chmod 600 "$HYDRA_STATE_V2_ROOT/schema-version" 2>/dev/null || true
    fi
    [ "$(sed -n '1p' "$HYDRA_STATE_V2_ROOT/schema-version")" = "2" ]
}

state_v2_project_dir() {
    hydra_valid_id "$1" || return 1
    printf '%s/projects/%s\n' "$HYDRA_STATE_V2_ROOT" "$1"
}

state_v2_head_dir() {
    _sv2hd_project="$1"
    _sv2hd_head="$2"
    hydra_valid_id "$_sv2hd_project" || return 1
    hydra_valid_id "$_sv2hd_head" || return 1
    printf '%s/projects/%s/heads/%s\n' "$HYDRA_STATE_V2_ROOT" "$_sv2hd_project" "$_sv2hd_head"
}

state_v2_write_scalar() {
    _sv2ws_path="$1"
    _sv2ws_value="$2"
    case "$_sv2ws_value" in
        *'
'*|*''*) echo "Error: scalar state values cannot contain newlines" >&2; return 1 ;;
    esac
    _sv2ws_tmp="$(mktemp_adjacent "$_sv2ws_path")" || return 1
    printf '%s\n' "$_sv2ws_value" > "$_sv2ws_tmp"
    chmod 600 "$_sv2ws_tmp" 2>/dev/null || true
    if ! atomic_replace "$_sv2ws_path" "$_sv2ws_tmp"; then
        rm -f "$_sv2ws_tmp"
        return 1
    fi
}

state_v2_write_text() {
    _sv2wt_path="$1"
    _sv2wt_value="$2"
    _sv2wt_tmp="$(mktemp_adjacent "$_sv2wt_path")" || return 1
    printf '%s' "$_sv2wt_value" > "$_sv2wt_tmp"
    chmod 600 "$_sv2wt_tmp" 2>/dev/null || true
    if ! atomic_replace "$_sv2wt_path" "$_sv2wt_tmp"; then
        rm -f "$_sv2wt_tmp"
        return 1
    fi
}

state_v2_scalar_valid() {
    [ -f "$1" ] && LC_ALL=C awk '
        index($0, "\r") || NR > 1 { invalid = 1 }
        END { exit !(NR == 1 && !invalid) }
    ' "$1"
}

state_v2_init_project() {
    _sv2ip_project="$1"
    _sv2ip_root="$2"
    state_v2_init || return 1
    _sv2ip_dir="$(state_v2_project_dir "$_sv2ip_project")" || return 1
    _sv2ip_lock="project_${_sv2ip_project}"
    acquire_lock "$_sv2ip_lock" "state project init" || return 1
    if [ -d "$_sv2ip_dir" ]; then
        if [ "$(sed -n '1p' "$_sv2ip_dir/project-id" 2>/dev/null || true)" != "$_sv2ip_project" ] || \
           ! mkdir -p "$_sv2ip_dir/heads" || \
           ! state_v2_write_scalar "$_sv2ip_dir/repo-root" "$_sv2ip_root"; then
            release_lock "$_sv2ip_lock"
            return 1
        fi
        release_lock "$_sv2ip_lock"
        return 0
    fi
    _sv2ip_tmp="$(mktemp -d "$HYDRA_STATE_V2_ROOT/projects/.project.XXXXXX")" || {
        release_lock "$_sv2ip_lock"; return 1;
    }
    mkdir -p "$_sv2ip_tmp/heads" || {
        rm -rf "$_sv2ip_tmp"; release_lock "$_sv2ip_lock"; return 1;
    }
    chmod 700 "$_sv2ip_tmp" "$_sv2ip_tmp/heads" 2>/dev/null || true
    if ! state_v2_write_scalar "$_sv2ip_tmp/project-id" "$_sv2ip_project" || \
       ! state_v2_write_scalar "$_sv2ip_tmp/repo-root" "$_sv2ip_root" || \
       ! mv "$_sv2ip_tmp" "$_sv2ip_dir"; then
        rm -rf "$_sv2ip_tmp"
        release_lock "$_sv2ip_lock"
        return 1
    fi
    release_lock "$_sv2ip_lock"
}

state_v2_find_head_by_branch() {
    _sv2fh_project="$1"
    _sv2fh_branch="$2"
    _sv2fh_project_dir="$(state_v2_project_dir "$_sv2fh_project")" || return 1
    [ -d "$_sv2fh_project_dir/heads" ] || return 1
    for _sv2fh_dir in "$_sv2fh_project_dir"/heads/head_*; do
        [ -d "$_sv2fh_dir" ] || continue
        if [ "$(sed -n '1p' "$_sv2fh_dir/branch" 2>/dev/null || true)" = "$_sv2fh_branch" ]; then
            basename "$_sv2fh_dir"
            return 0
        fi
    done
    return 1
}

state_v2_create_head() {
    _sv2ch_project="$1"
    _sv2ch_branch="$2"
    _sv2ch_session="$3"
    _sv2ch_profile="${4:--}"
    _sv2ch_group="${5:--}"
    _sv2ch_created="${6:-$(date +%s)}"
    _sv2ch_deps="${7:--}"
    _sv2ch_pr="${8:--}"
    _sv2ch_root="${9:-$(git rev-parse --show-toplevel 2>/dev/null || true)}"
    _sv2ch_requested_head="${10:-}"
    _sv2ch_requested_instance="${11:-}"
    _sv2ch_worktree="${12:-}"
    _sv2ch_task="${13:-}"
    _sv2ch_base_ref="${14:-}"
    _sv2ch_provider_session="${15:-}"
    _sv2ch_scopes="${16:-}"
    _sv2ch_completion_policy="${17:-declared-done}"

    if [ -z "$_sv2ch_branch" ] || [ -z "$_sv2ch_session" ]; then
        return 1
    fi
    case "$_sv2ch_completion_policy" in
        declared-done|observed-exit-zero|either) ;;
        *) return 1 ;;
    esac
    state_v2_init_project "$_sv2ch_project" "$_sv2ch_root" || return 1
    _sv2ch_lock="state_${_sv2ch_project}"
    acquire_lock "$_sv2ch_lock" "state head create" || return 1
    if _sv2ch_existing="$(state_v2_find_head_by_branch "$_sv2ch_project" "$_sv2ch_branch" 2>/dev/null)"; then
        release_lock "$_sv2ch_lock"
        printf '%s\n' "$_sv2ch_existing"
        return 0
    fi

    if [ -n "$_sv2ch_requested_head" ]; then
        hydra_valid_id "$_sv2ch_requested_head" || { release_lock "$_sv2ch_lock"; return 1; }
        _sv2ch_head="$_sv2ch_requested_head"
    else
        _sv2ch_head="$(hydra_new_id head "$_sv2ch_project|$_sv2ch_branch")" || {
            release_lock "$_sv2ch_lock"; return 1;
        }
    fi
    if [ -n "$_sv2ch_requested_instance" ]; then
        hydra_valid_id "$_sv2ch_requested_instance" || { release_lock "$_sv2ch_lock"; return 1; }
        _sv2ch_instance="$_sv2ch_requested_instance"
    else
        _sv2ch_instance="$(hydra_new_id instance "$_sv2ch_head|$_sv2ch_session")" || {
            release_lock "$_sv2ch_lock"; return 1;
        }
    fi
    _sv2ch_dir="$(state_v2_head_dir "$_sv2ch_project" "$_sv2ch_head")" || {
        release_lock "$_sv2ch_lock"; return 1;
    }
    _sv2ch_heads_dir="$(dirname "$_sv2ch_dir")"
    _sv2ch_tmp="$(mktemp -d "$_sv2ch_heads_dir/.head.XXXXXX")" || {
        release_lock "$_sv2ch_lock"; return 1;
    }
    mkdir -p "$_sv2ch_tmp/instances/$_sv2ch_instance" "$_sv2ch_tmp/events/archive" || {
        rm -rf "$_sv2ch_tmp"; release_lock "$_sv2ch_lock"; return 1;
    }
    chmod -R go-rwx "$_sv2ch_tmp" 2>/dev/null || true

    if ! state_v2_write_scalar "$_sv2ch_tmp/head-id" "$_sv2ch_head" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/branch" "$_sv2ch_branch" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/session" "$_sv2ch_session" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/profile" "$_sv2ch_profile" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/group" "$_sv2ch_group" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/created-at" "$_sv2ch_created" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/dependencies" "$_sv2ch_deps" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/pr" "$_sv2ch_pr" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/current-instance" "$_sv2ch_instance" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/desired-state" "running" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/completion-policy" "$_sv2ch_completion_policy" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/worktree" "$_sv2ch_worktree" || \
       ! state_v2_write_text "$_sv2ch_tmp/task" "$_sv2ch_task" || \
       ! state_v2_write_text "$_sv2ch_tmp/scopes" "$_sv2ch_scopes" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/base-ref" "$_sv2ch_base_ref" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/instances/$_sv2ch_instance/instance-id" "$_sv2ch_instance" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/instances/$_sv2ch_instance/session" "$_sv2ch_session" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/instances/$_sv2ch_instance/started-at" "$_sv2ch_created" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/instances/$_sv2ch_instance/observed-status" starting || \
       ! state_v2_write_scalar "$_sv2ch_tmp/instances/$_sv2ch_instance/observed-source" hydra || \
       ! state_v2_write_scalar "$_sv2ch_tmp/instances/$_sv2ch_instance/observed-confidence" exact || \
       ! state_v2_write_scalar "$_sv2ch_tmp/instances/$_sv2ch_instance/observed-at" "$_sv2ch_created" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/instances/$_sv2ch_instance/observed-exit-code" "" || \
       ! state_v2_write_scalar "$_sv2ch_tmp/instances/$_sv2ch_instance/provider-session-id" "$_sv2ch_provider_session"; then
        rm -rf "$_sv2ch_tmp"
        release_lock "$_sv2ch_lock"
        return 1
    fi
    : > "$_sv2ch_tmp/events/events.jsonl"
    chmod 600 "$_sv2ch_tmp/events/events.jsonl" 2>/dev/null || true
    if ! mv "$_sv2ch_tmp" "$_sv2ch_dir"; then
        rm -rf "$_sv2ch_tmp"
        release_lock "$_sv2ch_lock"
        return 1
    fi
    release_lock "$_sv2ch_lock"
    printf '%s\n' "$_sv2ch_head"
}

state_v2_verify() {
    [ -f "$HYDRA_STATE_V2_ROOT/schema-version" ] || {
        echo "state v2: schema-version is missing" >&2
        return 1
    }
    if ! state_v2_scalar_valid "$HYDRA_STATE_V2_ROOT/schema-version" || \
       [ "$(sed -n '1p' "$HYDRA_STATE_V2_ROOT/schema-version")" != "2" ]; then
        echo "state v2: unsupported schema version" >&2
        return 1
    fi
    _sv2v_errors=0
    for _sv2v_project in "$HYDRA_STATE_V2_ROOT"/projects/project_*; do
        [ -d "$_sv2v_project" ] || continue
        _sv2v_project_id="$(basename "$_sv2v_project")"
        if ! hydra_valid_id "$_sv2v_project_id" || \
           ! state_v2_scalar_valid "$_sv2v_project/project-id" || \
           ! state_v2_scalar_valid "$_sv2v_project/repo-root" || \
           [ "$(sed -n '1p' "$_sv2v_project/project-id" 2>/dev/null || true)" != "$_sv2v_project_id" ] || \
           [ -z "$(sed -n '1p' "$_sv2v_project/repo-root" 2>/dev/null || true)" ]; then
            echo "state v2: invalid project record $_sv2v_project" >&2
            _sv2v_errors=1
        fi
        for _sv2v_head in "$_sv2v_project"/heads/head_*; do
            [ -d "$_sv2v_head" ] || continue
            _sv2v_head_id="$(basename "$_sv2v_head")"
            _sv2v_instance="$(sed -n '1p' "$_sv2v_head/current-instance" 2>/dev/null || true)"
            _sv2v_required_missing=0
            for _sv2v_file in head-id branch session profile group created-at dependencies pr \
                current-instance desired-state completion-policy worktree base-ref; do
                state_v2_scalar_valid "$_sv2v_head/$_sv2v_file" || _sv2v_required_missing=1
            done
            for _sv2v_file in task scopes; do
                [ -f "$_sv2v_head/$_sv2v_file" ] || _sv2v_required_missing=1
            done
            _sv2v_instance_required_missing=0
            for _sv2v_file in instance-id session started-at observed-status observed-source \
                observed-confidence observed-at observed-exit-code provider-session-id; do
                state_v2_scalar_valid "$_sv2v_head/instances/$_sv2v_instance/$_sv2v_file" || \
                    _sv2v_instance_required_missing=1
            done
            if ! hydra_valid_id "$_sv2v_head_id" || \
               [ "$(sed -n '1p' "$_sv2v_head/head-id" 2>/dev/null || true)" != "$_sv2v_head_id" ] || \
               [ -z "$(sed -n '1p' "$_sv2v_head/branch" 2>/dev/null || true)" ] || \
               [ -z "$(sed -n '1p' "$_sv2v_head/session" 2>/dev/null || true)" ] || \
               [ "$_sv2v_required_missing" -ne 0 ] || \
               ! grep -Eq '^(running|stopping|stopped)$' "$_sv2v_head/desired-state" 2>/dev/null || \
               ! grep -Eq '^(declared-done|observed-exit-zero|either)$' "$_sv2v_head/completion-policy" 2>/dev/null || \
               ! hydra_valid_id "$_sv2v_instance" || \
               [ ! -d "$_sv2v_head/instances/$_sv2v_instance" ] || \
               [ "$_sv2v_instance_required_missing" -ne 0 ] || \
               [ "$(sed -n '1p' "$_sv2v_head/instances/$_sv2v_instance/instance-id" 2>/dev/null || true)" != "$_sv2v_instance" ]; then
                echo "state v2: invalid head record $_sv2v_head" >&2
                _sv2v_errors=1
            fi
            if command -v event_verify_file >/dev/null 2>&1 && \
               ! event_verify_file "$_sv2v_head/events/events.jsonl"; then
                _sv2v_errors=1
            fi
            _sv2v_branch="$(sed -n '1p' "$_sv2v_head/branch" 2>/dev/null || true)"
            for _sv2v_other in "$_sv2v_project"/heads/head_*; do
                [ -d "$_sv2v_other" ] || continue
                [ "$_sv2v_other" = "$_sv2v_head" ] && continue
                if [ "$(sed -n '1p' "$_sv2v_other/branch" 2>/dev/null || true)" = "$_sv2v_branch" ]; then
                    echo "state v2: duplicate branch identity '$_sv2v_branch' in $_sv2v_project" >&2
                    _sv2v_errors=1
                    break
                fi
            done
        done
    done
    return "$_sv2v_errors"
}

state_v2_backup() {
    _sv2b_stamp="$(date +%Y%m%dT%H%M%S)-$$"
    mkdir -p "$HYDRA_HOME/backups" || return 1
    _sv2b_dir="$(mktemp -d "$HYDRA_HOME/backups/state-$_sv2b_stamp.XXXXXX")" || return 1
    chmod 700 "$_sv2b_dir" 2>/dev/null || true
    if [ -f "${HYDRA_1X_MAP:-$HYDRA_HOME/map}" ]; then
        cp "${HYDRA_1X_MAP:-$HYDRA_HOME/map}" "$_sv2b_dir/map" || { rm -rf "$_sv2b_dir"; return 1; }
    else
        : > "$_sv2b_dir/map.absent" || { rm -rf "$_sv2b_dir"; return 1; }
    fi
    if [ ! -d "$HYDRA_HOME/state" ] || ! cp -R "$HYDRA_HOME/state" "$_sv2b_dir/state"; then
        rm -rf "$_sv2b_dir"
        return 1
    fi
    printf '%s\n' "$_sv2b_dir"
}

_state_v2_projection_matches() {
    _sv2pm_project_dir="$1"
    _sv2pm_map="$_sv2pm_project_dir/compat-map"
    [ -f "$_sv2pm_map" ] || return 0
    while IFS=' ' read -r _sv2pm_branch _sv2pm_session _sv2pm_profile _sv2pm_group \
        _sv2pm_created _sv2pm_deps _sv2pm_pr _sv2pm_extra || [ -n "${_sv2pm_branch:-}" ]; do
        if [ -z "${_sv2pm_branch:-}" ] || [ -z "${_sv2pm_session:-}" ] || \
            [ -n "${_sv2pm_extra:-}" ]; then
            echo "Error: malformed 1.9 compatibility projection: $_sv2pm_map" >&2
            return 1
        fi
        _sv2pm_head="$(state_v2_find_head_by_branch "$(basename "$_sv2pm_project_dir")" \
            "$_sv2pm_branch" 2>/dev/null)" || {
            echo "Error: compatibility projection has no durable head: $_sv2pm_branch" >&2
            return 1
        }
        _sv2pm_dir="$_sv2pm_project_dir/heads/$_sv2pm_head"
        if [ "$(sed -n '1p' "$_sv2pm_dir/session")" != "$_sv2pm_session" ] || \
            [ "$(sed -n '1p' "$_sv2pm_dir/profile")" != "${_sv2pm_profile:--}" ] || \
            [ "$(sed -n '1p' "$_sv2pm_dir/group")" != "${_sv2pm_group:--}" ] || \
            [ "$(sed -n '1p' "$_sv2pm_dir/created-at")" != "${_sv2pm_created:--}" ] || \
            [ "$(sed -n '1p' "$_sv2pm_dir/dependencies")" != "${_sv2pm_deps:--}" ] || \
            [ "$(sed -n '1p' "$_sv2pm_dir/pr")" != "${_sv2pm_pr:--}" ]; then
            echo "Error: compatibility projection diverges from durable head: $_sv2pm_branch" >&2
            return 1
        fi
    done < "$_sv2pm_map"
}

state_v2_migrate() {
    _sv2m_dry_run="${1:-}"
    state_v2_verify || return 1
    _sv2m_count=0
    for _sv2m_project in "$HYDRA_STATE_V2_ROOT"/projects/project_*; do
        [ -d "$_sv2m_project" ] || continue
        _state_v2_projection_matches "$_sv2m_project" || return 1
        [ ! -f "$_sv2m_project/compat-map" ] || _sv2m_count=$((_sv2m_count + 1))
    done
    if [ "$_sv2m_dry_run" = --dry-run ]; then
        echo "State migration plan:"
        echo "  remove verified 1.9 compatibility projections: $_sv2m_count"
        echo "  remove inactive global 1.x map: $([ -f "${HYDRA_1X_MAP:-$HYDRA_HOME/map}" ] && echo yes || echo no)"
        echo "  backup: $HYDRA_HOME/backups/state-<timestamp>-<pid>.<unique>"
        echo "  verify authoritative state v2 after migration: yes"
        return 0
    fi
    _sv2m_backup="$(state_v2_backup)" || return 1
    for _sv2m_project in "$HYDRA_STATE_V2_ROOT"/projects/project_*; do
        [ -d "$_sv2m_project" ] || continue
        rm -f "$_sv2m_project/compat-map" || return 1
    done
    rm -f "${HYDRA_1X_MAP:-$HYDRA_HOME/map}" || return 1
    state_v2_verify || {
        echo "Error: post-migration verification failed; rollback with: hydra state rollback $_sv2m_backup" >&2
        return 1
    }
    echo "Removed $_sv2m_count compatibility projection(s)"
    echo "Backup: $_sv2m_backup"
}

_state_v2_release_locks_file() {
    _sv2rl_file="$1"
    [ -f "$_sv2rl_file" ] || return 0
    while IFS= read -r _sv2rl_name; do
        [ -z "$_sv2rl_name" ] || release_lock "$_sv2rl_name"
    done < "$_sv2rl_file"
}

state_v2_rollback() {
    _sv2r_backup="$1"
    case "$_sv2r_backup" in
        "$HYDRA_HOME"/backups/state-*) ;;
        *) echo "Error: backup must be under $HYDRA_HOME/backups" >&2; return 1 ;;
    esac
    [ -d "$_sv2r_backup" ] || { echo "Error: backup does not exist: $_sv2r_backup" >&2; return 1; }
    if [ ! -d "$_sv2r_backup/state" ] || \
       { [ -f "$_sv2r_backup/map" ] && [ -f "$_sv2r_backup/map.absent" ]; } || \
       { [ ! -f "$_sv2r_backup/map" ] && [ ! -f "$_sv2r_backup/map.absent" ]; }; then
        echo "Error: backup is incomplete: $_sv2r_backup" >&2
        return 1
    fi
    _sv2r_guard="$HYDRA_HOME/state"
    _sv2r_map_target="${HYDRA_1X_MAP:-$HYDRA_HOME/map}"
    if [ -z "$HYDRA_HOME" ] || [ "$_sv2r_guard" = "/state" ]; then
        return 1
    fi

    _sv2r_locks="$(mktemp)" || return 1
    if ! acquire_lock state_rollback "state rollback barrier"; then
        rm -f "$_sv2r_locks"
        echo "Error: state rollback requires idle state writers" >&2
        return 1
    fi
    printf '%s\n' state_rollback >> "$_sv2r_locks"
    for _sv2r_project_dir in "$HYDRA_STATE_V2_ROOT"/projects/project_*; do
        [ -d "$_sv2r_project_dir" ] || continue
        _sv2r_project="$(basename "$_sv2r_project_dir")"
        hydra_valid_id "$_sv2r_project" || {
            _state_v2_release_locks_file "$_sv2r_locks"
            rm -f "$_sv2r_locks"
            return 1
        }
        for _sv2r_lock in "project_${_sv2r_project}" "state_${_sv2r_project}"; do
            if ! acquire_lock "$_sv2r_lock" "state rollback barrier"; then
                _state_v2_release_locks_file "$_sv2r_locks"
                rm -f "$_sv2r_locks"
                echo "Error: state rollback requires idle state writers" >&2
                return 1
            fi
            printf '%s\n' "$_sv2r_lock" >> "$_sv2r_locks"
        done
        for _sv2r_head_dir in "$_sv2r_project_dir"/heads/head_*; do
            [ -d "$_sv2r_head_dir" ] || continue
            _sv2r_head="$(basename "$_sv2r_head_dir")"
            hydra_valid_id "$_sv2r_head" || continue
            _sv2r_lock="events_${_sv2r_project}_${_sv2r_head}"
            if ! acquire_lock "$_sv2r_lock" "state rollback barrier"; then
                _state_v2_release_locks_file "$_sv2r_locks"
                rm -f "$_sv2r_locks"
                echo "Error: state rollback requires idle event writers" >&2
                return 1
            fi
            printf '%s\n' "$_sv2r_lock" >> "$_sv2r_locks"
        done
    done

    _sv2r_stage="$(mktemp -d "$HYDRA_HOME/.state-rollback.XXXXXX")" || {
        _state_v2_release_locks_file "$_sv2r_locks"
        rm -f "$_sv2r_locks"
        return 1
    }
    if [ -d "$_sv2r_backup/state" ]; then
        cp -R "$_sv2r_backup/state" "$_sv2r_stage/restored-state" || {
            rm -rf "$_sv2r_stage"
            _state_v2_release_locks_file "$_sv2r_locks"
            rm -f "$_sv2r_locks"
            return 1
        }
    fi
    _sv2r_map_tmp=""
    if [ -f "$_sv2r_backup/map" ]; then
        _sv2r_map_tmp="$(mktemp_adjacent "$_sv2r_map_target")" || {
            rm -rf "$_sv2r_stage"
            _state_v2_release_locks_file "$_sv2r_locks"
            rm -f "$_sv2r_locks"
            return 1
        }
        cp "$_sv2r_backup/map" "$_sv2r_map_tmp" || {
            [ -z "$_sv2r_map_tmp" ] || rm -f "$_sv2r_map_tmp"
            rm -rf "$_sv2r_stage"
            _state_v2_release_locks_file "$_sv2r_locks"
            rm -f "$_sv2r_locks"
            return 1
        }
    fi

    if [ -d "$_sv2r_guard" ]; then
        mv "$_sv2r_guard" "$_sv2r_stage/previous-state" || {
            rm -f "$_sv2r_map_tmp"
            rm -rf "$_sv2r_stage"
            _state_v2_release_locks_file "$_sv2r_locks"
            rm -f "$_sv2r_locks"
            return 1
        }
    fi
    if [ -d "$_sv2r_stage/restored-state" ] && ! mv "$_sv2r_stage/restored-state" "$_sv2r_guard"; then
        [ ! -d "$_sv2r_stage/previous-state" ] || mv "$_sv2r_stage/previous-state" "$_sv2r_guard"
        [ -z "$_sv2r_map_tmp" ] || rm -f "$_sv2r_map_tmp"
        rm -rf "$_sv2r_stage"
        _state_v2_release_locks_file "$_sv2r_locks"
        rm -f "$_sv2r_locks"
        return 1
    fi
    if { [ -n "$_sv2r_map_tmp" ] && ! atomic_replace "$_sv2r_map_target" "$_sv2r_map_tmp"; } || \
       { [ -z "$_sv2r_map_tmp" ] && ! rm -f "$_sv2r_map_target"; }; then
        [ ! -d "$_sv2r_guard" ] || mv "$_sv2r_guard" "$_sv2r_stage/failed-state"
        [ ! -d "$_sv2r_stage/previous-state" ] || mv "$_sv2r_stage/previous-state" "$_sv2r_guard"
        [ -z "$_sv2r_map_tmp" ] || rm -f "$_sv2r_map_tmp"
        rm -rf "$_sv2r_stage"
        _state_v2_release_locks_file "$_sv2r_locks"
        rm -f "$_sv2r_locks"
        return 1
    fi
    rm -rf "$_sv2r_stage"
    _state_v2_release_locks_file "$_sv2r_locks"
    rm -f "$_sv2r_locks"
    echo "Rolled back state from $_sv2r_backup"
}
