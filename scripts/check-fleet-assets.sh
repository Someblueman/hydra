#!/bin/sh
# Validate release/fleet-assets.tsv, the local trust anchor for remote
# provisioning, and compare rebuilt release binaries with its pinned rows.
#
#   check-fleet-assets.sh TABLE
#   check-fleet-assets.sh --verify TABLE VERSION PLATFORM BINARY
#
# The first line is exactly "version<TAB>platform<TAB>sha256<TAB>filename".
# Blank lines and lines starting with '#' are ignored. Every row names a
# dotted numeric version, linux-x86_64 or linux-aarch64, a lowercase SHA-256
# and hydra-fleet-VERSION-PLATFORM; (version, platform) pairs are unique.
set -eu

usage() {
    echo 'Usage: check-fleet-assets.sh TABLE | --verify TABLE VERSION PLATFORM BINARY' >&2
    exit 2
}
lint() {
    [ -f "$1" ] || { echo "Error: missing fleet asset table: $1" >&2; exit 1; }
    awk -F '\t' '
        function bad(message) { printf "%s:%d: %s\n", FILENAME, NR, message > "/dev/stderr"; failed = 1 }
        NR == 1 {
            if ($0 != "version\tplatform\tsha256\tfilename") bad("header must be version<TAB>platform<TAB>sha256<TAB>filename")
            next
        }
        $0 == "" || /^#/ { next }
        {
            if (NF != 4) { bad("rows need exactly four tab-separated fields"); next }
            if ($1 !~ /^[0-9]+(\.[0-9]+)*$/) bad("invalid version " $1)
            if ($2 != "linux-x86_64" && $2 != "linux-aarch64") bad("platform must be linux-x86_64 or linux-aarch64")
            if ($3 !~ /^[0-9a-f]+$/ || length($3) != 64) bad("sha256 must be 64 lowercase hex digits")
            if ($4 != "hydra-fleet-" $1 "-" $2) bad("filename must be hydra-fleet-" $1 "-" $2)
            if (($1 SUBSEP $2) in seen) bad("duplicate row for " $1 " " $2)
            seen[$1, $2] = 1
        }
        END {
            if (NR == 0) { printf "%s: empty table\n", FILENAME > "/dev/stderr"; failed = 1 }
            exit failed
        }
    ' "$1"
}
sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}
# Fails unless BINARY matches the committed row; prints the row to commit
# when it is missing so the release PR can pin the CI-built digest.
verify() {
    table="$1" version="$2" platform="$3" binary="$4"
    lint "$table"
    [ -f "$binary" ] || { echo "Error: missing rebuilt binary: $binary" >&2; exit 1; }
    actual="$(sha256_of "$binary")"
    expected="$(awk -F '\t' -v v="$version" -v p="$platform" 'NR > 1 && $1 == v && $2 == p { print $3 }' "$table")"
    if [ -z "$expected" ]; then
        echo "Error: $table has no row for $version $platform. Commit this CI-built row:" >&2
        printf '%s\t%s\t%s\thydra-fleet-%s-%s\n' "$version" "$platform" "$actual" "$version" "$platform" >&2
        exit 1
    fi
    if [ "$actual" != "$expected" ]; then
        echo "Error: rebuilt hydra-fleet $version $platform is $actual, but $table pins $expected" >&2
        exit 1
    fi
    printf 'Verified hydra-fleet %s %s %s\n' "$version" "$platform" "$actual"
}

case "${1:-}" in
    --verify) [ "$#" -eq 5 ] || usage; shift; verify "$@" ;;
    ''|-*) usage ;;
    *) [ "$#" -eq 1 ] || usage; lint "$1" ;;
esac
