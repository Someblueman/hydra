#!/bin/sh

if [ -n "${HYDRA_REVIEW_CALLS:-}" ]; then printf '%s\n' "$@" '---' >> "$HYDRA_REVIEW_CALLS"; fi

case "${1:-}:${2:-}" in
    workflow:review-data|fleet:review-data)
        if [ -n "${HYDRA_REVIEW_STARTED:-}" ]; then : > "$HYDRA_REVIEW_STARTED"; fi
        if [ -n "${HYDRA_REVIEW_FAIL:-}" ] && [ -f "$HYDRA_REVIEW_FAIL" ]; then exit 1; fi
        if [ -n "${HYDRA_REVIEW_CANCEL_OWNER:-}" ]; then
            exec "$HYDRA_REVIEW_CANCEL_OWNER" --review-cancel-owner "$HYDRA_REVIEW_PIDS" \
                "$HYDRA_REVIEW_CANCEL_SCRATCH" "$HYDRA_REVIEW_CANCEL_CLEANED" "$HYDRA_REVIEW_CANCEL_MODE"
        fi
        cat "$HYDRA_REVIEW_FIXTURE"
        if [ -n "${HYDRA_REVIEW_DELAY:-}" ] && [ -f "$HYDRA_REVIEW_DELAY" ]; then
            sleep "$(cat "$HYDRA_REVIEW_DELAY")" &
            review_child=$!
            if [ -n "${HYDRA_REVIEW_PIDS:-}" ]; then printf '%s\n' "$$" "$review_child" > "$HYDRA_REVIEW_PIDS"; fi
            wait "$review_child"
        fi
        ;;
    workflow:statistics-data)
        if [ -n "${HYDRA_TEST_STATS_FAIL_FILE:-}" ] && [ -f "$HYDRA_TEST_STATS_FAIL_FILE" ]; then exit 1; fi
        fixture_dir="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
        cat "$fixture_dir/statistics-v2.tsv"
        ;;
    fleet:tui-visual-data)
        fixture_dir="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
        if [ -n "${HYDRA_TEST_FLEET_FRESH:-}" ]; then
            cat "$fixture_dir/fleet-fresh.tsv"
        else
            cat "$fixture_dir/fleet-v2.tsv"
        fi
        ;;
    fleet:attach)
        if [ -n "${HYDRA_TEST_ATTACH_ARGV:-}" ]; then
            printf '%s\n' "$@" > "$HYDRA_TEST_ATTACH_ARGV"
        fi
        printf 'FAKE REMOTE ATTACH'
        printf ' <%s>' "$@"
        printf '\n'
        sleep 1
        ;;
    tui:--attach)
        # A fake agent conversation: reports its terminal size whenever the
        # attached pane is resized, so tests can assert pane geometry.
        printf 'FAKE AGENT PANE\n'
        last=''
        while :; do
            size="$(stty size 2>/dev/null || true)"
            if [ "$size" != "$last" ]; then printf 'AGENT SIZE %s\n' "$size"; last="$size"; fi
            sleep 0.1
        done
        ;;
    workflow:tui-data)
        fixture_dir="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
        cat "$fixture_dir/workflow-v1.tsv"
        ;;
    workflow:attention-data|fleet:attention-data)
        if [ -n "${HYDRA_TEST_ATTENTION_FAIL_FILE:-}" ] && [ -f "$HYDRA_TEST_ATTENTION_FAIL_FILE" ]; then exit 1; fi
        fixture_dir="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
        cat "${HYDRA_ATTENTION_FIXTURE:-$fixture_dir/attention-v1.tsv}"
        if [ -n "${HYDRA_ATTENTION_DONE_FILE:-}" ]; then
            : > "$HYDRA_ATTENTION_DONE_FILE"
        fi
        ;;
    workflow:attention-seen)
        # Shared per-user seen store for PTY tests; absent means unavailable.
        [ -n "${HYDRA_TEST_SEEN_FILE:-}" ] || exit 1
        # Clients mark and list concurrently: serialize like the real store
        # and never share a temporary file between processes.
        seen_lock="$HYDRA_TEST_SEEN_FILE.lock"; seen_wait=0
        until mkdir "$seen_lock" 2>/dev/null; do
            seen_wait=$((seen_wait + 1)); [ "$seen_wait" -lt 200 ] || exit 1; sleep 0.05
        done
        touch "$HYDRA_TEST_SEEN_FILE"; seen_tmp="$HYDRA_TEST_SEEN_FILE.tmp.$$"
        case "${3:-}" in
            mark) { grep -v "^$4	" "$HYDRA_TEST_SEEN_FILE"; printf '%s\t%s\n' "$4" "$5"; } > "$seen_tmp" && mv "$seen_tmp" "$HYDRA_TEST_SEEN_FILE" ;;
            clear) grep -v "^$4	" "$HYDRA_TEST_SEEN_FILE" > "$seen_tmp"; mv "$seen_tmp" "$HYDRA_TEST_SEEN_FILE" ;;
            list) : ;;
            *) rmdir "$seen_lock"; exit 2 ;;
        esac
        printf 'HYDRA_ATTENTION_SEEN\t1\n'
        sed 's/^/SEEN	/' "$HYDRA_TEST_SEEN_FILE"
        printf 'END\t%s\n' "$(wc -l < "$HYDRA_TEST_SEEN_FILE" | tr -d ' ')"
        rmdir "$seen_lock"
        ;;
    tui:--data)
        if [ -n "${HYDRA_TEST_FAIL_FILE:-}" ] && [ -f "$HYDRA_TEST_FAIL_FILE" ]; then exit 1; fi
        if [ -n "${HYDRA_TEST_TUI_DELAY:-}" ]; then sleep "$HYDRA_TEST_TUI_DELAY"; fi
        if [ -n "${HYDRA_TUI_FIXTURE:-}" ]; then
            cat "$HYDRA_TUI_FIXTURE"
        else
            fixture_dir="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
            cat "$fixture_dir/native-v2.tsv"
        fi
        ;;
    dashboard:)
        printf '%s\n' "FAKE DASHBOARD"
        ;;
    agent:list)
        printf "PROFILE AVAILABLE\ncodex yes\nnone yes\n"
        ;;
    init:*)
        printf "Ready: fixture\n"
        ;;
    spawn:*)
        if [ -n "${HYDRA_TEST_SPAWN_ARGV:-}" ]; then printf '%s\n' "$@" > "$HYDRA_TEST_SPAWN_ARGV"; fi
        printf 'FAKE SPAWN %s\n' "$*"
        ;;
    group:create)
        printf 'FAKE GROUP %s\n' "$*"
        ;;
    kill:*)
        printf 'FAKE KILL %s\n' "$*"
        ;;
    gc:*)
        printf 'FAKE GC'
        printf ' <%s>' "$@"
        printf '\n'
        case "$*" in 'gc --policy orphaned --apply --path '*) [ $# -eq 6 ] && printf 'removed-orphan\t%s\n' "$6" ;; esac
        ;;
    switch:*|regenerate:*|status:*|claim:list|collision:*|scope:show|queue:*|resource:status|diff:*|gate:status|doctor:*)
        printf 'FAKE ACTION'
        printf ' <%s>' "$@"
        printf '\n'
        ;;
    *) exit 1 ;;
esac
