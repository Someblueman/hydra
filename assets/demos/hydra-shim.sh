#!/bin/sh
# The CLI the recording's control centre runs (its --hydra): this checkout's
# real bin/hydra for everything except the remote-host observation, which is
# the recorded fixture beside this script, since no SSH host is reachable
# from the recorder.
case "${1:-}:${2:-}" in
    tui:--remote-data) exec cat "${HYDRA_DEMO_REMOTE_FIXTURE:?}" ;;
esac
exec "${HYDRA_DEMO_HYDRA:?}" "$@"
