#!/bin/sh
# Export a local, independently buildable source tree. Never publish or overwrite.
set -eu
if [ "$#" -ne 1 ] || [ -z "$1" ]; then
    echo 'Usage: scripts/package-termviz.sh <new-directory>' >&2
    exit 2
fi
_package_root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
_package_dest=$1
case $_package_dest in /*) ;; *) _package_dest=$PWD/$_package_dest ;; esac
mkdir "$_package_dest" || exit 1
mkdir -p "$_package_dest/src/termviz" "$_package_dest/examples" "$_package_dest/tests/c" "$_package_dest/tests/termviz"
cp "$_package_root"/src/termviz/* "$_package_dest/src/termviz/"
cp "$_package_root/LICENSE" "$_package_dest/LICENSE"
cp "$_package_root/scripts/termviz.mk" "$_package_dest/Makefile"
# The guide is source-relative in Hydra, but the exported copy is rooted above
# src/termviz. Rewrite only its local pointers for the exported layout.
sed \
    -e 's|(termviz\.h)|(src/termviz/termviz.h)|g' \
    -e 's|(workspace\.h)|(src/termviz/workspace.h)|g' \
    -e 's|(terminal\.h)|(src/termviz/terminal.h)|g' \
    -e 's|(TERMINAL\.md)|(src/termviz/TERMINAL.md)|g' \
    -e 's|(posix\.h)|(src/termviz/posix.h)|g' \
    -e 's|(pty_posix\.h)|(src/termviz/pty_posix.h)|g' \
    -e 's|(\.\./\.\./LICENSE)|(LICENSE)|g' \
    -e 's|(UNICODE-LICENSE\.txt)|(src/termviz/UNICODE-LICENSE.txt)|g' \
    "$_package_root/src/termviz/STANDALONE.md" > "$_package_dest/README.md"
cp "$_package_root"/examples/workspace*.c "$_package_root/examples/workspace_demo.h" "$_package_root/examples/termviz.c" "$_package_dest/examples/"
cp "$_package_root"/tests/c/test_termviz*.c "$_package_root/tests/c/test_workspace_child.c" "$_package_dest/tests/c/"
for _package_file in pty_support.h pty_support.c screen_support.c fixture_support.c test_pty.c; do
    cp "$_package_root/tests/termviz/$_package_file" "$_package_dest/tests/termviz/"
done
printf 'Exported termviz source to %s\n' "$_package_dest"
