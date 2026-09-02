#!/bin/sh
# Verified integration status, recovery, approval, and promotion actions.

integration_verified_status() {
    _ivst_dir="$(integration_verified_root)/$1"
    [ -d "$_ivst_dir" ] || { echo "Error: integration report not found" >&2; return 1; }
    printf 'Integration %s: %s\n' "$1" "$(sed -n '1p' "$_ivst_dir/state")"
    printf '  target: %s at %s\n  worktree: %s\n' \
        "$(sed -n '1p' "$_ivst_dir/target-ref")" \
        "$(sed -n '1p' "$_ivst_dir/target-commit")" \
        "$(sed -n '1p' "$_ivst_dir/worktree")"
    [ ! -f "$_ivst_dir/observed-conflicts" ] || sed 's/^/  observed conflict: /' "$_ivst_dir/observed-conflicts"
    _ivst_state="$(sed -n '1p' "$_ivst_dir/state")"
    case "$_ivst_state" in
        verified|promoted) ;;
        *)
            [ ! -f "$_ivst_dir/failure-class" ] || printf '  failure class: %s\n' "$(sed -n '1p' "$_ivst_dir/failure-class")"
            [ ! -f "$_ivst_dir/failed-candidate" ] || printf '  failed candidate: %s\n' "$(sed -n '1p' "$_ivst_dir/failed-candidate")"
            [ ! -f "$_ivst_dir/failed-gate" ] || printf '  failed gate: %s\n' "$(sed -n '1p' "$_ivst_dir/failed-gate")"
            [ ! -f "$_ivst_dir/expected-target" ] || printf '  expected target: %s\n' "$(sed -n '1p' "$_ivst_dir/expected-target")"
            [ ! -f "$_ivst_dir/observed-target" ] || printf '  observed target: %s\n' "$(sed -n '1p' "$_ivst_dir/observed-target")"
            ;;
    esac
    printf '  report: %s\n' "$_ivst_dir"
    printf '  recovery action: %s\n' "$(sed -n '1p' "$_ivst_dir/recovery-action" 2>/dev/null || true)"
}

integration_verified_cancel() {
    _ivca_dir="$(integration_verified_root)/$1"
    [ -d "$_ivca_dir" ] || { echo "Error: integration report not found" >&2; return 1; }
    _ivca_state="$(sed -n '1p' "$_ivca_dir/state")"
    [ "$_ivca_state" = assembling ] || return 1
    state_v2_write_scalar "$_ivca_dir/cancel-requested" "$(date +%s)"
    _ivca_owner="$(sed -n '1p' "$_ivca_dir/owner-pid" 2>/dev/null || true)"
    integration_pid_alive "$_ivca_owner" && kill -TERM "$_ivca_owner" 2>/dev/null || true
    _ivca_wait=0
    while [ "$(sed -n '1p' "$_ivca_dir/state")" = assembling ] && [ "$_ivca_wait" -lt 50 ]; do
        sleep 0.1
        _ivca_wait=$((_ivca_wait + 1))
    done
    printf 'Integration %s: %s\n' "$1" "$(sed -n '1p' "$_ivca_dir/state")"
    [ "$(sed -n '1p' "$_ivca_dir/state")" = cancelled ]
}

integration_verified_resume() {
    _ivrs_dir="$(integration_verified_root)/$1"
    [ -d "$_ivrs_dir" ] || { echo "Error: integration report not found" >&2; return 1; }
    _ivrs_state="$(sed -n '1p' "$_ivrs_dir/state")"
    case "$_ivrs_state" in interrupted|cancelled|verification-failed|resource-refused) ;; *) echo "Error: integration state '$_ivrs_state' is not resumable" >&2; return 1 ;; esac
    _ivrs_project="$(hydra_get_project_id)" || return 1
    _ivrs_project_dir="$(state_v2_project_dir "$_ivrs_project")" || return 1
    _ivrs_lock="integration_target_${_ivrs_project}"
    acquire_lock "$_ivrs_lock" "verified integration resume" || return 1
    _ivrs_worktree="$(sed -n '1p' "$_ivrs_dir/worktree")"
    _ivrs_target_ref="$(sed -n '1p' "$_ivrs_dir/target-ref")"
    _ivrs_target="$(sed -n '1p' "$_ivrs_dir/target-commit")"
    _ivrs_last="$(sed -n '1p' "$_ivrs_dir/last-result-commit")"
    if [ "$(git rev-parse "$_ivrs_target_ref" 2>/dev/null || true)" != "$_ivrs_target" ] || \
       [ "$(git -C "$_ivrs_worktree" rev-parse HEAD 2>/dev/null || true)" != "$_ivrs_last" ] || \
       [ -n "$(git -C "$_ivrs_worktree" status --porcelain=v1 2>/dev/null || true)" ]; then
        release_lock "$_ivrs_lock"
        echo "Error: integration target or worktree binding is stale; target unchanged" >&2
        return 1
    fi
    _ivrs_hash="$(git hash-object "$_ivrs_dir/manifest.tsv")"
    if [ "$_ivrs_hash" != "$(sed -n '1p' "$_ivrs_dir/manifest-hash")" ] || \
       [ "$(git hash-object "$_ivrs_dir/candidates.tsv")" != "$(sed -n '1p' "$_ivrs_dir/candidates-hash")" ] || \
       [ "$(git hash-object "$_ivrs_dir/gates.argv")" != "$(sed -n '1p' "$_ivrs_dir/gates-hash")" ]; then
        release_lock "$_ivrs_lock"
        echo "Error: integration manifest changed; target unchanged" >&2
        return 1
    fi
    rm -f "$_ivrs_dir/cancel-requested"
    state_v2_write_scalar "$_ivrs_dir/owner-pid" "$$"
    state_v2_write_scalar "$_ivrs_dir/state" assembling
    _ivrs_run="$1"
    trap 'integration_verified_interrupt "$_ivrs_dir" "$_ivrs_lock" "$_ivrs_run"; trap - HUP INT TERM; exit 130' HUP INT TERM
    if integration_verified_continue "$_ivrs_dir" "$_ivrs_project" "$_ivrs_project_dir"; then _ivrs_status=0; else _ivrs_status=$?; fi
    trap - HUP INT TERM
    release_lock "$_ivrs_lock"
    return "$_ivrs_status"
}

integration_verified_approve() {
    _iva_dir="$(integration_verified_root)/$1"
    [ "$(sed -n '1p' "$_iva_dir/state" 2>/dev/null || true)" = verified ] || {
        echo "Error: only a current passing verification can be approved" >&2
        return 1
    }
    state_v2_write_scalar "$_iva_dir/approved-result" "$(sed -n '1p' "$_iva_dir/result-commit")" &&
        state_v2_write_scalar "$_iva_dir/approved-by" "$2" &&
        state_v2_write_scalar "$_iva_dir/approved-at" "$(date +%s)"
}

integration_verified_target_worktree() {
    _ivtw_target_ref="$1"
    git worktree list --porcelain | awk -v target="$_ivtw_target_ref" '
        $1 == "worktree" { worktree = substr($0, 10) }
        $1 == "branch" && $2 == target { print worktree; exit }
    '
}

integration_verified_promote() {
    _ivpr_dir="$(integration_verified_root)/$1"
    _ivpr_project="$(hydra_get_project_id)" || return 1
    _ivpr_lock="integration_target_${_ivpr_project}"
    acquire_lock "$_ivpr_lock" "verified integration promotion" || return 1
    _ivpr_state="$(sed -n '1p' "$_ivpr_dir/state" 2>/dev/null || true)"
    _ivpr_result="$(sed -n '1p' "$_ivpr_dir/result-commit" 2>/dev/null || true)"
    _ivpr_target_ref="$(sed -n '1p' "$_ivpr_dir/target-ref" 2>/dev/null || true)"
    _ivpr_old="$(sed -n '1p' "$_ivpr_dir/target-commit" 2>/dev/null || true)"
    _ivpr_worktree="$(sed -n '1p' "$_ivpr_dir/worktree" 2>/dev/null || true)"
    if [ "$_ivpr_state" != verified ] || \
       [ "$(sed -n '1p' "$_ivpr_dir/approved-result" 2>/dev/null || true)" != "$_ivpr_result" ] || \
       [ "$(git hash-object "$_ivpr_dir/manifest.tsv" 2>/dev/null || true)" != "$(sed -n '1p' "$_ivpr_dir/manifest-hash" 2>/dev/null || true)" ] || \
       [ "$(git hash-object "$_ivpr_dir/candidates.tsv" 2>/dev/null || true)" != "$(sed -n '1p' "$_ivpr_dir/candidates-hash" 2>/dev/null || true)" ] || \
       [ "$(git hash-object "$_ivpr_dir/gates.argv" 2>/dev/null || true)" != "$(sed -n '1p' "$_ivpr_dir/gates-hash" 2>/dev/null || true)" ] || \
       [ "$(git rev-parse "$_ivpr_target_ref" 2>/dev/null || true)" != "$_ivpr_old" ] || \
       [ "$(git -C "$_ivpr_worktree" rev-parse HEAD 2>/dev/null || true)" != "$_ivpr_result" ] || \
       [ -n "$(git -C "$_ivpr_worktree" status --porcelain=v1 2>/dev/null || true)" ]; then
        release_lock "$_ivpr_lock"
        echo "Error: approval, verification, or target binding is stale; target unchanged" >&2
        return 1
    fi
    while IFS="$(printf '\t')" read -r _ivpr_head _ivpr_branch _ivpr_commit _ivpr_base; do
        _ivpr_hd="$(state_v2_project_dir "$_ivpr_project")/heads/$_ivpr_head"
        if [ "$(git -C "$(sed -n '1p' "$_ivpr_hd/worktree")" rev-parse HEAD 2>/dev/null || true)" != "$_ivpr_commit" ]; then
            release_lock "$_ivpr_lock"
            echo "Error: candidate binding is stale; target unchanged" >&2
            return 1
        fi
    done < "$_ivpr_dir/candidates.tsv"
    _ivpr_target_worktree="$(integration_verified_target_worktree "$_ivpr_target_ref")"
    if [ -n "$_ivpr_target_worktree" ]; then
        if ! integration_require_clean "$_ivpr_target_worktree" "target branch '$_ivpr_target_ref'" ||
           [ "$(git -C "$_ivpr_target_worktree" rev-parse HEAD)" != "$_ivpr_old" ] ||
           ! git -C "$_ivpr_target_worktree" merge --ff-only "$_ivpr_result" >/dev/null 2>&1; then
            release_lock "$_ivpr_lock"
            echo "Error: concurrent target movement or dirty target; target unchanged" >&2
            return 1
        fi
    elif ! git update-ref "$_ivpr_target_ref" "$_ivpr_result" "$_ivpr_old"; then
        release_lock "$_ivpr_lock"
        echo "Error: concurrent target movement; target unchanged" >&2
        return 1
    fi
    state_v2_write_scalar "$_ivpr_dir/state" promoted
    state_v2_write_scalar "$_ivpr_dir/promoted-at" "$(date +%s)"
    release_lock "$_ivpr_lock"
}

integration_verified_cleanup() {
    _ivc_dir="$(integration_verified_root)/$1"
    [ -d "$_ivc_dir" ] || return 1
    _ivc_worktree="$(sed -n '1p' "$_ivc_dir/worktree")"
    [ -d "$_ivc_worktree" ] || {
        state_v2_write_scalar "$_ivc_dir/cleanup" already-absent
        return 0
    }
    integration_require_clean "$_ivc_worktree" integration || {
        echo "Remaining: $_ivc_worktree and $_ivc_dir" >&2
        return 1
    }
    [ -n "$(sed -n '1p' "$_ivc_dir/state" 2>/dev/null || true)" ] || {
        echo "Error: refusing cleanup with unrecorded integration state" >&2
        return 1
    }
    git worktree remove "$_ivc_worktree" || return 1
    state_v2_write_scalar "$_ivc_dir/cleanup" removed
    state_v2_write_scalar "$_ivc_dir/cleaned-at" "$(date +%s)"
}

