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
    spawn:*)
        printf 'FAKE SPAWN %s\n' "$*"
        ;;
    group:create)
        printf 'FAKE GROUP %s\n' "$*"
        ;;
    kill:*)
        printf 'FAKE KILL %s\n' "$*"
        ;;
    switch:*|regenerate:*|status:*|claim:list|collision:*|scope:show|queue:*|resource:status|diff:*|gate:status|doctor:*)
        printf 'FAKE ACTION'
        printf ' <%s>' "$@"
        printf '\n'
        ;;
    *) exit 1 ;;
esac
