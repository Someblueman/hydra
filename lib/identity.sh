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
    case "$_hni_kind" in project|head|instance|run|step|evt|claim|pack) ;; *) return 1 ;; esac
    _hni_hash="$(printf '%s\n' "$_hni_kind|$_hni_seed|$(date +%s)|$$" | hydra_hash)" || return 1
    printf '%s_%s\n' "$_hni_kind" "$(printf '%.20s' "$_hni_hash")"
}

hydra_valid_id() {
    case "$1" in
        project_*|head_*|instance_*|run_*|step_*|evt_*|claim_*|pack_*) ;;
        *) return 1 ;;
    esac
    _hvi_suffix="${1#*_}"
    case "$_hvi_suffix" in ''|*[!0-9a-f]*) return 1 ;; esac
    return 0
}

hydra_git_common_dir() {
    _hgcd_value="$(git rev-parse --git-common-dir 2>/dev/null)" || return 1
    # Git reports relative paths against the caller, including ../.git when
    # invoked from a subdirectory, not against --show-toplevel.
    (CDPATH='' cd -- "$_hgcd_value" 2>/dev/null && pwd -P)
}

hydra_project_identity_file() {
    _hpif_common="$(hydra_git_common_dir)" || return 1
    printf '%s/hydra/project-id\n' "$_hpif_common"
}

# Where this repository keeps its project id, remembered for the working
# directory and Git environment it was resolved in. A cd or a different
# GIT_DIR gives another key, so the next lookup asks Git again.
_HYDRA_IDENTITY_KEY=
_HYDRA_IDENTITY_FILE=

# Resolve this repository's project id without printing it.
# Usage: hydra_load_project_id
# Sets HYDRA_LOADED_PROJECT_ID and returns 0; outside an initialised project
# it is empty and the status is 1. Git is asked for the identity file once
# per process and working directory (a subshell inherits the answer). The
# file itself is read on every call, so an id written later is seen.
hydra_load_project_id() {
    HYDRA_LOADED_PROJECT_ID=
    _hlpi_key="${PWD:-}|${GIT_DIR:-}|${GIT_COMMON_DIR:-}|${GIT_WORK_TREE:-}"
    if [ -z "$_HYDRA_IDENTITY_FILE" ] || [ "$_HYDRA_IDENTITY_KEY" != "$_hlpi_key" ]; then
        _HYDRA_IDENTITY_KEY=
        _HYDRA_IDENTITY_FILE="$(hydra_project_identity_file)" || {
            _HYDRA_IDENTITY_FILE=
            return 1
        }
        _HYDRA_IDENTITY_KEY="$_hlpi_key"
    fi
    [ -f "$_HYDRA_IDENTITY_FILE" ] || return 1
    _hlpi_id=
    # The first line, read by the shell itself (no sed process).
    { IFS= read -r _hlpi_id < "$_HYDRA_IDENTITY_FILE"; } 2>/dev/null || :
    hydra_valid_id "$_hlpi_id" || return 1
    HYDRA_LOADED_PROJECT_ID="$_hlpi_id"
}

hydra_get_project_id() {
    hydra_load_project_id || return 1
    printf '%s\n' "$HYDRA_LOADED_PROJECT_ID"
}

hydra_ensure_project_id() {
    if _hepi_existing="$(hydra_get_project_id 2>/dev/null)"; then
        printf '%s\n' "$_hepi_existing"
        return 0
    fi
    _hepi_common="$(hydra_git_common_dir)" || return 1
    _hepi_root="$(git rev-parse --show-toplevel 2>/dev/null)" || return 1
    _hepi_dir="$_hepi_common/hydra"
    hydra_private_mkdir "$_hepi_dir" || return 1
    chmod 700 "$_hepi_dir" 2>/dev/null || true
    _hepi_id="$(hydra_new_id project "$_hepi_root|$_hepi_common")" || return 1
    _hepi_tmp="$(mktemp "$_hepi_dir/.project-id.XXXXXX")" || return 1
    if ! printf '%s\n' "$_hepi_id" > "$_hepi_tmp"; then
        rm -f "$_hepi_tmp"
        return 1
    fi
    chmod 600 "$_hepi_tmp" 2>/dev/null || true
    if ! mv "$_hepi_tmp" "$_hepi_dir/project-id"; then
        rm -f "$_hepi_tmp"
        return 1
    fi
    printf '%s\n' "$_hepi_id"
}
