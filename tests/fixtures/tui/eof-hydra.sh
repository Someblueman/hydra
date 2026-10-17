#!/bin/sh
# A child that stops producing output but does not exit; every other adapter
# is simply unavailable.
case "${1:-}:${2:-}" in tui:--data) ;; *) exit 1 ;; esac
exec 1>&-
sleep 30 &
child=$!
if [ -n "${HYDRA_TEST_PID_FILE:-}" ]; then
    printf '%s\n%s\n' "$$" "$child" >> "$HYDRA_TEST_PID_FILE"
fi
wait "$child"
