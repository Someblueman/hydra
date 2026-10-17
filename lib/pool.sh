#!/bin/sh
# File-backed task pools: sealed bounded expansions, unique lock-based claims,
# retry and repair budgets, cancellation and stale-owner reconciliation.
#
# The pool lock is the one coordinator per pool. Execution never happens here:
# a claimed task is run through the public `hydra workflow run` or `hydra exec`
# commands, so admission and recovery stay on the existing path.
#
# Results are handed to callers through POOL_* variables.
# shellcheck disable=SC2034

pool_valid_name() {
    case "${1:-}" in ''|*[!a-z0-9_-]*|[!a-z]*|*__*|*--*) return 1 ;; esac
    [ "${#1}" -le 64 ]
}

pool_valid_number() {
    case "${1:-}" in ''|*[!0-9]*) return 1 ;; esac
    [ "$1" -ge "$2" ] && [ "$1" -le "$3" ]
}

pool_project() {
    POOL_PROJECT="$(hydra_get_project_id 2>/dev/null)" || {
        cli_error pool not_initialized "Hydra project identity is unavailable" "run hydra init inside the repository"
        return 1
    }
    POOL_PROJECT_DIR="$(state_v2_project_dir "$POOL_PROJECT")" || return 1
    [ -d "$POOL_PROJECT_DIR" ] || {
        cli_error pool not_initialized "Hydra project state is missing" "run hydra init inside the repository"
        return 1
    }
    POOL_ROOT="$POOL_PROJECT_DIR/pools"
    POOL_REPO_ROOT="$(sed -n '1p' "$POOL_PROJECT_DIR/repo-root" 2>/dev/null || true)"
    export POOL_PROJECT POOL_PROJECT_DIR POOL_ROOT POOL_REPO_ROOT
}

pool_read() {
    sed -n '1p' "$1" 2>/dev/null || true
}

pool_write() {
    hydra_private_mkdir "$(dirname "$1")" || return 1
    state_v2_write_scalar "$1" "$2"
}

pool_dir() {
    pool_valid_name "$1" || {
        cli_error pool invalid_input "invalid pool name '$1'" "use lowercase letters, digits, single hyphens or underscores"
        return 1
    }
    [ -d "$POOL_ROOT/$1" ] || {
        cli_error pool not_found "pool '$1' does not exist" "create it with hydra pool create $1"
        return 1
    }
    printf '%s/%s\n' "$POOL_ROOT" "$1"
}

pool_lock_name() {
    printf 'pool_%s_%s\n' "$POOL_PROJECT" "$1"
}

# The coordinator lock. Stale locks of dead local owners are removed first;
# a held lock is waited for briefly and then refused (a failed mutation).
pool_lock() {
    _pl_name="$(pool_lock_name "$1")"
    _pl_tries="${HYDRA_POOL_LOCK_TRIES:-200}"
    _pl_i=0
    while :; do
        try_lock "$_pl_name" "pool $1" && return 0
        remove_stale_lock_dir "$HYDRA_HOME/locks/$_pl_name.lock" 2>/dev/null && continue
        _pl_i=$((_pl_i + 1))
        [ "$_pl_i" -lt "$_pl_tries" ] || break
        sleep 0.05 2>/dev/null || sleep 1
    done
    cli_error pool busy "pool '$1' coordinator lock is held" "retry when the other pool command finishes"
    return 1
}

pool_unlock() {
    release_lock "$(pool_lock_name "$1")"
}

pool_now() {
    date +%s
}

pool_event() {
    _pe_dir="$1" _pe_task="$2" _pe_type="$3" _pe_detail="${4:-}"
    _pe_file="$_pe_dir/events.jsonl"
    hydra_private_touch "$_pe_file" || return 1
    _pe_seq="$(awk 'END { print NR + 1 }' "$_pe_file" 2>/dev/null || printf 1)"
    printf '{"schema_version":1,"sequence":%s,"occurred_at":"%s","pool":"%s","task_id":%s,"type":"%s","detail":"%s"}\n' \
        "$_pe_seq" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(json_escape "$(pool_read "$_pe_dir/pool-id")")" \
        "$(if [ -n "$_pe_task" ]; then printf '"%s"' "$(json_escape "$_pe_task")"; else printf null; fi)" \
        "$(json_escape "$_pe_type")" "$(json_escape "$_pe_detail")" >> "$_pe_file"
}

pool_new_token() {
    _pnt=""
    if [ -r /dev/urandom ]; then
        _pnt="$(head -c 12 /dev/urandom 2>/dev/null | od -An -tx1 | tr -d ' \n')"
    fi
    [ -n "$_pnt" ] || _pnt="$(printf '%s\n' "$1|$(pool_now)|$$" | hydra_hash | cut -c1-24)"
    printf '%s\n' "$_pnt"
}

# Usage: pool_create <name> <max-tasks> <max-outstanding> <max-expansions>
#        <max-artifact-bytes> <max-repair-rounds> <lease-seconds> <retry>
# The bounds manifest is sealed once, at creation, and never changes.
pool_create() {
    _pc_name="$1"
    pool_valid_name "$_pc_name" || {
        cli_error pool invalid_input "invalid pool name '$_pc_name'" "use lowercase letters, digits, single hyphens or underscores"
        return 1
    }
    hydra_private_mkdir "$POOL_ROOT" || return 1
    [ ! -e "$POOL_ROOT/$_pc_name" ] || {
        cli_error pool already_exists "pool '$_pc_name' already exists" "inspect it with hydra pool status $_pc_name"
        return 1
    }
    _pc_tmp="$(mktemp -d "$POOL_ROOT/.pool.XXXXXX")" || return 1
    chmod 700 "$_pc_tmp" 2>/dev/null || true
    _pc_created="$(pool_now)"
    hydra_private_mkdir "$_pc_tmp/tasks" "$_pc_tmp/expansions" || { rm -rf "$_pc_tmp"; return 1; }
    hydra_private_touch "$_pc_tmp/order" "$_pc_tmp/events.jsonl" "$_pc_tmp/manifest.json" || { rm -rf "$_pc_tmp"; return 1; }
    printf '{"schema_version":1,"pool":"%s","project_id":"%s","created_at":%s,"bounds":{"max_tasks":%s,"max_outstanding":%s,"max_expansions":%s,"max_artifact_bytes":%s,"max_repair_rounds":%s,"lease_seconds":%s,"retry":%s}}\n' \
        "$_pc_name" "$POOL_PROJECT" "$_pc_created" "$2" "$3" "$4" "$5" "$6" "$7" "$8" > "$_pc_tmp/manifest.json"
    if ! pool_write "$_pc_tmp/schema-version" 1 ||
        ! pool_write "$_pc_tmp/pool-id" "$_pc_name" ||
        ! pool_write "$_pc_tmp/project-id" "$POOL_PROJECT" ||
        ! pool_write "$_pc_tmp/state" active ||
        ! pool_write "$_pc_tmp/created-at" "$_pc_created" ||
        ! pool_write "$_pc_tmp/manifest-digest" "$(hydra_hash < "$_pc_tmp/manifest.json")" ||
        ! pool_write "$_pc_tmp/max-tasks" "$2" ||
        ! pool_write "$_pc_tmp/max-outstanding" "$3" ||
        ! pool_write "$_pc_tmp/max-expansions" "$4" ||
        ! pool_write "$_pc_tmp/max-artifact-bytes" "$5" ||
        ! pool_write "$_pc_tmp/max-repair-rounds" "$6" ||
        ! pool_write "$_pc_tmp/lease-seconds" "$7" ||
        ! pool_write "$_pc_tmp/retry" "$8" ||
        ! pool_write "$_pc_tmp/task-count" 0 ||
        ! pool_write "$_pc_tmp/expansion-count" 0 ||
        ! pool_write "$_pc_tmp/expansion-open" "" ||
        ! pool_write "$_pc_tmp/artifact-bytes" 0 ||
        ! pool_write "$_pc_tmp/repair-rounds" 0 ||
        ! mv "$_pc_tmp" "$POOL_ROOT/$_pc_name"; then
        rm -rf "$_pc_tmp"
        return 1
    fi
    pool_event "$POOL_ROOT/$_pc_name" "" pool.created "manifest=$(pool_read "$POOL_ROOT/$_pc_name/manifest-digest")"
}

# Usage: pool_add <pool-dir> <task-id|-> <kind> <definition> <idempotent> <retry> [argv...]
# Appends to the open expansion (opening one when needed) within the sealed
# bounds. Sets POOL_TASK_ID. Caller holds the pool lock.
pool_add() {
    _pa_dir="$1" _pa_id="$2" _pa_kind="$3" _pa_definition="$4" _pa_idempotent="$5" _pa_retry="$6"
    shift 6
    [ "$(pool_read "$_pa_dir/state")" = active ] || {
        cli_error pool pool_cancelled "pool is cancelled; it accepts no new tasks" "create a new pool"
        return 1
    }
    _pa_count="$(pool_read "$_pa_dir/task-count")"
    [ "$((_pa_count + 1))" -le "$(pool_read "$_pa_dir/max-tasks")" ] || {
        cli_error pool growth_exhausted "pool already holds its sealed maximum of $(pool_read "$_pa_dir/max-tasks") task(s)" "create a new pool with a larger max-tasks bound"
        return 1
    }
    _pa_bytes=$((${#_pa_definition} + 1))
    for _pa_arg in "$@"; do
        case "$_pa_arg" in *'
'*) cli_error pool invalid_input "argv elements cannot contain newlines" "pass each argument separately"; return 1 ;; esac
        _pa_bytes=$((_pa_bytes + ${#_pa_arg} + 1))
    done
    _pa_total="$(pool_read "$_pa_dir/artifact-bytes")"
    [ "$((_pa_total + _pa_bytes))" -le "$(pool_read "$_pa_dir/max-artifact-bytes")" ] || {
        cli_error pool artifact_bytes_exhausted "task definitions would exceed the sealed artifact byte bound" "create a new pool with a larger max-artifact-bytes bound"
        return 1
    }
    _pa_open="$(pool_read "$_pa_dir/expansion-open")"
    if [ -z "$_pa_open" ]; then
        _pa_open=$(($(pool_read "$_pa_dir/expansion-count") + 1))
        [ "$_pa_open" -le "$(pool_read "$_pa_dir/max-expansions")" ] || {
            cli_error pool growth_exhausted "pool already used its sealed maximum of $(pool_read "$_pa_dir/max-expansions") expansion(s)" "create a new pool with a larger max-expansions bound"
            return 1
        }
        pool_write "$_pa_dir/expansions/$_pa_open/state" open || return 1
        hydra_private_touch "$_pa_dir/expansions/$_pa_open/tasks" || return 1
        pool_write "$_pa_dir/expansion-count" "$_pa_open" || return 1
        pool_write "$_pa_dir/expansion-open" "$_pa_open" || return 1
        pool_event "$_pa_dir" "" expansion.opened "expansion=$_pa_open"
    fi
    _pa_seq=$((_pa_count + 1))
    if [ "$_pa_id" = - ]; then
        _pa_id="$(printf 'task-%04d' "$_pa_seq")"
    fi
    [ ! -e "$_pa_dir/tasks/$_pa_id" ] || {
        cli_error pool already_exists "task '$_pa_id' already exists in this pool" "choose another --id"
        return 1
    }
    _pa_tmp="$(mktemp -d "$_pa_dir/tasks/.task.XXXXXX")" || return 1
    chmod 700 "$_pa_tmp" 2>/dev/null || true
    hydra_private_touch "$_pa_tmp/argv" || { rm -rf "$_pa_tmp"; return 1; }
    for _pa_arg in "$@"; do printf '%s\n' "$_pa_arg" >> "$_pa_tmp/argv"; done
    if ! pool_write "$_pa_tmp/task-id" "$_pa_id" ||
        ! pool_write "$_pa_tmp/sequence" "$_pa_seq" ||
        ! pool_write "$_pa_tmp/state" pending ||
        ! pool_write "$_pa_tmp/kind" "$_pa_kind" ||
        ! pool_write "$_pa_tmp/definition" "$_pa_definition" ||
        ! pool_write "$_pa_tmp/idempotent" "$_pa_idempotent" ||
        ! pool_write "$_pa_tmp/retry" "$_pa_retry" ||
        ! pool_write "$_pa_tmp/attempts" 0 ||
        ! pool_write "$_pa_tmp/expansion" "$_pa_open" ||
        ! pool_write "$_pa_tmp/bytes" "$_pa_bytes" ||
        ! pool_write "$_pa_tmp/created-at" "$(pool_now)" ||
        ! mv "$_pa_tmp" "$_pa_dir/tasks/$_pa_id"; then
        rm -rf "$_pa_tmp"
        return 1
    fi
    printf '%s\n' "$_pa_id" >> "$_pa_dir/order"
    printf '%s\n' "$_pa_id" >> "$_pa_dir/expansions/$_pa_open/tasks"
    pool_write "$_pa_dir/task-count" "$_pa_seq" || return 1
    pool_write "$_pa_dir/artifact-bytes" "$((_pa_total + _pa_bytes))" || return 1
    pool_event "$_pa_dir" "$_pa_id" task.added "kind=$_pa_kind expansion=$_pa_open"
    POOL_TASK_ID="$_pa_id"
}

# Seal the open expansion: membership is frozen in a manifest and its tasks
# become claimable. Sets POOL_EXPANSION. Caller holds the pool lock.
pool_seal() {
    _ps_dir="$1"
    _ps_open="$(pool_read "$_ps_dir/expansion-open")"
    [ -n "$_ps_open" ] || {
        cli_error pool nothing_to_seal "no open expansion; add tasks first" "run hydra pool add"
        return 1
    }
    _ps_edir="$_ps_dir/expansions/$_ps_open"
    _ps_list="" _ps_count=0 _ps_bytes=0
    while IFS= read -r _ps_task; do
        [ -n "$_ps_task" ] || continue
        _ps_count=$((_ps_count + 1))
        _ps_bytes=$((_ps_bytes + $(pool_read "$_ps_dir/tasks/$_ps_task/bytes")))
        _ps_list="$_ps_list${_ps_list:+,}\"$(json_escape "$_ps_task")\""
    done < "$_ps_edir/tasks"
    hydra_private_touch "$_ps_edir/manifest.json" || return 1
    printf '{"schema_version":1,"pool":"%s","expansion":%s,"sealed_at":%s,"task_count":%s,"artifact_bytes":%s,"tasks":[%s]}\n' \
        "$(json_escape "$(pool_read "$_ps_dir/pool-id")")" "$_ps_open" "$(pool_now)" "$_ps_count" "$_ps_bytes" "$_ps_list" > "$_ps_edir/manifest.json"
    pool_write "$_ps_edir/manifest-digest" "$(hydra_hash < "$_ps_edir/manifest.json")" || return 1
    pool_write "$_ps_edir/state" sealed || return 1
    pool_write "$_ps_dir/expansion-open" "" || return 1
    while IFS= read -r _ps_task; do
        [ -n "$_ps_task" ] || continue
        [ "$(pool_read "$_ps_dir/tasks/$_ps_task/state")" = pending ] || continue
        pool_write "$_ps_dir/tasks/$_ps_task/state" ready || return 1
        pool_event "$_ps_dir" "$_ps_task" task.ready "expansion=$_ps_open"
    done < "$_ps_edir/tasks"
    pool_event "$_ps_dir" "" expansion.sealed "expansion=$_ps_open tasks=$_ps_count digest=$(pool_read "$_ps_edir/manifest-digest")"
    POOL_EXPANSION="$_ps_open"
}

# A claim is stale when its lease expired or its recorded owner is provably
# dead on this host (the shared lock protocol's evidence).
pool_claim_stale() {
    _pcs_claim="$1"
    [ -d "$_pcs_claim" ] || return 1
    _pcs_expires="$(pool_read "$_pcs_claim/expires-at")"
    case "$_pcs_expires" in ''|*[!0-9]*) return 0 ;; esac
    [ "$(pool_now)" -gt "$_pcs_expires" ] && return 0
    lock_dir_is_stale "$_pcs_claim"
}

# Usage: pool_task_run_id <task-dir> <attempt-dir>
# The workflow run an attempt started: recorded explicitly, or the first line
# of the captured `hydra workflow run` output.
pool_task_run_id() {
    _ptr_attempt="$2"
    _ptr_run="$(pool_read "$_ptr_attempt/run-id")"
    if [ -z "$_ptr_run" ] && [ "$(pool_read "$1/kind")" = workflow ]; then
        _ptr_run="$(pool_read "$_ptr_attempt/stdout")"
    fi
    hydra_valid_id "$_ptr_run" 2>/dev/null || return 1
    case "$_ptr_run" in run_*) printf '%s\n' "$_ptr_run" ;; *) return 1 ;; esac
}

# Record the outcome of a failed attempt: idempotent tasks with budget left
# return to ready, others fail. Cancellation requested earlier wins.
pool_task_after_failure() {
    _ptf_dir="$1" _ptf_task="$2" _ptf_source="$3"
    _ptf_td="$_ptf_dir/tasks/$_ptf_task"
    if [ -f "$_ptf_td/cancel-requested" ] || [ -f "$_ptf_dir/cancel-requested" ]; then
        pool_write "$_ptf_td/state" cancelled || return 1
        pool_event "$_ptf_dir" "$_ptf_task" task.cancelled "$_ptf_source"
        return 0
    fi
    _ptf_attempts="$(pool_read "$_ptf_td/attempts")"
    if [ "$(pool_read "$_ptf_td/idempotent")" = true ] && [ "$_ptf_attempts" -le "$(pool_read "$_ptf_td/retry")" ]; then
        pool_write "$_ptf_td/state" ready || return 1
        pool_event "$_ptf_dir" "$_ptf_task" task.retrying "$_ptf_source attempts=$_ptf_attempts"
    else
        pool_write "$_ptf_td/state" failed || return 1
        pool_event "$_ptf_dir" "$_ptf_task" task.failed "$_ptf_source attempts=$_ptf_attempts"
    fi
}

# A stale claim is resolved from recorded evidence only. A workflow run the
# attempt started is authoritative when it reached a terminal state; anything
# else is an unknown outcome: idempotent tasks with budget are re-queued, all
# others wait for an explicit operator decision. Nothing is replayed here.
pool_task_resolve_stale() {
    _prs_dir="$1" _prs_task="$2"
    _prs_td="$_prs_dir/tasks/$_prs_task"
    _prs_attempt="$_prs_td/attempt-$(pool_read "$_prs_td/attempts")"
    _prs_outcome=unknown _prs_source=unknown
    if _prs_run="$(pool_task_run_id "$_prs_td" "$_prs_attempt")"; then
        pool_write "$_prs_attempt/run-id" "$_prs_run" || return 1
        case "$(pool_read "$POOL_PROJECT_DIR/workflows/runs/$_prs_run/state")" in
            succeeded) _prs_outcome=succeeded _prs_source=workflow-record ;;
            failed|cancelled) _prs_outcome=failed _prs_source=workflow-record ;;
        esac
    fi
    pool_write "$_prs_attempt/stale-at" "$(pool_now)" || return 1
    pool_write "$_prs_attempt/outcome" "$_prs_outcome" || return 1
    pool_write "$_prs_attempt/source" "$_prs_source" || return 1
    rm -rf "$_prs_td/claim"
    pool_event "$_prs_dir" "$_prs_task" task.stale_owner "outcome=$_prs_outcome source=$_prs_source"
    case "$_prs_outcome" in
        succeeded)
            pool_write "$_prs_td/state" succeeded || return 1
            pool_event "$_prs_dir" "$_prs_task" task.succeeded "$_prs_source"
            ;;
        failed) pool_task_after_failure "$_prs_dir" "$_prs_task" "$_prs_source" ;;
        *)
            if [ ! -f "$_prs_td/cancel-requested" ] && [ ! -f "$_prs_dir/cancel-requested" ] &&
                [ "$(pool_read "$_prs_td/idempotent")" = true ] &&
                [ "$(pool_read "$_prs_td/attempts")" -le "$(pool_read "$_prs_td/retry")" ]; then
                pool_write "$_prs_td/state" ready || return 1
                pool_event "$_prs_dir" "$_prs_task" task.retrying "stale_owner_unknown_outcome"
            else
                pool_write "$_prs_td/state" recovery-required || return 1
                pool_event "$_prs_dir" "$_prs_task" task.recovery_required unknown_outcome
            fi
            ;;
    esac
}

# Reconcile every stale claim. Sets POOL_RECONCILED to the count.
pool_reconcile() {
    _pr_dir="$1"
    POOL_RECONCILED=0
    while IFS= read -r _pr_task; do
        [ -n "$_pr_task" ] || continue
        [ "$(pool_read "$_pr_dir/tasks/$_pr_task/state")" = claimed ] || continue
        pool_claim_stale "$_pr_dir/tasks/$_pr_task/claim" || continue
        pool_task_resolve_stale "$_pr_dir" "$_pr_task" || return 1
        POOL_RECONCILED=$((POOL_RECONCILED + 1))
    done < "$_pr_dir/order"
}

pool_count_state() {
    _pcs_n=0
    while IFS= read -r _pcs_task; do
        [ -n "$_pcs_task" ] || continue
        [ "$(pool_read "$1/tasks/$_pcs_task/state")" = "$2" ] && _pcs_n=$((_pcs_n + 1))
    done < "$1/order"
    printf '%s\n' "$_pcs_n"
}

# Usage: pool_claim <pool-dir> <worker> <owner-pid|-> <lease-seconds>
# Sets POOL_CLAIM_TASK and POOL_CLAIM_TOKEN, or POOL_CLAIM_REASON when nothing
# was claimed. Caller holds the pool lock.
pool_claim() {
    _pcl_dir="$1" _pcl_worker="$2" _pcl_pid="$3" _pcl_lease="$4"
    POOL_CLAIM_TASK="" POOL_CLAIM_TOKEN="" POOL_CLAIM_REASON=""
    pool_reconcile "$_pcl_dir" || return 1
    if [ -f "$_pcl_dir/cancel-requested" ]; then POOL_CLAIM_REASON=pool_cancelled; return 0; fi
    if [ "$(pool_count_state "$_pcl_dir" claimed)" -ge "$(pool_read "$_pcl_dir/max-outstanding")" ]; then
        POOL_CLAIM_REASON=outstanding_limit
        return 0
    fi
    _pcl_task=""
    while IFS= read -r _pcl_candidate; do
        [ -n "$_pcl_candidate" ] || continue
        if [ "$(pool_read "$_pcl_dir/tasks/$_pcl_candidate/state")" = ready ]; then
            _pcl_task="$_pcl_candidate"
            break
        fi
    done < "$_pcl_dir/order"
    if [ -z "$_pcl_task" ]; then
        if [ -n "$(pool_read "$_pcl_dir/expansion-open")" ]; then POOL_CLAIM_REASON=unsealed; else POOL_CLAIM_REASON=none; fi
        return 0
    fi
    _pcl_td="$_pcl_dir/tasks/$_pcl_task"
    _pcl_attempt=$(($(pool_read "$_pcl_td/attempts") + 1))
    _pcl_token="$(pool_new_token "$_pcl_task")"
    _pcl_now="$(pool_now)"
    [ "$_pcl_pid" != - ] || _pcl_pid=""
    _pcl_host="$(hostname 2>/dev/null || uname -n 2>/dev/null || echo unknown)"
    rm -rf "$_pcl_td/claim.tmp"
    hydra_private_mkdir "$_pcl_td/claim.tmp" "$_pcl_td/attempt-$_pcl_attempt" || return 1
    if ! pool_write "$_pcl_td/claim.tmp/pid" "$_pcl_pid" ||
        ! pool_write "$_pcl_td/claim.tmp/host" "$_pcl_host" ||
        ! pool_write "$_pcl_td/claim.tmp/created_at" "$_pcl_now" ||
        ! pool_write "$_pcl_td/claim.tmp/operation" "pool task $_pcl_task" ||
        ! pool_write "$_pcl_td/claim.tmp/worker" "$_pcl_worker" ||
        ! pool_write "$_pcl_td/claim.tmp/token" "$_pcl_token" ||
        ! pool_write "$_pcl_td/claim.tmp/attempt" "$_pcl_attempt" ||
        ! pool_write "$_pcl_td/claim.tmp/expires-at" "$((_pcl_now + _pcl_lease))" ||
        ! pool_write "$_pcl_td/attempt-$_pcl_attempt/worker" "$_pcl_worker" ||
        ! pool_write "$_pcl_td/attempt-$_pcl_attempt/claimed-at" "$_pcl_now" ||
        ! mv "$_pcl_td/claim.tmp" "$_pcl_td/claim" ||
        ! pool_write "$_pcl_td/attempts" "$_pcl_attempt" ||
        ! pool_write "$_pcl_td/state" claimed; then
        rm -rf "$_pcl_td/claim.tmp"
        return 1
    fi
    pool_event "$_pcl_dir" "$_pcl_task" task.claimed "worker=$_pcl_worker attempt=$_pcl_attempt"
    POOL_CLAIM_TASK="$_pcl_task"
    POOL_CLAIM_TOKEN="$_pcl_token"
}

# The presented token must match the live claim; an owner whose claim was
# reconciled away (or re-issued) is rejected as stale.
pool_claim_check() {
    _pcc_td="$1/tasks/$2"
    [ "$(pool_read "$_pcc_td/state")" = claimed ] || {
        cli_error pool not_claimed "task '$2' is not claimed (state: $(pool_read "$_pcc_td/state"))" "claim it with hydra pool claim"
        return 1
    }
    if [ -z "$3" ] || [ "$(pool_read "$_pcc_td/claim/token")" != "$3" ]; then
        cli_error pool stale_owner "claim token does not match the live claim of task '$2'" "the claim was reconciled or re-issued; stop this worker's work on it"
        return 1
    fi
}

# Usage: pool_renew <pool-dir> <task> <token> <lease-seconds>
pool_renew() {
    pool_claim_check "$1" "$2" "$3" || return 1
    pool_write "$1/tasks/$2/claim/expires-at" "$(($(pool_now) + $4))"
}

# Usage: pool_complete <pool-dir> <task> <token> <succeeded|failed> <run-id|->
pool_complete() {
    _pco_dir="$1" _pco_task="$2" _pco_outcome="$4" _pco_run="$5"
    pool_claim_check "$_pco_dir" "$_pco_task" "$3" || return 1
    _pco_td="$_pco_dir/tasks/$_pco_task"
    _pco_attempt="$_pco_td/attempt-$(pool_read "$_pco_td/attempts")"
    pool_write "$_pco_attempt/outcome" "$_pco_outcome" || return 1
    pool_write "$_pco_attempt/source" worker || return 1
    pool_write "$_pco_attempt/completed-at" "$(pool_now)" || return 1
    [ "$_pco_run" = - ] || pool_write "$_pco_attempt/run-id" "$_pco_run" || return 1
    rm -rf "$_pco_td/claim"
    if [ "$_pco_outcome" = succeeded ]; then
        pool_write "$_pco_td/state" succeeded || return 1
        pool_event "$_pco_dir" "$_pco_task" task.succeeded "worker"
    else
        pool_task_after_failure "$_pco_dir" "$_pco_task" worker || return 1
    fi
    POOL_TASK_STATE="$(pool_read "$_pco_td/state")"
}
