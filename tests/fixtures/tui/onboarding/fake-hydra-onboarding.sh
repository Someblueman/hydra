#!/bin/sh
# Scripted `hydra fleet ...` and `hydra tui --onboarding-home` for native
# onboarding journeys. It replays the envelopes in this directory (recorded
# from the real CLI by record-envelopes.sh) with the CLI's exit codes and
# writes the files the real commands would write (the qualification export
# at --progress, the intent at --output), so the control centre can only
# proceed through records that exist. Progress is kept as marker files in
# $HYDRA_ONBOARDING_FAKE; `set` there selects the recorded set (one, the
# default, or fifty). Every other command goes to the ordinary TUI fixture.
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
state="${HYDRA_ONBOARDING_FAKE:?HYDRA_ONBOARDING_FAKE names the journey state directory}"
set_name="$(cat "$state/set" 2>/dev/null || echo one)"
digest() { sed -n 's/.*"intent_sha256":"\([0-9a-f]*\)".*/\1/p' "$here/$1" | head -n 1; }
emit() { cat "$here/$1"; exit "$2"; }
has() { [ -f "$state/$1" ]; }
mark() { : > "$state/$1"; }
# option NAME ARGS...: the value after NAME, or empty.
option() {
    want=$1; shift
    while [ $# -gt 1 ]; do
        if [ "$1" = "$want" ]; then printf '%s' "$2"; return 0; fi
        shift
    done
    return 1
}

case "${1:-}:${2:-}" in
    tui:--onboarding-home)
        mkdir -p "$state/onboarding"
        chmod 700 "$state/onboarding"
        printf '%s\n' "$state/onboarding"
        exit 0 ;;
    fleet:*) ;;
    *) exec "$here/../fake-hydra.sh" "$@" ;;
esac
shift
printf '%s\n' "$*" >> "$state/calls.log"

case "$set_name" in
    fifty) qualify_file=qualify-50-complete.json review_file=review-50.json apply_file=apply-50.json
           status_before=status-50-reviewed.json status_after=status-50.json ;;
    *) qualify_file=qualify-one.json review_file=review-one.json apply_file=apply-one.json
       status_before=status-not-started.json status_after=status-enrolled.json ;;
esac

case "${1:-}:${2:-}" in
    discover:*)
        if has hundred; then emit discover-100.json 0; fi
        emit discover-one.json 0 ;;
    qualify:*)
        # slow: the handshake hangs until the control centre cancels it.
        if has slow && ! has slow_done; then mark slow_done; echo $$ > "$state/slow.pid"; sleep 60; exit 1; fi
        export="$(option --progress "$@")" || { echo "fake: qualify without --progress" >&2; exit 2; }
        case "$export" in "$state/onboarding/"*) ;; *) echo "fake: export outside the onboarding directory: $export" >&2; exit 2 ;; esac
        cp "$here/$qualify_file" "$export"
        mark qualified
        emit "$qualify_file" 0 ;;
    enroll:review)
        out="$(option --output "$@")" || { echo "fake: review without --output" >&2; exit 2; }
        case "$out" in "$state/onboarding/"*) ;; *) echo "fake: intent outside the onboarding directory: $out" >&2; exit 2 ;; esac
        case " $* " in *" --candidate cand_6f74686572 "*) emit review-revoked.json 1 ;; esac
        has qualified || { echo "fake: review before any export" >&2; emit status-wrong-input.json 1; }
        cp "$here/intent-one.json" "$out"
        mark reviewed
        emit "$review_file" 0 ;;
    enroll:status)
        has reviewed || emit status-wrong-input.json 1
        if has applied; then emit "$status_after" 0; fi
        emit "$status_before" 0 ;;
    enroll:apply)
        has reviewed || emit apply-wrong-digest.json 1
        [ "$(option --confirm "$@")" = "$(digest "$review_file")" ] || emit apply-wrong-digest.json 1
        mark applied
        emit "$apply_file" 0 ;;
    *) exit 1 ;;
esac
