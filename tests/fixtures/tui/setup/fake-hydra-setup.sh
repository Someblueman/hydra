#!/bin/sh
# Scripted `hydra remote ...` for native setup journeys. It replays the
# envelopes in this directory (recorded from the real CLI by
# record-envelopes.sh) with the CLI's exit codes (0 done, 1 error, 3
# approval_required) and follows the real CLI's order: a host key that is
# already known (known_key) passes the guided flow straight into preflight,
# a remote without tmux (no_tmux) passes preflight with a warning, the guided flow selects
# no agents without a terminal, `install-agent --json` selects one and returns
# its plan, and installers and sign-in run only on a terminal. Progress is
# kept as marker files in $HYDRA_SETUP_FAKE. Every other command goes to the
# ordinary TUI fixture.
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
state="${HYDRA_SETUP_FAKE:?HYDRA_SETUP_FAKE names the journey state directory}"
fingerprint='SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8'
plan_hash() { sed -n 's/.*"plan_sha256":"\([0-9a-f]*\)".*/\1/p' "$here/$1"; }
provision_hash="$(plan_hash provision-approval.json)"
install_hash="$(plan_hash install-approval.json)"

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

list() {
    if has complete; then emit list-done.json 0; fi
    if has started; then emit list-progress.json 0; fi
    emit list-empty.json 0
}

guided() {
    mark started
    # slow_host_key: the host-key check hangs (as it did behind a user SSH
    # master) until the control centre cancels it.
    if has slow_host_key && ! has trusted; then echo $$ > "$state/slow.pid"; sleep 60; exit 1; fi
    if has changed; then emit host-key-changed.json 1; fi
    has trusted || has known_key || emit host-key-approval.json 3
    if has needs_git && ! has git_fixed; then emit preflight-blocked.json 1; fi
    has provisioned || emit provision-approval.json 3
    if has selected && ! has installed; then emit install-approval.json 3; fi
    if has installed && ! has signed; then emit sign-in-tty.json 1; fi
    mark complete; emit done.json 0
}

case "${1:-}:${2:-}" in
    setup:status) status ;;
    setup:list) list ;;
    setup:*) guided "$2" ;;
    trust-key:ovh)
        [ "$4" = "$fingerprint" ] || emit host-key-approval.json 3
        mark trusted; emit host-key-trusted.json 0 ;;
    provision:ovh)
        [ "$4" = "$provision_hash" ] || emit provision-approval.json 3
        mark provisioned; emit provision-done.json 0 ;;
    install-agent:ovh)
        case " $* " in
            *" --approve $install_hash "*) ;;
            *" --json "*) mark selected; emit install-approval.json 3 ;;
            *) exit 3 ;;
        esac
        terminal_step install "$@"
        printf 'FAKE INSTALLER: installing claude for deploy on ovh\n'
        mark installed ;;
    sign-in:ovh)
        terminal_step sign-in "$@"
        printf 'FAKE SIGN-IN: open https://example.invalid/device, then press Enter\n'
        read -r _ || exit 1
        if has fail_sign_in && ! has sign_failed; then mark sign_failed; printf 'FAKE SIGN-IN FAILED\n'; exit 1; fi
        mark signed ;;
    preflight:ovh)
        if has needs_git && ! has git_fixed; then emit preflight-blocked.json 1; fi
        if has no_tmux; then emit preflight-tmux.json 0; fi
        emit preflight-ok.json 0 ;;
    agents:ovh) emit agents.json 0 ;;
    *) exit 1 ;;
esac
