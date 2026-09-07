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

profile_executable_path() {
    _pep_executable="$(profile_field "$1" executable)" || return 1
    [ "$_pep_executable" != none ] || { printf '%s\n' none; return 0; }
    case "$_pep_executable" in
        /*) [ -x "$_pep_executable" ] || return 1; printf '%s\n' "$_pep_executable" ;;
        *) command -v "$_pep_executable" 2>/dev/null ;;
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

profile_launch_command() {
    _plc_name="$1"
    _plc_task_file="${2:-}"
    _plc_provider_id="${3:-}"
    _plc_executable="$(profile_field "$_plc_name" executable)" || return 1
    [ "$_plc_name" != none ] || return 0
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

profile_resume_command() {
    _prc_name="$1"
    _prc_provider_id="${2:-}"
    case "$(profile_field "$_prc_name" resume_mode)" in
        session-id)
            [ -n "$_prc_provider_id" ] || return 1
            printf '%s --resume %s\n' "$(profile_shell_quote "$(profile_field "$_prc_name" executable)")" \
                "$(profile_shell_quote "$_prc_provider_id")"
            ;;
        cwd-last) printf '%s resume --last\n' "$(profile_shell_quote "$(profile_field "$_prc_name" executable)")" ;;
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
    mkdir -p "$_pcc_dir" || return 1
    chmod 700 "$_pcc_dir" 2>/dev/null || true
    state_v2_write_scalar "$_pcc_dir/executable" "$_pcc_executable" || return 1
    state_v2_write_scalar "$_pcc_dir/tier" "1" || return 1
    state_v2_write_scalar "$_pcc_dir/prompt_mode" "$_pcc_prompt_mode" || return 1
    state_v2_write_scalar "$_pcc_dir/resume_mode" "none" || return 1
    state_v2_write_scalar "$_pcc_dir/adapter" "none" || return 1
    state_v2_write_scalar "$_pcc_dir/confidence" "user-declared" || return 1
    state_v2_write_scalar "$_pcc_dir/environment" "TERM,COLORTERM" || return 1
}
