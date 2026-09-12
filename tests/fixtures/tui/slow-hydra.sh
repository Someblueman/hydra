#!/bin/sh

sleep 30 &
child=$!
if [ -n "${HYDRA_TEST_PID_FILE:-}" ]; then
    printf '%s\n%s\n' "$$" "$child" >> "$HYDRA_TEST_PID_FILE"
fi
wait "$child"
