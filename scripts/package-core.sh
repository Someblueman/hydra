#!/bin/sh
# Package the local native helper with offline verification metadata.

set -eu

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
core="$repo_root/build/hydra-core"
platform="$(uname -s) $(uname -m)"
slug="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
out="${HYDRA_CORE_PACKAGE_DIR:-$repo_root/dist/hydra-core-$slug}"
source_commit="$(git -C "$repo_root" rev-parse HEAD)"
if [ -n "$(git -C "$repo_root" status --porcelain=v1)" ]; then
    if [ "${HYDRA_ALLOW_DIRTY_PACKAGE:-0}" != 1 ]; then
        echo "Error: refusing to package a dirty source tree" >&2
        exit 1
    fi
    source_commit="$source_commit-dirty"
fi

[ -x "$core" ] || {
    echo "Error: build/hydra-core is missing; run make build-core" >&2
    exit 1
}
mkdir -p "$out"
cp "$core" "$out/hydra-core"
chmod +x "$out/hydra-core"

if command -v sha256sum >/dev/null 2>&1; then
    hash="$(sha256sum "$out/hydra-core" | awk '{print $1}')"
else
    hash="$(shasum -a 256 "$out/hydra-core" | awk '{print $1}')"
fi
printf '%s  hydra-core\n' "$hash" > "$out/hydra-core.sha256"
printf '%s\n' "$platform" > "$out/hydra-core.platform"
printf '%s\n' "$source_commit" > "$out/hydra-core.source"
if command -v otool >/dev/null 2>&1; then
    otool -L "$out/hydra-core" > "$out/hydra-core.dependencies"
elif command -v ldd >/dev/null 2>&1; then
    ldd "$out/hydra-core" > "$out/hydra-core.dependencies"
else
    printf 'dependency inspection unavailable on %s\n' "$platform" > "$out/hydra-core.dependencies"
fi
printf '%s\n' "$out"
