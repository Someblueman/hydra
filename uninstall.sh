#!/bin/sh
# Uninstall script for hydra
# POSIX-compliant uninstallation script

set -e

# Check for root permissions
if [ "$(id -u)" -ne 0 ]; then
    echo "This script requires root permissions. Please run with sudo." >&2
    exit 1
fi

PURGE=false

# Parse options
while [ $# -gt 0 ]; do
    case "$1" in
        --purge)
            PURGE=true
            shift
            ;;
        -h|--help)
            echo "Usage: sudo ./uninstall.sh [--purge]" >&2
            echo "  --purge   Remove user data non-interactively (HYDRA_HOME and ~/.hydra)" >&2
            exit 0
            ;;
        *)
            echo "Error: Unknown option '$1'" >&2
            echo "Usage: sudo ./uninstall.sh [--purge]" >&2
            exit 1
            ;;
    esac
done

echo "Uninstalling hydra..."

# Installation directories
BIN_DIR="/usr/local/bin"
LIB_DIR="/usr/local/lib/hydra"

# Remove the binary
if [ -f "$BIN_DIR/hydra" ]; then
    echo "Removing hydra binary..."
    rm -f "$BIN_DIR/hydra"
else
    echo "Hydra binary not found at $BIN_DIR/hydra"
fi

# Remove library directory
if [ -d "$LIB_DIR" ]; then
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
