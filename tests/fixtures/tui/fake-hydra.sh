#!/bin/sh

case "${1:-}:${2:-}" in
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
