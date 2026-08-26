#!/bin/sh
# Uninstall script for hydra
# POSIX-compliant uninstallation script
# Removes $PREFIX/bin/hydra and $PREFIX/lib/hydra (default PREFIX=/usr/local).

set -e

usage() {
    echo "Usage: [PREFIX=/usr/local] [DESTDIR=] ./uninstall.sh [--purge]" >&2
    echo "  PREFIX    installation prefix (default: /usr/local)" >&2
    echo "  DESTDIR   optional staging directory prepended to PREFIX" >&2
    echo "  --purge   Remove user data non-interactively (HYDRA_HOME and ~/.hydra)" >&2
    echo "" >&2
    echo "Non-root example:" >&2
    echo "  PREFIX=\$HOME/.local ./uninstall.sh" >&2
}

PURGE=false

while [ $# -gt 0 ]; do
    case "$1" in
        --purge)
            PURGE=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: Unknown option '$1'" >&2
            echo "Next: run ./uninstall.sh --help for usage" >&2
            usage
            exit 1
            ;;
    esac
done

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

echo "Uninstalling hydra from $PREFIX..."

# Remove the binary
if [ -f "$BIN_DIR/hydra" ]; then
    if [ ! -w "$BIN_DIR" ] && [ ! -w "$BIN_DIR/hydra" ]; then
        echo "Error: PREFIX is not writable: $BIN_DIR" >&2
        echo "Next: PREFIX=\$HOME/.local $0   or   sudo env PREFIX=$PREFIX $0" >&2
        exit 1
    fi
    echo "Removing hydra binary..."
    rm -f "$BIN_DIR/hydra"
else
    echo "Hydra binary not found at $BIN_DIR/hydra"
fi

# Remove library directory
if [ -d "$LIB_DIR" ]; then
    if [ ! -w "$LIB_DIR" ] && [ ! -w "$(dirname "$LIB_DIR")" ]; then
        echo "Error: PREFIX is not writable: $LIB_DIR" >&2
        echo "Next: PREFIX=\$HOME/.local $0   or   sudo env PREFIX=$PREFIX $0" >&2
        exit 1
    fi
    echo "Removing library files..."
    rm -rf "$LIB_DIR"
else
    echo "Library directory not found at $LIB_DIR"
fi

# Check for user data in default and custom locations
# Resolve the invoking user's home directory (not root's) when run via sudo
TARGET_HOME="$HOME"
if [ -n "${SUDO_USER:-}" ]; then
    if TARGET_HOME_TMP="$(cd ~"$SUDO_USER" 2>/dev/null && pwd)"; then
        TARGET_HOME="$TARGET_HOME_TMP"
    fi
fi

# Build candidate directories to check
CANDIDATE_DIRS=""
if [ -n "${HYDRA_HOME:-}" ]; then
    CANDIDATE_DIRS="$HYDRA_HOME"
fi
CANDIDATE_DIRS="$CANDIDATE_DIRS $TARGET_HOME/.hydra"

seen=""
for USER_DATA in $CANDIDATE_DIRS; do
    # Deduplicate paths
    case " $seen " in
        *" $USER_DATA "*) continue ;;
        *) seen="$seen $USER_DATA" ;;
    esac

    if [ -d "$USER_DATA" ]; then
        echo ""
        echo "User data found at $USER_DATA"
        echo "This may contain session mappings, layouts, and dashboard state."
        if [ "$PURGE" = true ]; then
            echo "--purge specified: removing user data at $USER_DATA..."
            rm -rf "$USER_DATA"
        else
            printf "Do you want to remove user data at this location? (y/N): "
            read -r response
            case "$response" in
                [yY][eE][sS]|[yY])
                    echo "Removing user data at $USER_DATA..."
                    rm -rf "$USER_DATA"
                    ;;
                *)
                    echo "Keeping user data at $USER_DATA"
                    ;;
            esac
        fi
    fi
done

echo ""
echo "Hydra has been uninstalled."
echo "Binary removed from: $BIN_DIR/hydra"
echo "Libraries removed from: $LIB_DIR"
