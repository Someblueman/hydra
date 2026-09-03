#!/bin/sh
# Native mission-control dispatch and its escaped tabular read boundary.

HYDRA_TUI_TIMEOUT_SECONDS="${HYDRA_TUI_TIMEOUT_SECONDS:-2}"

tui_native_candidate_is_qualified() {
    [ -x "$1" ] || return 1
    case "$HYDRA_TUI_TIMEOUT_SECONDS" in ''|*[!0-9]*|0) HYDRA_TUI_TIMEOUT_SECONDS=2 ;; esac
    _tnciq_dir="$(mktemp -d "${TMPDIR:-/tmp}/hydra-tui-handshake.XXXXXX")" || return 1
    "$1" --version > "$_tnciq_dir/stdout" 2>/dev/null &
    _tnciq_pid=$!
    (
        _tnciq_sleep=""
        trap '[ -z "$_tnciq_sleep" ] || kill "$_tnciq_sleep" 2>/dev/null; exit 0' TERM HUP INT
        sleep "$HYDRA_TUI_TIMEOUT_SECONDS" &
        _tnciq_sleep=$!
        wait "$_tnciq_sleep" || exit 0
        if kill -0 "$_tnciq_pid" 2>/dev/null; then
            : > "$_tnciq_dir/timed-out"
            kill "$_tnciq_pid" 2>/dev/null || true
            sleep 1
            kill -9 "$_tnciq_pid" 2>/dev/null || true
        fi
    ) >/dev/null 2>&1 &
    _tnciq_watchdog=$!
    if wait "$_tnciq_pid"; then _tnciq_status=0; else _tnciq_status=$?; fi
    kill "$_tnciq_watchdog" 2>/dev/null || true
    wait "$_tnciq_watchdog" 2>/dev/null || true
    if [ -f "$_tnciq_dir/timed-out" ] || [ "$_tnciq_status" -ne 0 ] || \
       [ "$(wc -l < "$_tnciq_dir/stdout" | tr -d ' ')" != 1 ] || \
       [ "$(sed -n '1p' "$_tnciq_dir/stdout")" != "Hydra TUI $HYDRA_VERSION protocol 2" ]; then
        rm -rf "$_tnciq_dir"
        return 1
    fi
    rm -rf "$_tnciq_dir"
}

tui_native_find_binary() {
    if [ -n "${HYDRA_TUI_BIN:-}" ]; then
        tui_native_candidate_is_qualified "$HYDRA_TUI_BIN" || return 1
        printf '%s\n' "$HYDRA_TUI_BIN"
        return 0
    fi
    for _tnfb_candidate in \
        "$HYDRA_BIN_DIR/../build/hydra-tui" \
        "$HYDRA_BIN_DIR/../libexec/hydra/hydra-tui" \
        "$HYDRA_BIN_DIR/../lib/hydra/hydra-tui"; do
        if tui_native_candidate_is_qualified "$_tnfb_candidate"; then
            printf '%s\n' "$_tnfb_candidate"
            return 0
        fi
    done
    return 1
}

tui_native_capabilities() {
    _tnc_json="${1:-0}"
    _tnc_native=false
    _tnc_path=""
    if _tnc_path="$(tui_native_find_binary 2>/dev/null)"; then
        _tnc_native=true
    fi
    if [ "$_tnc_json" -eq 1 ]; then
        printf '{"schema_version":1,"ok":true,"command":"tui capabilities","data":{'
        printf '"default_mode":"native","fallback_mode":"basic","basic":true,"native":%s,' "$_tnc_native"
        printf '"native_path":%s,' "$(json_string_or_null "$_tnc_path")"
        printf '"observation":"bounded-polling","tmux_control_mode":false,'
        printf '"headless_fixture":true,"mutation_authority":"shell-cli"}}\n'
    else
        printf 'default mode: native (visible basic fallback)\n'
        printf 'basic TUI: available\n'
        if [ "$_tnc_native" = true ]; then
            printf 'native TUI: available at %s\n' "$_tnc_path"
        else
            printf 'native TUI: unavailable (run make build-tui or install a native artifact)\n'
        fi
        printf 'observation: measured bounded polling; tmux control mode not enabled\n'
        printf 'mutations: delegated to the shell CLI with argv execution\n'
    fi
}

tui_native_safe_field() {
    printf '%s' "${1:-}" | tr '\t\r\n' '   '
}

tui_native_count_lines() {
    if [ -f "$1" ]; then
        awk 'END { print NR + 0 }' "$1" 2>/dev/null || printf '0\n'
    else
        printf '0\n'
    fi
}

tui_native_count_entries() {
    _tnce_root="$1"
    _tnce_pattern="$2"
    if [ ! -d "$_tnce_root" ]; then
        printf '0\n'
        return 0
    fi
    find "$_tnce_root" -maxdepth 1 -name "$_tnce_pattern" -type d ! -path "$_tnce_root" 2>/dev/null | awk 'END { print NR + 0 }'
}

tui_native_set_profile_status() {
    _tnsps_profile="$1"
    _tned_adapter_source='hydra capabilities --json'
    case "$_tnsps_profile" in
        -|none)
            _tned_adapter=none _tned_adapter_confidence=exact
            ;;
        claude|codex)
            _tned_adapter=none _tned_adapter_confidence=verified-local-help
            ;;
        cursor|copilot|aider|gemini)
            _tned_adapter=none _tned_adapter_confidence=launch-only
            ;;
        *)
            if profile_exists "$_tnsps_profile" 2>/dev/null; then
                _tned_adapter="$(profile_field "$_tnsps_profile" adapter 2>/dev/null || echo unavailable)"
                _tned_adapter_confidence="$(profile_field "$_tnsps_profile" confidence 2>/dev/null || echo unavailable)"
                _tned_adapter_source="$(profile_custom_dir "$_tnsps_profile" 2>/dev/null || printf '%s' 'hydra capabilities --json')"
                _tned_adapter="$(tui_native_safe_field "$_tned_adapter")"
                _tned_adapter_confidence="$(tui_native_safe_field "$_tned_adapter_confidence")"
            else
                _tned_adapter=unavailable _tned_adapter_confidence=unavailable
            fi
            ;;
    esac
}

tui_native_emit_invalid_heads() {
    _tneih_project_dir="$1"
    [ -d "$_tneih_project_dir/heads" ] || return 0
    for _tneih_dir in "$_tneih_project_dir"/heads/head_*; do
        [ -d "$_tneih_dir" ] || continue
        _tneih_id="$(basename "$_tneih_dir")"
        _tneih_branch="$(sed -n '1p' "$_tneih_dir/branch" 2>/dev/null || true)"
        _tneih_instance="$(sed -n '1p' "$_tneih_dir/current-instance" 2>/dev/null || true)"
        if ! hydra_valid_id "$_tneih_id" || \
           [ "$(sed -n '1p' "$_tneih_dir/head-id" 2>/dev/null || true)" != "$_tneih_id" ] || \
           [ -z "$_tneih_branch" ] || \
           [ -z "$(sed -n '1p' "$_tneih_dir/session" 2>/dev/null || true)" ] || \
           ! hydra_valid_id "$_tneih_instance" || \
           [ ! -d "$_tneih_dir/instances/$_tneih_instance" ] || \
           [ "$(sed -n '1p' "$_tneih_dir/instances/$_tneih_instance/instance-id" 2>/dev/null || true)" != "$_tneih_instance" ]; then
            printf 'R\tmalformed-state\t%s\t%s\texact\thydra state verify\n' \
                "$(tui_native_safe_field "${_tneih_branch:-$_tneih_id}")" \
                "$(tui_native_safe_field "$_tneih_dir")"
        fi
    done
}

# Protocol v2: one H row per head and one R row per recovery finding.
# Fields never contain tabs or newlines. The first row is the protocol handshake.
tui_native_emit_data() {
    printf 'HYDRA_TUI\t2\n'
    tmux_load_snapshot
    _tned_project_id="$(hydra_get_project_id 2>/dev/null || true)"
    _tned_project_dir=""
    if [ -n "$_tned_project_id" ]; then
        _tned_project_dir="$(state_v2_project_dir "$_tned_project_id" 2>/dev/null || true)"
    fi
    _tned_notification_source="$(notify_config_file 2>/dev/null || true)"
    _tned_notification_source_safe="$(tui_native_safe_field "$_tned_notification_source")"
    _tned_notification_count=0
    if [ -s "$_tned_notification_source" ]; then
        _tned_notification_count="$(awk 'NF == 3 { n++ } END { print n + 0 }' "$_tned_notification_source" 2>/dev/null || printf '0\n')"
    fi
    _tned_state_source="$(tui_native_safe_field "$_tned_project_dir")"
    _tned_snapshot_rows="$(state_list_heads | HYDRA_TUI_SESSION_SNAPSHOT="${_TMUX_SNAPSHOT_SESSIONS:-}" awk '
        BEGIN {
            sessions = ENVIRON["HYDRA_TUI_SESSION_SNAPSHOT"]
            count = split(sessions, names, "\n")
            for (i = 1; i <= count; i++) if (names[i] != "") live[names[i]] = 1
        }
        { print (($2 in live) ? "active" : "dead") " " $0 }
    ')"

    while IFS=' ' read -r _tned_status _tned_branch _tned_session _tned_ai _tned_group _tned_created _tned_deps _tned_pr _tned_extra; do
        [ -n "$_tned_branch" ] || continue
        _tned_liveness=stopped
        if [ "$_tned_status" = active ]; then
            _tned_liveness=live
        fi

        _tned_declared="" _tned_observed=unavailable _tned_confidence=unavailable
        _tned_instance="" _tned_profile="${_tned_ai:--}" _tned_desired=unavailable
        _tned_events=0 _tned_signals=0 _tned_messages=0 _tned_claims=0 _tned_scopes=0
        _tned_queue=0 _tned_resources=0 _tned_diff=0 _tned_gates=0 _tned_approved=0
        _tned_source="$_tned_project_dir"
        _tned_source_safe="$_tned_state_source"
        _tned_head_id=""
        _tned_head_dir=""

        if [ -n "$_tned_project_id" ]; then
            _tned_head_id="$(state_v2_find_head_by_branch "$_tned_project_id" "$_tned_branch" 2>/dev/null || true)"
        fi
        if [ -n "$_tned_head_id" ]; then
            _tned_head_dir="$(state_v2_head_dir "$_tned_project_id" "$_tned_head_id" 2>/dev/null || true)"
            _tned_instance="$(sed -n '1p' "$_tned_head_dir/current-instance" 2>/dev/null || true)"
            _tned_desired="$(sed -n '1p' "$_tned_head_dir/desired-state" 2>/dev/null || echo unavailable)"
            _tned_profile="$(sed -n '1p' "$_tned_head_dir/profile" 2>/dev/null || printf '%s' "$_tned_profile")"
            _tned_instance_dir="$_tned_head_dir/instances/$_tned_instance"
            if [ -d "$_tned_instance_dir" ]; then
                _tned_declared="$(sed -n '1p' "$_tned_instance_dir/declared-outcome" 2>/dev/null || true)"
                _tned_observed="$(sed -n '1p' "$_tned_instance_dir/observed-status" 2>/dev/null || echo unavailable)"
                _tned_confidence="$(sed -n '1p' "$_tned_instance_dir/observed-confidence" 2>/dev/null || echo unavailable)"
            fi
            _tned_events="$(tui_native_count_lines "$_tned_head_dir/events/events.jsonl")"
            _tned_signals="$(awk '/"type":"signal\./ { n++ } END { print n + 0 }' "$_tned_head_dir/events/events.jsonl" 2>/dev/null || printf '0\n')"
            if [ -d "$_tned_head_dir/messages/queue" ]; then
                _tned_messages="$(find "$_tned_head_dir/messages/queue" -maxdepth 1 -type f 2>/dev/null | awk 'END { print NR + 0 }')"
            fi
            _tned_scopes="$(tui_native_count_lines "$_tned_head_dir/scopes")"
            _tned_gates="$(tui_native_count_entries "$_tned_head_dir/gates" '*')"
            if [ -d "$_tned_head_dir/gates" ]; then
                _tned_approved="$(find "$_tned_head_dir/gates" -name approved-at -type f 2>/dev/null | awk 'END { print NR + 0 }')"
            fi
            _tned_worktree="$(sed -n '1p' "$_tned_head_dir/worktree" 2>/dev/null || true)"
            if [ -d "$_tned_worktree" ]; then
                _tned_diff="$(git -C "$_tned_worktree" status --short 2>/dev/null | awk 'END { print NR + 0 }')"
            fi
            _tned_source="$_tned_head_dir"
            _tned_source_safe="$(tui_native_safe_field "$_tned_head_dir")"
            if [ -d "$_tned_project_dir/claims" ]; then
                _tned_claims="$(find "$_tned_project_dir/claims" -name owner-head -type f -exec awk -v id="$_tned_head_id" '$0 == id { n++ } END { print n + 0 }' {} + 2>/dev/null | awk '{ n += $1 } END { print n + 0 }')"
            fi
            if [ -d "$_tned_project_dir/resources/$_tned_head_id" ]; then
                _tned_resources="$(tui_native_count_lines "$_tned_project_dir/resources/$_tned_head_id/ports")"
                [ -z "$(sed -n '1p' "$_tned_project_dir/resources/$_tned_head_id/compose-project" 2>/dev/null || true)" ] || _tned_resources=$((_tned_resources + 1))
                [ -z "$(sed -n '1p' "$_tned_project_dir/resources/$_tned_head_id/database" 2>/dev/null || true)" ] || _tned_resources=$((_tned_resources + 1))
            fi
            _tned_queue_dir="$(_get_queue_dir)"
            if [ -d "$_tned_queue_dir" ]; then
                _tned_queue="$(grep -l -F -x "branch=$_tned_branch" "$_tned_queue_dir"/*.queue 2>/dev/null | awk 'END { print NR + 0 }')"
            fi
        fi

        tui_native_set_profile_status "$_tned_profile"

        if [ "$_tned_adapter_source" = 'hydra capabilities --json' ]; then
            _tned_adapter_source_safe="$_tned_adapter_source"
        else
            _tned_adapter_source_safe="$(tui_native_safe_field "$_tned_adapter_source")"
        fi
        printf 'H\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$_tned_branch" "$_tned_session" "$_tned_profile" "$_tned_group" "$_tned_pr" \
            "$_tned_status" "$_tned_liveness" \
            "$_tned_declared" "$_tned_observed" "$_tned_confidence" "$_tned_instance" \
            "$_tned_events" "$_tned_signals" "$_tned_messages" "$_tned_claims" "$_tned_scopes" \
            "$_tned_queue" "$_tned_resources" "$_tned_diff" "$_tned_gates" "$_tned_approved" \
            "$_tned_desired" "$_tned_source_safe" "$_tned_head_id" \
            "$_tned_adapter" "$_tned_adapter_confidence" "$_tned_adapter_source_safe" \
            "$_tned_notification_count" "$_tned_notification_source_safe"

        if [ "$_tned_status" = dead ]; then
            printf 'R\tdead-session\t%s\t%s\texact\thydra doctor\n' \
                "$_tned_branch" "$_tned_state_source"
        fi
        if [ -n "${_tned_extra:-}" ]; then
            printf 'R\tmalformed-state\t%s\t%s\texact\thydra state verify\n' \
                "$_tned_branch" "$_tned_state_source"
        fi
        if [ "$_tned_desired" = stopping ] && [ -n "$_tned_head_dir" ]; then
            printf 'R\tteardown-failure\t%s\t%s\texact\thydra worktree doctor status\n' \
                "$_tned_branch" "$_tned_source_safe/desired-state"
            printf 'R\tinterrupted-lifecycle\t%s\t%s\texact\thydra lifecycle %s\n' \
                "$_tned_branch" "$_tned_source_safe/desired-state" "$_tned_branch"
        fi
    done <<EOF
$_tned_snapshot_rows
EOF

    if [ -n "$_tned_project_dir" ]; then
        tui_native_emit_invalid_heads "$_tned_project_dir"
    fi

    if [ -d "$HYDRA_HOME/locks" ]; then
        find "$HYDRA_HOME/locks" -name '*.lock' -type d 2>/dev/null | while IFS= read -r _tned_lock; do
            if lock_dir_is_stale "$_tned_lock"; then
                printf 'R\tstale-lock\t%s\t%s\texact\thydra doctor\n' \
                    "$(basename "$_tned_lock")" "$(tui_native_safe_field "$_tned_lock")"
            fi
        done
    fi
    list_orphan_worktree_paths 2>/dev/null | while IFS= read -r _tned_orphan; do
        [ -n "$_tned_orphan" ] || continue
        printf 'R\torphan-worktree\t%s\t%s\texact\thydra gc --dry-run\n' \
            "$(basename "$_tned_orphan")" "$(tui_native_safe_field "$_tned_orphan")"
    done
}

tui_native_exec() {
    _tne_binary="$(tui_native_find_binary)" || {
        echo "Error: native TUI is unavailable" >&2
        echo "Next: run make build-tui, install the native artifact, or use hydra tui --basic" >&2
        return 1
    }
    exec "$_tne_binary" --hydra "$HYDRA_BIN_CMD" "$@"
}

tui_native_run() {
    _tnr_saved_stty=""
    if [ -t 0 ]; then
        _tnr_saved_stty="$(stty -g 2>/dev/null || true)"
    fi
    _tnr_binary="$(tui_native_find_binary 2>/dev/null || true)"
    if [ -z "$_tnr_binary" ]; then
        if [ -n "$_tnr_saved_stty" ]; then
            stty "$_tnr_saved_stty" 2>/dev/null || true
        fi
        echo "Warning: native TUI is unavailable; starting the basic TUI" >&2
        cmd_tui --basic
        return $?
    fi
    if "$_tnr_binary" --hydra "$HYDRA_BIN_CMD" "$@"; then
        _tnr_status=0
    else
        _tnr_status=$?
    fi
    case "$_tnr_status" in
        0|2|129|130|131|133|137|143) return "$_tnr_status" ;;
        *)
            if [ -n "$_tnr_saved_stty" ]; then
                stty "$_tnr_saved_stty" 2>/dev/null || true
            fi
            echo "Warning: native TUI exited with status $_tnr_status; starting the basic TUI" >&2
            cmd_tui --basic
            return $?
            ;;
    esac
}
