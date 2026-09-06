#!/bin/sh
# Durable requests are distinct from automatic policy-issued gate approvals.

workflow_approval_binding() (
    _wab_dir="$1" _wab_step="$2" _wab_head="$3" _wab_name="$4" _wab_out="$5"
    workflow_bindings_match "$_wab_dir" || exit 1
    parallel_head_load "$_wab_head" || exit 1
    parallel_validate_name "$_wab_name" || exit 1
    _wab_fingerprint="$(workflow_data_tool fingerprint "$PARALLEL_WORKTREE")" || exit 1
    {
        printf 'definition\t%s\n' "$(sed -n '1p' "$_wab_dir/definition-hash")"
        printf 'base\t%s\n' "$(sed -n '1p' "$_wab_dir/base-commit")"
        printf 'head\t%s\n' "$PARALLEL_HEAD_ID"
        printf 'instance\t%s\n' "$(sed -n '1p' "$PARALLEL_HEAD_DIR/current-instance")"
        printf 'worktree\t%s\n' "$_wab_fingerprint"
        if [ -f "$_wab_dir/data-hash" ]; then
            printf 'data\t%s\n' "$(sed -n '1p' "$_wab_dir/data-hash")"
            printf 'inputs\t%s\n' "$(git hash-object "$_wab_dir/inputs.json")"
        fi
    } > "$_wab_out" || exit 1
    for _wab_queued in "$PARALLEL_HEAD_DIR"/messages/queue/*; do
        [ -f "$_wab_queued" ] && [ ! -L "$_wab_queued" ] || continue
        _wab_message_id="${_wab_queued##*/}"
        _wab_metadata="$PARALLEL_HEAD_DIR/messages/metadata/$_wab_message_id"
        [ "$(sed -n 's/^delivery=//p' "$_wab_metadata" 2>/dev/null)" = safe-point ] || continue
        _wab_message_hash="$(git hash-object "$_wab_queued")" || exit 1
        _wab_metadata_hash="$(git hash-object "$_wab_metadata")" || exit 1
        printf 'steering\t%s\t%s\t%s\n' "$_wab_message_id" "$_wab_message_hash" "$_wab_metadata_hash" >> "$_wab_out" || exit 1
    done
    while IFS="$(printf '\t')" read -r _wab_action _wab_profile; do
        [ -n "$_wab_profile" ] || continue
        _wab_contract="$(cmd_fleet_dispatch agent-profile contract "$_wab_profile")" || exit 1
        printf 'profile\t%s\t%s\n' "$_wab_action" "$(printf '%s' "$_wab_contract" | git hash-object --stdin)" >> "$_wab_out" || exit 1
    done <<EOF
$(awk -F '\t' '$1=="step" && $3=="exec" && $10!="-" {print $2 "\t" $10}' "$_wab_dir/graph.tsv")
EOF
    _wab_needs="$(awk -F '\t' -v id="$_wab_step" '$1=="step" && $2==id {print $4; exit}' "$_wab_dir/graph.tsv")"
    IFS=,
    set -f
    for _wab_dep in $_wab_needs; do
        [ "$_wab_dep" != - ] || continue
        _wab_sd="$_wab_dir/steps/$_wab_dep"
        [ "$(sed -n '1p' "$_wab_sd/state")" = succeeded ] || exit 1
        _wab_attempt="$(sed -n '1p' "$_wab_sd/authoritative-attempt")"
        case "$_wab_attempt" in ''|*[!0-9]*) exit 1 ;; esac
        printf 'dependency\t%s\t%s\n' "$_wab_dep" "$_wab_attempt" >> "$_wab_out"
        for _wab_file in exit-code stdout stderr outputs.json; do
            _wab_path="$_wab_sd/attempt-$_wab_attempt/$_wab_file"
            [ -f "$_wab_path" ] || continue
            printf '%s\t%s\n' "$_wab_file" "$(git hash-object "$_wab_path")" >> "$_wab_out"
        done
    done
    _wab_gate="$PARALLEL_HEAD_DIR/gates/$_wab_name"
    if [ -f "$_wab_gate/latest-run" ]; then
        _wab_gate_run="$(sed -n '1p' "$_wab_gate/latest-run")"
        hydra_valid_id "$_wab_gate_run" || exit 1
        printf 'gate\t%s\n' "$_wab_gate_run" >> "$_wab_out"
        for _wab_file in status argv head-commit worktree-hash stdout stderr; do
            _wab_hash="$(git hash-object "$_wab_gate/runs/$_wab_gate_run/$_wab_file")" || exit 1
            printf '%s\t%s\n' "$_wab_file" "$_wab_hash" >> "$_wab_out"
        done
    fi
    git hash-object "$_wab_out"
)

workflow_approval_create() (
    _wac_dir="$1" _wac_step="$2" _wac_head="$3" _wac_name="$4" _wac_message="$5" _wac_timeout="$6"
    _wac_sd="$_wac_dir/steps/$_wac_step"
    mkdir -p "$_wac_dir/approvals" || exit 1
    _wac_tmp="$(mktemp -d "$_wac_dir/approvals/.request.XXXXXX")" || exit 1
    trap 'rm -rf "$_wac_tmp"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    _wac_hash="$(workflow_approval_binding "$_wac_dir" "$_wac_step" "$_wac_head" "$_wac_name" "$_wac_tmp/binding.tsv")" || exit 1
    _wac_previous="$(sed -n '1p' "$_wac_sd/request-id" 2>/dev/null || true)"
    _wac_id="$(hydra_new_id step "$_wac_dir|$_wac_step|$_wac_hash|$_wac_previous|$_wac_tmp")" || exit 1
    _wac_now="$(date +%s)" _wac_expires=0
    [ "$_wac_timeout" = - ] || _wac_expires=$((_wac_now + _wac_timeout))
    workflow_atomic_scalar "$_wac_tmp/request-id" "$_wac_id" &&
        workflow_atomic_scalar "$_wac_tmp/step-id" "$_wac_step" &&
        workflow_atomic_scalar "$_wac_tmp/head" "$_wac_head" &&
        workflow_atomic_scalar "$_wac_tmp/name" "$_wac_name" &&
        workflow_atomic_scalar "$_wac_tmp/message" "$_wac_message" &&
        workflow_atomic_scalar "$_wac_tmp/binding-hash" "$_wac_hash" &&
        workflow_atomic_scalar "$_wac_tmp/created-at" "$_wac_now" &&
        workflow_atomic_scalar "$_wac_tmp/expires-at" "$_wac_expires" &&
        workflow_atomic_scalar "$_wac_tmp/state" pending || exit 1
    mv "$_wac_tmp" "$_wac_dir/approvals/$_wac_id" || exit 1
    workflow_atomic_scalar "$_wac_sd/attempts" 1 &&
        workflow_atomic_scalar "$_wac_sd/request-id" "$_wac_id" &&
        workflow_atomic_scalar "$_wac_sd/state" waiting-approval || exit 1
    workflow_event "$_wac_dir" "$_wac_step" approval.requested "request=$_wac_id binding=$_wac_hash"
)

workflow_approval_resume() (
    _wap_dir="$1"
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    # shellcheck disable=SC2094 # Both graph accesses are read-only.
    while IFS="$(printf '\t')" read -r _wap_tag _wap_step _wap_kind _wap_rest; do
        [ "$_wap_tag" = step ] && [ "$_wap_kind" = approval-wait ] || continue
        _wap_sd="$_wap_dir/steps/$_wap_step"
        [ "$(sed -n '1p' "$_wap_sd/state")" = waiting-approval ] || continue
        _wap_id="$(sed -n '1p' "$_wap_sd/request-id")"
        hydra_valid_id "$_wap_id" || exit 1
        _wap_request="$_wap_dir/approvals/$_wap_id"
        _wap_lock="workflow_approval_$(sed -n '1p' "$_wap_dir/run-id")_$_wap_step"
        acquire_lock "$_wap_lock" 'workflow approval' "$_wap_step" || exit 1
        _wap_tmp="$(mktemp -d)" || { release_lock "$_wap_lock"; exit 1; }
        trap 'rm -rf "$_wap_tmp"; release_lock "$_wap_lock"' EXIT
        _wap_head="$(sed -n '1p' "$_wap_request/head")" _wap_name="$(sed -n '1p' "$_wap_request/name")"
        _wap_hash="$(workflow_approval_binding "$_wap_dir" "$_wap_step" "$_wap_head" "$_wap_name" "$_wap_tmp/binding.tsv")" || exit 1
        _wap_expires="$(sed -n '1p' "$_wap_request/expires-at")"
        _wap_stale=""
        if [ "$_wap_hash" != "$(sed -n '1p' "$_wap_request/binding-hash")" ]; then _wap_stale=stale
        elif [ "$_wap_expires" -ne 0 ] && [ "$(date +%s)" -ge "$_wap_expires" ]; then _wap_stale=expired; fi
        if [ -n "$_wap_stale" ]; then
            workflow_atomic_scalar "$_wap_request/state" "$_wap_stale" || exit 1
            workflow_event "$_wap_dir" "$_wap_step" "approval.$_wap_stale" "request=$_wap_id"
            _wap_timeout="$(awk -F '\t' -v id="$_wap_step" '$1=="step" && $2==id {print $17; exit}' "$_wap_dir/graph.tsv")"
            workflow_approval_create "$_wap_dir" "$_wap_step" "$_wap_head" "$_wap_name" "$(sed -n '1p' "$_wap_request/message")" "$_wap_timeout" || exit 1
        elif [ -d "$_wap_request/decision" ]; then
            [ "$(sed -n '1p' "$_wap_request/decision/binding-hash")" = "$_wap_hash" ] || exit 1
            _wap_action="$(sed -n '1p' "$_wap_request/decision/action")"
            case "$_wap_action" in approve) _wap_state=succeeded; _wap_exit=0 ;; reject) _wap_state=failed; _wap_exit=1 ;; *) exit 1 ;; esac
            workflow_atomic_scalar "$_wap_sd/attempt-1/exit-code" "$_wap_exit" &&
                workflow_atomic_scalar "$_wap_sd/attempt-1/completed-at" "$(date +%s)" &&
                workflow_atomic_scalar "$_wap_sd/authoritative-attempt" 1 &&
                workflow_atomic_scalar "$_wap_sd/state" "$_wap_state" &&
                workflow_atomic_scalar "$_wap_request/state" "$_wap_action" || exit 1
            workflow_event "$_wap_dir" "$_wap_step" "approval.$_wap_action" "request=$_wap_id"
        fi
        rm -rf "$_wap_tmp"; release_lock "$_wap_lock"; trap - EXIT
    done < "$_wap_dir/graph.tsv"
)

workflow_approval_decide() (
    _wad_dir="$1" _wad_id="$2" _wad_action="$3" _wad_label="$4"
    hydra_valid_id "$_wad_id" || exit 1
    case "$_wad_action" in approve|reject) ;; *) exit 1 ;; esac
    _wad_request="$_wad_dir/approvals/$_wad_id"
    [ -d "$_wad_request" ] || exit 1
    _wad_step="$(sed -n '1p' "$_wad_request/step-id")"
    profile_validate_name "$_wad_step" || exit 1
    _wad_lock="workflow_approval_$(sed -n '1p' "$_wad_dir/run-id")_$_wad_step"
    acquire_lock "$_wad_lock" 'workflow approval decision' "$_wad_step" || exit 1
    _wad_tmp="$(mktemp -d "$_wad_request/.decision.XXXXXX")" || { release_lock "$_wad_lock"; exit 1; }
    trap 'rm -rf "$_wad_tmp"; release_lock "$_wad_lock"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    [ "$(sed -n '1p' "$_wad_dir/steps/$_wad_step/request-id")" = "$_wad_id" ] || exit 1
    [ "$(sed -n '1p' "$_wad_dir/steps/$_wad_step/state")" = waiting-approval ] || exit 1
    _wad_hash="$(workflow_approval_binding "$_wad_dir" "$_wad_step" "$(sed -n '1p' "$_wad_request/head")" "$(sed -n '1p' "$_wad_request/name")" "$_wad_tmp/binding.tsv")" || exit 1
    [ "$_wad_hash" = "$(sed -n '1p' "$_wad_request/binding-hash")" ] || exit 1
    _wad_expires="$(sed -n '1p' "$_wad_request/expires-at")"
    [ "$_wad_expires" -eq 0 ] || [ "$(date +%s)" -lt "$_wad_expires" ] || exit 1
    if [ -d "$_wad_request/decision" ]; then
        [ "$(sed -n '1p' "$_wad_request/decision/action")" = "$_wad_action" ] &&
            [ "$(sed -n '1p' "$_wad_request/decision/binding-hash")" = "$_wad_hash" ]
        exit $?
    fi
    workflow_atomic_scalar "$_wad_tmp/request-id" "$_wad_id" &&
        workflow_atomic_scalar "$_wad_tmp/action" "$_wad_action" &&
        workflow_atomic_scalar "$_wad_tmp/binding-hash" "$_wad_hash" &&
        workflow_atomic_scalar "$_wad_tmp/source" local-cli &&
        workflow_atomic_scalar "$_wad_tmp/principal-uid" "$(id -u)" &&
        workflow_atomic_scalar "$_wad_tmp/actor-label" "$_wad_label" &&
        workflow_atomic_scalar "$_wad_tmp/decided-at" "$(date +%s)" || exit 1
    mv "$_wad_tmp" "$_wad_request/decision" || exit 1
    workflow_event "$_wad_dir" "$_wad_step" approval.decided "request=$_wad_id action=$_wad_action source=local-cli"
)

workflow_approval_list() {
    _wal_dir="$1" _wal_json="$2" _wal_first=1
    if [ "$_wal_json" = --json ]; then
        printf '{"schema_version":1,"ok":true,"command":"workflow requests","data":{"requests":['
    fi
    for _wal_sd in "$_wal_dir"/steps/*; do
        [ -f "$_wal_sd/request-id" ] || continue
        _wal_id="$(sed -n '1p' "$_wal_sd/request-id")"
        hydra_valid_id "$_wal_id" || return 1
        _wal_rd="$_wal_dir/approvals/$_wal_id"
        _wal_state="$(sed -n '1p' "$_wal_rd/state")"
        if [ "$_wal_json" = --json ]; then
            [ "$_wal_first" -eq 1 ] || printf ','; _wal_first=0
            printf '{"request_id":"%s","step_id":"%s","state":"%s","binding":"%s","expires_at":%s,"message":"%s","evidence_path":"%s","decision":"%s"}' \
                "$_wal_id" "$(basename "$_wal_sd")" "$_wal_state" "$(sed -n '1p' "$_wal_rd/binding-hash")" \
                "$(sed -n '1p' "$_wal_rd/expires-at")" "$(json_escape "$(sed -n '1p' "$_wal_rd/message")")" \
                "$(json_escape "$_wal_rd/binding.tsv")" "$(sed -n '1p' "$_wal_rd/decision/action" 2>/dev/null || true)"
        else
            printf '%s [%s]: %s; binding=%s; evidence=%s\n' "$_wal_id" "$(basename "$_wal_sd")" "$_wal_state" "$(sed -n '1p' "$_wal_rd/binding-hash")" "$_wal_rd/binding.tsv"
        fi
    done
    [ "$_wal_json" != --json ] || printf ']}}\n'
}

# A decision may be recorded long before its dependent gets a worker slot.
workflow_approval_guard() (
    _wag_dir="$1" _wag_step="$2"
    _wag_needs="$(awk -F '\t' -v id="$_wag_step" '$1=="step" && $2==id {print $4; exit}' "$_wag_dir/graph.tsv")"
    _wag_tmp="$(mktemp -d)" || exit 1
    trap 'rm -rf "$_wag_tmp"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    _wag_ifs="$IFS"
    _wag_flags="$-"
    IFS=,
    set -f
    # shellcheck disable=SC2086 # Only the declared dependency separator splits.
    set -- $_wag_needs
    IFS="$_wag_ifs"
    case "$_wag_flags" in *f*) ;; *) set +f ;; esac
    for _wag_dep in "$@"; do
        [ "$_wag_dep" != - ] || continue
        [ -f "$_wag_dir/steps/$_wag_dep/request-id" ] || continue
        _wag_id="$(sed -n '1p' "$_wag_dir/steps/$_wag_dep/request-id")"
        hydra_valid_id "$_wag_id" || exit 1
        _wag_rd="$_wag_dir/approvals/$_wag_id"
        [ "$(sed -n '1p' "$_wag_rd/decision/action")" = approve ] || exit 1
        _wag_hash="$(workflow_approval_binding "$_wag_dir" "$_wag_dep" "$(sed -n '1p' "$_wag_rd/head")" "$(sed -n '1p' "$_wag_rd/name")" "$_wag_tmp/binding.tsv")" || exit 1
        [ "$_wag_hash" = "$(sed -n '1p' "$_wag_rd/decision/binding-hash")" ] || exit 1
        _wag_expires="$(sed -n '1p' "$_wag_rd/expires-at")"
        [ "$_wag_expires" -eq 0 ] || [ "$(date +%s)" -lt "$_wag_expires" ] || exit 1
    done
)
