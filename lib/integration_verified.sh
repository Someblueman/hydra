#!/bin/sh
# Verified multi-head integration assembly and gate execution.

# Verified multi-head integration. The durable report is authoritative for every
# mutating follow-up; callers never reconstruct candidate or target bindings.
integration_verified_root() {
    _ivr_project="$(hydra_get_project_id)" || return 1
    _ivr_dir="$(state_v2_project_dir "$_ivr_project")" || return 1
    printf '%s/integrations\n' "$_ivr_dir"
}

integration_pid_alive() {
    case "$1" in ''|*[!0-9]*) return 1 ;; esac
    kill -0 "$1" 2>/dev/null
}

integration_verified_ref() {
    case "$1" in
        refs/heads/*) _ivref="$1" ;;
        *) _ivref="refs/heads/$1" ;;
    esac
    git check-ref-format "$_ivref" >/dev/null 2>&1 || return 1
    printf '%s\n' "$_ivref"
}

integration_verified_select() {
    _ivs_selector="$1"
    _ivs_output="$2"
    _ivs_project="$3"
    _ivs_project_dir="$4"
    : > "$_ivs_output"
    _ivs_workflow="$_ivs_project_dir/workflows/runs/$_ivs_selector"
    if [ -d "$_ivs_workflow" ]; then
        [ "$(sed -n '1p' "$_ivs_workflow/state" 2>/dev/null || true)" = succeeded ] || {
            echo "Error: workflow run '$_ivs_selector' is not completed successfully" >&2
            return 1
        }
        awk -F '\t' '$1=="step" && $3=="spawn" && $8!="-" {print $8}' "$_ivs_workflow/graph.tsv"
    else
        for _ivs_head_dir in "$_ivs_project_dir"/heads/head_*; do
            [ -d "$_ivs_head_dir" ] || continue
            [ "$(sed -n '1p' "$_ivs_head_dir/group" 2>/dev/null || true)" = "$_ivs_selector" ] || continue
            printf '%s\t%s\n' "$(sed -n '1p' "$_ivs_head_dir/created-at")" "$(sed -n '1p' "$_ivs_head_dir/branch")"
        done | LC_ALL=C sort -k1,1n -k2,2 | cut -f2-
    fi | while IFS= read -r _ivs_branch; do
        [ -n "$_ivs_branch" ] || continue
        _ivs_head="$(state_v2_find_head_by_branch "$_ivs_project" "$_ivs_branch")" || exit 1
        _ivs_dir="$_ivs_project_dir/heads/$_ivs_head"
        _ivs_worktree="$(sed -n '1p' "$_ivs_dir/worktree")"
        [ -d "$_ivs_worktree" ] || exit 1
        integration_require_clean "$_ivs_worktree" "candidate '$_ivs_branch'" || exit 1
        _ivs_commit="$(git -C "$_ivs_worktree" rev-parse HEAD)" || exit 1
        _ivs_instance="$(sed -n '1p' "$_ivs_dir/current-instance")"
        _ivs_outcome="$(sed -n '1p' "$_ivs_dir/instances/$_ivs_instance/declared-outcome" 2>/dev/null || true)"
        case "$_ivs_outcome" in
            done|complete) ;;
            *) echo "Error: candidate '$_ivs_branch' is not completed" >&2; exit 1 ;;
        esac
        printf '%s\t%s\t%s\t%s\n' "$_ivs_head" "$_ivs_branch" "$_ivs_commit" "$(sed -n '1p' "$_ivs_dir/base-ref")"
    done > "$_ivs_output"
    [ -s "$_ivs_output" ] || {
        echo "Error: run or group '$_ivs_selector' has no completed candidates" >&2
        return 1
    }
}

integration_verified_preview() {
    _ivp_selector="$1"
    _ivp_base_ref="$2"
    _ivp_target_name="$3"
    _ivp_gates="$4"
    _ivp_mode="${5:-integration}"
    _ivp_project="$(hydra_get_project_id)" || return 1
    _ivp_project_dir="$(state_v2_project_dir "$_ivp_project")" || return 1
    _ivp_base="$(git rev-parse --verify "$_ivp_base_ref^{commit}" 2>/dev/null)" || {
        echo "Error: base ref is unavailable" >&2
        return 1
    }
    _ivp_target_ref="$(integration_verified_ref "$_ivp_target_name")" || {
        echo "Error: target must be an explicit local branch ref" >&2
        return 1
    }
    _ivp_target="$(git rev-parse --verify "$_ivp_target_ref^{commit}" 2>/dev/null)" || {
        echo "Error: target ref is unavailable" >&2
        return 1
    }
    _ivp_tmp="$(mktemp)" || return 1
    if ! integration_verified_select "$_ivp_selector" "$_ivp_tmp" "$_ivp_project" "$_ivp_project_dir"; then
        rm -f "$_ivp_tmp"
        return 1
    fi
    _ivp_plan="$(project_worktree_root "$_ivp_project")/integration-<run-id>"
    printf 'Integration dry-run\n  mode: %s\n  selector: %s\n  base: %s (%s)\n  target ref: %s (%s)\n' \
        "$_ivp_mode" "$_ivp_selector" "$_ivp_base_ref" "$_ivp_base" "$_ivp_target_ref" "$_ivp_target"
    _ivp_n=0
    _ivp_seen=""
    # The manifest is immutable while this loop reads it. ShellCheck cannot
    # distinguish cleanup on an error path from concurrent modification.
    # shellcheck disable=SC2094
    while IFS="$(printf '\t')" read -r _ivp_head _ivp_branch _ivp_commit _ivp_candidate_base; do
        _ivp_n=$((_ivp_n + 1))
        _ivp_mb="$(git merge-base "$_ivp_base" "$_ivp_commit")" || { rm -f "$_ivp_tmp"; return 1; }
        printf '  candidate %s: %s %s (recorded-base=%s, merge-base=%s)\n' \
            "$_ivp_n" "$_ivp_branch" "$_ivp_commit" "$_ivp_candidate_base" "$_ivp_mb"
        if [ "$_ivp_mode" = train ] && [ -s "$_ivp_gates" ]; then
            sed "s/^/    candidate gate: /" "$_ivp_gates"
        fi
        for _ivp_claim_dir in "$_ivp_project_dir"/claims/claim_*; do
            [ -d "$_ivp_claim_dir" ] || continue
            [ "$(sed -n '1p' "$_ivp_claim_dir/owner-head")" = "$_ivp_head" ] || continue
            printf '    claim: %s (%s)\n' "$(sed -n '1p' "$_ivp_claim_dir/path-pattern")" "$(sed -n '1p' "$_ivp_claim_dir/access")"
        done
        _ivp_paths="$(git diff --name-only "$_ivp_base" "$_ivp_commit")"
        if [ -n "$_ivp_seen" ]; then
            _ivp_overlap="$(printf '%s\n%s\n' "$_ivp_seen" "$_ivp_paths" | LC_ALL=C sort | uniq -d)"
            while IFS= read -r _ivp_path; do
                [ -z "$_ivp_path" ] || printf '    overlap: %s (path overlap is not proof of conflict)\n' "$_ivp_path"
            done <<EOF
$_ivp_overlap
EOF
            # Candidate commit IDs cannot contain whitespace.
            # shellcheck disable=SC2013
            for _ivp_prior in $(cut -f3 "$_ivp_tmp"); do
                [ "$_ivp_prior" = "$_ivp_commit" ] && break
                if ! git merge-tree --write-tree "$_ivp_prior" "$_ivp_commit" >/dev/null 2>&1; then
                    printf '    predicted conflict: %s with %s\n' "$_ivp_commit" "$_ivp_prior"
                fi
            done
        fi
        _ivp_seen="$_ivp_seen
$_ivp_paths"
    done < "$_ivp_tmp"
    if [ "$_ivp_mode" != train ]; then
        if [ -s "$_ivp_gates" ]; then
            sed 's/^/  required gate: /' "$_ivp_gates"
        else
            printf '  required gates: none configured\n'
        fi
    fi
    printf '  planned integration worktree: %s\n' "$_ivp_plan"
    printf 'No worktrees or refs were created or changed.\n'
    rm -f "$_ivp_tmp"
}

integration_verified_record_failure() {
    _ivrf_report="$1"
    _ivrf_state="$2"
    _ivrf_class="$3"
    _ivrf_candidate="$4"
    _ivrf_gate="$5"
    _ivrf_recovery="$6"
    state_v2_write_scalar "$_ivrf_report/state" "$_ivrf_state"
    state_v2_write_scalar "$_ivrf_report/failure-class" "$_ivrf_class"
    [ -z "$_ivrf_candidate" ] || state_v2_write_scalar "$_ivrf_report/failed-candidate" "$_ivrf_candidate"
    [ -z "$_ivrf_gate" ] || state_v2_write_scalar "$_ivrf_report/failed-gate" "$_ivrf_gate"
    state_v2_write_scalar "$_ivrf_report/expected-target" "$(sed -n '1p' "$_ivrf_report/target-commit")"
    state_v2_write_scalar "$_ivrf_report/observed-target" "$(git rev-parse "$(sed -n '1p' "$_ivrf_report/target-ref")" 2>/dev/null || printf unavailable)"
    state_v2_write_scalar "$_ivrf_report/report-location" "$_ivrf_report"
    state_v2_write_scalar "$_ivrf_report/recovery-action" "$_ivrf_recovery"
    state_v2_write_scalar "$_ivrf_report/completed-at" "$(date +%s)"
}

integration_verified_interrupt() {
    _ivi_report="$1"
    _ivi_lock="$2"
    _ivi_run="$3"
    _ivi_child="$(sed -n '1p' "$_ivi_report/active-child" 2>/dev/null || true)"
    if integration_pid_alive "$_ivi_child"; then
        operations_signal_tree "$_ivi_child" TERM 2>/dev/null || kill -TERM "$_ivi_child" 2>/dev/null || true
    fi
    _ivi_worktree="$(sed -n '1p' "$_ivi_report/worktree" 2>/dev/null || true)"
    if [ -n "$_ivi_worktree" ] && git -C "$_ivi_worktree" rev-parse --verify MERGE_HEAD >/dev/null 2>&1; then
        git -C "$_ivi_worktree" merge --abort >/dev/null 2>&1 || true
    fi
    if [ -f "$_ivi_report/cancel-requested" ]; then
        _ivi_state=cancelled
    else
        _ivi_state=interrupted
    fi
    integration_verified_record_failure "$_ivi_report" "$_ivi_state" interruption "$(sed -n '1p' "$_ivi_report/current-candidate" 2>/dev/null || true)" "" "hydra integrate resume $_ivi_run"
    release_lock "$_ivi_lock"
}

integration_verified_run_gates() {
    _ivg_report="$1"
    _ivg_worktree="$2"
    _ivg_candidate_index="$3"
    _ivg_candidate_branch="$4"
    _ivg_gate_n=0
    while IFS= read -r _ivg_gate; do
        [ -n "$_ivg_gate" ] || continue
        _ivg_gate_n=$((_ivg_gate_n + 1))
        _ivg_attempts_file="$_ivg_report/gate-${_ivg_candidate_index}-${_ivg_gate_n}-attempts"
        _ivg_attempt="$(sed -n '1p' "$_ivg_attempts_file" 2>/dev/null || printf 0)"
        _ivg_attempt=$((_ivg_attempt + 1))
        state_v2_write_scalar "$_ivg_attempts_file" "$_ivg_attempt"
        _ivg_dir="$_ivg_report/gate-${_ivg_candidate_index}-${_ivg_gate_n}-attempt-$_ivg_attempt"
        mkdir -p "$_ivg_dir" || return 1
        state_v2_write_scalar "$_ivg_dir/command" "$_ivg_gate"
        state_v2_write_scalar "$_ivg_dir/started-at" "$(date +%s)"
        (cd "$_ivg_worktree" && sh -c "$_ivg_gate") >"$_ivg_dir/stdout" 2>"$_ivg_dir/stderr" &
        _ivg_child=$!
        state_v2_write_scalar "$_ivg_report/active-child" "$_ivg_child"
        if wait "$_ivg_child"; then _ivg_status=0; else _ivg_status=$?; fi
        rm -f "$_ivg_report/active-child"
        state_v2_write_scalar "$_ivg_dir/exit-status" "$_ivg_status"
        state_v2_write_scalar "$_ivg_dir/completed-at" "$(date +%s)"
        if [ "$_ivg_status" -ne 0 ]; then
            integration_verified_record_failure "$_ivg_report" verification-failed gate "$_ivg_candidate_branch" "$_ivg_gate_n" "hydra integrate resume $(sed -n '1p' "$_ivg_report/run-id")"
            return 1
        fi
    done < "$_ivg_report/gates.argv"
}

integration_verified_continue() {
    _ivct_report="$1"
    _ivct_project="$2"
    _ivct_project_dir="$3"
    _ivct_run="$(sed -n '1p' "$_ivct_report/run-id")"
    _ivct_mode="$(sed -n '1p' "$_ivct_report/mode")"
    _ivct_worktree="$(sed -n '1p' "$_ivct_report/worktree")"
    _ivct_target_ref="$(sed -n '1p' "$_ivct_report/target-ref")"
    _ivct_target="$(sed -n '1p' "$_ivct_report/target-commit")"
    _ivct_completed="$(sed -n '1p' "$_ivct_report/completed-candidates")"
    _ivct_stage="$(sed -n '1p' "$_ivct_report/progress-stage")"

    if [ "$_ivct_mode" = train ] && [ "$_ivct_stage" = gates ]; then
        _ivct_line="$(sed -n "${_ivct_completed}p" "$_ivct_report/candidates.tsv")"
        IFS="$(printf '\t')" read -r _ivct_head _ivct_branch _ivct_commit _ivct_base <<EOF
$_ivct_line
EOF
        integration_verified_run_gates "$_ivct_report" "$_ivct_worktree" "$_ivct_completed" "$_ivct_branch" || return 1
        state_v2_write_scalar "$_ivct_report/progress-stage" merge
    fi

    _ivct_total="$(awk 'END { print NR }' "$_ivct_report/candidates.tsv")"
    _ivct_index=$((_ivct_completed + 1))
    while [ "$_ivct_index" -le "$_ivct_total" ]; do
        _ivct_line="$(sed -n "${_ivct_index}p" "$_ivct_report/candidates.tsv")"
        IFS="$(printf '\t')" read -r _ivct_head _ivct_branch _ivct_commit _ivct_base <<EOF
$_ivct_line
EOF
        state_v2_write_scalar "$_ivct_report/current-candidate" "$_ivct_branch"
        state_v2_write_scalar "$_ivct_report/progress-stage" merge
        _ivct_head_dir="$_ivct_project_dir/heads/$_ivct_head"
        _ivct_candidate_worktree="$(sed -n '1p' "$_ivct_head_dir/worktree")"
        if [ "$(git -C "$_ivct_candidate_worktree" rev-parse HEAD 2>/dev/null || true)" != "$_ivct_commit" ]; then
            integration_verified_record_failure "$_ivct_report" stale-candidate stale-candidate "$_ivct_branch" "" "hydra integrate cleanup $_ivct_run --apply"
            return 1
        fi
        _ivct_observed="$(git rev-parse "$_ivct_target_ref" 2>/dev/null || printf unavailable)"
        if [ "$_ivct_observed" != "$_ivct_target" ]; then
            integration_verified_record_failure "$_ivct_report" stale-target stale-target "$_ivct_branch" "" "hydra integrate cleanup $_ivct_run --apply"
            return 1
        fi
        _ivct_available="$(df -Pk "$_ivct_worktree" 2>/dev/null | awk 'END { print $4 }')"
        case "$_ivct_available" in ''|*[!0-9]*) _ivct_available=0 ;; esac
        if [ "$_ivct_available" -lt 1024 ]; then
            integration_verified_record_failure "$_ivct_report" resource-refused disk "$_ivct_branch" "" "hydra integrate resume $_ivct_run"
            return 1
        fi
        git -C "$_ivct_worktree" merge --no-ff --no-edit "$_ivct_commit" >"$_ivct_report/merge-$_ivct_index.stdout" 2>"$_ivct_report/merge-$_ivct_index.stderr" &
        _ivct_child=$!
        state_v2_write_scalar "$_ivct_report/active-child" "$_ivct_child"
        if wait "$_ivct_child"; then _ivct_status=0; else _ivct_status=$?; fi
        rm -f "$_ivct_report/active-child"
        if [ "$_ivct_status" -ne 0 ]; then
            git -C "$_ivct_worktree" diff --name-only --diff-filter=U > "$_ivct_report/observed-conflicts" || true
            git -C "$_ivct_worktree" merge --abort >/dev/null 2>&1 || true
            integration_verified_record_failure "$_ivct_report" observed-conflict conflict "$_ivct_branch" "" "hydra integrate cleanup $_ivct_run --apply"
            echo "Error: observed conflict at candidate $_ivct_branch; preserved report and worktree: $_ivct_run" >&2
            return 1
        fi
        _ivct_completed="$_ivct_index"
        state_v2_write_scalar "$_ivct_report/completed-candidates" "$_ivct_completed"
        state_v2_write_scalar "$_ivct_report/last-result-commit" "$(git -C "$_ivct_worktree" rev-parse HEAD)"
        if [ "$_ivct_mode" = train ]; then
            state_v2_write_scalar "$_ivct_report/progress-stage" gates
            integration_verified_run_gates "$_ivct_report" "$_ivct_worktree" "$_ivct_index" "$_ivct_branch" || return 1
        fi
        state_v2_write_scalar "$_ivct_report/progress-stage" merge
        _ivct_index=$((_ivct_index + 1))
    done
    if [ "$_ivct_mode" != train ]; then
        state_v2_write_scalar "$_ivct_report/progress-stage" gates
        integration_verified_run_gates "$_ivct_report" "$_ivct_worktree" 0 all-candidates || return 1
    fi
    if [ "$(git rev-parse "$_ivct_target_ref" 2>/dev/null || printf unavailable)" != "$_ivct_target" ]; then
        integration_verified_record_failure "$_ivct_report" stale-target stale-target final "" "hydra integrate cleanup $_ivct_run --apply"
        return 1
    fi
    _ivct_result="$(git -C "$_ivct_worktree" rev-parse HEAD)"
    state_v2_write_scalar "$_ivct_report/result-commit" "$_ivct_result"
    state_v2_write_scalar "$_ivct_report/result-tree" "$(git -C "$_ivct_worktree" rev-parse 'HEAD^{tree}')"
    state_v2_write_scalar "$_ivct_report/verified-at" "$(date +%s)"
    state_v2_write_scalar "$_ivct_report/recovery-action" "hydra integrate cleanup $_ivct_run --apply"
    state_v2_write_scalar "$_ivct_report/completed-at" "$(date +%s)"
    state_v2_write_scalar "$_ivct_report/state" verified
}

integration_verified_execute() {
    _ive_selector="$1"
    _ive_base_ref="$2"
    _ive_target_name="$3"
    _ive_gates="$4"
    _ive_mode="${5:-integration}"
    _ive_project="$(hydra_get_project_id)" || return 1
    _ive_project_dir="$(state_v2_project_dir "$_ive_project")" || return 1
    _ive_lock="integration_target_${_ive_project}"
    acquire_lock "$_ive_lock" "verified integration assembly" || return 1
    _ive_base="$(git rev-parse --verify "$_ive_base_ref^{commit}" 2>/dev/null)" || { release_lock "$_ive_lock"; return 1; }
    _ive_target_ref="$(integration_verified_ref "$_ive_target_name")" || { release_lock "$_ive_lock"; return 1; }
    _ive_target="$(git rev-parse --verify "$_ive_target_ref^{commit}" 2>/dev/null)" || { release_lock "$_ive_lock"; return 1; }
    [ "$_ive_target" = "$_ive_base" ] || { release_lock "$_ive_lock"; echo "Error: recorded base must equal the current target commit" >&2; return 1; }
    _ive_root="$(integration_verified_root)" || { release_lock "$_ive_lock"; return 1; }
    mkdir -p "$_ive_root" || { release_lock "$_ive_lock"; return 1; }
    _ive_run="$(hydra_new_id run "$_ive_project|integrate|$_ive_selector")" || { release_lock "$_ive_lock"; return 1; }
    _ive_report="$_ive_root/$_ive_run"
    mkdir -p "$_ive_report" || { release_lock "$_ive_lock"; return 1; }
    _ive_candidates="$_ive_report/candidates.tsv"
    if ! integration_verified_select "$_ive_selector" "$_ive_candidates" "$_ive_project" "$_ive_project_dir"; then
        rm -rf "$_ive_report"; release_lock "$_ive_lock"; return 1
    fi
    _ive_worktree="$(project_worktree_root "$_ive_project")/integration-$_ive_run"
    state_v2_write_scalar "$_ive_report/run-id" "$_ive_run"
    state_v2_write_scalar "$_ive_report/mode" "$_ive_mode"
    state_v2_write_scalar "$_ive_report/selector" "$_ive_selector"
    state_v2_write_scalar "$_ive_report/base-ref" "$_ive_base_ref"
    state_v2_write_scalar "$_ive_report/base-commit" "$_ive_base"
    state_v2_write_scalar "$_ive_report/target-ref" "$_ive_target_ref"
    state_v2_write_scalar "$_ive_report/target-commit" "$_ive_target"
    state_v2_write_scalar "$_ive_report/worktree" "$_ive_worktree"
    state_v2_write_scalar "$_ive_report/created-at" "$(date +%s)"
    state_v2_write_scalar "$_ive_report/owner-pid" "$$"
    state_v2_write_scalar "$_ive_report/completed-candidates" 0
    state_v2_write_scalar "$_ive_report/progress-stage" merge
    state_v2_write_scalar "$_ive_report/last-result-commit" "$_ive_base"
    cp "$_ive_gates" "$_ive_report/gates.argv"
    {
        printf 'schema_version\t1\nmode\t%s\nselector\t%s\nbase_commit\t%s\ntarget_ref\t%s\ninitial_target_commit\t%s\n' "$_ive_mode" "$_ive_selector" "$_ive_base" "$_ive_target_ref" "$_ive_target"
        awk -F '\t' '{ printf "candidate\t%d\t%s\t%s\t%s\n", NR, $2, $3, $4 }' "$_ive_candidates"
        awk '{ printf "gate\t%d\t%s\n", NR, $0 }' "$_ive_gates"
    } > "$_ive_report/manifest.tsv"
    state_v2_write_scalar "$_ive_report/manifest-hash" "$(git hash-object "$_ive_report/manifest.tsv")"
    state_v2_write_scalar "$_ive_report/candidates-hash" "$(git hash-object "$_ive_report/candidates.tsv")"
    state_v2_write_scalar "$_ive_report/gates-hash" "$(git hash-object "$_ive_report/gates.argv")"
    state_v2_write_scalar "$_ive_report/state" assembling
    printf '%s\n' "$_ive_run"
    trap 'integration_verified_interrupt "$_ive_report" "$_ive_lock" "$_ive_run"; trap - HUP INT TERM; exit 130' HUP INT TERM
    if ! git worktree add --detach "$_ive_worktree" "$_ive_base" >"$_ive_report/worktree.stdout" 2>"$_ive_report/worktree.stderr"; then
        integration_verified_record_failure "$_ive_report" assembly-failed worktree "" "" "hydra integrate cleanup $_ive_run --apply"
        trap - HUP INT TERM; release_lock "$_ive_lock"; return 1
    fi
    if integration_verified_continue "$_ive_report" "$_ive_project" "$_ive_project_dir"; then _ive_status=0; else _ive_status=$?; fi
    trap - HUP INT TERM
    release_lock "$_ive_lock"
    [ "$_ive_status" -eq 0 ] || return "$_ive_status"
}


