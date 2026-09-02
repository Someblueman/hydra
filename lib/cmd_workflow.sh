#!/bin/sh

cmd_workflow() {
    _cw_action="${1:-}"
    case "$_cw_action" in
        -h|--help|'')
            printf '%s\n' \
                'Usage: hydra workflow list' \
                '       hydra workflow show <id|path>' \
                '       hydra workflow validate <id|path>' \
                '       hydra workflow dry-run <id|path>' \
                '       hydra workflow run <id|path>' \
                '       hydra workflow status <run-id> [--json]' \
                '       hydra workflow cancel <run-id>' \
                '       hydra workflow resume <run-id>'
            [ -n "$_cw_action" ]
            return
            ;;
        list)
            [ "$#" -eq 1 ] || { cli_error workflow invalid_arguments "list accepts no arguments" "run hydra workflow --help"; return 1; }
            workflow_list
            ;;
        show|validate|dry-run)
            [ "$#" -eq 2 ] || { cli_error workflow invalid_arguments "$_cw_action requires exactly one workflow ID or path" "run hydra workflow --help"; return 1; }
            _cw_file="$(workflow_resolve "$2")" || return 1
            workflow_require_trust "$_cw_file" || return 1
            case "$_cw_action" in
                show) workflow_parse "$_cw_file" normalized ;;
                validate)
                    workflow_parse "$_cw_file" validate || return 1
                    printf 'Valid workflow: %s\n' "$_cw_file"
                    ;;
                dry-run) workflow_parse "$_cw_file" dry-run ;;
            esac
            ;;
        run)
            [ "$#" -eq 2 ] || { cli_error workflow invalid_arguments "run requires exactly one workflow ID or path" "run hydra workflow --help"; return 1; }
            _cw_file="$(workflow_resolve "$2")" || return 1
            workflow_require_trust "$_cw_file" || return 1
            workflow_parse "$_cw_file" validate || return 1
            _cw_runs="$(workflow_runs_dir)" || return 1; mkdir -p "$_cw_runs" || return 1
            _cw_project="$(hydra_get_project_id)" _cw_base="$(git rev-parse HEAD)"
            _cw_run="$(hydra_new_id run "$_cw_project|workflow|$_cw_file")" || return 1
            _cw_dir="$_cw_runs/$_cw_run"; mkdir -p "$_cw_dir/steps" || return 1; chmod 700 "$_cw_dir" 2>/dev/null || true
            workflow_parse "$_cw_file" normalized > "$_cw_dir/resolved.yml" || return 1
            workflow_parse "$_cw_file" runtime > "$_cw_dir/graph.tsv" || return 1
            _cw_hash="$(git hash-object "$_cw_dir/resolved.yml")" _cw_created="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
            workflow_atomic_scalar "$_cw_dir/run-id" "$_cw_run"; workflow_atomic_scalar "$_cw_dir/schema-version" 1
            workflow_atomic_scalar "$_cw_dir/runtime-version" 1; workflow_atomic_scalar "$_cw_dir/project-id" "$_cw_project"
            workflow_atomic_scalar "$_cw_dir/definition-hash" "$_cw_hash"; workflow_atomic_scalar "$_cw_dir/base-commit" "$_cw_base"
            workflow_atomic_scalar "$_cw_dir/owner" "${USER:-unknown}"; workflow_atomic_scalar "$_cw_dir/created-at" "$_cw_created"
            IFS="$(printf '\t')" read -r _cw_header _cw_workflow _cw_parallelism _cw_disk _cw_heads < "$_cw_dir/graph.tsv"
            workflow_atomic_scalar "$_cw_dir/parallelism" "$_cw_parallelism"; workflow_atomic_scalar "$_cw_dir/disk-mb" "$_cw_disk"; workflow_atomic_scalar "$_cw_dir/max-heads" "$_cw_heads"
            while IFS="$(printf '\t')" read -r _cw_tag _cw_id _cw_rest; do [ "$_cw_tag" = step ] || continue; mkdir -p "$_cw_dir/steps/$_cw_id"; workflow_atomic_scalar "$_cw_dir/steps/$_cw_id/state" queued; workflow_atomic_scalar "$_cw_dir/steps/$_cw_id/attempts" 0; done < "$_cw_dir/graph.tsv"
            workflow_atomic_scalar "$_cw_dir/state" queued; : > "$_cw_dir/events.jsonl"; workflow_event "$_cw_dir" "" run.created
            printf '%s\n' "$_cw_run"
            workflow_drive "$_cw_dir"
            ;;
        status)
            [ "$#" -ge 2 ] && [ "$#" -le 3 ] || { cli_error workflow invalid_arguments "status requires a run ID and optional --json" "run hydra workflow --help"; return 1; }
            _cw_runs="$(workflow_runs_dir)" || return 1; _cw_dir="$_cw_runs/$2"; [ -d "$_cw_dir" ] || { cli_error workflow run_not_found "workflow run not found: $2" "inspect the project workflow runs"; return 1; }
            _cw_state="$(sed -n '1p' "$_cw_dir/state")"; _cw_owner="$(sed -n '1p' "$_cw_dir/owner-pid" 2>/dev/null || true)"
            if [ "$_cw_state" = running ] && ! workflow_pid_alive "$_cw_owner"; then _cw_state=stale; fi
            if [ "${3:-}" = --json ]; then
                printf '{"schema_version":1,"run_id":"%s","state":"%s","source":"recorded","steps":[' "$2" "$_cw_state"; _cw_first=1
                while IFS="$(printf '\t')" read -r _cw_tag _cw_id _cw_rest; do [ "$_cw_tag" = step ] || continue; [ "$_cw_first" -eq 1 ] || printf ','; _cw_first=0; printf '{"step_id":"%s","state":"%s","attempts":%s}' "$_cw_id" "$(sed -n '1p' "$_cw_dir/steps/$_cw_id/state")" "$(sed -n '1p' "$_cw_dir/steps/$_cw_id/attempts")"; done < "$_cw_dir/graph.tsv"; printf ']}\n'
            else
                printf 'Workflow %s: %s (source: recorded)\n' "$2" "$_cw_state"
                while IFS="$(printf '\t')" read -r _cw_tag _cw_id _cw_kind _cw_rest; do [ "$_cw_tag" = step ] || continue; printf '  %s [%s]: %s (attempts=%s)\n' "$_cw_id" "$_cw_kind" "$(sed -n '1p' "$_cw_dir/steps/$_cw_id/state")" "$(sed -n '1p' "$_cw_dir/steps/$_cw_id/attempts")"; done < "$_cw_dir/graph.tsv"
            fi
            ;;
        cancel)
            [ "$#" -eq 2 ] || { cli_error workflow invalid_arguments "cancel requires a run ID" "run hydra workflow --help"; return 1; }
            _cw_runs="$(workflow_runs_dir)" || return 1; _cw_dir="$_cw_runs/$2"; [ -d "$_cw_dir" ] || return 1
            workflow_atomic_scalar "$_cw_dir/cancel-requested" "$(date +%s)"; workflow_event "$_cw_dir" "" run.cancel_requested
            _cw_owner="$(sed -n '1p' "$_cw_dir/owner-pid" 2>/dev/null || true)"; workflow_pid_alive "$_cw_owner" && kill -TERM "$_cw_owner" 2>/dev/null || true
            workflow_drive "$_cw_dir"
            ;;
        resume)
            [ "$#" -eq 2 ] || { cli_error workflow invalid_arguments "resume requires a run ID" "run hydra workflow --help"; return 1; }
            _cw_runs="$(workflow_runs_dir)" || return 1; _cw_dir="$_cw_runs/$2"; [ -d "$_cw_dir" ] || return 1
            workflow_bindings_match "$_cw_dir" || { workflow_atomic_scalar "$_cw_dir/state" recovery-required; cli_error workflow binding_mismatch "recorded workflow bindings do not match this checkout" "restore the recorded project and base commit"; return 1; }
            _cw_owner="$(sed -n '1p' "$_cw_dir/owner-pid" 2>/dev/null || true)"; _cw_state="$(sed -n '1p' "$_cw_dir/state")"
            if [ "$_cw_state" = running ] && workflow_pid_alive "$_cw_owner"; then cli_error workflow already_running "workflow owner is still alive" "wait or cancel the run"; return 1; fi
            case "$_cw_state" in succeeded|failed|cancelled) return 0 ;; esac
            while IFS= read -r _cw_sd; do [ -n "$_cw_sd" ] || continue; if [ "$(sed -n '1p' "$_cw_sd/state")" = running ]; then if [ -f "$_cw_sd/authoritative-attempt" ]; then workflow_atomic_scalar "$_cw_sd/state" succeeded; else workflow_atomic_scalar "$_cw_sd/state" ready; fi; fi; done <<EOF
$(find "$_cw_dir/steps" -mindepth 1 -maxdepth 1 -type d 2>/dev/null)
EOF
            workflow_event "$_cw_dir" "" run.recovered "previous_owner=$_cw_owner"; workflow_drive "$_cw_dir"
            ;;
        *)
            cli_error workflow invalid_arguments "unknown workflow command '$_cw_action'" "run hydra workflow --help"
            return 1
            ;;
    esac
}
