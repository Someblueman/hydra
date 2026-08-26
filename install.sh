#!/bin/sh
# POSIX-compliant installation script
# Installs to $PREFIX/bin/hydra and $PREFIX/lib/hydra (default PREFIX=/usr/local).
# No root required when PREFIX is writable. DESTDIR is optional staging.

set -e

usage() {
    echo "Usage: [PREFIX=/usr/local] [DESTDIR=] ./install.sh" >&2
    echo "  PREFIX    installation prefix (default: /usr/local)" >&2
    echo "  DESTDIR   optional staging directory prepended to PREFIX" >&2
    echo "" >&2
    echo "Non-root example:" >&2
    echo "  PREFIX=\$HOME/.local ./install.sh" >&2
    echo "See README Quick Start." >&2
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

if [ $# -gt 0 ]; then
    echo "Error: Unexpected argument '$1'" >&2
    echo "Next: pass PREFIX and DESTDIR as environment variables, not flags" >&2
    usage
    exit 1
fi

SCRIPT_DIR="$(CDPATH= cd "$(dirname "$0")" && pwd)"
SRC_BIN="$SCRIPT_DIR/bin/hydra"
SRC_LIB="$SCRIPT_DIR/lib"

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"

case "$PREFIX" in
    /*) ;;
    *)
        PREFIX="$(pwd)/$PREFIX"
        ;;
esac

BIN_DIR="${DESTDIR}${PREFIX}/bin"
LIB_DIR="${DESTDIR}${PREFIX}/lib/hydra"

if [ ! -f "$SRC_BIN" ] || [ ! -d "$SRC_LIB" ]; then
    echo "Error: This script must be run from a hydra source checkout" >&2
    echo "Precondition: bin/hydra and lib/ must exist next to install.sh" >&2
    echo "Next: cd to the hydra directory and run: PREFIX=\$HOME/.local ./install.sh" >&2
    exit 1
fi

if ! grep -q "Hydra - POSIX-compliant CLI" "$SRC_BIN" 2>/dev/null; then
    echo "Error: bin/hydra does not appear to be the hydra binary" >&2
    echo "Next: cd to the hydra source checkout and retry ./install.sh" >&2
    exit 1
fi

ensure_writable() {
    target="$1"
    if [ -d "$target" ]; then
        if [ -w "$target" ]; then
            return 0
        fi
    elif mkdir -p "$target" 2>/dev/null; then
        return 0
    fi
    echo "Error: PREFIX is not writable: $target" >&2
    echo "Next: PREFIX=\$HOME/.local $0   or   sudo env PREFIX=$PREFIX $0" >&2
    echo "See README Quick Start." >&2
    return 1
}

echo "Installing hydra to $PREFIX..."

ensure_writable "$BIN_DIR"
ensure_writable "$LIB_DIR"

echo "Installing hydra binary..."
cp "$SRC_BIN" "$BIN_DIR/hydra"
chmod +x "$BIN_DIR/hydra"

echo "Installing library files..."
for lib_file in "$SRC_LIB"/*.sh; do
    if [ -f "$lib_file" ]; then
        filename="$(basename "$lib_file")"
        echo "  Installing $filename..."
        cp "$lib_file" "$LIB_DIR/$filename"
    fi
done

if [ ! -x "$BIN_DIR/hydra" ]; then
    echo "Error: Installation failed: $BIN_DIR/hydra is not executable" >&2
    echo "Next: PREFIX=\$HOME/.local $0 or see README Quick Start" >&2
    exit 1
fi

if [ ! -f "$LIB_DIR/git.sh" ]; then
    echo "Error: Installation failed: libraries missing at $LIB_DIR" >&2
    echo "Next: re-run from a complete source checkout (lib/*.sh must exist)" >&2
    exit 1
fi

# Confirm library discovery without a HYDRA_ROOT override
ver_out="$(
    unset HYDRA_ROOT
    "$BIN_DIR/hydra" version
)" || {
    echo "Error: Installed hydra could not run (library discovery failed)" >&2
    echo "Next: confirm $LIB_DIR/git.sh exists, or set HYDRA_ROOT and see README Quick Start" >&2
    exit 1
}

echo ""
echo "Installation complete!"
echo ""
echo "$ver_out"
echo "Binary: $BIN_DIR/hydra"
echo "Libraries: $LIB_DIR"
echo ""
echo "Run 'hydra doctor' to verify readiness, or see README Quick Start."
echo ""

case ":$PATH:" in
    *":$PREFIX/bin:"*)
        ;;
    *)
        echo "WARNING: $PREFIX/bin is not in your PATH"
        echo "Next: add it to your shell configuration:"
        echo "  export PATH=\"$PREFIX/bin:\$PATH\""
        ;;
esac
