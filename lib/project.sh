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
    state_v2_write_scalar "$_pwhv_dir/$_pwhv_name" "$_pwhv_value"
}

project_repo_config() {
    _prc_root="$(git rev-parse --show-toplevel 2>/dev/null)" || return 1
    printf '%s/.hydra/config.yml\n' "$_prc_root"
}

project_config_hash() {
    _pch_config="$(project_repo_config)" || return 1
    [ -f "$_pch_config" ] || { printf '%s\n' none; return 0; }
    hydra_hash < "$_pch_config"
}

project_is_trusted() {
    _pit_recorded="$(project_host_value trusted-config-hash 2>/dev/null || true)"
    [ -n "$_pit_recorded" ] || return 1
    _pit_current="$(project_config_hash)" || return 1
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
    hydra_valid_id "$_pwp_project" && hydra_valid_id "$_pwp_head" || return 1
    _pwp_root="$(project_worktree_root "$_pwp_project")" || return 1
    printf '%s/%s\n' "$_pwp_root" "$_pwp_head"
}

project_activate_state_v2() {
    _pas_project="$1"
    _pas_root="$(git rev-parse --show-toplevel 2>/dev/null)" || return 1
    _pas_legacy_map="${HYDRA_LEGACY_MAP:-${HYDRA_MAP:-$HYDRA_HOME/map}}"
    if [ -s "$_pas_legacy_map" ] && \
       [ "$(sed -n '1p' "$HYDRA_HOME/state/active-schema" 2>/dev/null || true)" != "2" ]; then
        echo "Error: legacy state must be migrated before project initialization" >&2
        echo "Next: hydra state migrate --dry-run && hydra state migrate" >&2
        return 1
    fi
    state_v2_init_project "$_pas_project" "$_pas_root" || return 1
    _pas_project_dir="$(state_v2_project_dir "$_pas_project")" || return 1
    [ -f "$_pas_project_dir/compat-map" ] || : > "$_pas_project_dir/compat-map"
    chmod 600 "$_pas_project_dir/compat-map" 2>/dev/null || true
    state_v2_write_scalar "$HYDRA_HOME/state/active-schema" "2" || return 1
    HYDRA_MAP="$_pas_project_dir/compat-map"
    export HYDRA_MAP
}
