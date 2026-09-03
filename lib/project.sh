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

project_config_hash() {
    _pch_root="${1:-}"
    _pch_config="$(project_repo_config "$_pch_root")" || return 1
    _pch_dir="$(dirname "$_pch_config")"
    [ -d "$_pch_dir" ] || { printf '%s\n' none; return 0; }
    _pch_manifest="$(mktemp)" || return 1
    find "$_pch_dir" -type f ! -name local.yml -print | sort | while IFS= read -r _pch_file; do
        _pch_relative="${_pch_file#"$_pch_dir"/}"
        case "$_pch_relative" in *'
'*|*''*) exit 1 ;; esac
        printf '%s %s\n' "$_pch_relative" "$(hydra_hash < "$_pch_file")"
    done > "$_pch_manifest" || { rm -f "$_pch_manifest"; return 1; }
    if [ -s "$_pch_manifest" ]; then
        hydra_hash < "$_pch_manifest"
    else
        printf '%s\n' none
    fi
    rm -f "$_pch_manifest"
}

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
