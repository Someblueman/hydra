#!/bin/sh
# Project initialization, agent profiles, and capability discovery.

cmd_init() {
    _ci_profile=""
    _ci_trust=0
    _ci_worktree_root=""
    _ci_force=0
    _ci_json=0
    _ci_write_shared=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --profile) [ $# -ge 2 ] || { cli_error init invalid_input "--profile requires a name" "run hydra agent list"; return 1; }; _ci_profile="$2"; shift 2 ;;
            --no-agent) _ci_profile=none; shift ;;
            --trust) _ci_trust=1; shift ;;
            --worktree-root) [ $# -ge 2 ] || { cli_error init invalid_input "--worktree-root requires a path" "pass an absolute or project-relative path"; return 1; }; _ci_worktree_root="$2"; shift 2 ;;
            --write-shared-config) _ci_write_shared=1; shift ;;
            --force) _ci_force=1; shift ;;
            -j|--json) _ci_json=1; shift ;;
            *) cli_error init invalid_input "unknown init option '$1'" "run hydra help"; return 1 ;;
        esac
    done

    _ci_root="$(git rev-parse --show-toplevel 2>/dev/null)" || {
        cli_error init not_git_repository "hydra init must run inside a Git repository" "cd into a repository and retry"
        return 1
    }
    if [ -L "$_ci_root/.hydra" ]; then
        cli_error init unsafe_configuration "repository .hydra must not be a symbolic link" "replace the link with reviewed regular configuration files"
        return 1
    fi
    _ci_project="$(hydra_ensure_project_id)" || return 1
    if [ -z "$_ci_profile" ]; then
        _ci_profile="$(profile_resolve "")" || return 1
    else
        _ci_profile="$(profile_resolve "$_ci_profile")" || return 1
    fi
    # Without --worktree-root, reopening keeps the root this host already uses.
    # A host-local record left by an earlier release is imported before removal.
    _ci_legacy_root=""
    if [ -z "$_ci_worktree_root" ]; then
        _ci_worktree_root="$(project_host_value worktree-root 2>/dev/null || true)"
        _ci_legacy_root="$(init_legacy_local_worktree_root "$_ci_root")"
        if [ -n "$_ci_legacy_root" ] && [ "$_ci_legacy_root" != "$_ci_worktree_root" ]; then
            _ci_worktree_root="$_ci_legacy_root"
        else
            _ci_legacy_root=""
        fi
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

    # Registration is complete and lives under the Git common directory. Nothing
    # below writes into the source tree unless the user asks for shared config.
    if project_is_trusted "$_ci_root" 2>/dev/null; then _ci_was_trusted=1; else _ci_was_trusted=0; fi
    init_migrate_generated_config "$_ci_root" || return 1
    if [ -n "$INIT_MIGRATED_FILES" ] && [ "$_ci_was_trusted" -eq 1 ] && [ "$_ci_trust" -eq 0 ]; then
        # Removing generated, content-free files cannot widen an approval that was
        # current a moment ago; carry it forward instead of invalidating it.
        project_set_trusted || return 1
    fi
    if [ "$_ci_write_shared" -eq 1 ]; then
        init_write_shared_config "$_ci_root" "$_ci_force" "$_ci_json" || return 1
    fi

    if [ "$_ci_trust" -eq 1 ]; then
        project_set_trusted || {
            cli_error init unsafe_configuration "cannot approve repository configuration" "use readable regular files and directories without symbolic links or newline-bearing paths"
            return 1
        }
        _ci_trusted=true
    else
        _ci_trusted=false
    fi

    if [ "$_ci_json" -eq 1 ]; then
        [ -z "$INIT_MIGRATED_FILES" ] || echo "Note: removed generated $INIT_MIGRATED_FILES from an earlier Hydra; host-local settings live under the repository's .git directory" >&2
        [ -z "$_ci_legacy_root" ] || echo "Note: imported worktree root $_ci_legacy_root from the removed .hydra/local.yml" >&2
        json_success init "{\"project_id\":\"$(json_escape "$_ci_project")\",\"profile\":\"$(json_escape "$_ci_profile")\",\"worktree_root\":\"$(json_escape "$_ci_worktree_root")\",\"trusted\":$_ci_trusted}"
        return 0
    fi
    [ -z "$INIT_MIGRATED_FILES" ] || echo "Removed generated $INIT_MIGRATED_FILES from an earlier Hydra; host-local settings live under the repository's .git directory."
    [ -z "$_ci_legacy_root" ] || echo "Imported worktree root $_ci_legacy_root from the removed .hydra/local.yml."
    # Report the standing approval, not only this invocation's --trust.
    if [ "$_ci_trusted" = true ] || project_is_trusted "$_ci_root" 2>/dev/null; then
        _ci_config_state="Repository config trusted."
    elif [ "$(project_config_hash "$_ci_root" 2>/dev/null || echo none)" = none ]; then
        _ci_config_state="No repository config."
    else
        _ci_config_state="Repository config not trusted."
    fi
    echo "Ready: $(basename "$_ci_root") (agent: $_ci_profile). Worktrees go in $_ci_worktree_root. $_ci_config_state"
    echo "  project id: $_ci_project"
    [ "$_ci_config_state" != "Repository config not trusted." ] || echo "  Next: review .hydra/, then run 'hydra init --trust'"
}

# Earlier releases wrote a content-free .hydra/config.yml stub. Match that exact
# shape only; anything else is user configuration.
init_config_is_generated_stub() {
    awk '
        NR == 1 && $0 != "version: 1" { invalid = 1 }
        NR == 2 && $0 !~ /^profile: ?[A-Za-z0-9_-]*$/ { invalid = 1 }
        NR == 3 && $0 != "setup:" { invalid = 1 }
        END { exit !invalid && NR == 3 ? 0 : 1 }
    ' "$1"
}

# Earlier releases wrote .hydra/local.yml holding only version and worktree_root.
init_local_is_generated() {
    awk '
        /^[[:space:]]*$/ { next }
        /^#/ { next }
        /^version: 1$/ { next }
        /^worktree_root: / { next }
        { exit 1 }
    ' "$1"
}

init_path_is_tracked() {
    git -C "$1" ls-files --error-unmatch -- "$2" >/dev/null 2>&1
}

# Print the worktree_root recorded by a generated .hydra/local.yml, if any.
init_legacy_local_worktree_root() {
    _illwr_local="$1/.hydra/local.yml"
    [ -f "$_illwr_local" ] && [ ! -L "$_illwr_local" ] || return 0
    init_local_is_generated "$_illwr_local" || return 0
    sed -n 's/^worktree_root: //p' "$_illwr_local" | sed -n '1p'
}

# Remove the generated stub files and the exclude rule an earlier init left in
# the source tree. Sets INIT_MIGRATED_FILES to a description of what was removed.
init_migrate_generated_config() {
    _imgc_root="$1"
    _imgc_dir="$_imgc_root/.hydra"
    INIT_MIGRATED_FILES=""
    [ -d "$_imgc_dir" ] && [ ! -L "$_imgc_dir" ] || return 0
    _imgc_config="$_imgc_dir/config.yml"
    if [ -f "$_imgc_config" ] && [ ! -L "$_imgc_config" ] &&
       init_config_is_generated_stub "$_imgc_config" &&
       ! init_path_is_tracked "$_imgc_root" .hydra/config.yml; then
        rm -f "$_imgc_config" || return 1
        INIT_MIGRATED_FILES=".hydra/config.yml"
    fi
    _imgc_local="$_imgc_dir/local.yml"
    if [ -f "$_imgc_local" ] && [ ! -L "$_imgc_local" ] &&
       init_local_is_generated "$_imgc_local" &&
       ! init_path_is_tracked "$_imgc_root" .hydra/local.yml; then
        rm -f "$_imgc_local" || return 1
        INIT_MIGRATED_FILES="${INIT_MIGRATED_FILES:+$INIT_MIGRATED_FILES and }.hydra/local.yml"
    fi
    # The exclude rule only ever covered the generated local.yml.
    [ -e "$_imgc_local" ] || init_remove_exclude_rule || return 1
    rmdir "$_imgc_dir" 2>/dev/null || true
}

init_remove_exclude_rule() {
    _irer_common="$(hydra_git_common_dir)" || return 1
    _irer_file="$_irer_common/info/exclude"
    [ -f "$_irer_file" ] || return 0
    grep -Fqx '.hydra/local.yml' "$_irer_file" 2>/dev/null || return 0
    _irer_tmp="$(mktemp_adjacent "$_irer_file")" || return 1
    grep -Fvx '.hydra/local.yml' "$_irer_file" > "$_irer_tmp" || [ $? -eq 1 ] || { rm -f "$_irer_tmp"; return 1; }
    chmod 644 "$_irer_tmp" 2>/dev/null || true
    atomic_replace "$_irer_file" "$_irer_tmp" || { rm -f "$_irer_tmp"; return 1; }
}

init_shared_config_template() {
    cat <<'TEMPLATE'
# Hydra shared repository configuration. Commit this file.
# Hydra runs nothing from it until each host approves its exact content with:
#   hydra init --trust
version: 1
# Commands run inside every new worktree before its session starts.
setup:
#   - npm install
# Windows, panes, and startup commands are documented under "YAML Config" in
# docs/USAGE.md.
TEMPLATE
}

# Usage: init_write_shared_config <root> <force> <json>
init_write_shared_config() {
    _iwsc_root="$1"
    _iwsc_force="$2"
    _iwsc_json="$3"
    _iwsc_dir="$_iwsc_root/.hydra"
    _iwsc_config="$_iwsc_dir/config.yml"
    if [ -e "$_iwsc_config" ] && [ "$_iwsc_force" -ne 1 ]; then
        cli_error init config_exists ".hydra/config.yml already exists" "review it, or rerun with --force to replace it"
        return 1
    fi
    if [ "$_iwsc_json" -ne 1 ]; then
        echo "Writing .hydra/config.yml (shared repository configuration; commit it):"
        init_shared_config_template | sed 's/^/  | /'
    fi
    mkdir -p "$_iwsc_dir" || return 1
    _iwsc_tmp="$(mktemp "$_iwsc_dir/.config.XXXXXX")" || return 1
    init_shared_config_template > "$_iwsc_tmp" || { rm -f "$_iwsc_tmp"; return 1; }
    chmod 644 "$_iwsc_tmp" 2>/dev/null || true
    mv "$_iwsc_tmp" "$_iwsc_config" || { rm -f "$_iwsc_tmp"; return 1; }
}

cmd_agent() {
    _ca_action="${1:-list}"
    [ $# -eq 0 ] || shift
    case "$_ca_action" in
        contract|probe|import)
            _load_lib cmd_fleet
            cmd_fleet_dispatch agent-profile "$_ca_action" "$@"
            ;;
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
        *) echo "Usage: hydra agent <list|show|doctor|init|contract|probe|import>" >&2; return 1 ;;
    esac
}

cmd_capabilities() {
    if [ "${1:-}" != "--json" ] || [ $# -ne 1 ]; then
        cli_error capabilities invalid_input "--json is required and must be the only argument" "run hydra capabilities --json"
        return 1
    fi
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
