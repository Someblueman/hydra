#!/bin/sh
# Hydra 1.7 parallel-safety state and evidence helpers.

parallel_validate_name() {
    case "${1:-}" in
        ''|*[!A-Za-z0-9._-]*|[!A-Za-z0-9]*) return 1 ;;
        *) return 0 ;;
    esac
}

parallel_validate_path_pattern() {
    _pvp_pattern="${1:-}"
    case "$_pvp_pattern" in
        ''|/*|*'	'*|*'
'*|../*|*/../*|*/..) return 1 ;;
        *) return 0 ;;
    esac
}

parallel_project_load() {
    PARALLEL_PROJECT_ID="$(hydra_get_project_id)" || return 1
    PARALLEL_PROJECT_DIR="$(state_v2_project_dir "$PARALLEL_PROJECT_ID")" || return 1
    [ -d "$PARALLEL_PROJECT_DIR" ] || return 1
    export PARALLEL_PROJECT_ID PARALLEL_PROJECT_DIR
}

parallel_head_load() {
    lifecycle_load_head "$1" || return 1
    PARALLEL_HEAD_ID="$LIFECYCLE_HEAD_ID"
    PARALLEL_HEAD_DIR="$LIFECYCLE_HEAD_DIR"
    PARALLEL_BRANCH="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/branch")"
    PARALLEL_WORKTREE="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/worktree")"
    PARALLEL_BASE_REF="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/base-ref")"
    export PARALLEL_HEAD_ID PARALLEL_HEAD_DIR PARALLEL_BRANCH PARALLEL_WORKTREE PARALLEL_BASE_REF
}

parallel_claim_cleanup_expired() {
    parallel_project_load || return 1
    _pcc_root="$PARALLEL_PROJECT_DIR/claims"
    [ -d "$_pcc_root" ] || return 0
    _pcc_now="$(date +%s)"
    for _pcc_dir in "$_pcc_root"/claim_*; do
        [ -d "$_pcc_dir" ] || continue
        _pcc_expiry="$(sed -n '1p' "$_pcc_dir/expires-at" 2>/dev/null || true)"
        case "$_pcc_expiry" in
            ''|*[!0-9]*) continue ;;
        esac
        if [ "$_pcc_expiry" -le "$_pcc_now" ]; then
            rm -rf "$_pcc_dir"
        fi
    done
}

parallel_claim_add() {
    _pca_branch="$1"
    _pca_pattern="$2"
    _pca_access="$3"
    _pca_reason="$4"
    _pca_expiry="$5"
    parallel_validate_path_pattern "$_pca_pattern" || return 1
    case "$_pca_access" in read|write) ;; *) return 1 ;; esac
    case "$_pca_reason" in *'	'*|*'
'*) return 1 ;; esac
    case "$_pca_expiry" in ''|*[!0-9]*) return 1 ;; esac
    [ "$_pca_expiry" -gt "$(date +%s)" ] || return 1
    parallel_head_load "$_pca_branch" || return 1
    _pca_head="$PARALLEL_HEAD_ID"
    parallel_project_load || return 1
    _pca_root="$PARALLEL_PROJECT_DIR/claims"
    _pca_lock="claims_${PARALLEL_PROJECT_ID}"
    acquire_lock "$_pca_lock" "parallel claim add" "$_pca_head" || return 1
    mkdir -p "$_pca_root" || { release_lock "$_pca_lock"; return 1; }
    chmod 700 "$_pca_root" 2>/dev/null || true
    parallel_claim_cleanup_expired || { release_lock "$_pca_lock"; return 1; }
    _pca_id="$(hydra_new_id claim "$_pca_head|$_pca_pattern|$_pca_access|$_pca_expiry")" || {
        release_lock "$_pca_lock"; return 1;
    }
    _pca_tmp="$(mktemp -d "$_pca_root/.claim.XXXXXX")" || {
        release_lock "$_pca_lock"; return 1;
    }
    if ! state_v2_write_scalar "$_pca_tmp/claim-id" "$_pca_id" || \
       ! state_v2_write_scalar "$_pca_tmp/owner-head" "$_pca_head" || \
       ! state_v2_write_scalar "$_pca_tmp/path-pattern" "$_pca_pattern" || \
       ! state_v2_write_scalar "$_pca_tmp/access" "$_pca_access" || \
       ! state_v2_write_scalar "$_pca_tmp/reason" "$_pca_reason" || \
       ! state_v2_write_scalar "$_pca_tmp/created-at" "$(date +%s)" || \
       ! state_v2_write_scalar "$_pca_tmp/expires-at" "$_pca_expiry" || \
       ! mv "$_pca_tmp" "$_pca_root/$_pca_id"; then
        rm -rf "$_pca_tmp"
        release_lock "$_pca_lock"
        return 1
    fi
    release_lock "$_pca_lock"
    printf '%s\n' "$_pca_id"
}

parallel_claim_remove() {
    _pcr_id="$1"
    hydra_valid_id "$_pcr_id" || return 1
    case "$_pcr_id" in claim_*) ;; *) return 1 ;; esac
    parallel_project_load || return 1
    _pcr_lock="claims_${PARALLEL_PROJECT_ID}"
    acquire_lock "$_pcr_lock" "parallel claim remove" || return 1
    _pcr_dir="$PARALLEL_PROJECT_DIR/claims/$_pcr_id"
    if [ ! -d "$_pcr_dir" ]; then
        release_lock "$_pcr_lock"
        return 1
    fi
    rm -rf "$_pcr_dir"
    release_lock "$_pcr_lock"
}

parallel_claim_rows() {
    parallel_claim_cleanup_expired || return 1
    _pcl_root="$PARALLEL_PROJECT_DIR/claims"
    [ -d "$_pcl_root" ] || return 0
    for _pcl_dir in "$_pcl_root"/claim_*; do
        [ -d "$_pcl_dir" ] || continue
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$(basename "$_pcl_dir")" \
            "$(sed -n '1p' "$_pcl_dir/owner-head")" \
            "$(sed -n '1p' "$_pcl_dir/path-pattern")" \
            "$(sed -n '1p' "$_pcl_dir/access")" \
            "$(sed -n '1p' "$_pcl_dir/expires-at")" \
            "$(sed -n '1p' "$_pcl_dir/reason")"
    done
}

parallel_scope_write() {
    _psw_branch="$1"
    _psw_rules="$2"
    parallel_head_load "$_psw_branch" || return 1
    [ -n "$_psw_rules" ] || return 1
    _psw_tab="$(printf '\t')"
    while IFS="$_psw_tab" read -r _psw_mode _psw_pattern; do
        case "$_psw_mode" in read|write) ;; *) return 1 ;; esac
        parallel_validate_path_pattern "$_psw_pattern" || return 1
    done <<EOF
$_psw_rules
EOF
    _psw_lock="head_${LIFECYCLE_HEAD_ID}"
    acquire_lock "$_psw_lock" "parallel scope update" "$LIFECYCLE_HEAD_ID" || return 1
    state_v2_write_text "$LIFECYCLE_HEAD_DIR/scopes" "$_psw_rules
" || { release_lock "$_psw_lock"; return 1; }
    release_lock "$_psw_lock"
}

parallel_changed_files() {
    _pcf_worktree="$1"
    _pcf_base="$2"
    [ -d "$_pcf_worktree" ] || return 1
    {
        git -C "$_pcf_worktree" diff --name-only "$_pcf_base" --
        git -C "$_pcf_worktree" ls-files --others --exclude-standard
    } | LC_ALL=C sort -u
}

parallel_scope_access() {
    _psa_path="$1"
    _psa_rules="$2"
    _psa_result=out-of-scope
    _psa_tab="$(printf '\t')"
    while IFS="$_psa_tab" read -r _psa_mode _psa_pattern; do
        [ -n "$_psa_pattern" ] || continue
        # shellcheck disable=SC2254 # The stored scope is intentionally a shell pattern.
        case "$_psa_path" in
            $_psa_pattern)
                if [ "$_psa_mode" = write ]; then
                    printf '%s\n' writable
                    return 0
                fi
                _psa_result=read-only
                ;;
        esac
    done <<EOF
$_psa_rules
EOF
    printf '%s\n' "$_psa_result"
}

parallel_scope_rows() {
    _psr_branch="$1"
    parallel_head_load "$_psr_branch" || return 1
    _psr_rules="$(cat "$LIFECYCLE_HEAD_DIR/scopes" 2>/dev/null || true)"
    [ -n "$_psr_rules" ] || return 2
    parallel_changed_files "$PARALLEL_WORKTREE" "$PARALLEL_BASE_REF" | while IFS= read -r _psr_path; do
        [ -n "$_psr_path" ] || continue
        _psr_access="$(parallel_scope_access "$_psr_path" "$_psr_rules")"
        printf '%s\t%s\n' "$_psr_access" "$_psr_path"
    done
}

parallel_claim_pattern_intersection() {
    _pcpi_left="$1"
    _pcpi_right="$2"
    if [ "$_pcpi_left" = "$_pcpi_right" ]; then
        printf '%s\n' "$_pcpi_left"
        return 0
    fi
    # Comparing the stored patterns directly catches the documented nested
    # prefix form (for example src/* and src/api/*) without evaluating either
    # value as shell code.
    # shellcheck disable=SC2254 # Stored claim values are intentionally patterns.
    case "$_pcpi_right" in $_pcpi_left) printf '%s\n' "$_pcpi_right"; return 0 ;; esac
    # shellcheck disable=SC2254 # Stored claim values are intentionally patterns.
    case "$_pcpi_left" in $_pcpi_right) printf '%s\n' "$_pcpi_left"; return 0 ;; esac
    return 1
}

parallel_collision_claim_rows() {
    _pccr_left="$1"
    _pccr_right="$2"
    parallel_project_load || return 1
    parallel_claim_cleanup_expired || return 1
    _pccr_root="$PARALLEL_PROJECT_DIR/claims"
    [ -d "$_pccr_root" ] || return 0
    for _pccr_a in "$_pccr_root"/claim_*; do
        [ -d "$_pccr_a" ] || continue
        [ "$(sed -n '1p' "$_pccr_a/owner-head")" = "$_pccr_left" ] || continue
        _pccr_pattern="$(sed -n '1p' "$_pccr_a/path-pattern")"
        _pccr_access="$(sed -n '1p' "$_pccr_a/access")"
        for _pccr_b in "$_pccr_root"/claim_*; do
            [ -d "$_pccr_b" ] || continue
            [ "$(sed -n '1p' "$_pccr_b/owner-head")" = "$_pccr_right" ] || continue
            _pccr_other_pattern="$(sed -n '1p' "$_pccr_b/path-pattern")"
            _pccr_intersection="$(parallel_claim_pattern_intersection "$_pccr_pattern" "$_pccr_other_pattern")" || continue
            _pccr_other_access="$(sed -n '1p' "$_pccr_b/access")"
            if [ "$_pccr_access" = write ] || [ "$_pccr_other_access" = write ]; then
                printf 'claim\t%s\n' "$_pccr_intersection"
            fi
        done
    done | LC_ALL=C sort -u
}

parallel_collision_rows() {
    _pcol_left_branch="$1"
    _pcol_right_branch="$2"
    parallel_head_load "$_pcol_left_branch" || return 1
    _pcol_left_head="$PARALLEL_HEAD_ID"
    _pcol_left_worktree="$PARALLEL_WORKTREE"
    _pcol_left_base="$PARALLEL_BASE_REF"
    parallel_head_load "$_pcol_right_branch" || return 1
    _pcol_right_head="$PARALLEL_HEAD_ID"
    _pcol_right_worktree="$PARALLEL_WORKTREE"
    _pcol_right_base="$PARALLEL_BASE_REF"
    [ "$_pcol_left_head" != "$_pcol_right_head" ] || return 1

    _pcol_tmp="$(mktemp -d)" || return 1
    parallel_changed_files "$_pcol_left_worktree" "$_pcol_left_base" > "$_pcol_tmp/left"
    parallel_changed_files "$_pcol_right_worktree" "$_pcol_right_base" > "$_pcol_tmp/right"
    parallel_collision_claim_rows "$_pcol_left_head" "$_pcol_right_head"
    comm -12 "$_pcol_tmp/left" "$_pcol_tmp/right" | while IFS= read -r _pcol_path; do
        [ -n "$_pcol_path" ] || continue
        printf 'overlap\t%s\n' "$_pcol_path"
        mkdir -p "$_pcol_tmp/merge"
        _pcol_base_file="$_pcol_tmp/merge/base"
        _pcol_left_file="$_pcol_tmp/merge/left"
        _pcol_right_file="$_pcol_tmp/merge/right"
        git -C "$_pcol_left_worktree" show "$_pcol_left_base:$_pcol_path" > "$_pcol_base_file" 2>/dev/null || : > "$_pcol_base_file"
        [ ! -f "$_pcol_left_worktree/$_pcol_path" ] || cp "$_pcol_left_worktree/$_pcol_path" "$_pcol_left_file"
        [ -f "$_pcol_left_file" ] || : > "$_pcol_left_file"
        [ ! -f "$_pcol_right_worktree/$_pcol_path" ] || cp "$_pcol_right_worktree/$_pcol_path" "$_pcol_right_file"
        [ -f "$_pcol_right_file" ] || : > "$_pcol_right_file"
        if git merge-file -p "$_pcol_left_file" "$_pcol_base_file" "$_pcol_right_file" >/dev/null 2>&1; then
            :
        else
            _pcol_status=$?
            [ "$_pcol_status" -gt 1 ] || printf 'predicted-conflict\t%s\n' "$_pcol_path"
        fi
    done
    _pcol_left_commit="$(git -C "$_pcol_left_worktree" rev-parse HEAD)" || { rm -rf "$_pcol_tmp"; return 1; }
    _pcol_right_commit="$(git -C "$_pcol_right_worktree" rev-parse HEAD)" || { rm -rf "$_pcol_tmp"; return 1; }
    _pcol_merge_base="$(git -C "$_pcol_left_worktree" merge-base "$_pcol_left_commit" "$_pcol_right_commit")" || {
        rm -rf "$_pcol_tmp"; return 1;
    }
    git -C "$_pcol_left_worktree" merge-tree "$_pcol_merge_base" "$_pcol_left_commit" "$_pcol_right_commit" 2>/dev/null | \
        awk '/^(changed in both|added in both)$/ { if (getline > 0) { sub(/^[^ ]+ [^ ]+ [^ ]+ /, ""); print "observed-conflict\t" $0 } }'
    {
        git -C "$_pcol_left_worktree" diff --name-only --diff-filter=U --
        git -C "$_pcol_right_worktree" diff --name-only --diff-filter=U --
    } | LC_ALL=C sort -u | while IFS= read -r _pcol_path; do
        [ -n "$_pcol_path" ] || continue
        printf 'observed-conflict\t%s\n' "$_pcol_path"
    done
    rm -rf "$_pcol_tmp"
}

parallel_resource_port_used() {
    _prpu_root="$1"
    _prpu_port="$2"
    for _prpu_file in "$_prpu_root"/head_*/ports; do
        [ -f "$_prpu_file" ] || continue
        while IFS='	' read -r _prpu_name _prpu_existing; do
            [ "$_prpu_existing" != "$_prpu_port" ] || return 0
        done < "$_prpu_file"
    done
    return 1
}

parallel_resource_value_used() {
    _prvu_root="$1"
    _prvu_field="$2"
    _prvu_value="$3"
    [ -n "$_prvu_value" ] || return 1
    for _prvu_file in "$_prvu_root"/head_*/"$_prvu_field"; do
        [ -f "$_prvu_file" ] || continue
        [ "$(sed -n '1p' "$_prvu_file")" != "$_prvu_value" ] || return 0
    done
    return 1
}

parallel_resource_allocate() {
    _pra_branch="$1"
    _pra_port_specs="$2"
    _pra_compose="$3"
    _pra_database="$4"
    parallel_head_load "$_pra_branch" || return 1
    _pra_head="$PARALLEL_HEAD_ID"
    parallel_project_load || return 1
    _pra_root="$PARALLEL_PROJECT_DIR/resources"
    _pra_lock="resources_${PARALLEL_PROJECT_ID}"
    [ -z "$_pra_compose" ] || parallel_validate_name "$_pra_compose" || return 1
    [ -z "$_pra_database" ] || parallel_validate_name "$_pra_database" || return 1
    acquire_lock "$_pra_lock" "parallel resource allocate" "$_pra_head" || return 1
    mkdir -p "$_pra_root" || { release_lock "$_pra_lock"; return 1; }
    [ ! -e "$_pra_root/$_pra_head" ] || { release_lock "$_pra_lock"; return 1; }
    if parallel_resource_value_used "$_pra_root" compose-project "$_pra_compose" || \
       parallel_resource_value_used "$_pra_root" database "$_pra_database"; then
        release_lock "$_pra_lock"
        return 1
    fi
    _pra_tmp="$(mktemp -d "$_pra_root/.resource.XXXXXX")" || {
        release_lock "$_pra_lock"; return 1;
    }
    : > "$_pra_tmp/ports"
    _pra_tab="$(printf '\t')"
    while IFS="$_pra_tab" read -r _pra_name _pra_range; do
        [ -n "$_pra_name" ] || continue
        parallel_validate_name "$_pra_name" || { rm -rf "$_pra_tmp"; release_lock "$_pra_lock"; return 1; }
        _pra_start="${_pra_range%-*}"
        _pra_end="${_pra_range#*-}"
        case "$_pra_start:$_pra_end" in *[!0-9:]*) rm -rf "$_pra_tmp"; release_lock "$_pra_lock"; return 1 ;; esac
        if [ "$_pra_start" -lt 1 ] || [ "$_pra_end" -gt 65535 ] || [ "$_pra_start" -gt "$_pra_end" ]; then
            rm -rf "$_pra_tmp"; release_lock "$_pra_lock"; return 1
        fi
        _pra_port="$_pra_start"
        while [ "$_pra_port" -le "$_pra_end" ]; do
            if ! parallel_resource_port_used "$_pra_root" "$_pra_port" && \
               ! awk -F '\t' -v port="$_pra_port" '$2 == port { found=1 } END { exit !found }' "$_pra_tmp/ports"; then
                break
            fi
            _pra_port=$((_pra_port + 1))
        done
        [ "$_pra_port" -le "$_pra_end" ] || { rm -rf "$_pra_tmp"; release_lock "$_pra_lock"; return 1; }
        printf '%s\t%s\n' "$_pra_name" "$_pra_port" >> "$_pra_tmp/ports"
    done <<EOF
$_pra_port_specs
EOF
    if ! state_v2_write_scalar "$_pra_tmp/head-id" "$_pra_head" || \
       ! state_v2_write_scalar "$_pra_tmp/compose-project" "$_pra_compose" || \
       ! state_v2_write_scalar "$_pra_tmp/database" "$_pra_database" || \
       ! state_v2_write_scalar "$_pra_tmp/created-at" "$(date +%s)" || \
       ! mv "$_pra_tmp" "$_pra_root/$_pra_head"; then
        rm -rf "$_pra_tmp"
        release_lock "$_pra_lock"
        return 1
    fi
    release_lock "$_pra_lock"
}

parallel_resource_release() {
    _prr_branch="$1"
    parallel_head_load "$_prr_branch" || return 1
    _prr_head="$PARALLEL_HEAD_ID"
    parallel_project_load || return 1
    _prr_lock="resources_${PARALLEL_PROJECT_ID}"
    acquire_lock "$_prr_lock" "parallel resource release" "$_prr_head" || return 1
    rm -rf "$PARALLEL_PROJECT_DIR/resources/$_prr_head"
    release_lock "$_prr_lock"
}

parallel_release_head_records() {
    _prhr_branch="$1"
    # A legacy/phantom map entry has no v2 head and therefore cannot own v2
    # resources or claims. Treat that teardown as an idempotent no-op.
    parallel_head_load "$_prhr_branch" >/dev/null 2>&1 || return 0
    _prhr_head="$PARALLEL_HEAD_ID"
    parallel_project_load || return 1

    _prhr_resource_lock="resources_${PARALLEL_PROJECT_ID}"
    acquire_lock "$_prhr_resource_lock" "parallel teardown resources" "$_prhr_head" || return 1
    rm -rf "$PARALLEL_PROJECT_DIR/resources/$_prhr_head"
    release_lock "$_prhr_resource_lock"

    _prhr_claim_lock="claims_${PARALLEL_PROJECT_ID}"
    acquire_lock "$_prhr_claim_lock" "parallel teardown claims" "$_prhr_head" || return 1
    for _prhr_claim in "$PARALLEL_PROJECT_DIR"/claims/claim_*; do
        [ -d "$_prhr_claim" ] || continue
        [ "$(sed -n '1p' "$_prhr_claim/owner-head" 2>/dev/null || true)" != "$_prhr_head" ] || rm -rf "$_prhr_claim"
    done
    release_lock "$_prhr_claim_lock"
}

parallel_resource_rows() {
    _prrows_branch="$1"
    parallel_head_load "$_prrows_branch" || return 1
    parallel_project_load || return 1
    _prrows_dir="$PARALLEL_PROJECT_DIR/resources/$PARALLEL_HEAD_ID"
    [ -d "$_prrows_dir" ] || return 1
    while IFS='	' read -r _prrows_name _prrows_port; do
        [ -n "$_prrows_name" ] || continue
        printf 'port\t%s\t%s\n' "$_prrows_name" "$_prrows_port"
    done < "$_prrows_dir/ports"
    printf 'compose-project\t%s\t%s\n' value "$(sed -n '1p' "$_prrows_dir/compose-project")"
    printf 'database\t%s\t%s\n' value "$(sed -n '1p' "$_prrows_dir/database")"
}

parallel_gate_run() {
    _pgr_branch="$1"
    _pgr_name="$2"
    shift 2
    parallel_validate_name "$_pgr_name" || return 1
    [ $# -gt 0 ] || return 1
    parallel_head_load "$_pgr_branch" || return 1
    [ -d "$PARALLEL_WORKTREE" ] || return 1
    _pgr_gate="$LIFECYCLE_HEAD_DIR/gates/$_pgr_name"
    _pgr_head_commit="$(git -C "$PARALLEL_WORKTREE" rev-parse HEAD)" || return 1
    _pgr_worktree_hash="$(git -C "$PARALLEL_WORKTREE" status --porcelain=v1 | hydra_hash)" || return 1
    _pgr_lock="gate_${LIFECYCLE_HEAD_ID}_${_pgr_name}"
    acquire_lock "$_pgr_lock" "parallel verification gate" "$LIFECYCLE_HEAD_ID" || return 1
    mkdir -p "$_pgr_gate/runs" || { release_lock "$_pgr_lock"; return 1; }
    _pgr_run="$(hydra_new_id run "$LIFECYCLE_HEAD_ID|gate|$_pgr_name")" || {
        release_lock "$_pgr_lock"; return 1;
    }
    _pgr_tmp="$(mktemp -d "$_pgr_gate/runs/.run.XXXXXX")" || {
        release_lock "$_pgr_lock"; return 1;
    }
    if (cd "$PARALLEL_WORKTREE" && "$@") > "$_pgr_tmp/stdout.raw" 2> "$_pgr_tmp/stderr.raw"; then
        _pgr_status=0
    else
        _pgr_status=$?
    fi
    _pgr_max="${HYDRA_GATE_MAX_BYTES:-1048576}"
    case "$_pgr_max" in ''|*[!0-9]*) _pgr_max=1048576 ;; esac
    head -c "$_pgr_max" "$_pgr_tmp/stdout.raw" > "$_pgr_tmp/stdout"
    head -c "$_pgr_max" "$_pgr_tmp/stderr.raw" > "$_pgr_tmp/stderr"
    rm -f "$_pgr_tmp/stdout.raw" "$_pgr_tmp/stderr.raw"
    : > "$_pgr_tmp/argv"
    for _pgr_arg in "$@"; do
        case "$_pgr_arg" in *'
'*) rm -rf "$_pgr_tmp"; release_lock "$_pgr_lock"; return 1 ;; esac
        printf '%s\n' "$_pgr_arg" >> "$_pgr_tmp/argv"
    done
    if ! state_v2_write_scalar "$_pgr_tmp/run-id" "$_pgr_run" || \
       ! state_v2_write_scalar "$_pgr_tmp/status" "$_pgr_status" || \
       ! state_v2_write_scalar "$_pgr_tmp/head-commit" "$_pgr_head_commit" || \
       ! state_v2_write_scalar "$_pgr_tmp/worktree-hash" "$_pgr_worktree_hash" || \
       ! state_v2_write_scalar "$_pgr_tmp/completed-at" "$(date +%s)" || \
       ! mv "$_pgr_tmp" "$_pgr_gate/runs/$_pgr_run" || \
       ! state_v2_write_scalar "$_pgr_gate/latest-run" "$_pgr_run" || \
       ! state_v2_write_scalar "$_pgr_gate/latest-status" "$_pgr_status" || \
       ! state_v2_write_scalar "$_pgr_gate/latest-head-commit" "$_pgr_head_commit" || \
       ! state_v2_write_scalar "$_pgr_gate/latest-worktree-hash" "$_pgr_worktree_hash"; then
        rm -rf "$_pgr_tmp"
        release_lock "$_pgr_lock"
        return 1
    fi
    rm -f "$_pgr_gate/approved-by" "$_pgr_gate/approved-at" "$_pgr_gate/approval-reason"
    release_lock "$_pgr_lock"
    printf '%s\n' "$_pgr_run"
    return "$_pgr_status"
}

parallel_gate_approve() {
    _pga_branch="$1"
    _pga_name="$2"
    _pga_actor="$3"
    _pga_reason="$4"
    parallel_validate_name "$_pga_name" || return 1
    [ -n "$_pga_actor" ] || return 1
    case "$_pga_actor$_pga_reason" in *'	'*|*'
'*) return 1 ;; esac
    parallel_head_load "$_pga_branch" || return 1
    _pga_gate="$LIFECYCLE_HEAD_DIR/gates/$_pga_name"
    _pga_lock="gate_${LIFECYCLE_HEAD_ID}_${_pga_name}"
    acquire_lock "$_pga_lock" "parallel gate approval" "$LIFECYCLE_HEAD_ID" || return 1
    _pga_commit="$(git -C "$PARALLEL_WORKTREE" rev-parse HEAD)" || {
        release_lock "$_pga_lock"
        return 1
    }
    _pga_worktree_hash="$(git -C "$PARALLEL_WORKTREE" status --porcelain=v1 | hydra_hash)" || {
        release_lock "$_pga_lock"
        return 1
    }
    if [ "$(sed -n '1p' "$_pga_gate/latest-status" 2>/dev/null || true)" != 0 ] || \
       [ "$(sed -n '1p' "$_pga_gate/latest-head-commit" 2>/dev/null || true)" != "$_pga_commit" ] || \
       [ "$(sed -n '1p' "$_pga_gate/latest-worktree-hash" 2>/dev/null || true)" != "$_pga_worktree_hash" ]; then
        release_lock "$_pga_lock"
        return 1
    fi
    if ! state_v2_write_scalar "$_pga_gate/approved-by" "$_pga_actor" || \
       ! state_v2_write_scalar "$_pga_gate/approved-at" "$(date +%s)" || \
       ! state_v2_write_scalar "$_pga_gate/approval-reason" "$_pga_reason"; then
        release_lock "$_pga_lock"
        return 1
    fi
    release_lock "$_pga_lock"
}

parallel_gate_rows() {
    _pgrows_branch="$1"
    parallel_head_load "$_pgrows_branch" || return 1
    _pgrows_root="$LIFECYCLE_HEAD_DIR/gates"
    [ -d "$_pgrows_root" ] || return 0
    for _pgrows_gate in "$_pgrows_root"/*; do
        [ -d "$_pgrows_gate" ] || continue
        printf '%s\t%s\t%s\t%s\t%s\n' \
            "$(basename "$_pgrows_gate")" \
            "$(sed -n '1p' "$_pgrows_gate/latest-run" 2>/dev/null || true)" \
            "$(sed -n '1p' "$_pgrows_gate/latest-status" 2>/dev/null || true)" \
            "$(sed -n '1p' "$_pgrows_gate/approved-by" 2>/dev/null || true)" \
            "$(sed -n '1p' "$_pgrows_gate/approved-at" 2>/dev/null || true)"
    done
}
