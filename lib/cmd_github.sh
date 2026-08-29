#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

cmd_pr() {
    # Create or show PR for a branch
    branch="${1:-}"

    # If no branch specified, use current session's branch
    if [ -z "$branch" ]; then
        current_session=""
        if [ -n "${TMUX:-}" ]; then
            current_session="$(tmux display-message -p '#{session_name}' 2>/dev/null || true)"
        fi
        if [ -z "$current_session" ]; then
            echo "Error: Not in a Hydra session and no branch specified" >&2
            echo "Usage: hydra pr [<branch>]" >&2
            return 1
        fi
        branch="$(get_branch_for_session "$current_session" 2>/dev/null || true)"
        if [ -z "$branch" ]; then
            echo "Error: Could not determine branch for current session" >&2
            return 1
        fi
    fi

    # Check if branch exists in mappings
    session="$(get_session_for_branch "$branch" 2>/dev/null || true)"
    if [ -z "$session" ]; then
        echo "Error: No session found for branch '$branch'" >&2
        return 1
    fi

    # Check if already has PR
    existing_pr="$(get_pr_for_branch "$branch" 2>/dev/null || true)"
    if [ -n "$existing_pr" ] && [ "$existing_pr" != "-" ]; then
        echo "Branch '$branch' already has PR #$existing_pr"
        printf "Open in browser? [y/N] "
        read -r response
        case "$response" in
            [yY][eE][sS]|[yY])
                _load_lib github
                gh pr view "$existing_pr" --web
                ;;
        esac
        return 0
    fi

    # Check gh CLI
    _load_lib github
    if ! check_gh_cli; then
        return 1
    fi

    # Create new PR
    echo "Creating PR for branch '$branch'..."
    new_pr="$(create_pr_for_branch "$branch")" || return 1

    # Store PR number in state
    set_pr_for_branch "$branch" "$new_pr"

    echo "Created PR #$new_pr"
    printf "Open in browser? [y/N] "
    read -r response
    case "$response" in
        [yY][eE][sS]|[yY])
            gh pr view "$new_pr" --web
            ;;
    esac
}

cmd_template() {
    subcommand="${1:-}"
    shift 2>/dev/null || true

    case "$subcommand" in
        list|ls)
            templates="$(list_templates)"
            if [ -z "$templates" ]; then
                echo "No templates found"
                echo "Create one with: hydra template create <name>"
            else
                echo "Available templates:"
                echo "$templates" | while read -r t; do
                    [ -z "$t" ] && continue
                    desc="$(get_template_field "$(get_template_path "$t")" "description" 2>/dev/null || true)"
                    if [ -n "$desc" ]; then
                        printf "  %s - %s\n" "$t" "$desc"
                    else
                        printf "  %s\n" "$t"
                    fi
                done
            fi
            ;;

        create)
            name="${1:-}"
            if [ -z "$name" ]; then
                echo "Error: Template name required" >&2
                echo "Usage: hydra template create <name>" >&2
                return 1
            fi

            # Try to find current session's config
            source_cfg=""
            if [ -n "${TMUX:-}" ]; then
                _load_lib state
                _load_lib git
                current_session="$(tmux display-message -p '#{session_name}' 2>/dev/null || true)"
                branch="$(get_branch_for_session "$current_session" 2>/dev/null || true)"
                if [ -n "$branch" ]; then
                    _load_lib paths
                    wt="$(get_worktree_path_for_branch "$branch" 2>/dev/null || true)"
                    if [ -n "$wt" ] && [ -f "$wt/.hydra/config.yml" ]; then
                        source_cfg="$wt/.hydra/config.yml"
                    elif [ -n "$wt" ] && [ -f "$wt/.hydra/config.yaml" ]; then
                        source_cfg="$wt/.hydra/config.yaml"
                    fi
                fi
            fi

            create_template "$name" "$source_cfg"
            ;;

        show|view)
            name="${1:-}"
            if [ -z "$name" ]; then
                echo "Error: Template name required" >&2
                echo "Usage: hydra template show <name>" >&2
                return 1
            fi
            show_template "$name"
            ;;

        delete|rm)
            name=""
            force=""

            while [ $# -gt 0 ]; do
                case "$1" in
                    -f|--force)
                        force="--force"
                        shift
                        ;;
                    -*)
                        echo "Error: Unknown option '$1'" >&2
                        return 1
                        ;;
                    *)
                        name="$1"
                        shift
                        ;;
                esac
            done

            if [ -z "$name" ]; then
                echo "Error: Template name required" >&2
                echo "Usage: hydra template delete <name> [-f|--force]" >&2
                return 1
            fi

            delete_template "$name" "$force"
            ;;

        edit)
            name="${1:-}"
            if [ -z "$name" ]; then
                echo "Error: Template name required" >&2
                echo "Usage: hydra template edit <name>" >&2
                return 1
            fi

            path="$(get_template_path "$name")" || {
                echo "Error: Template '$name' not found" >&2
                return 1
            }

            "${EDITOR:-vi}" "$path"
            ;;

        ""|help|-h|--help)
            cat <<EOF
hydra template - Manage session templates

Usage:
  hydra template list              List available templates
  hydra template create <name>     Save current config as template
  hydra template show <name>       Display template contents
  hydra template edit <name>       Edit template in \$EDITOR
  hydra template delete <name>     Delete a template
                                   Options: -f, --force

Templates are stored in: ${HYDRA_HOME:-~/.hydra}/templates/
EOF
            ;;

        *)
            echo "Error: Unknown subcommand '$subcommand'" >&2
            echo "Run 'hydra template help' for usage" >&2
            return 1
            ;;
    esac
}
