#!/bin/sh
# Scripted `hydra remote ...` for native setup journeys. It replays the
# recorded envelopes in this directory with the CLI's exit codes (0 done,
# 1 error, 3 approval_required) and keeps progress as marker files in
# $HYDRA_SETUP_FAKE. Every other command goes to the ordinary TUI fixture.
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
state="${HYDRA_SETUP_FAKE:?HYDRA_SETUP_FAKE names the journey state directory}"
fingerprint='SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8'
provision_hash='3f6c2a9e1b7d4c8a0e5f2b9d6c3a1e8f7b4d0c2a9e6f3b1d8c5a2e0f7b4d9c1a'
install_hash='5e8a1c4f7b2d9e6a3c0f5b8d1e4a7c2f9b6d3e0a5c8f1b4d7e2a9c6f3b0d5e8a'

[ "${1:-}" = remote ] || exec "$here/../fake-hydra.sh" "$@"
shift
printf '%s\n' "$*" >> "$state/calls.log"

emit() { cat "$here/$1"; exit "$2"; }
has() { [ -f "$state/$1" ]; }
mark() { : > "$state/$1"; }

# Interactive steps own a terminal and never print JSON.
terminal_step() {
    if [ ! -t 0 ] || [ ! -t 1 ]; then echo "fake: $1 needs a terminal" >&2; exit 2; fi
    case " $* " in *' --json '*) echo "fake: unexpected --json" >&2; exit 2 ;; esac
    mark "tty-$1"
}

status() {
    if has complete; then emit status-done.json 0; fi
    if has signed; then emit status-signed.json 0; fi
    if has sign_failed; then emit status-sign-failed.json 0; fi
    if has installed; then emit status-installed.json 0; fi
    if has trusted; then emit status-provision-approval.json 0; fi
    emit fresh.json 0
}

guided() {
    mkdir -p "$HYDRA_HOME/fleet/setup"
    printf '{}\n' > "$HYDRA_HOME/fleet/setup/$1.json"
    if has changed; then emit host-key-changed.json 1; fi
    has trusted || emit host-key-approval.json 3
    if has needs_git && ! has git_fixed; then emit preflight-blocked.json 1; fi
    has provisioned || emit provision-approval.json 3
    has installed || emit install-approval.json 3
    if has signed; then mark complete; emit done.json 0; fi
    emit sign-in-tty.json 1
}

case "${1:-}:${2:-}" in
    setup:status) status ;;
    setup:*) guided "$2" ;;
    trust-key:ovh)
        [ "$4" = "$fingerprint" ] || emit host-key-approval.json 3
        mark trusted; emit host-key-trusted.json 0 ;;
    provision:ovh)
        [ "$4" = "$provision_hash" ] || emit provision-approval.json 3
        mark provisioned; emit provision-done.json 0 ;;
    install-agent:ovh)
        terminal_step install "$@"
        case "$*" in *"--approve $install_hash"*) ;; *) exit 3 ;; esac
        printf 'FAKE INSTALLER: installing claude for deploy on ovh\n'
        mark installed ;;
    sign-in:ovh)
        terminal_step sign-in "$@"
        printf 'FAKE SIGN-IN: open https://example.invalid/device, then press Enter\n'
        read -r _ || exit 1
        if has fail_sign_in && ! has sign_failed; then mark sign_failed; printf 'FAKE SIGN-IN FAILED\n'; exit 1; fi
        mark signed ;;
    preflight:ovh) emit preflight-ok.json 0 ;;
    agents:ovh) emit agents.json 0 ;;
    *) exit 1 ;;
esac
