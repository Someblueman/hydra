#!/bin/sh
# Install a prebuilt, self-contained fleet helper; no new shell-only dependency.
set -eu
source_binary="$1"
destination="$2"
mode="${HYDRA_INSTALL_FLEET:-auto}"
case "$mode" in auto|required|never) ;; *) echo 'Invalid HYDRA_INSTALL_FLEET' >&2; exit 1 ;; esac
[ "$mode" != never ] || exit 0
if [ ! -x "$source_binary" ]; then
    [ "$mode" != required ] || { echo 'Run make build-fleet before installing with fleet required' >&2; exit 1; }
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
