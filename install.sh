#!/bin/sh
# Install script for hydra

echo "Installing hydra..."
mkdir -p /usr/local/bin
cp bin/hydra /usr/local/bin/hydra
chmod +x /usr/local/bin/hydra
mkdir -p /usr/local/lib/hydra
cp lib/*.sh /usr/local/lib/hydra/
echo "Installation complete"
echo "Run 'hydra help' to get started"