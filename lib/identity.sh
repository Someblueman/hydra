#!/bin/sh
# Durable project/head/instance identity helpers for state v2.

hydra_hash() {
    if command -v git >/dev/null 2>&1; then
        git hash-object --stdin
    else
        cksum | awk '{printf "%08x\n", $1}'
    fi
}

hydra_new_id() {
    _hni_kind="$1"
    _hni_seed="${2:-}"
    case "$_hni_kind" in project|head|instance|run|step|evt) ;; *) return 1 ;; esac
    _hni_hash="$(printf '%s\n' "$_hni_kind|$_hni_seed|$(date +%s)|$$" | hydra_hash)" || return 1
    printf '%s_%s\n' "$_hni_kind" "$(printf '%.20s' "$_hni_hash")"
}

hydra_valid_id() {
    case "$1" in
        project_*|head_*|instance_*|run_*|step_*|evt_*) ;;
        *) return 1 ;;
    esac
    _hvi_suffix="${1#*_}"
    case "$_hvi_suffix" in ''|*[!0-9a-f]*) return 1 ;; esac
    return 0
}

hydra_git_common_dir() {
    _hgcd_value="$(git rev-parse --git-common-dir 2>/dev/null)" || return 1
    case "$_hgcd_value" in
        /*) (cd "$_hgcd_value" 2>/dev/null && pwd) ;;
        *)
            _hgcd_root="$(git rev-parse --show-toplevel 2>/dev/null)" || return 1
            (cd "$_hgcd_root/$_hgcd_value" 2>/dev/null && pwd)
            ;;
    esac
}

hydra_project_identity_file() {
    _hpif_common="$(hydra_git_common_dir)" || return 1
    printf '%s/hydra/project-id\n' "$_hpif_common"
}

hydra_get_project_id() {
    _hgpi_file="$(hydra_project_identity_file)" || return 1
    [ -f "$_hgpi_file" ] || return 1
    _hgpi_id="$(sed -n '1p' "$_hgpi_file")"
    hydra_valid_id "$_hgpi_id" || return 1
    printf '%s\n' "$_hgpi_id"
}

hydra_ensure_project_id() {
    if _hepi_existing="$(hydra_get_project_id 2>/dev/null)"; then
        printf '%s\n' "$_hepi_existing"
        return 0
    fi
    _hepi_common="$(hydra_git_common_dir)" || return 1
    _hepi_root="$(git rev-parse --show-toplevel 2>/dev/null)" || return 1
    _hepi_dir="$_hepi_common/hydra"
    mkdir -p "$_hepi_dir" || return 1
    chmod 700 "$_hepi_dir" 2>/dev/null || true
    _hepi_id="$(hydra_new_id project "$_hepi_root|$_hepi_common")" || return 1
    _hepi_tmp="$(mktemp "$_hepi_dir/.project-id.XXXXXX")" || return 1
    printf '%s\n' "$_hepi_id" > "$_hepi_tmp"
    chmod 600 "$_hepi_tmp" 2>/dev/null || true
    if ! mv "$_hepi_tmp" "$_hepi_dir/project-id"; then
        rm -f "$_hepi_tmp"
        return 1
    fi
    printf '%s\n' "$_hepi_id"
}
