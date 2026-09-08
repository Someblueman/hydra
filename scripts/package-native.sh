#!/bin/sh
# Package a local native executable with offline verification metadata.

set -eu

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
case "${1:-}" in
    core) name=hydra-core; package_dir="${HYDRA_CORE_PACKAGE_DIR:-}" ;;
    tui) name=hydra-tui; package_dir="${HYDRA_TUI_PACKAGE_DIR:-}" ;;
    *) echo 'Usage: package-native.sh core|tui' >&2; exit 2 ;;
esac
binary="$repo_root/build/$name"
platform="$(uname -s) $(uname -m)"
slug="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
out="${package_dir:-$repo_root/dist/$name-$slug}"
source_commit="$(git -C "$repo_root" rev-parse HEAD)"
if [ -n "$(git -C "$repo_root" status --porcelain=v1)" ]; then
    if [ "${HYDRA_ALLOW_DIRTY_PACKAGE:-0}" != 1 ]; then
        echo "Error: refusing to package a dirty source tree" >&2
        exit 1
    fi
    source_commit="$source_commit-dirty"
fi

[ -x "$binary" ] || {
    echo "Error: build/$name is missing; run make build-$1" >&2
    exit 1
}
mkdir -p "$out"
cp "$binary" "$out/$name"
chmod +x "$out/$name"

if command -v sha256sum >/dev/null 2>&1; then
    hash="$(sha256sum "$out/$name" | awk '{print $1}')"
else
    hash="$(shasum -a 256 "$out/$name" | awk '{print $1}')"
fi
printf '%s  %s\n' "$hash" "$name" > "$out/$name.sha256"
printf '%s\n' "$platform" > "$out/$name.platform"
printf '%s\n' "$source_commit" > "$out/$name.source"
if command -v otool >/dev/null 2>&1; then
    otool -L "$out/$name" > "$out/$name.dependencies"
elif command -v ldd >/dev/null 2>&1; then
    ldd "$out/$name" > "$out/$name.dependencies"
else
    printf 'dependency inspection unavailable on %s\n' "$platform" > "$out/$name.dependencies"
fi
printf '%s\n' "$out"
