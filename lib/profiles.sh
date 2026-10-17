#!/bin/sh
# Fixed built-in and explicit custom agent launch profiles.

profile_validate_name() {
    case "$1" in ''|*[!a-z0-9_-]*) return 1 ;; esac
}

profile_builtin_exists() {
    case "$1" in none|claude|codex|cursor|agy|opencode|copilot|aider|gemini) return 0 ;; esac
    return 1
}

profile_custom_dir() {
    profile_validate_name "$1" || return 1
    printf '%s/profiles/%s\n' "$HYDRA_HOME" "$1"
}

profile_exists() {
    [ ! -f "$HYDRA_HOME/profiles/$1/adapter.json" ] || return 1
    profile_builtin_exists "$1" && return 0
    _pe_dir="$(profile_custom_dir "$1")" || return 1
    [ -f "$_pe_dir/executable" ]
}

profile_field() {
    _pf_name="$1"
    _pf_field="$2"
    _pf_dir="$(profile_custom_dir "$_pf_name")" || return 1
    [ ! -f "$_pf_dir/adapter.json" ] || return 1
    # Keep previously registered custom profiles when a new builtin is introduced.
    if [ -f "$_pf_dir/executable" ]; then
        [ -f "$_pf_dir/$_pf_field" ] || return 1
        sed -n '1p' "$_pf_dir/$_pf_field"
        return 0
    fi
    if profile_builtin_exists "$_pf_name"; then
        case "$_pf_field" in
            executable)
                case "$_pf_name" in cursor) printf 'cursor-agent\n' ;; *) printf '%s\n' "$_pf_name" ;; esac
                ;;
            tier)
                if [ "$_pf_name" = none ]; then printf '0\n'; else printf '1\n'; fi
                ;;
            prompt_mode)
                case "$_pf_name" in claude|codex|agy|cursor|opencode) printf 'task-file\n' ;; *) printf 'none\n' ;; esac
                ;;
            resume_mode)
                case "$_pf_name" in claude) printf 'session-id\n' ;; codex) printf 'cwd-last\n' ;; *) printf 'none\n' ;; esac
                ;;
            adapter) printf 'none\n' ;;
            confidence)
                case "$_pf_name" in claude|codex|agy|cursor|opencode) printf 'verified-local-help\n' ;; none) printf 'exact\n' ;; *) printf 'launch-only\n' ;; esac
                ;;
            environment) printf 'TERM,COLORTERM\n' ;;
            *) return 1 ;;
        esac
        return 0
    fi
    return 1
}

profile_list() {
    for _pl_name in none claude codex cursor agy opencode copilot aider gemini; do
        [ ! -f "$HYDRA_HOME/profiles/$_pl_name/adapter.json" ] || continue
        printf '%s\n' "$_pl_name"
    done
    if [ -d "$HYDRA_HOME/profiles" ]; then
        for _pl_dir in "$HYDRA_HOME"/profiles/*; do
            [ -d "$_pl_dir" ] || continue
            [ -f "$_pl_dir/executable" ] || continue
            [ ! -f "$_pl_dir/adapter.json" ] || continue
            _pl_name="$(basename "$_pl_dir")"
            profile_builtin_exists "$_pl_name" || printf '%s\n' "$_pl_name"
        done
    fi
}

# PATH first, then a valid `hydra agent locate --record` location.
profile_executable_path() {
    _pep_executable="$(profile_field "$1" executable)" || return 1
    [ "$_pep_executable" != none ] || { printf '%s\n' none; return 0; }
    case "$_pep_executable" in
        /*) [ -x "$_pep_executable" ] || return 1; printf '%s\n' "$_pep_executable" ;;
        *) agent_location_resolve "$_pep_executable" ;;
    esac
}

profile_resolve() {
    _pr_requested="${1:-}"
    if [ -n "$_pr_requested" ]; then
        if [ -f "$HYDRA_HOME/profiles/$_pr_requested/adapter.json" ]; then
            echo "Error: '$_pr_requested' is a headless profile; use hydra exec --profile $_pr_requested --prompt-file <file>" >&2
            return 1
        fi
        profile_exists "$_pr_requested" || {
            echo "Error: unknown agent profile '$_pr_requested'" >&2
            return 1
        }
        if [ "$_pr_requested" != none ] && ! profile_executable_path "$_pr_requested" >/dev/null; then
            echo "Error: agent profile '$_pr_requested' is not available on PATH" >&2
            echo "Next: install it, choose another --profile, or use --no-agent" >&2
            return 1
        fi
        printf '%s\n' "$_pr_requested"
        return 0
    fi

    _pr_configured="$(project_host_value default-profile 2>/dev/null || true)"
    if [ -n "$_pr_configured" ]; then
        profile_resolve "$_pr_configured"
        return $?
    fi

    _pr_found=""
    _pr_count=0
    for _pr_name in claude codex cursor agy opencode copilot aider gemini; do
        if profile_executable_path "$_pr_name" >/dev/null 2>&1; then
            _pr_found="$_pr_name"
            _pr_count=$((_pr_count + 1))
        fi
    done
    case "$_pr_count" in
        0) printf 'none\n' ;;
        1) printf '%s\n' "$_pr_found" ;;
        *)
            echo "Error: multiple agent profiles are available; selection must be explicit" >&2
            echo "Next: pass --profile <name>, run hydra init --profile <name>, or use --no-agent" >&2
            return 1
            ;;
    esac
}

profile_new_provider_id() {
    _pnpi_hash="$(printf '%s|%s|%s\n' "$1" "$2" "$(date +%s)-$$" | hydra_hash)" || return 1
    printf '%s-%s-4%s-8%s-%s\n' \
        "$(printf '%.8s' "$_pnpi_hash")" \
        "$(printf '%s' "$_pnpi_hash" | cut -c9-12)" \
        "$(printf '%s' "$_pnpi_hash" | cut -c14-16)" \
        "$(printf '%s' "$_pnpi_hash" | cut -c18-20)" \
        "$(printf '%s' "$_pnpi_hash" | cut -c21-32)"
}

profile_shell_quote() {
    printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

# Usage: profile_launch_command <profile> [task_file] [provider_id] [executable]
# An explicit executable (normally the absolute path from
# profile_executable_path) replaces the profile's declared command name so the
# recipe runs exactly what `hydra agent list` reported, whatever PATH the
# launching shell ends up with.
profile_launch_command() {
    _plc_name="$1"
    _plc_task_file="${2:-}"
    _plc_provider_id="${3:-}"
    _plc_executable="$(profile_field "$_plc_name" executable)" || return 1
    [ "$_plc_name" != none ] || return 0
    [ -z "${4:-}" ] || _plc_executable="$4"
    _plc_command="$(profile_shell_quote "$_plc_executable")"
    if [ "$_plc_name" = claude ] && [ -n "$_plc_provider_id" ]; then
        _plc_command="$_plc_command --session-id $(profile_shell_quote "$_plc_provider_id")"
    fi
    if [ -n "$_plc_task_file" ]; then
        [ "$(profile_field "$_plc_name" prompt_mode)" = task-file ] || {
            echo "Error: profile '$_plc_name' does not support safe task injection" >&2
            return 1
        }
        _plc_prompt_option=""
        if [ ! -f "$HYDRA_HOME/profiles/$_plc_name/executable" ]; then
            case "$_plc_name" in
                agy) _plc_prompt_option="--prompt-interactive=" ;;
                opencode) _plc_prompt_option="--prompt=" ;;
                cursor) _plc_prompt_option="-- " ;;
            esac
        fi
        _plc_command="$_plc_command $_plc_prompt_option\"\$(cat $(profile_shell_quote "$_plc_task_file"))\""
    fi
    printf '%s\n' "$_plc_command"
}

# Usage: profile_resume_command <profile> [provider_id] [executable]
profile_resume_command() {
    _prc_name="$1"
    _prc_provider_id="${2:-}"
    _prc_executable="${3:-}"
    [ -n "$_prc_executable" ] || _prc_executable="$(profile_field "$_prc_name" executable)" || return 1
    case "$(profile_field "$_prc_name" resume_mode)" in
        session-id)
            [ -n "$_prc_provider_id" ] || return 1
            printf '%s --resume %s\n' "$(profile_shell_quote "$_prc_executable")" \
                "$(profile_shell_quote "$_prc_provider_id")"
            ;;
        cwd-last) printf '%s resume --last\n' "$(profile_shell_quote "$_prc_executable")" ;;
        *) return 1 ;;
    esac
}

profile_create_custom() {
    _pcc_name="$1"
    _pcc_executable="$2"
    _pcc_prompt_mode="${3:-none}"
    profile_validate_name "$_pcc_name" || return 1
    profile_builtin_exists "$_pcc_name" && return 1
    case "$_pcc_executable" in /*) ;; *) return 1 ;; esac
    [ -x "$_pcc_executable" ] || return 1
    case "$_pcc_prompt_mode" in none|task-file) ;; *) return 1 ;; esac
    _pcc_dir="$(profile_custom_dir "$_pcc_name")" || return 1
    [ ! -e "$_pcc_dir/adapter.json" ] || return 1
    hydra_private_mkdir "$_pcc_dir" || return 1
    chmod 700 "$_pcc_dir" 2>/dev/null || true
    state_v2_write_scalar "$_pcc_dir/executable" "$_pcc_executable" || return 1
    state_v2_write_scalar "$_pcc_dir/tier" "1" || return 1
    state_v2_write_scalar "$_pcc_dir/prompt_mode" "$_pcc_prompt_mode" || return 1
    state_v2_write_scalar "$_pcc_dir/resume_mode" "none" || return 1
    state_v2_write_scalar "$_pcc_dir/adapter" "none" || return 1
    state_v2_write_scalar "$_pcc_dir/confidence" "user-declared" || return 1
    state_v2_write_scalar "$_pcc_dir/environment" "TERM,COLORTERM" || return 1
}

# ---------------------------------------------------------------------------
# Recorded agent locations (hydra agent locate).
# A record maps an executable name (never a profile) to one absolute path that
# PATH lookup does not find, such as an installer's ~/.local/bin. PATH always
# wins, so a record never shadows an installed executable, and records cannot
# create profiles: custom profiles still cannot reuse built-in names.
# src/fleet/agent/agent_probe.c applies the same rules to headless runs;
# tests/test_agent_locate.sh checks that both resolvers agree.
# ---------------------------------------------------------------------------

agent_location_name_valid() {
    case "$1" in ''|.*|-*|*[!A-Za-z0-9._-]*) return 1 ;; esac
    [ "${#1}" -le 64 ]
}

agent_location_dir() {
    printf '%s/agents/locations\n' "$HYDRA_HOME"
}

# Usage: agent_location_owner_ok <owner|private|shared> <ls -lnd line>
# owner: the current user or root owns the entry. shared: also neither group-
# nor world-writable. private: the current user owns it, same write rule.
agent_location_owner_ok() {
    _aloo_mode="" _aloo_uid=""
    read -r _aloo_mode _aloo_links _aloo_uid _aloo_rest <<EOF
$2
EOF
    [ -n "$_aloo_uid" ] || return 1
    [ "$_aloo_uid" = "$(id -u)" ] || { [ "$1" != private ] && [ "$_aloo_uid" = 0 ]; } || return 1
    [ "$1" != owner ] || return 0
    case "$_aloo_mode" in ?????w*|????????w*) return 1 ;; esac
    return 0
}

# Usage: agent_location_check <executable> <path>
# Prints the reason and fails when path cannot serve as executable's location:
# it must be absolute, end in /executable, name an executable regular file (a
# symlink is followed but the invoked name is kept, as version managers
# dispatch on it), and the entry and its target must belong to the user or
# root, with the target neither group- nor world-writable.
agent_location_check() {
    agent_location_name_valid "$1" || { echo "invalid executable name"; return 1; }
    case "$2" in
        *'
'*) echo "path contains a newline"; return 1 ;;
        /*) ;;
        *) echo "path is not absolute"; return 1 ;;
    esac
    [ "${2##*/}" = "$1" ] || { echo "file name is not $1"; return 1; }
    if [ ! -f "$2" ] || [ ! -x "$2" ]; then
        echo "not an executable file"; return 1
    fi
    if ! agent_location_owner_ok owner "$(LC_ALL=C ls -lnd -- "$2" 2>/dev/null)" ||
        ! agent_location_owner_ok shared "$(LC_ALL=C ls -lndL -- "$2" 2>/dev/null)"; then
        echo "owned by another user, or group/world-writable"; return 1
    fi
}

# Usage: agent_location_recorded <executable>
# Prints the recorded path when the private record and its target are valid.
agent_location_recorded() {
    agent_location_name_valid "$1" || return 1
    _alr_file="$(agent_location_dir)/$1"
    [ -f "$_alr_file" ] && [ ! -h "$_alr_file" ] || return 1
    agent_location_owner_ok private "$(LC_ALL=C ls -lnd -- "$_alr_file" 2>/dev/null)" || return 1
    _alr_path="$(sed -n '1p' "$_alr_file")" || return 1
    agent_location_check "$1" "$_alr_path" >/dev/null || return 1
    printf '%s\n' "$_alr_path"
}

# Usage: agent_location_on_path <executable>
# First executable regular file named executable in PATH, as an absolute
# path. Walks PATH itself: `command -v` in dash can report a non-executable
# entry and hide a later executable one.
agent_location_on_path() {
    _alop_rest="${PATH:-}:"
    while [ -n "$_alop_rest" ]; do
        _alop_dir="${_alop_rest%%:*}"
        _alop_rest="${_alop_rest#*:}"
        [ -n "$_alop_dir" ] || _alop_dir=.
        if [ -f "$_alop_dir/$1" ] && [ -x "$_alop_dir/$1" ]; then
            case "$_alop_dir" in
                /*) ;;
                *) _alop_dir="$(cd "$_alop_dir" 2>/dev/null && pwd -P)" || continue ;;
            esac
            printf '%s/%s\n' "$_alop_dir" "$1"
            return 0
        fi
    done
    return 1
}

# Usage: agent_location_resolve <executable>
# PATH first, then a valid record.
agent_location_resolve() {
    agent_location_on_path "$1" && return 0
    agent_location_recorded "$1"
}

# Usage: agent_location_record <executable> <path>
# Writes the scalar record 0600 in a 0700 directory whatever the umask; prints
# the reason on failure. Needs locks.sh (hydra_private_mkdir).
agent_location_record() {
    _alw_reason="$(agent_location_check "$1" "$2")" || { printf '%s\n' "$_alw_reason"; return 1; }
    _alw_dir="$(agent_location_dir)"
    if [ -h "$HYDRA_HOME/agents" ] || [ -h "$_alw_dir" ]; then
        echo "the location directory is a symlink"; return 1
    fi
    if ! hydra_private_mkdir "$_alw_dir" || ! chmod 700 "$HYDRA_HOME/agents" "$_alw_dir"; then
        echo "cannot create $_alw_dir"; return 1
    fi
    _alw_tmp="$(umask 077; mktemp "$_alw_dir/.$1.XXXXXX")" || { echo "cannot write the record"; return 1; }
    if ! printf '%s\n' "$2" > "$_alw_tmp" || ! chmod 600 "$_alw_tmp" || ! mv -f "$_alw_tmp" "$_alw_dir/$1"; then
        rm -f "$_alw_tmp"; echo "cannot write the record"; return 1
    fi
}

# Usage: agent_location_forget <executable>
agent_location_forget() {
    agent_location_name_valid "$1" || return 1
    rm -f "$(agent_location_dir)/$1"
}
