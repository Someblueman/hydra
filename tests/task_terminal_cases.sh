#!/bin/sh
# Sourced by the public task acceptance fixture with no tmux on PATH.
# shellcheck disable=SC2154
# shellcheck source=/dev/null
. "$root/tests/fixture-tools.sh"
# Terminal requirements are negotiated before acceptance; headless is explicit
# when the caller needs receivers predating T2 to reject safely.
"$root/bin/hydra" fleet handshake --json > "$fixture/capabilities"
grep -q '"execution-headless"' "$fixture/capabilities"
if grep -q '"tmux"' "$fixture/capabilities"; then exit 1; fi
for capability in tmux execution-headless; do
    sed "s/\"capabilities\":\[\"exec\"\]/\"capabilities\":[\"exec\",\"$capability\"]/" "$fixture/spec" > "$fixture/$capability-spec"
    task prepare --source "$fixture/source" --spec "$fixture/$capability-spec" --output "$fixture/$capability-package" >/dev/null
done
if task submit build --input "$fixture/tmux-package" --key terminal-required > "$fixture/error"; then exit 1; fi
grep -q '"code":"capability_unavailable"' "$fixture/error"
(cd "$fixture/receiver" && HYDRA_HOME="$fixture/host" "$root/bin/hydra" doctor) > "$fixture/doctor"
grep -q 'headless execution does not require tmux' "$fixture/doctor"

# The receiver enforces terminal requirements independently of its
# handshake. Neither rejection may publish an acceptance record.
fixture_json terminal-request "$fixture/tmux-package" > "$fixture/terminal-request"
if HYDRA_HOME="$fixture/host" "$root/bin/hydra" fleet serve < "$fixture/terminal-request" > "$fixture/error"; then exit 1; fi
grep -q '"code":"capability_unavailable"' "$fixture/error"
[ "$(find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" -eq 0 ]
printf '%s\n' 'Terminal negotiation: absent tmux, receiver enforcement and doctor passed'
