#!/bin/sh
# Retire a finished plan run's verifier heads through the public kill path.
#
# A verifier head is a head one of the run's spawn steps created on which only
# verify-role steps ran. When the run reaches succeeded, failed or cancelled,
# each such head whose evidence is sealed in the run record is removed with
# `hydra kill --protect-untracked` (the branch is kept, as for any kill). A head
# with uncommitted or untracked changes is kept for the user to review. Worker
# heads (work and compose roles) always stay until the user lands or dismisses
# their result. Every outcome is recorded under retirement/<spawn-step>/ and
# as a run event; a failure is reported and never changes the run result.

# Usage: workflow_retire_candidates <run-dir> <roles-file>
# Prints "spawn-step<TAB>branch" for each verifier head of the run.
workflow_retire_candidates() {
    awk -F '\t' -v roles="$2" '
        BEGIN {
            while ((getline line < roles) > 0) {
                split(line, f, "\t")
                if (f[3] == "spawn" || f[4] == "" || f[4] == "-") continue
                if (f[2] == "verify") verifies[f[4]] = 1
                else works[f[4]] = 1
            }
            close(roles)
        }
        $1 == "step" && $3 == "spawn" && $8 != "-" && $8 != "" && ($8 in verifies) && !($8 in works) { print $2 "\t" $8 }
    ' "$1/graph.tsv"
}

# Usage: workflow_retire_sealed <run-dir> <branch>
# Succeeds when no step on the branch is still active and every succeeded step
# on it has sealed its declared outputs in the run record.
workflow_retire_sealed() {
    while IFS="$(printf '\t')" read -r _wrs_tag _wrs_id _wrs_kind _wrs_needs _wrs_retry _wrs_idem _wrs_head _wrs_rest; do
        if [ "$_wrs_tag" != step ] || [ "$_wrs_kind" = spawn ] || [ "$_wrs_head" != "$2" ]; then continue; fi
        _wrs_state="$(sed -n '1p' "$1/steps/$_wrs_id/state" 2>/dev/null)" || true
        case "$_wrs_state" in
            succeeded)
                [ -f "$1/data.json" ] || continue
                _wrs_attempt="$(sed -n '1p' "$1/steps/$_wrs_id/attempts" 2>/dev/null)" || true
                [ -s "$1/steps/$_wrs_id/attempt-$_wrs_attempt/data-seal.json" ] || return 1
                ;;
            failed|cancelled|queued) ;;
            *) return 1 ;;
        esac
    done < "$1/graph.tsv"
    return 0
}

# Usage: workflow_retire_record <run-dir> <spawn-step> <state> <detail> <event>
workflow_retire_record() {
    workflow_atomic_scalar "$1/retirement/$2/detail" "$4" &&
        workflow_atomic_scalar "$1/retirement/$2/state" "$3" || return 1
    workflow_event "$1" "$2" "$5" "$4" || true
}

# Usage: workflow_retire_head <run-dir> <spawn-step> <branch>
workflow_retire_head() {
    _wrh_dir="$1" _wrh_step="$2" _wrh_branch="$3"
    [ ! -f "$_wrh_dir/retirement/$_wrh_step/state" ] || return 0
    mkdir -p "$_wrh_dir/retirement/$_wrh_step" || return 1
    if ! workflow_retire_sealed "$_wrh_dir" "$_wrh_branch"; then
        workflow_retire_record "$_wrh_dir" "$_wrh_step" kept "verifier head $_wrh_branch kept: its evidence is not sealed in the run record" head.retire_skipped
        return 0
    fi
    _wrh_project="$(sed -n '1p' "$_wrh_dir/project-id")"
    _wrh_head="$(state_v2_find_head_by_branch "$_wrh_project" "$_wrh_branch" 2>/dev/null)" || _wrh_head=""
    _wrh_head_dir=""
    [ -z "$_wrh_head" ] || _wrh_head_dir="$(state_v2_head_dir "$_wrh_project" "$_wrh_head" 2>/dev/null)" || _wrh_head_dir=""
    if [ -z "$_wrh_head_dir" ] || [ "$(sed -n '1p' "$_wrh_head_dir/desired-state" 2>/dev/null)" = stopped ]; then
        workflow_retire_record "$_wrh_dir" "$_wrh_step" retired "verifier head $_wrh_branch was already removed" head.retired
        return 0
    fi
    _wrh_worktree="$(sed -n '1p' "$_wrh_head_dir/worktree" 2>/dev/null)" || true
    if [ -d "$_wrh_worktree" ] && [ -n "$(git -C "$_wrh_worktree" status --porcelain 2>/dev/null)" ]; then
        workflow_retire_record "$_wrh_dir" "$_wrh_step" kept \
            "verifier head $_wrh_branch kept: it has uncommitted or untracked changes; review them, then remove it with hydra kill $_wrh_branch" head.retire_skipped
        return 0
    fi
    if HYDRA_NONINTERACTIVE=1 "$HYDRA_BIN_PATH" kill "$_wrh_branch" --protect-untracked \
        < /dev/null > "$_wrh_dir/retirement/$_wrh_step/kill.log" 2>&1; then
        workflow_retire_record "$_wrh_dir" "$_wrh_step" retired \
            "verifier head $_wrh_branch retired after the run; its evidence stays in the run record and branch $_wrh_branch is kept" head.retired
    else
        workflow_retire_record "$_wrh_dir" "$_wrh_step" failed \
            "could not retire verifier head $_wrh_branch; see retirement/$_wrh_step/kill.log in the run record, or remove it with hydra kill $_wrh_branch" head.retire_failed
    fi
    return 0
}

# Usage: workflow_retire_verifiers <run-dir>
workflow_retire_verifiers() (
    _wrv_dir="$1"
    [ -f "$_wrv_dir/compiled.json" ] || exit 0
    case "$(sed -n '1p' "$_wrv_dir/state" 2>/dev/null)" in succeeded|failed|cancelled) ;; *) exit 0 ;; esac
    _wrv_roles="$_wrv_dir/plan-roles.tsv"
    if [ ! -f "$_wrv_roles" ]; then
        _wrv_roles="$(mktemp)" || exit 0
        trap 'rm -f "$_wrv_roles"' EXIT
        workflow_plan_tool roles "$_wrv_dir/compiled.json" > "$_wrv_roles" 2>/dev/null || exit 0
    fi
    workflow_retire_candidates "$_wrv_dir" "$_wrv_roles" | while IFS="$(printf '\t')" read -r _wrv_step _wrv_branch; do
        [ -n "$_wrv_branch" ] || continue
        case "$_wrv_step" in ''|*[!a-z0-9_-]*|[-_0-9]*) continue ;; esac
        workflow_retire_head "$_wrv_dir" "$_wrv_step" "$_wrv_branch" || true
    done
    exit 0
)
