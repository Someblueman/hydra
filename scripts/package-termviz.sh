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
cp "$_package_root/src/termviz/STANDALONE.md" "$_package_dest/README.md"
cp "$_package_root"/examples/workspace*.c "$_package_root/examples/workspace_demo.h" "$_package_root/examples/termviz.c" "$_package_dest/examples/"
cp "$_package_root"/tests/c/test_termviz*.c "$_package_root/tests/c/test_workspace_child.c" "$_package_dest/tests/c/"
cp "$_package_root/tests/termviz/pty_support.py" "$_package_root/tests/termviz/test_pty.py" "$_package_dest/tests/termviz/"
printf 'Exported termviz source to %s\n' "$_package_dest"
