#!/bin/sh
# Export a local, independently buildable source tree. Never publish or overwrite.
set -eu
if [ "$#" -ne 1 ] || [ -z "$1" ]; then
    echo 'Usage: scripts/package-kettle.sh <new-directory>' >&2
    exit 2
fi
_package_root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
_package_dest=$1
case $_package_dest in /*) ;; *) _package_dest=$PWD/$_package_dest ;; esac
mkdir "$_package_dest" || exit 1
mkdir -p "$_package_dest/src/kettle" "$_package_dest/examples" "$_package_dest/tests/c" "$_package_dest/tests/kettle"
cp "$_package_root"/src/kettle/* "$_package_dest/src/kettle/"
cp "$_package_root/LICENSE" "$_package_dest/LICENSE"
cp "$_package_root/scripts/kettle.mk" "$_package_dest/Makefile"
# The guide is source-relative in Hydra, but the exported copy is rooted above
# src/kettle. Rewrite only its local pointers for the exported layout.
sed \
    -e 's|(kettle\.h)|(src/kettle/kettle.h)|g' \
    -e 's|(workspace\.h)|(src/kettle/workspace.h)|g' \
    -e 's|(terminal\.h)|(src/kettle/terminal.h)|g' \
    -e 's|(TERMINAL\.md)|(src/kettle/TERMINAL.md)|g' \
    -e 's|(posix\.h)|(src/kettle/posix.h)|g' \
    -e 's|(program\.h)|(src/kettle/program.h)|g' \
    -e 's|(testing\.h)|(src/kettle/testing.h)|g' \
    -e 's|(pty_posix\.h)|(src/kettle/pty_posix.h)|g' \
    -e 's|(\.\./\.\./LICENSE)|(LICENSE)|g' \
    -e 's|(UNICODE-LICENSE\.txt)|(src/kettle/UNICODE-LICENSE.txt)|g' \
    "$_package_root/src/kettle/STANDALONE.md" > "$_package_dest/README.md"
cp "$_package_root"/examples/workspace*.c "$_package_root/examples/workspace_demo.h" "$_package_root/examples/kettle.c" \
    "$_package_root/examples/counter.c" "$_package_root/examples/counter_test.c" "$_package_dest/examples/"
cp "$_package_root"/tests/c/test_kettle*.c "$_package_root/tests/c/test_workspace_child.c" "$_package_dest/tests/c/"
for _package_file in pty_support.h pty_support.c screen_support.c fixture_support.c test_pty.c \
    program_fixture.c test_program_pty.c; do
    cp "$_package_root/tests/kettle/$_package_file" "$_package_dest/tests/kettle/"
done
printf 'Exported kettle source to %s\n' "$_package_dest"
