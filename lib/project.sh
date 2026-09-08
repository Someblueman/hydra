#!/bin/sh
# Project initialization, trust, and host-local configuration.

project_host_dir() {
    _phd_common="$(hydra_git_common_dir)" || return 1
    printf '%s/hydra\n' "$_phd_common"
}

project_host_value() {
    _phv_name="$1"
    _phv_dir="$(project_host_dir)" || return 1
    sed -n '1p' "$_phv_dir/$_phv_name" 2>/dev/null
}

project_write_host_value() {
    _pwhv_name="$1"
    _pwhv_value="$2"
    case "$_pwhv_name" in *[!a-z0-9-]*|'') return 1 ;; esac
    _pwhv_dir="$(project_host_dir)" || return 1
    mkdir -p "$_pwhv_dir" || return 1
    chmod 700 "$_pwhv_dir" 2>/dev/null || true
    _pwhv_lock="project_config_$(printf '%s' "$_pwhv_dir" | cksum | cut -d' ' -f1)"
    acquire_lock "$_pwhv_lock" "project host configuration" || return 1
    if ! state_v2_write_scalar "$_pwhv_dir/$_pwhv_name" "$_pwhv_value"; then
        release_lock "$_pwhv_lock"
        return 1
    fi
    release_lock "$_pwhv_lock"
}

project_repo_config() {
    _prc_root="${1:-}"
    if [ -z "$_prc_root" ]; then
        _prc_root="$(git rev-parse --show-toplevel 2>/dev/null)" || return 1
    fi
    [ -d "$_prc_root" ] || return 1
    printf '%s/.hydra/config.yml\n' "$_prc_root"
}

project_config_hash() (
    _pch_root="${1:-}"
    _pch_config="$(project_repo_config "$_pch_root")" || return 1
    _pch_dir="$(dirname "$_pch_config")"
    [ ! -L "$_pch_dir" ] || return 1
    [ -d "$_pch_dir" ] || { printf '%s\n' none; return 0; }
    _pch_scratch="$(mktemp -d)" || return 1
    trap 'rm -rf "$_pch_scratch"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    # Validate names before newline serialization. Do not follow links or ignore
    # nested local.yml files: only the root host-local record is exempt.
    find "$_pch_dir" -exec sh -c '
        excluded="$1/local.yml"; shift
        cr="$(printf "\r")"
        for path do
            [ "$path" = "$excluded" ] && continue
            case "$path" in *"
"*|*"$cr"*) exit 1 ;; esac
            [ ! -L "$path" ] || exit 1
            if [ -f "$path" ]; then printf "%s\n" "$path"
            elif [ ! -d "$path" ]; then exit 1
            fi
        done
    ' sh "$_pch_dir" {} + > "$_pch_scratch/files" || return 1
    LC_ALL=C sort "$_pch_scratch/files" > "$_pch_scratch/sorted" || return 1
    while IFS= read -r _pch_file; do
        _pch_relative="${_pch_file#"$_pch_dir"/}"
        _pch_hash="$(hydra_hash < "$_pch_file")" || return 1
        printf '%s %s\n' "$_pch_relative" "$_pch_hash"
    done < "$_pch_scratch/sorted" > "$_pch_scratch/manifest" || return 1
    if [ -s "$_pch_scratch/manifest" ]; then
        hydra_hash < "$_pch_scratch/manifest"
    else
        printf '%s\n' none
    fi
)

project_is_trusted() {
    _pit_root="${1:-}"
    _pit_recorded="$(project_host_value trusted-config-hash 2>/dev/null || true)"
    [ -n "$_pit_recorded" ] || return 1
    _pit_current="$(project_config_hash "$_pit_root")" || return 1
    [ "$_pit_recorded" = "$_pit_current" ]
}

project_set_trusted() {
    _pst_hash="$(project_config_hash)" || return 1
    project_write_host_value trusted-config-hash "$_pst_hash"
}

project_default_worktree_root() {
    _pdwr_root="$(git rev-parse --show-toplevel 2>/dev/null)" || return 1
    _pdwr_project="$1"
    printf '%s/.hydra-worktrees/%s\n' "$(dirname "$_pdwr_root")" "$_pdwr_project"
}

project_worktree_root() {
    _pwr_project="$1"
    _pwr_stored="$(project_host_value worktree-root 2>/dev/null || true)"
    if [ -n "$_pwr_stored" ]; then
        printf '%s\n' "$_pwr_stored"
    else
        project_default_worktree_root "$_pwr_project"
    fi
}

project_worktree_path() {
    _pwp_project="$1"
    _pwp_head="$2"
    if ! hydra_valid_id "$_pwp_project" || ! hydra_valid_id "$_pwp_head"; then
        return 1
    fi
    _pwp_root="$(project_worktree_root "$_pwp_project")" || return 1
    printf '%s/%s\n' "$_pwp_root" "$_pwp_head"
}

project_activate_state_v2() {
    _pas_project="$1"
    _pas_root="$(git rev-parse --show-toplevel 2>/dev/null)" || return 1
    state_v2_init_project "$_pas_project" "$_pas_root"
}
