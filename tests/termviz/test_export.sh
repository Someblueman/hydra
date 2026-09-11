#!/bin/sh
# Build and exercise the exported source outside Hydra's checkout.
set -eu
_export_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
_export_temp=$(mktemp -d)
trap 'rm -rf "$_export_temp"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
"$_export_root/scripts/package-termviz.sh" "$_export_temp/source"
# The source guide uses paths relative to src/termviz in the checkout; the
# exported README is rooted one level higher and must point into that directory.
for _guide_path in \
    src/termviz/termviz.h \
    src/termviz/workspace.h \
    src/termviz/terminal.h \
    src/termviz/TERMINAL.md \
    src/termviz/posix.h \
    src/termviz/pty_posix.h \
    LICENSE \
    src/termviz/UNICODE-LICENSE.txt; do
    grep -F "($_guide_path)" "$_export_temp/source/README.md" >/dev/null
    [ -f "$_export_temp/source/$_guide_path" ]
done
# The independent Makefile owns build/, regardless of Hydra's build override.
make -C "$_export_temp/source" BUILD_DIR=build all test test-pty
# A repeated export must not overwrite even a caller's edited source.
printf 'caller-owned\n' > "$_export_temp/source/README.md"
if "$_export_root/scripts/package-termviz.sh" "$_export_temp/source" >/dev/null 2>&1; then
    echo 'FAIL: exporter accepted an existing destination' >&2
    exit 1
fi
[ "$(cat "$_export_temp/source/README.md")" = caller-owned ]
printf '%s\n' 'PASS standalone export: independent build, component/PTY tests, destination preservation'
