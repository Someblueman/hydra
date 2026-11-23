#!/bin/sh
# Shell completion generation for Hydra
# POSIX-compliant shell script

# Generate completion script for specified shell
# Usage: generate_completion <shell>
# shell: bash, zsh, or fish (default: bash)
# Returns: Completion script on stdout, 1 on unknown shell
generate_completion() {
    shell="${1:-bash}"

    case "$shell" in
        bash)
            generate_bash_completion
            ;;
        zsh)
            generate_zsh_completion
            ;;
        fish)
            generate_fish_completion
            ;;
        *)
            echo "Error: Unknown shell '$shell'" >&2
            echo "Supported shells: bash, zsh, fish" >&2
            return 1
            ;;
    esac
}

