#!/bin/sh
# Typed context packs and recoverable sync/land helpers.

integration_require_clean() {
    _irc_worktree="$1"
    _irc_label="$2"
    [ -d "$_irc_worktree" ] || { echo "Error: $_irc_label worktree is unavailable" >&2; return 1; }
    _irc_dirty="$(git -C "$_irc_worktree" status --porcelain=v1 2>/dev/null || true)"
    if [ -n "$_irc_dirty" ]; then
        echo "Error: $_irc_label has dirty or untracked work; nothing was changed" >&2
        return 1
    fi
}

integration_gate_approved() {
    _iga_branch="$1"
    _iga_name="$2"
    parallel_validate_name "$_iga_name" || return 1
    parallel_head_load "$_iga_branch" || return 1
    _iga_dir="$LIFECYCLE_HEAD_DIR/gates/$_iga_name"
    _iga_commit="$(git -C "$PARALLEL_WORKTREE" rev-parse HEAD)" || return 1
    _iga_worktree_hash="$(git -C "$PARALLEL_WORKTREE" status --porcelain=v1 | hydra_hash)" || return 1
    [ "$(sed -n '1p' "$_iga_dir/latest-status" 2>/dev/null || true)" = 0 ] && \
        [ -n "$(sed -n '1p' "$_iga_dir/approved-by" 2>/dev/null || true)" ] && \
        [ "$(sed -n '1p' "$_iga_dir/latest-head-commit" 2>/dev/null || true)" = "$_iga_commit" ] && \
        [ "$(sed -n '1p' "$_iga_dir/latest-worktree-hash" 2>/dev/null || true)" = "$_iga_worktree_hash" ]
}

integration_sensitive_path() {
    _isp_path="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
    case "$_isp_path" in
        .env|*/.env|.env.*|*/.env.*|*.pem|*.key|*credentials*|*secret*|*token*|*id_rsa*|*.p12|*.pfx) return 0 ;;
        *) return 1 ;;
    esac
}

integration_context_create() {
    _icc_branch="$1"
    _icc_diff="$2"
    _icc_files="$3"
    _icc_notes="$4"
    _icc_history="$5"
    _icc_artifacts="$6"
    parallel_head_load "$_icc_branch" || return 1
    _icc_head="$PARALLEL_HEAD_ID"
    _icc_worktree="$PARALLEL_WORKTREE"
    _icc_base="$PARALLEL_BASE_REF"
    _icc_pack="$(hydra_new_id pack "$_icc_head|context")" || return 1
    _icc_root="$PARALLEL_HEAD_DIR/context-packs"
    _icc_lock="context_${_icc_head}"
    acquire_lock "$_icc_lock" "typed context pack" "$_icc_head" || return 1
    mkdir -p "$_icc_root" || { release_lock "$_icc_lock"; return 1; }
    _icc_tmp="$(mktemp -d "$_icc_root/.pack.XXXXXX")" || {
        release_lock "$_icc_lock"; return 1;
    }
    if ! state_v2_write_scalar "$_icc_tmp/pack-id" "$_icc_pack" || \
       ! state_v2_write_scalar "$_icc_tmp/schema-version" 1 || \
       ! state_v2_write_scalar "$_icc_tmp/head-id" "$_icc_head" || \
       ! state_v2_write_scalar "$_icc_tmp/created-at" "$(date +%s)"; then
        rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1
    fi

    : > "$_icc_tmp/manifest.tsv"
    if [ -n "$_icc_files" ]; then
        while IFS= read -r _icc_file; do
            [ -n "$_icc_file" ] || continue
            parallel_validate_path_pattern "$_icc_file" || { rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1; }
            integration_sensitive_path "$_icc_file" && {
                echo "Error: refusing sensitive manifest path '$_icc_file'" >&2
                rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1
            }
            [ -f "$_icc_worktree/$_icc_file" ] || { rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1; }
            _icc_bytes="$(LC_ALL=C wc -c < "$_icc_worktree/$_icc_file" | tr -d ' ')"
            _icc_hash="$(git hash-object "$_icc_worktree/$_icc_file")"
            printf 'file\t%s\t%s\t%s\n' "$_icc_file" "$_icc_bytes" "$_icc_hash" >> "$_icc_tmp/manifest.tsv"
        done <<EOF
$_icc_files
EOF
    fi
    if [ "$_icc_diff" -eq 1 ]; then
        _icc_changed="$(parallel_changed_files "$_icc_worktree" "$_icc_base")" || {
            rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1;
        }
        while IFS= read -r _icc_path; do
            [ -n "$_icc_path" ] || continue
            integration_sensitive_path "$_icc_path" && {
                echo "Error: refusing diff containing sensitive path '$_icc_path'" >&2
                rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1
            }
        done <<EOF
$_icc_changed
EOF
        git -C "$_icc_worktree" diff --binary "$_icc_base" -- > "$_icc_tmp/diff.patch" || {
            rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1;
        }
        printf 'diff\tdiff.patch\t%s\t%s\n' \
            "$(LC_ALL=C wc -c < "$_icc_tmp/diff.patch" | tr -d ' ')" \
            "$(git hash-object "$_icc_tmp/diff.patch")" >> "$_icc_tmp/manifest.tsv"
    fi
    if [ -n "$_icc_notes" ]; then
        state_v2_write_text "$_icc_tmp/notes.txt" "$_icc_notes
" || { rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1; }
        printf 'notes\tnotes.txt\t%s\t%s\n' \
            "$(LC_ALL=C wc -c < "$_icc_tmp/notes.txt" | tr -d ' ')" \
            "$(git hash-object "$_icc_tmp/notes.txt")" >> "$_icc_tmp/manifest.tsv"
    fi
    case "$_icc_history" in ''|*[!0-9]*) rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1 ;; esac
    if [ "$_icc_history" -gt 0 ]; then
        [ "$_icc_history" -le 100 ] || { rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1; }
        git -C "$_icc_worktree" log -n "$_icc_history" --format='%H%x09%aI%x09%s' > "$_icc_tmp/history.tsv" || {
            rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1;
        }
        printf 'history\thistory.tsv\t%s\t%s\n' \
            "$(LC_ALL=C wc -c < "$_icc_tmp/history.tsv" | tr -d ' ')" \
            "$(git hash-object "$_icc_tmp/history.tsv")" >> "$_icc_tmp/manifest.tsv"
    fi
    if [ -n "$_icc_artifacts" ]; then
        : > "$_icc_tmp/artifacts.tsv"
        while IFS= read -r _icc_artifact; do
            [ -n "$_icc_artifact" ] || continue
            case "$_icc_artifact" in *'	'*|*'
'*) rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1 ;; esac
            if [ -f "$_icc_artifact" ]; then
                printf '%s\t%s\t%s\n' "$_icc_artifact" \
                    "$(LC_ALL=C wc -c < "$_icc_artifact" | tr -d ' ')" \
                    "$(git hash-object "$_icc_artifact")" >> "$_icc_tmp/artifacts.tsv"
            else
                printf '%s\t-\t-\n' "$_icc_artifact" >> "$_icc_tmp/artifacts.tsv"
            fi
        done <<EOF
$_icc_artifacts
EOF
        printf 'artifact-references\tartifacts.tsv\t%s\t%s\n' \
            "$(LC_ALL=C wc -c < "$_icc_tmp/artifacts.tsv" | tr -d ' ')" \
            "$(git hash-object "$_icc_tmp/artifacts.tsv")" >> "$_icc_tmp/manifest.tsv"
    fi
    if ! mv "$_icc_tmp" "$_icc_root/$_icc_pack"; then
        rm -rf "$_icc_tmp"; release_lock "$_icc_lock"; return 1
    fi
    release_lock "$_icc_lock"
    printf '%s\n' "$_icc_root/$_icc_pack"
}

integration_release_locks() {
    for _irl_lock in "$@"; do
        [ -z "$_irl_lock" ] || release_lock "$_irl_lock"
    done
}

# Caller holds the per-head integration lock.
integration_archive_locked() {
    _ia_branch="$1"
    _ia_kind="$2"
    _ia_source="$3"
    parallel_head_load "$_ia_branch" || return 1
    _ia_run="$(hydra_new_id run "$PARALLEL_HEAD_ID|$_ia_kind")" || return 1
    _ia_root="$PARALLEL_HEAD_DIR/archives/$_ia_kind/$_ia_run"
    mkdir -p "$_ia_root" || return 1
    if ! state_v2_write_scalar "$_ia_root/run-id" "$_ia_run" || \
       ! state_v2_write_scalar "$_ia_root/pre-head" "$(git -C "$PARALLEL_WORKTREE" rev-parse HEAD)" || \
       ! state_v2_write_scalar "$_ia_root/source" "$_ia_source" || \
       ! state_v2_write_scalar "$_ia_root/created-at" "$(date +%s)" || \
       ! git -C "$PARALLEL_WORKTREE" bundle create "$_ia_root/pre-operation.bundle" HEAD >/dev/null 2>&1; then
        return 1
    fi
    INTEGRATION_ARCHIVE_DIR="$_ia_root"
    INTEGRATION_RUN_ID="$_ia_run"
    export INTEGRATION_ARCHIVE_DIR INTEGRATION_RUN_ID
}

integration_merge_dry_run() {
    _imdr_worktree="$1"
    _imdr_left="$2"
    _imdr_right="$3"
    _imdr_base="$(git -C "$_imdr_worktree" merge-base "$_imdr_left" "$_imdr_right")" || return 1
    _imdr_output="$(git -C "$_imdr_worktree" merge-tree "$_imdr_base" "$_imdr_left" "$_imdr_right")" || return 1
    if printf '%s\n' "$_imdr_output" | grep -E '(<<<<<<<|changed in both|added in both)' >/dev/null 2>&1; then
        echo "Merge simulation predicts a conflict"
        return 1
    fi
    echo "Merge simulation completed without a predicted conflict"
}

integration_sync() {
    _is_branch="$1"
    _is_source="$2"
    _is_gate="$3"
    _is_dry="$4"
    parallel_head_load "$_is_branch" || return 1
    _is_worktree="$PARALLEL_WORKTREE"
    _is_integration_lock="integration_${PARALLEL_HEAD_ID}"
    _is_gate_lock="gate_${PARALLEL_HEAD_ID}_${_is_gate}"
    acquire_lock "$_is_integration_lock" "sync operation" "$PARALLEL_HEAD_ID" || return 1
    acquire_lock "$_is_gate_lock" "sync gate barrier" "$PARALLEL_HEAD_ID" || {
        release_lock "$_is_integration_lock"
        return 1
    }
    integration_require_clean "$_is_worktree" "head '$_is_branch'" || {
        integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
        return 1
    }
    integration_gate_approved "$_is_branch" "$_is_gate" || {
        echo "Error: gate '$_is_gate' is not both passing and explicitly approved" >&2
        integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
        return 1
    }
    _is_source_commit="$(git -C "$_is_worktree" rev-parse --verify "$_is_source^{commit}" 2>/dev/null)" || {
        echo "Error: source ref '$_is_source' is unavailable" >&2
        integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
        return 1
    }
    if [ "$_is_dry" -eq 1 ]; then
        integration_merge_dry_run "$_is_worktree" HEAD "$_is_source_commit"
        _is_status=$?
        integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
        return "$_is_status"
    fi
    if ! integration_require_clean "$_is_worktree" "head '$_is_branch'" || \
       ! integration_gate_approved "$_is_branch" "$_is_gate"; then
        echo "Error: sync preflight changed before merge; nothing was changed" >&2
        integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
        return 1
    fi
    integration_archive_locked "$_is_branch" sync "$_is_source_commit" || {
        integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
        return 1
    }
    _is_pre="$(git -C "$_is_worktree" rev-parse HEAD)"
    if git -C "$_is_worktree" merge --no-edit "$_is_source_commit" \
        > "$INTEGRATION_ARCHIVE_DIR/merge.stdout" 2> "$INTEGRATION_ARCHIVE_DIR/merge.stderr"; then
        if ! state_v2_write_scalar "$PARALLEL_HEAD_DIR/base-ref" "$_is_source_commit" || \
           ! state_v2_write_scalar "$INTEGRATION_ARCHIVE_DIR/result" success; then
            integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
            echo "Error: sync merged but durable result recording requires recovery; archive: $INTEGRATION_ARCHIVE_DIR" >&2
            return 1
        fi
        integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
        printf '%s\n' "$INTEGRATION_RUN_ID"
        return 0
    fi
    git -C "$_is_worktree" merge --abort >/dev/null 2>&1 || true
    if [ "$(git -C "$_is_worktree" rev-parse HEAD)" != "$_is_pre" ] || \
       [ -n "$(git -C "$_is_worktree" status --porcelain=v1)" ]; then
        state_v2_write_scalar "$INTEGRATION_ARCHIVE_DIR/result" recovery-required || true
        integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
        echo "Error: sync failed and automatic recovery is incomplete; archive: $INTEGRATION_ARCHIVE_DIR" >&2
        return 1
    fi
    state_v2_write_scalar "$INTEGRATION_ARCHIVE_DIR/result" recovered || true
    integration_release_locks "$_is_gate_lock" "$_is_integration_lock"
    echo "Error: sync conflict was aborted; head restored; archive: $INTEGRATION_ARCHIVE_DIR" >&2
    return 1
}

integration_land() {
    _il_branch="$1"
    _il_target="$2"
    _il_gate="$3"
    _il_dry="$4"
    _il_keep="$5"
    parallel_head_load "$_il_branch" || return 1
    _il_source_worktree="$PARALLEL_WORKTREE"
    _il_target_worktree="$(get_repo_root)" || return 1
    _il_integration_lock="integration_${PARALLEL_HEAD_ID}"
    _il_target_lock="integration_target_${LIFECYCLE_PROJECT_ID}"
    _il_gate_lock="gate_${PARALLEL_HEAD_ID}_${_il_gate}"
    acquire_lock "$_il_integration_lock" "land source operation" "$PARALLEL_HEAD_ID" || return 1
    acquire_lock "$_il_target_lock" "land target operation" "$PARALLEL_HEAD_ID" || {
        release_lock "$_il_integration_lock"
        return 1
    }
    acquire_lock "$_il_gate_lock" "land gate barrier" "$PARALLEL_HEAD_ID" || {
        integration_release_locks "$_il_target_lock" "$_il_integration_lock"
        return 1
    }
    _il_current="$(git -C "$_il_target_worktree" branch --show-current)"
    [ "$_il_current" = "$_il_target" ] || {
        echo "Error: land target '$_il_target' is not the current branch '$_il_current'" >&2
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        return 1
    }
    integration_require_clean "$_il_source_worktree" "source head '$_il_branch'" || {
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        return 1
    }
    integration_require_clean "$_il_target_worktree" "target branch '$_il_target'" || {
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        return 1
    }
    integration_gate_approved "$_il_branch" "$_il_gate" || {
        echo "Error: gate '$_il_gate' is not both passing and explicitly approved" >&2
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        return 1
    }
    _il_source_ref="$(git -C "$_il_source_worktree" rev-parse HEAD)" || {
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        return 1
    }
    if [ "$_il_dry" -eq 1 ]; then
        integration_merge_dry_run "$_il_target_worktree" HEAD "$_il_source_ref"
        _il_status=$?
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        return "$_il_status"
    fi
    _il_current="$(git -C "$_il_target_worktree" branch --show-current)"
    if [ "$_il_current" != "$_il_target" ] || \
       ! integration_require_clean "$_il_source_worktree" "source head '$_il_branch'" || \
       ! integration_require_clean "$_il_target_worktree" "target branch '$_il_target'" || \
       ! integration_gate_approved "$_il_branch" "$_il_gate" || \
       [ "$(git -C "$_il_source_worktree" rev-parse HEAD)" != "$_il_source_ref" ]; then
        echo "Error: land preflight changed before merge; nothing was changed" >&2
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        return 1
    fi
    integration_archive_locked "$_il_branch" land "$_il_target" || {
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        return 1
    }
    _il_pre="$(git -C "$_il_target_worktree" rev-parse HEAD)"
    if ! git -C "$_il_target_worktree" merge --no-ff --no-edit "$_il_source_ref" \
        > "$INTEGRATION_ARCHIVE_DIR/merge.stdout" 2> "$INTEGRATION_ARCHIVE_DIR/merge.stderr"; then
        git -C "$_il_target_worktree" merge --abort >/dev/null 2>&1 || true
        if [ "$(git -C "$_il_target_worktree" rev-parse HEAD)" != "$_il_pre" ] || \
           [ -n "$(git -C "$_il_target_worktree" status --porcelain=v1)" ]; then
            state_v2_write_scalar "$INTEGRATION_ARCHIVE_DIR/result" recovery-required || true
            integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
            echo "Error: land failed and automatic recovery is incomplete; archive: $INTEGRATION_ARCHIVE_DIR" >&2
            return 1
        fi
        state_v2_write_scalar "$INTEGRATION_ARCHIVE_DIR/result" recovered || true
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        echo "Error: land conflict was aborted; target restored; archive: $INTEGRATION_ARCHIVE_DIR" >&2
        return 1
    fi
    if ! state_v2_write_scalar "$INTEGRATION_ARCHIVE_DIR/result" success; then
        integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
        echo "Error: land merged but durable result recording requires recovery; archive: $INTEGRATION_ARCHIVE_DIR" >&2
        return 1
    fi
    integration_release_locks "$_il_gate_lock" "$_il_target_lock" "$_il_integration_lock"
    if [ "$_il_keep" -eq 0 ]; then
        _il_session="$(sed -n '1p' "$PARALLEL_HEAD_DIR/session")"
        kill_single_head "$_il_branch" "$_il_session" || {
            echo "Error: land succeeded but head teardown requires recovery" >&2
            return 1
        }
    fi
    printf '%s\n' "$INTEGRATION_RUN_ID"
}
