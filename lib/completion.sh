#!/bin/sh
# Shell completion generation for Hydra
# POSIX-compliant shell script

# Generate bash completion script
# Usage: generate_bash_completion
# Returns: Bash completion script on stdout
generate_bash_completion() {
    cat <<'EOF'
# Bash completion for hydra
# Source this file or place it in /etc/bash_completion.d/

_hydra_completion() {
    local cur prev opts commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    
    commands="spawn list switch kill regenerate status doctor dashboard cycle-layout completion version help"
    opts="-h --help -v --version"
    
    case "${prev}" in
        hydra)
            COMPREPLY=($(compgen -W "${commands} ${opts}" -- ${cur}))
            return 0
            ;;
        spawn|kill)
            # Complete with git branch names
            local branches=$(git branch 2>/dev/null | sed 's/^[ *]*//' | grep -v '^(')
            COMPREPLY=($(compgen -W "${branches}" -- ${cur}))
            return 0
            ;;
        switch)
            # Complete with hydra session names from map file
            if [ -f "${HYDRA_MAP:-$HOME/.hydra/map}" ]; then
                local sessions=$(awk '{print $1}' "${HYDRA_MAP:-$HOME/.hydra/map}" 2>/dev/null)
                COMPREPLY=($(compgen -W "${sessions}" -- ${cur}))
            fi
            return 0
            ;;
        -l|--layout)
            # Complete with layout names
            COMPREPLY=($(compgen -W "default dev full" -- ${cur}))
            return 0
            ;;
        *)
            ;;
    esac
    
    # Check if we're completing a flag for spawn command
    if [[ "${COMP_WORDS[@]}" =~ spawn ]]; then
        case "${prev}" in
            -n|--count)
                # Complete with numbers 1-10
                COMPREPLY=($(compgen -W "1 2 3 4 5 6 7 8 9 10" -- ${cur}))
                return 0
                ;;
            --ai)
                # Complete with AI tools
                COMPREPLY=($(compgen -W "claude aider codex cursor copilot" -- ${cur}))
                return 0
                ;;
            --agents)
                # Suggest example format
                COMPREPLY=($(compgen -W "claude:2,aider:1" -- ${cur}))
                return 0
                ;;
            -i|--issue)
                # GitHub issue numbers
                return 0
                ;;
        esac
        
        case "${cur}" in
            -*)
                COMPREPLY=($(compgen -W "-l --layout -n --count --ai --agents -i --issue" -- ${cur}))
                return 0
                ;;
        esac
    fi
}

complete -F _hydra_completion hydra
EOF
}

# Generate zsh completion script
# Usage: generate_zsh_completion
# Returns: Zsh completion script on stdout
generate_zsh_completion() {
    cat <<'EOF'
#compdef hydra
# Zsh completion for hydra

_hydra() {
    local context state line
    typeset -A opt_args
    
    _arguments -C \
        '1: :_hydra_commands' \
        '*::arg:->args' \
        && return 0
    
    case $state in
        args)
            case $words[1] in
                spawn)
                    _arguments \
                        '(-l --layout)'{-l,--layout}'[Layout to use]:layout:(default dev full)' \
                        '(-n --count)'{-n,--count}'[Number of sessions to spawn]:count:(1 2 3 4 5 6 7 8 9 10)' \
                        '--ai[AI tool to use]:ai:(claude aider codex cursor copilot)' \
                        '--agents[Mixed agents specification]:agents:' \
                        '(-i --issue)'{-i,--issue}'[Create from GitHub issue]:issue:' \
                        '1:branch:_hydra_branches'
                    ;;
                kill)
                    _arguments '1:branch:_hydra_branches'
                    ;;
                switch)
                    _arguments '1:session:_hydra_sessions'
                    ;;
            esac
            ;;
    esac
}

_hydra_commands() {
    local commands; commands=(
        'spawn:Create a new worktree and tmux session'
        'list:List all active Hydra heads'
        'switch:Switch to a different head (interactive)'
        'kill:Remove a worktree and its tmux session'
        'regenerate:Restore tmux sessions for existing worktrees'
        'status:Show health status of all heads'
        'doctor:Check system performance'
        'dashboard:View all sessions in a single dashboard'
        'cycle-layout:Cycle through tmux pane layouts'
        'completion:Generate shell completion scripts'
        'version:Show version information'
        'help:Show help message'
    )
    _describe 'command' commands
}

_hydra_branches() {
    local branches
    branches=(${(f)"$(git branch 2>/dev/null | sed 's/^[ *]*//' | grep -v '^(')"})
    _describe 'branch' branches
}

_hydra_sessions() {
    local sessions
    if [[ -f "${HYDRA_MAP:-$HOME/.hydra/map}" ]]; then
        sessions=(${(f)"$(awk '{print $1}' "${HYDRA_MAP:-$HOME/.hydra/map}" 2>/dev/null)"})
        _describe 'session' sessions
    fi
}

_hydra "$@"
EOF
}

# Generate fish completion script
# Usage: generate_fish_completion
# Returns: Fish completion script on stdout
generate_fish_completion() {
    cat <<'EOF'
# Fish completion for hydra

# Complete commands
complete -c hydra -f -n '__fish_use_subcommand' -a 'spawn' -d 'Create a new worktree and tmux session'
complete -c hydra -f -n '__fish_use_subcommand' -a 'list' -d 'List all active Hydra heads'
complete -c hydra -f -n '__fish_use_subcommand' -a 'switch' -d 'Switch to a different head (interactive)'
complete -c hydra -f -n '__fish_use_subcommand' -a 'kill' -d 'Remove a worktree and its tmux session'
complete -c hydra -f -n '__fish_use_subcommand' -a 'regenerate' -d 'Restore tmux sessions for existing worktrees'
complete -c hydra -f -n '__fish_use_subcommand' -a 'status' -d 'Show health status of all heads'
complete -c hydra -f -n '__fish_use_subcommand' -a 'doctor' -d 'Check system performance'
complete -c hydra -f -n '__fish_use_subcommand' -a 'dashboard' -d 'View all sessions in a single dashboard'
complete -c hydra -f -n '__fish_use_subcommand' -a 'cycle-layout' -d 'Cycle through tmux pane layouts'
complete -c hydra -f -n '__fish_use_subcommand' -a 'completion' -d 'Generate shell completion scripts'
complete -c hydra -f -n '__fish_use_subcommand' -a 'version' -d 'Show version information'
complete -c hydra -f -n '__fish_use_subcommand' -a 'help' -d 'Show help message'

# Complete flags
complete -c hydra -f -n '__fish_use_subcommand' -s h -l help -d 'Show help message'
complete -c hydra -f -n '__fish_use_subcommand' -s v -l version -d 'Show version information'

# Complete spawn command
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -s l -l layout -d 'Layout to use' -a 'default dev full'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -s n -l count -d 'Number of sessions to spawn' -a '1 2 3 4 5 6 7 8 9 10'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l ai -d 'AI tool to use' -a 'claude aider codex cursor copilot'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l agents -d 'Mixed agents specification (e.g., claude:2,aider:1)'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -s i -l issue -d 'Create from GitHub issue number'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn; and not __fish_seen_subcommand_from -l --layout -n --count --ai --agents -i --issue' -a '(git branch 2>/dev/null | sed "s/^[ *]*//" | grep -v "^(")'

# Complete kill command with git branches
complete -c hydra -f -n '__fish_seen_subcommand_from kill' -a '(git branch 2>/dev/null | sed "s/^[ *]*//" | grep -v "^(")'

# Complete switch command with hydra sessions
complete -c hydra -f -n '__fish_seen_subcommand_from switch' -a '(test -f "$HYDRA_MAP"; or test -f "$HOME/.hydra/map"; and awk "{print \$1}" "$HYDRA_MAP" "$HOME/.hydra/map" 2>/dev/null)'
EOF
}

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