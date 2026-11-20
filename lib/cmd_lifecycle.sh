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
    # Without --worktree-root, reopening keeps the root this host already uses.
    if [ -z "$_ci_worktree_root" ]; then
        _ci_worktree_root="$(project_host_value worktree-root 2>/dev/null || true)"
        # Hydra 2.5 and earlier kept this host's worktree root in
        # .hydra/local.yml, and releases up to 4.0.0 took it from there.
        # Nothing reads that file now. Where a root is recorded or given the
        # file changes nothing and is ignored; where none is, init stops
        # before it registers anything, instead of choosing a root other than
        # the one the file names.
        if [ -z "$_ci_worktree_root" ] &&
            { [ -e "$_ci_root/.hydra/local.yml" ] || [ -L "$_ci_root/.hydra/local.yml" ]; }; then
            cli_error init unsupported_configuration ".hydra/local.yml is a host-local record from Hydra 2.5 or earlier, which this release does not read" "run 'hydra init' with Hydra 4.0.0 first, which imports its worktree root, or delete the file"
            return 1
        fi
    fi
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

    project_activate_state_v2 "$_ci_project" || return 1
    project_write_host_value default-profile "$_ci_profile" || return 1
    project_write_host_value worktree-root "$_ci_worktree_root" || return 1

    # Registration is complete and lives under the Git common directory. Nothing
    # below writes into the source tree unless the user asks for shared config.
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
        json_success init "{\"project_id\":\"$(json_escape "$_ci_project")\",\"profile\":\"$(json_escape "$_ci_profile")\",\"worktree_root\":\"$(json_escape "$_ci_worktree_root")\",\"trusted\":$_ci_trusted}"
        return 0
    fi
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
        locate) cmd_agent_locate "$@" ;;
        *) echo "Usage: hydra agent <list|show|doctor|init|locate|contract|probe|import>" >&2; return 1 ;;
    esac
}

# Built-in agents as profile:executable, in inventory order.
agent_locate_agents() {
    printf '%s\n' claude:claude codex:codex cursor:cursor-agent agy:agy opencode:opencode \
        pi:pi copilot:copilot aider:aider gemini:gemini
}

# Directories searched beyond PATH, one per line; a leading ~/ means $HOME.
# ~/.pi/agent/bin is where the pi installer puts pi when no user bin directory
# is on PATH. Login shells are never sourced. Keep this list identical to
# setup_agent_search_dirs in src/fleet/setup/preflight.c (the remote setup
# preflight scan); tests/test_remote_setup.sh compares the two.
agent_locate_search_dirs() {
    # shellcheck disable=SC2088
    printf '%s\n' '~/.local/bin' '~/bin' '~/.claude/local' '~/.opencode/bin' '~/.pi/agent/bin' \
        '~/.npm-global/bin' '~/.bun/bin' '~/.volta/bin' '~/.local/share/pnpm' '~/.cargo/bin' \
        '~/.local/share/mise/shims' '~/.asdf/shims' '~/.nvm/versions/node/*/bin' \
        /usr/local/bin /opt/homebrew/bin /home/linuxbrew/.linuxbrew/bin /snap/bin
}

# Prints each existing search directory, expanding ~/ and the nvm version glob.
agent_locate_expand_dirs() {
    agent_locate_search_dirs | while IFS= read -r _aled_dir; do
        # shellcheck disable=SC2088 # a literal ~/ prefix, expanded below
        case "$_aled_dir" in
            '~/'*)
                case "${HOME:-}" in /*) ;; *) continue ;; esac
                _aled_dir="$HOME/${_aled_dir#\~/}"
                ;;
        esac
        case "$_aled_dir" in
            *'/*/'*)
                for _aled_match in "${_aled_dir%%/\**}"/*/"${_aled_dir#*/\*/}"; do
                    [ -d "$_aled_match" ] && printf '%s\n' "$_aled_match"
                done
                ;;
            *) [ -d "$_aled_dir" ] && printf '%s\n' "$_aled_dir" ;;
        esac
    done
}

# First line of `PATH --version`, bounded to five seconds and printable ASCII.
agent_locate_version() {
    _alv_out="$(mktemp "${TMPDIR:-/tmp}/hydra-agent-version.XXXXXX")" || return 0
    "$1" --version < /dev/null > "$_alv_out" 2>/dev/null &
    _alv_pid=$!
    (sleep 5; kill "$_alv_pid" 2>/dev/null) > /dev/null 2>&1 &
    _alv_watch=$!
    if wait "$_alv_pid" 2>/dev/null; then
        sed -n '1p' "$_alv_out" | LC_ALL=C tr -cd ' -~' | cut -c1-200
    fi
    kill "$_alv_watch" 2>/dev/null || :
    wait "$_alv_watch" 2>/dev/null || :
    rm -f "$_alv_out"
}

# Usage: agent_locate_candidates <executable>
# Prints source<TAB>path lines: the PATH hit, the record, then search hits.
agent_locate_candidates() {
    _alc_hit="$(agent_location_on_path "$1" || true)"
    [ -z "$_alc_hit" ] || printf 'path\t%s\n' "$_alc_hit"
    _alc_hit="$(agent_location_recorded "$1" 2>/dev/null || true)"
    [ -z "$_alc_hit" ] || printf 'record\t%s\n' "$_alc_hit"
    agent_locate_expand_dirs | while IFS= read -r _alc_dir; do
        printf 'search\t%s/%s\n' "$_alc_dir" "$1"
    done
}

# Usage: agent_locate_scan <executable>
# Sets _alsc_on_path, _alsc_recorded, _alsc_status, _alsc_first (the first
# search hit) and _alsc_candidates (the body of a JSON array).
agent_locate_scan() {
    _alsc_exe="$1" _alsc_on_path="" _alsc_recorded="" _alsc_first="" _alsc_candidates=""
    _alsc_found=0 _alsc_seen="
"
    _alsc_tab="$(printf '\t')"
    while IFS="$_alsc_tab" read -r _alsc_source _alsc_path; do
        [ -n "$_alsc_path" ] || continue
        case "$_alsc_seen" in *"
$_alsc_path
"*) continue ;; esac
        if [ ! -f "$_alsc_path" ] || [ ! -x "$_alsc_path" ]; then continue; fi
        _alsc_seen="$_alsc_seen$_alsc_path
"
        case "$_alsc_source" in
            path) _alsc_on_path="$_alsc_path" ;;
            record) _alsc_recorded="$_alsc_path" ;;
            *) _alsc_found=$((_alsc_found + 1)); [ -n "$_alsc_first" ] || _alsc_first="$_alsc_path" ;;
        esac
        _alsc_version="" _alsc_recordable=false
        # Only entries that could be recorded are executed, and only for --version.
        if agent_location_check "$_alsc_exe" "$_alsc_path" > /dev/null; then
            _alsc_recordable=true
            _alsc_version="$(agent_locate_version "$_alsc_path")"
        fi
        [ -z "$_alsc_candidates" ] || _alsc_candidates="$_alsc_candidates,"
        _alsc_candidates="$_alsc_candidates{\"path\":\"$(json_escape "$_alsc_path")\",\"source\":\"$_alsc_source\",\"version\":$(json_string_or_null "$_alsc_version"),\"recordable\":$_alsc_recordable}"
    done <<EOF
$(agent_locate_candidates "$_alsc_exe")
EOF
    if [ -n "$_alsc_on_path" ]; then _alsc_status=on_path
    elif [ -n "$_alsc_recorded" ]; then _alsc_status=recorded
    elif [ "$_alsc_found" -eq 1 ]; then _alsc_status=found_off_path
    elif [ "$_alsc_found" -gt 1 ]; then _alsc_status=ambiguous
    else _alsc_status=missing
    fi
}

# Usage: agent_locate_row <profile> <executable>
# Prints one agent-inventory row as JSON.
agent_locate_row() {
    agent_locate_scan "$2"
    printf '{"profile":"%s","executable":"%s","on_path":%s,"candidates":[%s],"recorded":%s,"status":"%s"}' \
        "$1" "$2" "$(json_string_or_null "$_alsc_on_path")" "$_alsc_candidates" \
        "$(json_string_or_null "$_alsc_recorded")" "$_alsc_status"
}

agent_locate_inventory_json() {
    _alij_dirs=""
    while IFS= read -r _alij_dir; do
        [ -z "$_alij_dirs" ] || _alij_dirs="$_alij_dirs,"
        _alij_dirs="$_alij_dirs\"$(json_escape "$_alij_dir")\""
    done <<EOF
$(agent_locate_search_dirs)
EOF
    _alij_rows=""
    for _alij_pair in $(agent_locate_agents); do
        [ -z "$_alij_rows" ] || _alij_rows="$_alij_rows,"
        _alij_rows="$_alij_rows$(agent_locate_row "${_alij_pair%%:*}" "${_alij_pair#*:}")"
    done
    printf '{"schema":"agent-inventory","schema_version":1,"home":%s,"path":%s,"search_dirs":[%s],"agents":[%s]}' \
        "$(json_string_or_null "${HOME:-}")" "$(json_string_or_null "${PATH:-}")" "$_alij_dirs" "$_alij_rows"
}

# Usage: hydra agent locate [--json] [--record EXECUTABLE PATH] [--forget EXECUTABLE]
cmd_agent_locate() {
    _cal_json=0 _cal_action=inventory _cal_exe="" _cal_path=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --json|-j) _cal_json=1; shift ;;
            --record) [ $# -ge 3 ] || { _cal_action=usage; break; }; _cal_action=record; _cal_exe="$2"; _cal_path="$3"; shift 3 ;;
            --forget) [ $# -ge 2 ] || { _cal_action=usage; break; }; _cal_action=forget; _cal_exe="$2"; shift 2 ;;
            *) _cal_action=usage; break ;;
        esac
    done
    HYDRA_JSON_REQUESTED="$_cal_json"
    export HYDRA_JSON_REQUESTED
    case "$_cal_action" in
        usage)
            cli_error agent-locate invalid_input "unknown or incomplete agent locate option" \
                "use hydra agent locate [--json] [--record EXECUTABLE /absolute/path] [--forget EXECUTABLE]"
            return 1
            ;;
        record) agent_locate_record "$_cal_exe" "$_cal_path" "$_cal_json" ;;
        forget)
            agent_location_name_valid "$_cal_exe" || {
                cli_error agent-locate invalid_input "invalid executable name '$_cal_exe'" "use the executable's file name, such as claude or cursor-agent"
                return 1
            }
            agent_location_forget "$_cal_exe" || { cli_error agent-locate io_failed "cannot remove the record" "inspect $(agent_location_dir)"; return 1; }
            if [ "$_cal_json" -eq 1 ]; then
                json_success agent-locate "{\"executable\":\"$_cal_exe\",\"recorded\":null}"
            else
                echo "Forgot any recorded location for $_cal_exe"
            fi
            ;;
        *) agent_locate_show "$_cal_json" ;;
    esac
}

agent_locate_record() {
    agent_location_name_valid "$1" || {
        cli_error agent-locate invalid_input "invalid executable name '$1'" "use the executable's file name, such as claude or cursor-agent"
        return 1
    }
    _alrc_reason="$(agent_location_record "$1" "$2")" || {
        cli_error agent-locate location_invalid "cannot record $2 for $1: $_alrc_reason" \
            "choose an absolute path ending in /$1 that you or root own and that is not group- or world-writable (chmod go-w)"
        return 1
    }
    if [ "$3" -eq 1 ]; then
        json_success agent-locate "{\"executable\":\"$1\",\"recorded\":\"$(json_escape "$2")\"}"
    else
        echo "Recorded $1 at $2 (used when $1 is not on PATH)"
    fi
}

agent_locate_show() {
    _als_json="$(agent_locate_inventory_json)" || return 1
    if [ "$1" -eq 1 ]; then
        json_success agent-locate "$_als_json"
        return 0
    fi
    printf '%-10s %-14s %-15s %s\n' PROFILE EXECUTABLE STATUS LOCATION
    for _als_pair in $(agent_locate_agents); do
        _als_exe="${_als_pair#*:}"
        agent_locate_scan "$_als_exe"
        case "$_alsc_status" in
            on_path) _als_where="$_alsc_on_path" ;;
            recorded) _als_where="$_alsc_recorded" ;;
            found_off_path) _als_where="$_alsc_first (record: hydra agent locate --record $_als_exe $_alsc_first)" ;;
            ambiguous) _als_where="several candidates (first $_alsc_first); record one explicitly" ;;
            *) _als_where="-" ;;
        esac
        printf '%-10s %-14s %-15s %s\n' "${_als_pair%%:*}" "$_als_exe" "$_alsc_status" "$_als_where"
    done
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
