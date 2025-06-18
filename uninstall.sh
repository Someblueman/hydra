#!/bin/sh
# Uninstall script for hydra
# POSIX-compliant uninstallation script

set -e

# Check for root permissions
if [ "$(id -u)" -ne 0 ]; then
    echo "This script requires root permissions. Please run with sudo." >&2
    exit 1
fi

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

# Check for user data
USER_DATA="$HOME/.hydra"
if [ -d "$USER_DATA" ]; then
    echo ""
    echo "User data found at $USER_DATA"
    echo "This contains your session mappings and layouts."
    printf "Do you want to remove user data? (y/N): "
    read -r response
    case "$response" in
        [yY][eE][sS]|[yY])
            echo "Removing user data..."
            rm -rf "$USER_DATA"
            ;;
        *)
            echo "Keeping user data at $USER_DATA"
            ;;
    esac
fi

echo ""
echo "Hydra has been uninstalled."