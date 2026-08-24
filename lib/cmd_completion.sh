#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

cmd_completion() {
    shell="${1:-bash}"
    generate_completion "$shell"
}
