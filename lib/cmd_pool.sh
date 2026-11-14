#!/bin/sh
# hydra pool: file-backed task pools with sealed bounded expansions.

cmd_pool_help() {
    # shellcheck disable=SC2016 # Command names are quoted literally.
    printf '%s\n' \
        'Usage: hydra pool create <pool> [--max-tasks N] [--max-outstanding N] [--max-expansions N]' \
        '                         [--max-artifact-bytes N] [--max-repair-rounds N] [--lease SECONDS] [--retry N]' \
        '       hydra pool add <pool> --workflow <id|path> [--id <task>] [--idempotent] [--retry N] [--seal]' \
        '       hydra pool add <pool> --exec <head> [--id <task>] [--idempotent] [--retry N] [--seal] -- <argv...>' \
        '       hydra pool seal <pool>' \
        '       hydra pool claim <pool> --worker <id> [--owner-pid <pid>] [--lease SECONDS]' \
        '       hydra pool complete <pool> <task> --token <token> --outcome succeeded|failed [--run <run-id>]' \
        '       hydra pool complete <pool> <task> --by <label> --outcome succeeded|failed|retry' \
        '       hydra pool cancel <pool> [<task>]' \
        '       hydra pool status <pool>' \
        '       hydra pool list' \
        '       hydra pool run <pool> [--worker <id>] [--max N] [--lease SECONDS]' \
        '' \
        'Every form accepts --json. Bounds are sealed when the pool is created. Tasks' \
        'join the open expansion and become claimable only after `seal` freezes its' \
        'membership. Claimed tasks run through `hydra workflow run` or `hydra exec`;' \
        '`run` is a worker loop around those commands. A stale claim is reconciled' \
        'from recorded evidence, never replayed: a non-idempotent task with an' \
        'unknown outcome waits for `complete --by <label>`.'
}

cmd_pool_json_bool() {
    [ "$1" = true ] && printf true || printf false
}

cmd_pool_task_json() {
    _cptj_td="$1/tasks/$2"
    _cptj_attempts="$(pool_read "$_cptj_td/attempts")"
    _cptj_attempt="$_cptj_td/attempt-$_cptj_attempts"
    _cptj_argv=""
    while IFS= read -r _cptj_arg || [ -n "$_cptj_arg" ]; do
        _cptj_argv="$_cptj_argv${_cptj_argv:+,}\"$(json_escape "$_cptj_arg")\""
    done < "$_cptj_td/argv"
    _cptj_claim=null
    if [ -d "$_cptj_td/claim" ]; then
        _cptj_stale=false
        pool_claim_stale "$_cptj_td/claim" && _cptj_stale=true
        _cptj_claim="{\"worker\":\"$(json_escape "$(pool_read "$_cptj_td/claim/worker")")\",\"host\":\"$(json_escape "$(pool_read "$_cptj_td/claim/host")")\",\"pid\":$(json_string_or_null "$(pool_read "$_cptj_td/claim/pid")"),\"claimed_at\":$(pool_read "$_cptj_td/claim/created_at"),\"expires_at\":$(pool_read "$_cptj_td/claim/expires-at"),\"stale\":$_cptj_stale}"
    fi
    _cptj_run=""
    [ "$_cptj_attempts" -eq 0 ] || _cptj_run="$(pool_task_run_id "$_cptj_td" "$_cptj_attempt" 2>/dev/null || true)"
    _cptj_cancel=false
    [ ! -f "$_cptj_td/cancel-requested" ] || _cptj_cancel=true
    printf '{"task_id":"%s","state":"%s","kind":"%s","definition":"%s","argv":[%s],"idempotent":%s,"retry":%s,"attempts":%s,"expansion":%s,"cancel_requested":%s,"claim":%s,"run_id":%s,"last_outcome":%s,"outcome_source":%s}' \
        "$(json_escape "$2")" "$(pool_read "$_cptj_td/state")" "$(pool_read "$_cptj_td/kind")" \
        "$(json_escape "$(pool_read "$_cptj_td/definition")")" "$_cptj_argv" \
        "$(cmd_pool_json_bool "$(pool_read "$_cptj_td/idempotent")")" "$(pool_read "$_cptj_td/retry")" \
        "$_cptj_attempts" "$(pool_read "$_cptj_td/expansion")" "$_cptj_cancel" "$_cptj_claim" \
        "$(json_string_or_null "$_cptj_run")" \
        "$(json_string_or_null "$(pool_read "$_cptj_attempt/outcome")")" \
        "$(json_string_or_null "$(pool_read "$_cptj_attempt/source")")"
}

cmd_pool_status_json() {
    _cpsj_dir="$1"
    _cpsj_tasks="" _cpsj_expansions=""
    _cpsj_pending=0 _cpsj_ready=0 _cpsj_claimed=0 _cpsj_succeeded=0 _cpsj_failed=0 _cpsj_cancelled=0 _cpsj_recovery=0
    while IFS= read -r _cpsj_task; do
        [ -n "$_cpsj_task" ] || continue
        case "$(pool_read "$_cpsj_dir/tasks/$_cpsj_task/state")" in
            pending) _cpsj_pending=$((_cpsj_pending + 1)) ;;
            ready) _cpsj_ready=$((_cpsj_ready + 1)) ;;
            claimed) _cpsj_claimed=$((_cpsj_claimed + 1)) ;;
            succeeded) _cpsj_succeeded=$((_cpsj_succeeded + 1)) ;;
            failed) _cpsj_failed=$((_cpsj_failed + 1)) ;;
            cancelled) _cpsj_cancelled=$((_cpsj_cancelled + 1)) ;;
            recovery-required) _cpsj_recovery=$((_cpsj_recovery + 1)) ;;
        esac
        _cpsj_tasks="$_cpsj_tasks${_cpsj_tasks:+,}$(cmd_pool_task_json "$_cpsj_dir" "$_cpsj_task")"
    done < "$_cpsj_dir/order"
    _cpsj_n=1
    while [ -d "$_cpsj_dir/expansions/$_cpsj_n" ]; do
        _cpsj_edir="$_cpsj_dir/expansions/$_cpsj_n"
        _cpsj_expansions="$_cpsj_expansions${_cpsj_expansions:+,}{\"number\":$_cpsj_n,\"state\":\"$(pool_read "$_cpsj_edir/state")\",\"task_count\":$(grep -c . "$_cpsj_edir/tasks" 2>/dev/null || printf 0),\"manifest_digest\":$(json_string_or_null "$(pool_read "$_cpsj_edir/manifest-digest")")}"
        _cpsj_n=$((_cpsj_n + 1))
    done
    _cpsj_open="$(pool_read "$_cpsj_dir/expansion-open")"
    _cpsj_drained=false
    if [ "$((_cpsj_pending + _cpsj_ready + _cpsj_claimed))" -eq 0 ] && [ -z "$_cpsj_open" ]; then _cpsj_drained=true; fi
    printf '{"pool":"%s","state":"%s","created_at":%s,"manifest_digest":"%s","bounds":{"max_tasks":%s,"max_outstanding":%s,"max_expansions":%s,"max_artifact_bytes":%s,"max_repair_rounds":%s,"lease_seconds":%s,"retry":%s},"usage":{"tasks":%s,"expansions":%s,"open_expansion":%s,"artifact_bytes":%s,"repair_rounds":%s,"outstanding":%s},"counts":{"pending":%s,"ready":%s,"claimed":%s,"succeeded":%s,"failed":%s,"cancelled":%s,"recovery_required":%s},"drained":%s,"cancel_requested":%s,"expansions":[%s],"tasks":[%s]}' \
        "$(json_escape "$(pool_read "$_cpsj_dir/pool-id")")" "$(pool_read "$_cpsj_dir/state")" "$(pool_read "$_cpsj_dir/created-at")" \
        "$(pool_read "$_cpsj_dir/manifest-digest")" \
        "$(pool_read "$_cpsj_dir/max-tasks")" "$(pool_read "$_cpsj_dir/max-outstanding")" "$(pool_read "$_cpsj_dir/max-expansions")" \
        "$(pool_read "$_cpsj_dir/max-artifact-bytes")" "$(pool_read "$_cpsj_dir/max-repair-rounds")" "$(pool_read "$_cpsj_dir/lease-seconds")" "$(pool_read "$_cpsj_dir/retry")" \
        "$(pool_read "$_cpsj_dir/task-count")" "$(pool_read "$_cpsj_dir/expansion-count")" "${_cpsj_open:-null}" \
        "$(pool_read "$_cpsj_dir/artifact-bytes")" "$(pool_read "$_cpsj_dir/repair-rounds")" "$_cpsj_claimed" \
        "$_cpsj_pending" "$_cpsj_ready" "$_cpsj_claimed" "$_cpsj_succeeded" "$_cpsj_failed" "$_cpsj_cancelled" "$_cpsj_recovery" \
        "$_cpsj_drained" "$([ -f "$_cpsj_dir/cancel-requested" ] && printf true || printf false)" "$_cpsj_expansions" "$_cpsj_tasks"
}

cmd_pool_status_text() {
    _cpst_dir="$1"
    printf 'Pool %s: %s (tasks %s/%s, expansions %s/%s, outstanding %s/%s, repair rounds %s/%s)\n' \
        "$(pool_read "$_cpst_dir/pool-id")" "$(pool_read "$_cpst_dir/state")" \
        "$(pool_read "$_cpst_dir/task-count")" "$(pool_read "$_cpst_dir/max-tasks")" \
        "$(pool_read "$_cpst_dir/expansion-count")" "$(pool_read "$_cpst_dir/max-expansions")" \
        "$(pool_count_state "$_cpst_dir" claimed)" "$(pool_read "$_cpst_dir/max-outstanding")" \
        "$(pool_read "$_cpst_dir/repair-rounds")" "$(pool_read "$_cpst_dir/max-repair-rounds")"
    _cpst_open="$(pool_read "$_cpst_dir/expansion-open")"
    [ -z "$_cpst_open" ] || printf '  expansion %s is open; seal it before its tasks can be claimed\n' "$_cpst_open"
    while IFS= read -r _cpst_task; do
        [ -n "$_cpst_task" ] || continue
        _cpst_td="$_cpst_dir/tasks/$_cpst_task"
        _cpst_extra=""
        if [ -d "$_cpst_td/claim" ]; then
            _cpst_extra=" worker=$(pool_read "$_cpst_td/claim/worker")"
            pool_claim_stale "$_cpst_td/claim" && _cpst_extra="$_cpst_extra stale"
        fi
        [ ! -f "$_cpst_td/cancel-requested" ] || _cpst_extra="$_cpst_extra cancel-requested"
        printf '  %s [%s]: %s (attempts=%s, expansion=%s)%s\n' "$_cpst_task" "$(pool_read "$_cpst_td/kind")" \
            "$(pool_read "$_cpst_td/state")" "$(pool_read "$_cpst_td/attempts")" "$(pool_read "$_cpst_td/expansion")" "$_cpst_extra"
    done < "$_cpst_dir/order"
}

cmd_pool_number_option() {
    pool_valid_number "$2" "$3" "$4" || {
        cli_error pool invalid_input "$1 must be an integer between $3 and $4" "run hydra pool --help"
        return 1
    }
}

cmd_pool_create() {
    _cpc_name="${1:-}"; [ $# -eq 0 ] || shift
    _cpc_tasks=64 _cpc_outstanding=1 _cpc_expansions=8 _cpc_bytes=65536 _cpc_repairs=0 _cpc_lease=300 _cpc_retry=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --max-tasks) cmd_pool_number_option "$1" "${2:-}" 1 1024 || return 1; _cpc_tasks="$2"; shift 2 ;;
            --max-outstanding) cmd_pool_number_option "$1" "${2:-}" 1 16 || return 1; _cpc_outstanding="$2"; shift 2 ;;
            --max-expansions) cmd_pool_number_option "$1" "${2:-}" 1 64 || return 1; _cpc_expansions="$2"; shift 2 ;;
            --max-artifact-bytes) cmd_pool_number_option "$1" "${2:-}" 1 1048576 || return 1; _cpc_bytes="$2"; shift 2 ;;
            --max-repair-rounds) cmd_pool_number_option "$1" "${2:-}" 0 10 || return 1; _cpc_repairs="$2"; shift 2 ;;
            --lease) cmd_pool_number_option "$1" "${2:-}" 1 86400 || return 1; _cpc_lease="$2"; shift 2 ;;
            --retry) cmd_pool_number_option "$1" "${2:-}" 0 10 || return 1; _cpc_retry="$2"; shift 2 ;;
            -j|--json) shift ;;
            *) cli_error pool invalid_input "unknown option '$1'" "run hydra pool --help"; return 1 ;;
        esac
    done
    [ -n "$_cpc_name" ] || { cli_error pool invalid_input "pool name is required" "run hydra pool --help"; return 1; }
    pool_valid_name "$_cpc_name" || { cli_error pool invalid_input "invalid pool name '$_cpc_name'" "use lowercase letters, digits, single hyphens or underscores"; return 1; }
    pool_create "$_cpc_name" "$_cpc_tasks" "$_cpc_outstanding" "$_cpc_expansions" "$_cpc_bytes" "$_cpc_repairs" "$_cpc_lease" "$_cpc_retry" || return 1
    if [ "$CMD_POOL_JSON" -eq 1 ]; then
        json_success "pool create" "$(cmd_pool_status_json "$POOL_ROOT/$_cpc_name")"
    else
        printf 'Created pool %s (manifest %s)\n' "$_cpc_name" "$(pool_read "$POOL_ROOT/$_cpc_name/manifest-digest")"
    fi
}

cmd_pool_add() {
    _cpa_name="${1:-}"; [ $# -eq 0 ] || shift
    _cpa_dir="$(pool_dir "$_cpa_name")" || return 1
    _cpa_id=- _cpa_kind="" _cpa_definition="" _cpa_idempotent=false _cpa_retry="$(pool_read "$_cpa_dir/retry")" _cpa_seal=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --workflow) [ -n "${2:-}" ] || { cli_error pool invalid_input "--workflow requires a workflow ID or path" "run hydra pool --help"; return 1; }; _cpa_kind=workflow; _cpa_definition="$2"; shift 2 ;;
            --exec) [ -n "${2:-}" ] || { cli_error pool invalid_input "--exec requires a head" "run hydra pool --help"; return 1; }; _cpa_kind="exec"; _cpa_definition="$2"; shift 2 ;;
            --id) [ -n "${2:-}" ] || { cli_error pool invalid_input "--id requires a task ID" "run hydra pool --help"; return 1; }; _cpa_id="$2"; shift 2 ;;
            --idempotent) _cpa_idempotent=true; shift ;;
            --retry) cmd_pool_number_option "$1" "${2:-}" 0 10 || return 1; _cpa_retry="$2"; shift 2 ;;
            --seal) _cpa_seal=1; shift ;;
            -j|--json) shift ;;
            --) shift; break ;;
            *) cli_error pool invalid_input "unknown option '$1'" "run hydra pool --help"; return 1 ;;
        esac
    done
    [ "$_cpa_id" = - ] || pool_valid_name "$_cpa_id" || { cli_error pool invalid_input "invalid task ID '$_cpa_id'" "use lowercase letters, digits, single hyphens or underscores"; return 1; }
    case "$_cpa_kind" in
        workflow)
            [ $# -eq 0 ] || { cli_error pool invalid_input "--workflow takes no argv" "use --exec <head> -- <argv...> for commands"; return 1; }
            _cpa_definition="$(cd "$POOL_REPO_ROOT" && workflow_resolve "$_cpa_definition")" || return 1
            (cd "$POOL_REPO_ROOT" && workflow_require_trust "$_cpa_definition") || return 1
            ;;
        exec)
            [ $# -gt 0 ] || { cli_error pool invalid_input "--exec requires a command argv after --" "run hydra pool --help"; return 1; }
            state_v2_find_head_by_branch "$POOL_PROJECT" "$_cpa_definition" >/dev/null 2>&1 || { cli_error pool head_not_found "head '$_cpa_definition' has no durable state" "spawn it first"; return 1; }
            ;;
        *) cli_error pool invalid_input "one of --workflow or --exec is required" "run hydra pool --help"; return 1 ;;
    esac
    [ "$_cpa_retry" -eq 0 ] || [ "$_cpa_idempotent" = true ] || { cli_error pool invalid_input "--retry requires --idempotent" "only idempotent tasks may retry"; return 1; }
    pool_lock "$_cpa_name" || return 1
    if ! pool_add "$_cpa_dir" "$_cpa_id" "$_cpa_kind" "$_cpa_definition" "$_cpa_idempotent" "$_cpa_retry" "$@"; then
        pool_unlock "$_cpa_name"; return 1
    fi
    _cpa_task="$POOL_TASK_ID" _cpa_expansion=null
    if [ "$_cpa_seal" -eq 1 ]; then
        pool_seal "$_cpa_dir" || { pool_unlock "$_cpa_name"; return 1; }
        _cpa_expansion="$POOL_EXPANSION"
    fi
    pool_unlock "$_cpa_name"
    if [ "$CMD_POOL_JSON" -eq 1 ]; then
        json_success "pool add" "{\"pool\":\"$(json_escape "$_cpa_name")\",\"task_id\":\"$(json_escape "$_cpa_task")\",\"state\":\"$(pool_read "$_cpa_dir/tasks/$_cpa_task/state")\",\"expansion\":$(pool_read "$_cpa_dir/tasks/$_cpa_task/expansion"),\"sealed_expansion\":$_cpa_expansion}"
    else
        printf 'Added %s to pool %s (expansion %s, %s)\n' "$_cpa_task" "$_cpa_name" "$(pool_read "$_cpa_dir/tasks/$_cpa_task/expansion")" "$(pool_read "$_cpa_dir/tasks/$_cpa_task/state")"
    fi
}

cmd_pool_seal() {
    _cps_name="${1:-}"
    _cps_dir="$(pool_dir "$_cps_name")" || return 1
    pool_lock "$_cps_name" || return 1
    pool_seal "$_cps_dir" || { pool_unlock "$_cps_name"; return 1; }
    pool_unlock "$_cps_name"
    _cps_edir="$_cps_dir/expansions/$POOL_EXPANSION"
    if [ "$CMD_POOL_JSON" -eq 1 ]; then
        json_success "pool seal" "{\"pool\":\"$(json_escape "$_cps_name")\",\"expansion\":$POOL_EXPANSION,\"task_count\":$(grep -c . "$_cps_edir/tasks" || printf 0),\"manifest_digest\":\"$(pool_read "$_cps_edir/manifest-digest")\"}"
    else
        printf 'Sealed expansion %s of pool %s (%s task(s), manifest %s)\n' "$POOL_EXPANSION" "$_cps_name" "$(grep -c . "$_cps_edir/tasks" || printf 0)" "$(pool_read "$_cps_edir/manifest-digest")"
    fi
}

cmd_pool_claim() {
    _cpcl_name="${1:-}"; [ $# -eq 0 ] || shift
    _cpcl_dir="$(pool_dir "$_cpcl_name")" || return 1
    _cpcl_worker="" _cpcl_pid=- _cpcl_lease="$(pool_read "$_cpcl_dir/lease-seconds")"
    while [ $# -gt 0 ]; do
        case "$1" in
            --worker) [ -n "${2:-}" ] || { cli_error pool invalid_input "--worker requires a label" "run hydra pool --help"; return 1; }; _cpcl_worker="$2"; shift 2 ;;
            --owner-pid) cmd_pool_number_option "$1" "${2:-}" 1 4194304 || return 1; _cpcl_pid="$2"; shift 2 ;;
            --lease) cmd_pool_number_option "$1" "${2:-}" 1 86400 || return 1; _cpcl_lease="$2"; shift 2 ;;
            -j|--json) shift ;;
            *) cli_error pool invalid_input "unknown option '$1'" "run hydra pool --help"; return 1 ;;
        esac
    done
    [ -n "$_cpcl_worker" ] || { cli_error pool invalid_input "--worker is required" "name the worker that will execute the task"; return 1; }
    case "$_cpcl_worker" in *[!A-Za-z0-9._@:-]*) cli_error pool invalid_input "worker labels use letters, digits, '.', '_', '@', ':' and '-'" "choose a plain label"; return 1 ;; esac
    pool_lock "$_cpcl_name" || return 1
    pool_claim "$_cpcl_dir" "$_cpcl_worker" "$_cpcl_pid" "$_cpcl_lease" || { pool_unlock "$_cpcl_name"; return 1; }
    pool_unlock "$_cpcl_name"
    if [ -n "$POOL_CLAIM_TASK" ]; then
        if [ "$CMD_POOL_JSON" -eq 1 ]; then
            json_success "pool claim" "{\"pool\":\"$(json_escape "$_cpcl_name")\",\"claimed\":true,\"reason\":null,\"token\":\"$POOL_CLAIM_TOKEN\",\"lease_seconds\":$_cpcl_lease,\"reconciled\":$POOL_RECONCILED,\"task\":$(cmd_pool_task_json "$_cpcl_dir" "$POOL_CLAIM_TASK")}"
        else
            printf 'Claimed %s (token %s, lease %ss)\n' "$POOL_CLAIM_TASK" "$POOL_CLAIM_TOKEN" "$_cpcl_lease"
        fi
    elif [ "$CMD_POOL_JSON" -eq 1 ]; then
        json_success "pool claim" "{\"pool\":\"$(json_escape "$_cpcl_name")\",\"claimed\":false,\"reason\":\"$POOL_CLAIM_REASON\",\"token\":null,\"lease_seconds\":$_cpcl_lease,\"reconciled\":$POOL_RECONCILED,\"task\":null}"
    else
        printf 'Nothing claimed: %s\n' "$POOL_CLAIM_REASON"
    fi
}

cmd_pool_complete() {
    _cpco_name="${1:-}" _cpco_task="${2:-}"; [ $# -lt 2 ] || shift 2
    _cpco_dir="$(pool_dir "$_cpco_name")" || return 1
    if ! pool_valid_name "$_cpco_task" || [ ! -d "$_cpco_dir/tasks/$_cpco_task" ]; then
        cli_error pool not_found "task '$_cpco_task' does not exist in pool '$_cpco_name'" "inspect with hydra pool status $_cpco_name"
        return 1
    fi
    _cpco_token="" _cpco_by="" _cpco_outcome="" _cpco_run=-
    while [ $# -gt 0 ]; do
        case "$1" in
            --token) [ -n "${2:-}" ] || return 1; _cpco_token="$2"; shift 2 ;;
            --by) [ -n "${2:-}" ] || return 1; _cpco_by="$2"; shift 2 ;;
            --outcome) [ -n "${2:-}" ] || return 1; _cpco_outcome="$2"; shift 2 ;;
            --run) hydra_valid_id "${2:-}" || { cli_error pool invalid_input "--run requires a workflow run ID" "copy the ID printed by hydra workflow run"; return 1; }; _cpco_run="$2"; shift 2 ;;
            -j|--json) shift ;;
            *) cli_error pool invalid_input "unknown option '$1'" "run hydra pool --help"; return 1 ;;
        esac
    done
    pool_lock "$_cpco_name" || return 1
    if [ -n "$_cpco_token" ]; then
        case "$_cpco_outcome" in succeeded|failed) ;; *) pool_unlock "$_cpco_name"; cli_error pool invalid_input "--outcome must be succeeded or failed" "run hydra pool --help"; return 1 ;; esac
        pool_complete "$_cpco_dir" "$_cpco_task" "$_cpco_token" "$_cpco_outcome" "$_cpco_run" || { pool_unlock "$_cpco_name"; return 1; }
    elif [ -n "$_cpco_by" ]; then
        case "$_cpco_outcome" in succeeded|failed|retry) ;; *) pool_unlock "$_cpco_name"; cli_error pool invalid_input "--outcome must be succeeded, failed or retry" "run hydra pool --help"; return 1 ;; esac
        pool_resolve "$_cpco_dir" "$_cpco_task" "$_cpco_outcome" "$_cpco_by" || { pool_unlock "$_cpco_name"; return 1; }
    else
        pool_unlock "$_cpco_name"
        cli_error pool invalid_input "complete requires --token (worker) or --by (operator decision)" "run hydra pool --help"
        return 1
    fi
    pool_unlock "$_cpco_name"
    if [ "$CMD_POOL_JSON" -eq 1 ]; then
        json_success "pool complete" "{\"pool\":\"$(json_escape "$_cpco_name")\",\"task_id\":\"$(json_escape "$_cpco_task")\",\"outcome\":\"$_cpco_outcome\",\"state\":\"$POOL_TASK_STATE\"}"
    else
        printf 'Task %s: %s\n' "$_cpco_task" "$POOL_TASK_STATE"
    fi
}

cmd_pool_cancel() {
    _cpca_name="${1:-}" _cpca_task="${2:-}"
    _cpca_dir="$(pool_dir "$_cpca_name")" || return 1
    pool_lock "$_cpca_name" || return 1
    if [ -n "$_cpca_task" ]; then
        if ! pool_valid_name "$_cpca_task" || [ ! -d "$_cpca_dir/tasks/$_cpca_task" ]; then
            pool_unlock "$_cpca_name"
            cli_error pool not_found "task '$_cpca_task' does not exist in pool '$_cpca_name'" "inspect with hydra pool status $_cpca_name"
            return 1
        fi
        pool_cancel_task "$_cpca_dir" "$_cpca_task" || { pool_unlock "$_cpca_name"; return 1; }
        pool_unlock "$_cpca_name"
        if [ "$CMD_POOL_JSON" -eq 1 ]; then
            json_success "pool cancel" "{\"pool\":\"$(json_escape "$_cpca_name")\",\"task_id\":\"$(json_escape "$_cpca_task")\",\"result\":\"$POOL_CANCEL_RESULT\",\"state\":\"$(pool_read "$_cpca_dir/tasks/$_cpca_task/state")\"}"
        else
            printf 'Task %s: %s (%s)\n' "$_cpca_task" "$(pool_read "$_cpca_dir/tasks/$_cpca_task/state")" "$POOL_CANCEL_RESULT"
        fi
        return 0
    fi
    pool_cancel_all "$_cpca_dir" || { pool_unlock "$_cpca_name"; return 1; }
    pool_unlock "$_cpca_name"
    if [ "$CMD_POOL_JSON" -eq 1 ]; then
        json_success "pool cancel" "{\"pool\":\"$(json_escape "$_cpca_name")\",\"task_id\":null,\"cancelled\":$POOL_CANCELLED,\"signalled\":$POOL_SIGNALLED,\"state\":\"cancelled\"}"
    else
        printf 'Pool %s: cancelled (%s queued task(s) cancelled, %s claimed task(s) asked to stop)\n' "$_cpca_name" "$POOL_CANCELLED" "$POOL_SIGNALLED"
    fi
}

cmd_pool_list() {
    _cpl_first=1
    [ "$CMD_POOL_JSON" -eq 1 ] && printf '{"schema_version":1,"ok":true,"command":"pool list","data":{"pools":['
    for _cpl_dir in "$POOL_ROOT"/*; do
        [ -f "$_cpl_dir/pool-id" ] || continue
        if [ "$CMD_POOL_JSON" -eq 1 ]; then
            [ "$_cpl_first" -eq 1 ] || printf ','
            printf '{"pool":"%s","state":"%s","tasks":%s,"claimed":%s,"ready":%s,"recovery_required":%s}' \
                "$(json_escape "$(pool_read "$_cpl_dir/pool-id")")" "$(pool_read "$_cpl_dir/state")" "$(pool_read "$_cpl_dir/task-count")" \
                "$(pool_count_state "$_cpl_dir" claimed)" "$(pool_count_state "$_cpl_dir" ready)" "$(pool_count_state "$_cpl_dir" recovery-required)"
        else
            printf '%s\t%s\ttasks=%s\tclaimed=%s\tready=%s\trecovery-required=%s\n' "$(pool_read "$_cpl_dir/pool-id")" "$(pool_read "$_cpl_dir/state")" \
                "$(pool_read "$_cpl_dir/task-count")" "$(pool_count_state "$_cpl_dir" claimed)" "$(pool_count_state "$_cpl_dir" ready)" "$(pool_count_state "$_cpl_dir" recovery-required)"
        fi
        _cpl_first=0
    done
    if [ "$CMD_POOL_JSON" -eq 1 ]; then printf ']}}\n'; elif [ "$_cpl_first" -eq 1 ]; then echo "No pools"; fi
}

cmd_pool_run() {
    _cpr_name="${1:-}"; [ $# -eq 0 ] || shift
    _cpr_dir="$(pool_dir "$_cpr_name")" || return 1
    _cpr_worker="$(hostname 2>/dev/null || echo host)-$$" _cpr_max=0 _cpr_lease="$(pool_read "$_cpr_dir/lease-seconds")"
    while [ $# -gt 0 ]; do
        case "$1" in
            --worker) [ -n "${2:-}" ] || return 1; _cpr_worker="$2"; shift 2 ;;
            --max) cmd_pool_number_option "$1" "${2:-}" 1 1024 || return 1; _cpr_max="$2"; shift 2 ;;
            --lease) cmd_pool_number_option "$1" "${2:-}" 1 86400 || return 1; _cpr_lease="$2"; shift 2 ;;
            -j|--json) shift ;;
            *) cli_error pool invalid_input "unknown option '$1'" "run hydra pool --help"; return 1 ;;
        esac
    done
    _load_lib pool_run
    pool_run_worker "$_cpr_name" "$_cpr_worker" "$_cpr_max" "$_cpr_lease" "$CMD_POOL_JSON" || return 1
    if [ "$CMD_POOL_JSON" -eq 1 ]; then
        json_success "pool run" "{\"pool\":\"$(json_escape "$_cpr_name")\",\"worker\":\"$(json_escape "$_cpr_worker")\",\"executed\":$POOL_RUN_COUNT,\"stopped\":\"$POOL_RUN_LAST_REASON\",\"results\":[$POOL_RUN_RESULTS]}"
    else
        printf 'Worker %s executed %s task(s); stopped: %s\n' "$_cpr_worker" "$POOL_RUN_COUNT" "$POOL_RUN_LAST_REASON"
    fi
    [ "$POOL_RUN_LAST_REASON" != interrupted ]
}

cmd_pool() {
    # --json may appear anywhere before `--`; strip it so subcommands only
    # see their positional arguments. Everything after `--` is task argv.
    CMD_POOL_JSON=0
    _cp_n=$# _cp_i=0 _cp_argv=0
    while [ "$_cp_i" -lt "$_cp_n" ]; do
        _cp_arg="$1"; shift; _cp_i=$((_cp_i + 1))
        if [ "$_cp_argv" -eq 1 ]; then set -- "$@" "$_cp_arg"; continue; fi
        case "$_cp_arg" in
            --) _cp_argv=1; set -- "$@" "$_cp_arg" ;;
            -j|--json) CMD_POOL_JSON=1 ;;
            *) set -- "$@" "$_cp_arg" ;;
        esac
    done
    _cp_action="${1:-}"
    [ $# -eq 0 ] || shift
    case "$_cp_action" in
        -h|--help|'') cmd_pool_help; [ -n "$_cp_action" ]; return ;;
        create|add|seal|claim|complete|cancel|status|list|run) ;;
        *) cli_error pool invalid_input "unknown pool command '$_cp_action'" "run hydra pool --help"; return 1 ;;
    esac
    pool_project || return 1
    case "$_cp_action" in
        create) cmd_pool_create "$@" ;;
        add) cmd_pool_add "$@" ;;
        seal) cmd_pool_seal "$@" ;;
        claim) cmd_pool_claim "$@" ;;
        complete) cmd_pool_complete "$@" ;;
        cancel) cmd_pool_cancel "$@" ;;
        list) cmd_pool_list ;;
        run) cmd_pool_run "$@" ;;
        status)
            _cp_dir="$(pool_dir "${1:-}")" || return 1
            if [ "$CMD_POOL_JSON" -eq 1 ]; then json_success "pool status" "$(cmd_pool_status_json "$_cp_dir")"; else cmd_pool_status_text "$_cp_dir"; fi
            ;;
    esac
}
