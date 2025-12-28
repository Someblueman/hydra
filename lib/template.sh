#!/bin/sh
# Template management functions for Hydra
# POSIX-compliant shell script

# Templates directory
HYDRA_TEMPLATES_DIR="${HYDRA_HOME:-$HOME/.hydra}/templates"

# Ensure templates directory exists
# Usage: init_templates_dir
init_templates_dir() {
    mkdir -p "$HYDRA_TEMPLATES_DIR"
}

# List all available templates
# Usage: list_templates
# Returns: Template names (one per line) on stdout
list_templates() {
    init_templates_dir
    for f in "$HYDRA_TEMPLATES_DIR"/*.yml "$HYDRA_TEMPLATES_DIR"/*.yaml; do
        [ -f "$f" ] || continue
        basename "$f" | sed 's/\.ya\{0,1\}ml$//'
    done | sort -u
}

# Get template path by name
# Usage: get_template_path <name>
# Returns: Full path on stdout, 1 if not found
get_template_path() {
    _name="$1"
    init_templates_dir

    # Try .yml first, then .yaml
    if [ -f "$HYDRA_TEMPLATES_DIR/${_name}.yml" ]; then
        printf '%s' "$HYDRA_TEMPLATES_DIR/${_name}.yml"
        return 0
    elif [ -f "$HYDRA_TEMPLATES_DIR/${_name}.yaml" ]; then
        printf '%s' "$HYDRA_TEMPLATES_DIR/${_name}.yaml"
        return 0
    fi
    return 1
}

# Check if template exists
# Usage: template_exists <name>
# Returns: 0 if exists, 1 otherwise
template_exists() {
    get_template_path "$1" >/dev/null 2>&1
}

# Show template contents
# Usage: show_template <name>
# Returns: Template contents on stdout
show_template() {
    _name="$1"
    _path="$(get_template_path "$_name")" || {
        echo "Error: Template '$_name' not found" >&2
        return 1
    }
    cat "$_path"
}

# Validate template name
# Usage: validate_template_name <name>
# Returns: 0 if valid, 1 if invalid
validate_template_name() {
    _name="$1"

    if [ -z "$_name" ]; then
        echo "Error: Template name is required" >&2
        return 1
    fi

    # Only allow alphanumeric, dash, underscore
    case "$_name" in
        *[!a-zA-Z0-9_-]*)
            echo "Error: Template name must be alphanumeric, dash, or underscore only" >&2
            return 1
            ;;
    esac

    return 0
}

# Create template from current config or generate minimal
# Usage: create_template <name> [source_config_path]
# Returns: 0 on success, 1 on failure
create_template() {
    _name="$1"
    _source="${2:-}"

    init_templates_dir

    if ! validate_template_name "$_name"; then
        return 1
    fi

    _dest="$HYDRA_TEMPLATES_DIR/${_name}.yml"

    # Check for overwrite (only in interactive mode)
    if [ -f "$_dest" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
        printf "Template '%s' already exists. Overwrite? [y/N] " "$_name"
        read -r response
        case "$response" in
            [yY][eE][sS]|[yY]) ;;
            *) echo "Aborted" >&2; return 1 ;;
        esac
    fi

    if [ -n "$_source" ] && [ -f "$_source" ]; then
        # Copy and add description header
        {
            echo "# Template: $_name"
            echo "# Created: $(date '+%Y-%m-%d %H:%M:%S')"
            echo "# Source: $_source"
            echo ""
            cat "$_source"
        } > "$_dest"
    else
        # Generate minimal template
        {
            echo "# Template: $_name"
            echo "# Created: $(date '+%Y-%m-%d %H:%M:%S')"
            echo "description: \"Template description\""
            echo ""
            echo "layout: default"
            echo "ai_tool: claude"
            echo ""
            echo "# setup:"
            echo "#   - npm install"
            echo "#   - cp .env.example .env"
            echo ""
            echo "# startup:"
            echo "#   - git status"
        } > "$_dest"
    fi

    echo "Created template: $_dest"
    return 0
}

# Delete a template
# Usage: delete_template <name> [--force]
# Returns: 0 on success, 1 on failure
delete_template() {
    _name="$1"
    _force="${2:-}"

    _path="$(get_template_path "$_name")" || {
        echo "Error: Template '$_name' not found" >&2
        return 1
    }

    if [ "$_force" != "--force" ] && [ "$_force" != "-f" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
        printf "Delete template '%s'? [y/N] " "$_name"
        read -r response
        case "$response" in
            [yY][eE][sS]|[yY]) ;;
            *) echo "Aborted" >&2; return 1 ;;
        esac
    fi

    rm "$_path"
    echo "Deleted template: $_name"
    return 0
}

# Get a field from template YAML
# Usage: get_template_field <template_path> <field>
# Returns: Field value on stdout
get_template_field() {
    _tpl="$1"
    _field="$2"

    [ -f "$_tpl" ] || return 1

    # Extract top-level field value (handles quoted and unquoted)
    awk -v field="$_field" '
        /^[[:space:]]*#/ { next }
        $0 ~ "^" field ":" {
            sub(/^[^:]+:[[:space:]]*/, "")
            gsub(/^["'\''"]|["'\''"]$/, "")
            print
            exit
        }
    ' "$_tpl"
}

# Expand template variables
# Usage: expand_template_vars <template_path> <branch> <session> <worktree> <repo_root>
# Returns: Expanded content on stdout
expand_template_vars() {
    _tpl="$1"
    _branch="$2"
    _session="$3"
    _wt="$4"
    _repo="$5"

    sed \
        -e "s|\${BRANCH}|$_branch|g" \
        -e "s|\${SESSION}|$_session|g" \
        -e "s|\${WORKTREE}|$_wt|g" \
        -e "s|\${REPO_ROOT}|$_repo|g" \
        -e "s|\${HYDRA_HOME}|${HYDRA_HOME:-$HOME/.hydra}|g" \
        "$_tpl"
}

# Apply template to create merged config
# Usage: apply_template <template_name> <worktree> <repo_root> <branch> <session>
# Returns: Path to merged config file on stdout
apply_template() {
    _tpl_name="$1"
    _wt="$2"
    _repo="$3"
    _branch="$4"
    _session="$5"

    _tpl_path="$(get_template_path "$_tpl_name")" || return 1

    # Create temp file for merged config
    _merged="$(mktemp)"

    # Expand variables and write to merged file
    expand_template_vars "$_tpl_path" "$_branch" "$_session" "$_wt" "$_repo" > "$_merged"

    # If session-level config exists, append it (later entries override)
    _session_cfg=""
    if [ -f "$_wt/.hydra/config.yml" ]; then
        _session_cfg="$_wt/.hydra/config.yml"
    elif [ -f "$_wt/.hydra/config.yaml" ]; then
        _session_cfg="$_wt/.hydra/config.yaml"
    fi

    if [ -n "$_session_cfg" ]; then
        {
            echo ""
            echo "# Session-level overrides from: $_session_cfg"
            cat "$_session_cfg"
        } >> "$_merged"
    fi

    printf '%s' "$_merged"
}
