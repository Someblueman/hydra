#!/bin/sh
# Shell completion generation for Hydra
# POSIX-compliant shell script

# Install completion scripts
# Usage: install_completions [bash|zsh|fish]
# Returns: 0 on success, 1 on failure
install_completions() {
    shell="${1:-all}"
    
    case "$shell" in
        bash|all)
            # Try to find bash completion directory
            if [ -d "/etc/bash_completion.d" ]; then
                comp_dir="/etc/bash_completion.d"
            elif [ -d "/usr/share/bash-completion/completions" ]; then
                comp_dir="/usr/share/bash-completion/completions"
            elif [ -d "/usr/local/etc/bash_completion.d" ]; then
                comp_dir="/usr/local/etc/bash_completion.d"
            else
                echo "Warning: No bash completion directory found" >&2
                echo "Generated completion saved to hydra-completion.bash" >&2
                generate_bash_completion > hydra-completion.bash
                return 0
            fi
            
            echo "Installing bash completion to $comp_dir/hydra"
            if generate_bash_completion > "$comp_dir/hydra" 2>/dev/null; then
                echo "Bash completion installed successfully"
            else
                echo "Error: Failed to install bash completion (permission denied?)" >&2
                echo "Try running with sudo or save manually:" >&2
                echo "  hydra completion bash > hydra-completion.bash" >&2
                return 1
            fi
            ;;
    esac
    
    case "$shell" in
        zsh|all)
            # Try to find zsh completion directory
            if [ -d "/usr/share/zsh/site-functions" ]; then
                comp_dir="/usr/share/zsh/site-functions"
            elif [ -d "/usr/local/share/zsh/site-functions" ]; then
                comp_dir="/usr/local/share/zsh/site-functions"
            else
                echo "Warning: No zsh completion directory found" >&2
                echo "Generated completion saved to _hydra" >&2
                generate_zsh_completion > _hydra
                return 0
            fi
            
            echo "Installing zsh completion to $comp_dir/_hydra"
            if generate_zsh_completion > "$comp_dir/_hydra" 2>/dev/null; then
                echo "Zsh completion installed successfully"
            else
                echo "Error: Failed to install zsh completion (permission denied?)" >&2
                echo "Try running with sudo or save manually:" >&2
                echo "  hydra completion zsh > _hydra" >&2
                return 1
            fi
            ;;
    esac
    
    case "$shell" in
        fish|all)
            # Try to find fish completion directory
            if [ -d "$HOME/.config/fish/completions" ]; then
                comp_dir="$HOME/.config/fish/completions"
            elif [ -d "/usr/share/fish/completions" ]; then
                comp_dir="/usr/share/fish/completions"
            elif [ -d "/usr/local/share/fish/completions" ]; then
                comp_dir="/usr/local/share/fish/completions"
            else
                echo "Warning: No fish completion directory found" >&2
                echo "Generated completion saved to hydra.fish" >&2
                generate_fish_completion > hydra.fish
                return 0
            fi
            
            echo "Installing fish completion to $comp_dir/hydra.fish"
            if generate_fish_completion > "$comp_dir/hydra.fish" 2>/dev/null; then
                echo "Fish completion installed successfully"
            else
                echo "Error: Failed to install fish completion (permission denied?)" >&2
                echo "Try saving manually:" >&2
                echo "  hydra completion fish > hydra.fish" >&2
                return 1
            fi
            ;;
    esac
    
    return 0
}

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

