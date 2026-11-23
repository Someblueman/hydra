#!/bin/sh
# Install a prebuilt, self-contained fleet helper; no new shell-only dependency.
set -eu
source_binary="$1"
destination="$2"
mode="${HYDRA_INSTALL_FLEET:-auto}"
case "$mode" in auto|required|never) ;; *) echo 'Invalid HYDRA_INSTALL_FLEET' >&2; exit 1 ;; esac
# Workflows (hydra workflow run, plans) are driven by hydra-fleet alone, so an
# installation without it says so. It is not an error: every other command
# works. A helper that an earlier installation left in place stays and is not
# reported as missing.
workflows_unavailable() {
    [ ! -x "$destination/hydra-fleet" ] || return 0
    {
        echo "Warning: hydra-fleet was not installed ($1)."
        echo "         Workflows are unavailable without it: hydra workflow run and plans will refuse to run."
        echo "         Other hydra commands work. To get it, install JSON-C development files and pkg-config,"
        echo "         run make build-fleet, then run ./install.sh again."
    } >&2
}
if [ "$mode" = never ]; then
    workflows_unavailable 'HYDRA_INSTALL_FLEET=never'
    exit 0
fi
if [ ! -x "$source_binary" ]; then
    [ "$mode" != required ] || { echo 'Run make build-fleet before installing with fleet required' >&2; exit 1; }
    workflows_unavailable "$source_binary is missing"
    exit 0
fi
[ "$("$source_binary" --version)" = 'Hydra fleet protocol 1' ] || exit 1
mkdir -p "$destination"
stage="$(mktemp "$destination/.fleet.XXXXXX")"
trap 'rm -f "$stage"' 0
trap 'exit 130' INT
trap 'exit 143' HUP TERM
cp "$source_binary" "$stage"
chmod 755 "$stage"
mv "$stage" "$destination/hydra-fleet"
license="$(dirname "$0")/../docs/licenses/json-c.txt"
if [ -f "$license" ]; then cp "$license" "$destination/hydra-fleet.LICENSE"; fi
