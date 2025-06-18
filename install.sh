#!/bin/sh
# Install script for hydra
# POSIX-compliant installation script

set -e

# Check for root permissions
if [ "$(id -u)" -ne 0 ]; then
    echo "This script requires root permissions. Please run with sudo." >&2
    exit 1
fi

echo "Installing hydra..."

# Installation directories
BIN_DIR="/usr/local/bin"
LIB_DIR="/usr/local/lib/hydra"

# Verify we're in the hydra project directory
if [ ! -f "bin/hydra" ] || [ ! -d "lib" ]; then
    echo "Error: This script must be run from the hydra project directory" >&2
    echo "Please cd to the hydra directory and run: sudo ./install.sh" >&2
    exit 1
fi

# Additional safety check - verify it's actually hydra
if ! grep -q "Hydra - POSIX-compliant CLI" bin/hydra 2>/dev/null; then
    echo "Error: bin/hydra does not appear to be the correct hydra binary" >&2
    exit 1
fi

# Create directories if they don't exist
echo "Creating installation directories..."
mkdir -p "$BIN_DIR"
mkdir -p "$LIB_DIR"

# Install the main binary
echo "Installing hydra binary..."
cp bin/hydra "$BIN_DIR/hydra"
chmod +x "$BIN_DIR/hydra"

# Install library files
echo "Installing library files..."
for lib_file in lib/*.sh; do
    if [ -f "$lib_file" ]; then
        filename="$(basename "$lib_file")"
        echo "  Installing $filename..."
        cp "$lib_file" "$LIB_DIR/$filename"
    fi
done

# Verify installation
if [ -x "$BIN_DIR/hydra" ]; then
    echo ""
    echo "Installation complete!"
    echo ""
    echo "Hydra has been installed to $BIN_DIR/hydra"
    echo "Library files installed to $LIB_DIR/"
    echo ""
    echo "Run 'hydra help' to get started"
    echo ""
    
    # Check if /usr/local/bin is in PATH
    if ! echo "$PATH" | grep -q "/usr/local/bin"; then
        echo "WARNING: /usr/local/bin is not in your PATH"
        echo "You may need to add it to your shell configuration:"
        echo "  export PATH=\"/usr/local/bin:\$PATH\""
    fi
else
    echo "Error: Installation failed" >&2
    exit 1
fi