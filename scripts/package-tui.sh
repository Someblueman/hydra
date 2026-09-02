#!/bin/sh
# Package the local native TUI with offline verification metadata.

set -eu

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
tui="$repo_root/build/hydra-tui"
platform="$(uname -s) $(uname -m)"
slug="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
out="${HYDRA_TUI_PACKAGE_DIR:-$repo_root/dist/hydra-tui-$slug}"
source_commit="$(git -C "$repo_root" rev-parse HEAD)"
if [ -n "$(git -C "$repo_root" status --porcelain=v1)" ]; then
    if [ "${HYDRA_ALLOW_DIRTY_PACKAGE:-0}" != 1 ]; then
        echo "Error: refusing to package a dirty source tree" >&2
        exit 1
    fi
    source_commit="$source_commit-dirty"
fi

[ -x "$tui" ] || {
    echo "Error: build/hydra-tui is missing; run make build-tui" >&2
    exit 1
}
mkdir -p "$out"
cp "$tui" "$out/hydra-tui"
chmod +x "$out/hydra-tui"

if command -v sha256sum >/dev/null 2>&1; then
    hash="$(sha256sum "$out/hydra-tui" | awk '{print $1}')"
else
    hash="$(shasum -a 256 "$out/hydra-tui" | awk '{print $1}')"
fi
printf '%s  hydra-tui\n' "$hash" > "$out/hydra-tui.sha256"
printf '%s\n' "$platform" > "$out/hydra-tui.platform"
printf '%s\n' "$source_commit" > "$out/hydra-tui.source"
if command -v otool >/dev/null 2>&1; then
    otool -L "$out/hydra-tui" > "$out/hydra-tui.dependencies"
elif command -v ldd >/dev/null 2>&1; then
    ldd "$out/hydra-tui" > "$out/hydra-tui.dependencies"
else
    printf 'dependency inspection unavailable on %s\n' "$platform" > "$out/hydra-tui.dependencies"
fi
printf '%s\n' "$out"
