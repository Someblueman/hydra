#!/bin/sh
# Project initialization, agent profiles, and capability discovery.

cmd_init() {
    _ci_profile=""
    _ci_trust=0
    _ci_worktree_root=""
    _ci_force=0
    _ci_json=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --profile) [ $# -ge 2 ] || { echo "Error: --profile requires a name" >&2; return 1; }; _ci_profile="$2"; shift 2 ;;
            --no-agent) _ci_profile=none; shift ;;
            --trust) _ci_trust=1; shift ;;
            --worktree-root) [ $# -ge 2 ] || { echo "Error: --worktree-root requires a path" >&2; return 1; }; _ci_worktree_root="$2"; shift 2 ;;
            --force) _ci_force=1; shift ;;
            -j|--json) _ci_json=1; shift ;;
            *) echo "Error: unknown init option '$1'" >&2; return 1 ;;
        esac
    done

    _ci_root="$(git rev-parse --show-toplevel 2>/dev/null)" || {
        [ "$_ci_json" -eq 0 ] || json_error init not_git_repository "Not in a Git repository" "cd into a repository and retry"
        [ "$_ci_json" -eq 1 ] || echo "Error: hydra init must run inside a Git repository" >&2
        return 1
    }
    _ci_project="$(hydra_ensure_project_id)" || return 1
    if [ -z "$_ci_profile" ]; then
        _ci_profile="$(profile_resolve "")" || return 1
    else
        _ci_profile="$(profile_resolve "$_ci_profile")" || return 1
    fi
    if [ -z "$_ci_worktree_root" ]; then
        _ci_worktree_root="$(project_default_worktree_root "$_ci_project")" || return 1
    fi
    case "$_ci_worktree_root" in
        /*) ;;
        *) _ci_worktree_root="$_ci_root/$_ci_worktree_root" ;;
    esac
    validate_worktree_path "$_ci_worktree_root" || return 1

    # Refuse partial initialization when v1 data needs an explicit backup/migration.
    project_activate_state_v2 "$_ci_project" || return 1
    project_write_host_value default-profile "$_ci_profile" || return 1
    project_write_host_value worktree-root "$_ci_worktree_root" || return 1

    _ci_config_dir="$_ci_root/.hydra"
    _ci_config="$_ci_config_dir/config.yml"
    mkdir -p "$_ci_config_dir" || return 1
    if [ ! -f "$_ci_config" ] || [ "$_ci_force" -eq 1 ]; then
        _ci_tmp="$(mktemp "$_ci_config_dir/.config.XXXXXX")" || return 1
        {
            echo "version: 1"
            echo "profile: $_ci_profile"
            echo "setup:"
        } > "$_ci_tmp"
        chmod 644 "$_ci_tmp" 2>/dev/null || true
        mv "$_ci_tmp" "$_ci_config" || { rm -f "$_ci_tmp"; return 1; }
    fi

    _ci_common="$(hydra_git_common_dir)" || return 1
    _ci_exclude="$_ci_common/info/exclude"
    mkdir -p "$(dirname "$_ci_exclude")" || return 1
    [ -f "$_ci_exclude" ] || : > "$_ci_exclude"
    grep -Fqx '.hydra/local.yml' "$_ci_exclude" 2>/dev/null || printf '%s\n' '.hydra/local.yml' >> "$_ci_exclude"
    _ci_local="$_ci_config_dir/local.yml"
    _ci_local_tmp="$(mktemp_adjacent "$_ci_local")" || return 1
    {
        echo "version: 1"
        echo "worktree_root: $_ci_worktree_root"
    } > "$_ci_local_tmp"
    chmod 600 "$_ci_local_tmp" 2>/dev/null || true
    atomic_replace "$_ci_local" "$_ci_local_tmp" || return 1

    if [ "$_ci_trust" -eq 1 ]; then
        project_set_trusted || return 1
        _ci_trusted=true
    else
        _ci_trusted=false
    fi

    if [ "$_ci_json" -eq 1 ]; then
        json_success init "{\"project_id\":\"$(json_escape "$_ci_project")\",\"profile\":\"$(json_escape "$_ci_profile")\",\"worktree_root\":\"$(json_escape "$_ci_worktree_root")\",\"trusted\":$_ci_trusted}"
    else
        echo "Initialized Hydra project $_ci_project"
        echo "  profile: $_ci_profile"
        echo "  worktree root: $_ci_worktree_root"
        echo "  repository config trusted: $_ci_trusted"
        [ "$_ci_trusted" = true ] || echo "Next: review .hydra/config.yml, then run 'hydra init --trust --profile $_ci_profile'"
    fi
}

cmd_agent() {
    _ca_action="${1:-list}"
    [ $# -eq 0 ] || shift
    case "$_ca_action" in
        list)
            printf '%-12s %-10s %-12s %s\n' PROFILE AVAILABLE TIER CONFIDENCE
            profile_list | while IFS= read -r _ca_name; do
                [ -n "$_ca_name" ] || continue
                if [ "$_ca_name" = none ] || profile_executable_path "$_ca_name" >/dev/null 2>&1; then _ca_available=yes; else _ca_available=no; fi
                printf '%-12s %-10s %-12s %s\n' "$_ca_name" "$_ca_available" \
                    "$(profile_field "$_ca_name" tier)" "$(profile_field "$_ca_name" confidence)"
            done
            ;;
        show)
            [ $# -eq 1 ] || { echo "Usage: hydra agent show <profile>" >&2; return 1; }
            profile_exists "$1" || { echo "Error: unknown profile '$1'" >&2; return 1; }
            echo "Profile: $1"
            for _ca_field in executable tier prompt_mode resume_mode environment adapter confidence; do
                echo "  $_ca_field: $(profile_field "$1" "$_ca_field")"
            done
            _ca_path="$(profile_executable_path "$1" 2>/dev/null || true)"
            echo "  resolved_path: ${_ca_path:-unavailable}"
            ;;
        doctor)
            _ca_fail=0
            profile_list | while IFS= read -r _ca_name; do
                [ -n "$_ca_name" ] || continue
                if [ "$_ca_name" = none ]; then
                    echo "[OK] none: Tier 0 shell head"
                elif _ca_path="$(profile_executable_path "$_ca_name" 2>/dev/null)"; then
                    _ca_version="$("$_ca_path" --version 2>/dev/null | sed -n '1p' || true)"
                    echo "[OK] $_ca_name: ${_ca_version:-version unavailable}; prompt=$(profile_field "$_ca_name" prompt_mode); resume=$(profile_field "$_ca_name" resume_mode); adapter=$(profile_field "$_ca_name" adapter)"
                else
                    echo "[INFO] $_ca_name: executable unavailable; deterministic fallback is Tier 0/none"
                fi
            done
            return "$_ca_fail"
            ;;
        init)
            _ca_name="${1:-}"
            [ -n "$_ca_name" ] || { echo "Usage: hydra agent init <name> --executable <absolute-path> [--prompt-mode none|task-file]" >&2; return 1; }
            shift
            _ca_executable=""
            _ca_prompt=none
            while [ $# -gt 0 ]; do
                case "$1" in
                    --executable) [ $# -ge 2 ] || return 1; _ca_executable="$2"; shift 2 ;;
                    --prompt-mode) [ $# -ge 2 ] || return 1; _ca_prompt="$2"; shift 2 ;;
                    *) echo "Error: unknown agent init option '$1'" >&2; return 1 ;;
                esac
            done
            profile_create_custom "$_ca_name" "$_ca_executable" "$_ca_prompt" || {
                echo "Error: custom profile requires a unique safe name and executable absolute path" >&2
                return 1
            }
            echo "Created agent profile '$_ca_name'"
            ;;
        *) echo "Usage: hydra agent <list|show|doctor|init>" >&2; return 1 ;;
    esac
}

cmd_capabilities() {
    [ "${1:-}" = "--json" ] || { echo "Usage: hydra capabilities --json" >&2; return 1; }
    _cc_tmp="$(mktemp)" || return 1
    profile_list | while IFS= read -r _cc_name; do
        [ -n "$_cc_name" ] || continue
        if [ "$_cc_name" = none ] || profile_executable_path "$_cc_name" >/dev/null 2>&1; then _cc_available=true; else _cc_available=false; fi
        printf '{"name":"%s","available":%s,"tier":%s,"prompt_mode":"%s","resume_mode":"%s","adapter":"%s","confidence":"%s"}\n' \
            "$(json_escape "$_cc_name")" "$_cc_available" "$(profile_field "$_cc_name" tier)" \
            "$(json_escape "$(profile_field "$_cc_name" prompt_mode)")" \
            "$(json_escape "$(profile_field "$_cc_name" resume_mode)")" \
            "$(json_escape "$(profile_field "$_cc_name" adapter)")" \
            "$(json_escape "$(profile_field "$_cc_name" confidence)")" >> "$_cc_tmp"
    done
    printf '{"schema_version":1,"ok":true,"command":"capabilities","data":{"state_schema":2,"event_schema":1,"json_schema":1,"profiles":['
    _cc_first=1
    while IFS= read -r _cc_line; do
        [ "$_cc_first" -eq 1 ] || printf ','
        _cc_first=0
        printf '%s' "$_cc_line"
    done < "$_cc_tmp"
    printf ']}}\n'
    rm -f "$_cc_tmp"
}

cmd_path() {
    _cp_branch="${1:-$(git branch --show-current 2>/dev/null || true)}"
    _cp_project="$(hydra_get_project_id)" || return 1
    _cp_head="$(state_v2_find_head_by_branch "$_cp_project" "$_cp_branch")" || {
        echo "Error: no Hydra head for branch '$_cp_branch' in this project" >&2
        return 1
    }
    _cp_dir="$(state_v2_head_dir "$_cp_project" "$_cp_head")" || return 1
    _cp_stored="$(sed -n '1p' "$_cp_dir/worktree" 2>/dev/null || true)"
    if [ -n "$_cp_stored" ]; then
        printf '%s\n' "$_cp_stored"
    else
        project_worktree_path "$_cp_project" "$_cp_head"
    fi
}
