#!/bin/sh
# Reproducible static musl hydra-fleet for Linux release assets.
#
# Usage: scripts/build-fleet-static.sh x86_64|aarch64 [OUTDIR]
#
# Runs the build inside a digest-pinned Alpine image (Docker) for the native
# platform of ARCH, with JSON-C from a pinned, SHA-256-verified tarball, and
# writes OUTDIR/hydra-fleet-linux-ARCH (default OUTDIR build/static) plus
# .sha256 and .buildinfo files. The binary is stripped, fully static (no
# INTERP or NEEDED entries) and built with -ffile-prefix-map,
# -Wl,--build-id=none and SOURCE_DATE_EPOCH from the source commit, so two
# builds of the same tree in the same image produce identical bytes.
#
# The same script runs inside the container as:
#   build-fleet-static.sh --inside [--dry-run] ARCH OUTDIR
# --dry-run (development only) skips the Alpine package install and the
# static link so the JSON-C and Makefile steps can be exercised on a host
# without Docker; its output is not a release asset.
set -eu

ALPINE_IMAGE='alpine:3.22@sha256:5291449c3df73caf6ed85e649dec1b9e818b39a5d8c871e97afc13e9cd5e8fa8'
JSONC_VERSION='0.18'
JSONC_URL="https://s3.amazonaws.com/json-c_releases/releases/json-c-$JSONC_VERSION.tar.gz"
JSONC_SHA256='876ab046479166b869afc6896d288183bbc0e5843f141200c677b3e8dfb11724'
APK_PACKAGES='build-base make cmake pkgconf binutils curl'

usage() {
    echo 'Usage: build-fleet-static.sh x86_64|aarch64 [OUTDIR]' >&2
    exit 2
}
die() {
    echo "Error: $*" >&2
    exit 1
}
docker_platform() {
    case "$1" in
        x86_64) echo linux/amd64 ;;
        aarch64) echo linux/arm64 ;;
        *) usage ;;
    esac
}
sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

# ---- Host: run this script inside the pinned image ----
host_build() {
    arch="$1"
    root="$(cd "$(dirname "$0")/.." && pwd)"
    out="${2:-$root/build/static}"
    platform="$(docker_platform "$arch")"
    command -v docker >/dev/null 2>&1 || die 'Docker is required for the static release build'
    docker info >/dev/null 2>&1 || die 'the Docker daemon is not running; start it and retry'
    mkdir -p "$out"
    out="$(cd "$out" && pwd)"
    # The last commit touching the build inputs: identical for a release
    # branch and its merge commit, so tag rebuilds match release-PR builds.
    epoch="$(git -C "$root" log -1 --format=%ct -- src Makefile scripts 2>/dev/null || true)"
    epoch="${epoch:-0}"
    docker run --rm --platform "$platform" \
        -e SOURCE_DATE_EPOCH="$epoch" -e HOST_OWNER="$(id -u):$(id -g)" \
        -v "$root:/src:ro" -v "$out:/out" \
        "$ALPINE_IMAGE" sh /src/scripts/build-fleet-static.sh --inside "$arch" /out
    printf '%s\n' "$out/hydra-fleet-linux-$arch"
}

# ---- Inside: fetch and verify JSON-C, then build with the Makefile ----
fetch_jsonc() {
    tarball="$1/json-c-$JSONC_VERSION.tar.gz"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --proto '=https' -o "$tarball" "$JSONC_URL"
    else
        wget -q -O "$tarball" "$JSONC_URL"
    fi
    actual="$(sha256_of "$tarball")"
    [ "$actual" = "$JSONC_SHA256" ] || die "JSON-C tarball digest $actual differs from the pinned $JSONC_SHA256"
    tar -xzf "$tarball" -C "$1"
}
build_jsonc() {
    work="$1" prefix="$2"
    fetch_jsonc "$work"
    src="$work/json-c-$JSONC_VERSION"
    cmake -S "$src" -B "$work/json-c-build" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DBUILD_APPS=OFF \
        -DDISABLE_EXTRA_LIBS=ON -DDISABLE_WERROR=ON \
        -DCMAKE_C_FLAGS="-O2 -ffile-prefix-map=$src=json-c -ffile-prefix-map=$work=." >/dev/null
    cmake --build "$work/json-c-build" --parallel >/dev/null
    cmake --install "$work/json-c-build" >/dev/null
}
# Copies only what the Makefile needs to build the fleet helper.
stage_source() {
    mkdir -p "$2/scripts"
    cp "$1/Makefile" "$2/"
    cp "$1"/scripts/*.mk "$2/scripts/"
    cp -R "$1/src" "$2/"
}
check_static() {
    if readelf -lW "$1" | grep -q 'INTERP'; then die 'static helper has a program interpreter'; fi
    if readelf -dW "$1" | grep -q 'NEEDED'; then die 'static helper needs shared libraries'; fi
    if [ "$("$1" --version)" != 'Hydra fleet protocol 1' ]; then die 'static helper failed its version handshake'; fi
}
inside_build() {
    dry_run=0
    if [ "${1:-}" = --dry-run ]; then dry_run=1; shift; fi
    [ "$#" -eq 2 ] || usage
    arch="$1" out="$2"
    docker_platform "$arch" >/dev/null
    root="$(cd "$(dirname "$0")/.." && pwd)"
    if [ "$dry_run" -eq 0 ]; then
        if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != "$arch" ]; then die "the inside build must run on linux-$arch"; fi
        # shellcheck disable=SC2086
        apk add --no-cache $APK_PACKAGES >/dev/null
        ldflags='-static -Wl,--build-id=none'
        # A fixed path in the fresh container keeps every recorded path stable.
        work=/tmp/hydra-fleet-static
        mkdir "$work"
    else
        ldflags=''
        work="$(mktemp -d "${TMPDIR:-/tmp}/hydra-fleet-static.XXXXXX")"
    fi
    export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-0}" LC_ALL=C TZ=UTC
    trap 'rm -rf "$work"' 0
    trap 'exit 130' INT
    trap 'exit 143' HUP TERM
    build_jsonc "$work" "$work/json-c"
    stage_source "$root" "$work/hydra"
    PKG_CONFIG_PATH="$work/json-c/lib/pkgconfig" make -s -C "$work/hydra" BUILD_DIR=build \
        CC="${CC:-cc}" CFLAGS="-O2 -ffile-prefix-map=$work/hydra=. -ffile-prefix-map=$work=." \
        FLEET_LDFLAGS="$ldflags" build-fleet >/dev/null
    binary="$work/hydra/build/hydra-fleet"
    [ "$dry_run" -eq 1 ] || { strip --strip-all "$binary"; check_static "$binary"; }
    mkdir -p "$out"
    name="hydra-fleet-linux-$arch"
    cp "$binary" "$out/$name"
    chmod 755 "$out/$name"
    printf '%s  %s\n' "$(sha256_of "$out/$name")" "$name" > "$out/$name.sha256"
    {
        printf 'image %s\n' "$ALPINE_IMAGE"
        printf 'json-c %s %s\n' "$JSONC_VERSION" "$JSONC_SHA256"
        printf 'source_date_epoch %s\n' "$SOURCE_DATE_EPOCH"
        printf 'dry_run %s\n' "$dry_run"
        if command -v apk >/dev/null 2>&1; then
            # shellcheck disable=SC2086
            apk info -v $APK_PACKAGES musl gcc 2>/dev/null | LC_ALL=C sort
        fi
    } > "$out/$name.buildinfo"
    # Files written through the bind mount belong to the invoking user.
    if [ -n "${HOST_OWNER:-}" ]; then chown "$HOST_OWNER" "$out/$name" "$out/$name.sha256" "$out/$name.buildinfo"; fi
    cat "$out/$name.sha256"
}

case "${1:-}" in
    --inside) shift; inside_build "$@" ;;
    x86_64|aarch64) [ "$#" -le 2 ] || usage; host_build "$@" ;;
    amd64) [ "$#" -le 2 ] || usage; shift; host_build x86_64 "$@" ;;
    arm64) [ "$#" -le 2 ] || usage; shift; host_build aarch64 "$@" ;;
    *) usage ;;
esac
