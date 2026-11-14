#!/bin/sh
# Landing a plan's verified result: a local merge of the exact commit the
# plan's checks ran on into the checkout's current branch, never a push.

# Usage: integration_plan_run <branch>
# Prints the directory of the most recent plan run whose spawn step created
# branch.
integration_plan_run() {
    _ipr_dir="$(state_v2_project_dir "$LIFECYCLE_PROJECT_ID")/workflows/runs" || return 1
    for _ipr_run in "$_ipr_dir"/run_*; do
        [ -f "$_ipr_run/compiled.json" ] && [ -f "$_ipr_run/plan-roles.tsv" ] || continue
        awk -F '\t' -v head="$1" '$3 == "spawn" && $4 == head { found = 1 } END { exit !found }' "$_ipr_run/plan-roles.tsv" || continue
        printf '%s\t%s\n' "$(sed -n '1p' "$_ipr_run/created-at" 2>/dev/null || true)" "$_ipr_run"
    done | LC_ALL=C sort | tail -n 1 | cut -f2-
}

# Usage: integration_plan_tracked_clean <worktree>
# The checkout has no staged or unstaged changes to tracked files. Untracked
# files are left to Git, which refuses a merge that would overwrite one.
integration_plan_tracked_clean() {
    git -C "$1" diff --quiet HEAD -- 2>/dev/null && git -C "$1" diff --cached --quiet -- 2>/dev/null
}

# Usage: integration_plan_preflight <branch> <commit> <target> <into>
integration_plan_preflight() {
    _ipp_current="$(git -C "$3" branch --show-current 2>/dev/null || true)"
    if [ "$_ipp_current" != "$4" ]; then
        cli_error land target_changed "the checkout is on '${_ipp_current:-a detached HEAD}', not '$4'; nothing was changed" "switch to $4, or pass --into $_ipp_current"
        return 1
    fi
    if ! integration_plan_tracked_clean "$3"; then
        cli_error land dirty_target "your checkout at $3 has uncommitted changes to tracked files; nothing was changed" 'commit or stash them, then land again'
        return 1
    fi
    _ipp_tip="$(git -C "$3" rev-parse --verify --quiet "refs/heads/$1^{commit}" 2>/dev/null || true)"
    if [ "$_ipp_tip" != "$2" ]; then
        cli_error land worker_moved "branch $1 is at ${_ipp_tip:-nothing} but its checks ran on $2; nothing was changed" \
            "review what changed on $1 since the run; landing takes only the verified commit, so reset $1 to it or run the plan again"
        return 1
    fi
}

# Usage: integration_plan_retire <branch> <keep>
# Removes the worker head after landing, keeping its branch, unless asked not
# to or its worktree holds anything that is not on the branch.
integration_plan_retire() {
    if [ "$2" -eq 1 ]; then printf 'The worker head %s is kept (--keep-head).\n' "$1"; return 0; fi
    _iprt_tree="$PARALLEL_WORKTREE"
    if [ -d "$_iprt_tree" ] && { ! integration_plan_tracked_clean "$_iprt_tree" ||
        [ -n "$(git -C "$_iprt_tree" ls-files --others --exclude-standard 2>/dev/null)" ]; }; then
        printf 'The worker head %s is kept: its worktree has changes that are not on the branch. Remove it with hydra kill %s once they are kept elsewhere.\n' "$1" "$1"
        return 0
    fi
    _iprt_session="$(sed -n '1p' "$PARALLEL_HEAD_DIR/session" 2>/dev/null || true)"
    if kill_protect_untracked=true kill_single_head "$1" "${_iprt_session:--}" >/dev/null 2>&1; then
        printf 'The worker head %s was removed; branch %s is kept.\n' "$1" "$1"
    else
        printf 'The worker head %s could not be removed; remove it with hydra kill %s.\n' "$1" "$1"
    fi
}

# Usage: integration_plan_confirm <branch> <commit> <target> <into>
# Asks on a terminal unless HYDRA_NONINTERACTIVE is set.
integration_plan_confirm() {
    [ -t 0 ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ] || return 0
    _ipc_base="$(git -C "$3" merge-base HEAD "$2" 2>/dev/null || true)"
    _ipc_files="$(git -C "$3" diff --name-only "${_ipc_base:-HEAD}" "$2" 2>/dev/null | grep -c . || true)"
    printf 'Merge %s (commit %.12s, %s file(s) changed, verified by its plan run) into %s at %s? [y/N] ' "$1" "$2" "$_ipc_files" "$4" "$3"
    IFS= read -r _ipc_answer || _ipc_answer=""
    case "$_ipc_answer" in y|Y|yes|YES|Yes) return 0 ;; esac
    echo 'Not landed; nothing was changed.'
    return 1
}

# Usage: integration_plan_merge <branch> <commit> <target> <message>
# Merges under the target lock, archiving the target first; a conflict is
# aborted and the target restored.
integration_plan_merge() {
    integration_archive_locked "$1" land "$2" "$3" || return 1
    _ipm_pre="$(git -C "$3" rev-parse HEAD)"
    if git -C "$3" merge --no-ff -m "$4" "$2" > "$INTEGRATION_ARCHIVE_DIR/merge.stdout" 2> "$INTEGRATION_ARCHIVE_DIR/merge.stderr"; then
        state_v2_write_scalar "$INTEGRATION_ARCHIVE_DIR/result" success || true
        return 0
    fi
    git -C "$3" merge --abort >/dev/null 2>&1 || true
    if [ "$(git -C "$3" rev-parse HEAD)" != "$_ipm_pre" ] || ! integration_plan_tracked_clean "$3"; then
        state_v2_write_scalar "$INTEGRATION_ARCHIVE_DIR/result" recovery-required || true
        cli_error land merge_failed "the merge failed and the checkout could not be restored automatically; archive: $INTEGRATION_ARCHIVE_DIR" 'inspect git status in your checkout'
        return 1
    fi
    state_v2_write_scalar "$INTEGRATION_ARCHIVE_DIR/result" recovered || true
    cli_error land conflict "merging $1 conflicts with $(git -C "$3" branch --show-current); the merge was aborted and your checkout is unchanged" \
        "resolve it yourself with git merge $2, or ask the agent to rebase the work and run the plan again"
    return 1
}

# Usage: integration_land_verified <branch> <into-or-empty> <dry-run> <keep-head>
integration_land_verified() {
    _ilv_branch="$1" _ilv_into="$2" _ilv_dry="$3" _ilv_keep="$4"
    workflow_plan_agent_guard land 'land a plan result' || return 1
    parallel_head_load "$_ilv_branch" || return 1
    _ilv_run="$(integration_plan_run "$_ilv_branch")"
    if [ -z "$_ilv_run" ]; then
        cli_error land no_plan_result "no plan run created $_ilv_branch" "land a worker branch a plan created, or use --gate <name> for a head gate"
        return 1
    fi
    _ilv_tmp="$(mktemp "${TMPDIR:-/tmp}/hydra-land.XXXXXX")" || return 1
    if ! workflow_plan_tool verified-commit "$_ilv_run" "$_ilv_branch" > "$_ilv_tmp"; then
        _load_lib workflow_plan_proposal
        printf 'Error: %s has no verified result to land (run %s):\n' "$_ilv_branch" "$(basename "$_ilv_run")" >&2
        workflow_plan_diagnostics < "$_ilv_tmp" >&2
        rm -f "$_ilv_tmp"
        return 1
    fi
    IFS="$(printf '\t')" read -r _ilv_commit _ilv_plan < "$_ilv_tmp"
    rm -f "$_ilv_tmp"
    _ilv_target="$(sed -n '1p' "$(state_v2_project_dir "$LIFECYCLE_PROJECT_ID")/repo-root" 2>/dev/null || true)"
    [ -d "$_ilv_target" ] || _ilv_target="$(get_repo_root)" || return 1
    [ -n "$_ilv_into" ] || _ilv_into="$(git -C "$_ilv_target" branch --show-current 2>/dev/null || true)"
    [ -n "$_ilv_into" ] || { cli_error land detached_target "your checkout at $_ilv_target is not on a branch" 'switch to the branch to land into'; return 1; }
    _ilv_locks="integration_${PARALLEL_HEAD_ID} integration_target_${LIFECYCLE_PROJECT_ID}"
    acquire_lock "integration_${PARALLEL_HEAD_ID}" 'land source operation' "$PARALLEL_HEAD_ID" || return 1
    acquire_lock "integration_target_${LIFECYCLE_PROJECT_ID}" 'land target operation' "$PARALLEL_HEAD_ID" || { release_lock "integration_${PARALLEL_HEAD_ID}"; return 1; }
    # shellcheck disable=SC2086 # Lock names are fixed words.
    if ! integration_plan_preflight "$_ilv_branch" "$_ilv_commit" "$_ilv_target" "$_ilv_into"; then integration_release_locks $_ilv_locks; return 1; fi
    if [ "$_ilv_dry" -eq 1 ]; then
        integration_merge_dry_run "$_ilv_target" HEAD "$_ilv_commit"
        _ilv_status=$?
        # shellcheck disable=SC2086
        integration_release_locks $_ilv_locks
        return "$_ilv_status"
    fi
    # shellcheck disable=SC2086
    if ! integration_plan_confirm "$_ilv_branch" "$_ilv_commit" "$_ilv_target" "$_ilv_into" ||
        ! integration_plan_preflight "$_ilv_branch" "$_ilv_commit" "$_ilv_target" "$_ilv_into" ||
        ! integration_plan_merge "$_ilv_branch" "$_ilv_commit" "$_ilv_target" "Land $_ilv_branch (Hydra plan $_ilv_plan, run $(basename "$_ilv_run"))"; then
        integration_release_locks $_ilv_locks
        return 1
    fi
    _ilv_merge="$(git -C "$_ilv_target" rev-parse HEAD)"
    # The run records where its result landed; the review reports it.
    state_v2_write_scalar "$_ilv_run/landed" "$(printf '%s\t%s\t%s' "$_ilv_commit" "$_ilv_into" "$_ilv_merge")" || true
    # shellcheck disable=SC2086
    integration_release_locks $_ilv_locks
    printf 'Landed %s into %s: merge commit %.12s of verified commit %.12s. Nothing was pushed.\n' "$_ilv_branch" "$_ilv_into" "$_ilv_merge" "$_ilv_commit"
    integration_plan_retire "$_ilv_branch" "$_ilv_keep"
}
