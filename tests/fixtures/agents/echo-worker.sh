#!/bin/sh
# Deterministic session fixture; never a claim of live AI qualification.
set -eu
case "${1:-}" in --help|--version) printf 'echo-worker-v1\n'; exit 0 ;; esac
mode="$1" session="$2"
case "$session" in ''|*[!a-zA-Z0-9-]*) exit 2 ;; esac
prompt="$(cat)"
printf %s "$prompt" > received-prompt
case "$mode" in
    --new)
        [ ! -e ".echo-session-$session" ] || exit 2
        value="$(printf '%s\n' "$prompt" | sed -n 's/^ARTIFACT=//p')"
        [ -n "$value" ] || exit 2
        printf %s "$value" > ".echo-session-$session"
        ;;
    --resume) value="$(cat ".echo-session-$session")" ;;
    *) exit 2 ;;
esac
case "$value" in *[!a-zA-Z0-9_-]*) exit 2 ;; esac
printf '{"schema_version":1,"type":"session","session_id":"%s"}\n' "$session"
printf '{"schema_version":1,"type":"observation","status":"running"}\n'
printf '{"schema_version":1,"type":"result","text":"%s"}\n' "$value"
printf '{"schema_version":1,"type":"observation","status":"idle"}\n'
