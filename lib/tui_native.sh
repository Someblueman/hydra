#!/bin/sh
# Native mission-control dispatch, and the rows the control centre asks the
# CLI for. The head snapshot itself is read by the helper from the state store
# (src/tui/store_rows.c); only what the store cannot say is asked here.

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
        printf '"default_mode":"native","fallback_mode":"none","basic":false,"native":%s,' "$_tnc_native"
        printf '"native_path":%s,' "$(json_string_or_null "$_tnc_path")"
        printf '"observation":"bounded-polling","tmux_control_mode":false,'
        printf '"headless_fixture":true,"mutation_authority":"shell-cli"}}\n'
    else
        printf 'default mode: native (no fallback)\n'
        if [ "$_tnc_native" = true ]; then
            printf 'native TUI: available at %s\n' "$_tnc_path"
        else
            printf 'native TUI: unavailable (run make build-tui or install a native artifact)\n'
            printf 'without it: hydra list and hydra switch still work\n'
        fi
        printf 'observation: measured bounded polling; tmux control mode not enabled\n'
        printf 'mutations: delegated to the shell CLI with argv execution\n'
    fi
}

tui_native_safe_field() {
    printf '%s' "${1:-}" | tr '\t\r\n' '   '
}

# R rows for leftover worktrees, the one part of the head snapshot the control
# centre asks the CLI for (hydra tui --orphan-rows), and only while a worktree
# directory without a head record exists. Same authority as 'hydra gc --policy
# orphaned'; the label is "<branch> (<path>), <size>" with a cached, bounded
# size, and the source is the exact path an in-app removal passes to gc --path.
# Fields never contain tabs or newlines.
tui_native_emit_orphans() {
    list_orphan_worktree_rows cached | while IFS="$(printf '\t')" read -r _tned_state _tned_obranch _tned_okib _tned_opath; do
        [ -n "$_tned_opath" ] || continue
        _tned_okind=orphan-worktree
        [ "$_tned_state" = clean ] || _tned_okind=orphan-worktree-dirty
        [ "$_tned_obranch" != - ] || _tned_obranch="detached HEAD"
        printf 'R\t%s\t%s\t%s\texact\thydra gc --policy orphaned --dry-run\n' "$_tned_okind" \
            "$(tui_native_safe_field "$_tned_obranch ($_tned_opath), $(worktree_format_kib "$_tned_okib")")" \
            "$(tui_native_safe_field "$_tned_opath")"
    done
}

# Say why the control centre cannot start, how to get the helper, and which
# commands work without it.
tui_native_report_unavailable() {
    echo "Error: native TUI is unavailable: the control centre needs the hydra-tui helper built for Hydra $HYDRA_VERSION" >&2
    echo "Next: run 'make build-tui' in a source checkout, or rerun install.sh with HYDRA_BUILD_TUI=1 or HYDRA_TUI_ARTIFACT=/path/to/hydra-tui" >&2
    echo "Meanwhile: 'hydra list' and 'hydra switch' work without it" >&2
}

# Remote work beside local work (U11): the fleet helper's visual stream, or
# an empty stream when no remote host is registered. A registered host that
# cannot be observed because the helper is missing is a recovery finding, not
# a failure of the local snapshot.
tui_native_remote_count() {
    _tnrc_dir="$HYDRA_HOME/fleet/remotes"
    if [ -d "$_tnrc_dir" ]; then
        find "$_tnrc_dir" -maxdepth 1 -name '*.json' -type f 2>/dev/null | awk 'END { print NR + 0 }'
    else
        printf '0\n'
    fi
}

tui_native_emit_remote_data() {
    _tnerd_count="$(tui_native_remote_count)"
    if [ "$_tnerd_count" -eq 0 ]; then
        printf 'HYDRA_FLEET_TUI\t3\n'
        return 0
    fi
    _load_lib cmd_fleet
    if ! _tnerd_binary="$(fleet_binary 2>/dev/null)"; then
        printf 'HYDRA_FLEET_TUI\t3\n'
        printf 'R\tfleet-unavailable\t%s registered remote host%s\t%s\texact\tmake build-fleet\n' \
            "$_tnerd_count" "$( [ "$_tnerd_count" -eq 1 ] || printf 's' )" \
            "$(tui_native_safe_field "$HYDRA_HOME/fleet/remotes")"
        return 0
    fi
    exec "$_tnerd_binary" fleet tui-visual-data
}

# One attention stream for local and remote items: the workflow items of this
# project and, when remote hosts are registered, the fleet items. A producer
# that fails contributes nothing and marks the stream partial; the item rows
# and the END counts are the producers' own.
tui_native_emit_attention_data() {
    _tnead_dir="$(mktemp -d "${TMPDIR:-/tmp}/hydra-tui-attention.XXXXXX")" || return 1
    "$HYDRA_BIN_CMD" workflow attention-data > "$_tnead_dir/local.tsv" 2>/dev/null || :
    : > "$_tnead_dir/remote.tsv"
    _tnead_remote=skip
    if [ "$(tui_native_remote_count)" -gt 0 ]; then
        _tnead_remote=merge
        "$HYDRA_BIN_CMD" fleet attention-data > "$_tnead_dir/remote.tsv" 2>/dev/null || :
    fi
    awk -F '\t' -v remote="$_tnead_remote" '
        FILENAME != file { file = FILENAME; index_of_file++; expected[index_of_file] = (remote == "merge" || index_of_file == 1) }
        $1 == "HYDRA_ATTENTION" { version[index_of_file] = $2; next }
        $1 == "ITEM" && version[index_of_file] == "3" { rows[++n] = $0; count[index_of_file]++; next }
        $1 == "END" && version[index_of_file] == "3" && $2 == count[index_of_file] + 0 {
            ended[index_of_file] = 1
            if ($3 == "1") partial = 1
            if ($4 == "1") truncated = 1
            next
        }
        END {
            ok = 0
            for (i = 1; i <= 2; i++) {
                if (!expected[i]) continue
                if (ended[i]) ok = 1; else partial = 1
            }
            if (!ok) exit 1
            printf "HYDRA_ATTENTION\t3\n"
            for (i = 1; i <= n; i++) print rows[i]
            printf "END\t%d\t%d\t%d\n", n, partial ? 1 : 0, truncated ? 1 : 0
        }' "$_tnead_dir/local.tsv" "$_tnead_dir/remote.tsv"
    _tnead_status=$?
    rm -rf "$_tnead_dir"
    return "$_tnead_status"
}

# Onboarding operator view (H5): the private directory where the control
# centre asks the CLI to record qualification exports (`fleet qualify
# --progress`) and reviewed intents (`fleet enroll review --output`). Created
# owner-only like other fleet state; the native TUI never creates it itself.
tui_native_onboarding_home() {
    _tnoh_dir="$HYDRA_HOME/fleet/onboarding"
    hydra_private_mkdir "$_tnoh_dir" || return 1
    printf '%s\n' "$_tnoh_dir"
}

tui_native_exec() {
    _tne_binary="$(tui_native_find_binary)" || {
        tui_native_report_unavailable
        return 1
    }
    exec "$_tne_binary" --hydra "$HYDRA_BIN_CMD" "$@"
}

# Run the control centre in this terminal. An unexpected exit status leaves the
# terminal as it was found and is reported and returned as is.
tui_native_run() {
    _tnr_saved_stty=""
    if [ -t 0 ]; then
        _tnr_saved_stty="$(stty -g 2>/dev/null || true)"
    fi
    _tnr_binary="$(tui_native_find_binary 2>/dev/null || true)"
    if [ -z "$_tnr_binary" ]; then
        tui_native_report_unavailable
        return 1
    fi
    # The helper reads this CLI's state store in its own process and asks the
    # CLI only for what the store cannot say.
    if "$_tnr_binary" --hydra "$HYDRA_BIN_CMD" "$@"; then
        _tnr_status=0
    else
        _tnr_status=$?
    fi
    if [ -t 1 ]; then
        # The helper draws on the alternate screen and leaves it itself; one
        # ended by a signal may not have. A helper that refused to start never
        # took the screen, and leaving it again would move the cursor.
        if [ "$_tnr_status" -gt 128 ]; then
            printf '\033[?1049l'
        fi
        printf '\033[?1000l\033[?1006l\033[?1002l\033[?2004l\033[0m\033[?25h'
    fi
    case "$_tnr_status" in
        # 3 is the helper refusing to start (no terminal, TERM=dumb, too small);
        # it has already said why.
        0|2|3|129|130|131|133|137|143) ;;
        *)
            if [ -n "$_tnr_saved_stty" ]; then
                stty "$_tnr_saved_stty" 2>/dev/null || true
            fi
            echo "Error: native TUI exited unexpectedly with status $_tnr_status" >&2
            ;;
    esac
    return "$_tnr_status"
}
