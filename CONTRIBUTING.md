# Contributing to Hydra

## POSIX Compliance

Hydra is strictly POSIX-compliant. All scripts must work with `/bin/sh` and pass validation with both ShellCheck and dash.

### Shebang

Always use:
```sh
#!/bin/sh
```

Never use `#!/bin/bash`, `#!/usr/bin/env bash`, `#!/bin/zsh`, etc.

### Forbidden Constructs

The following are **NOT** allowed in POSIX shell:

#### Arrays
```sh
# FORBIDDEN
x=(one two three)
echo ${x[0]}

# POSIX alternative
set -- one two three
echo "$1"
```

#### Double Brackets
```sh
# FORBIDDEN
if [[ $var == pattern* ]]; then

# POSIX alternative
case "$var" in
    pattern*) ;;
esac
```

#### Process Substitution
```sh
# FORBIDDEN
diff <(command1) <(command2)

# POSIX alternative
command1 > tmp1
command2 > tmp2
diff tmp1 tmp2
rm -f tmp1 tmp2
```

#### Non-POSIX Parameter Expansions
```sh
# FORBIDDEN
${var,,}       # lowercase
${var^^}       # uppercase
${var/old/new} # substitution

# POSIX alternatives
echo "$var" | tr '[:upper:]' '[:lower:]'  # lowercase
echo "$var" | tr '[:lower:]' '[:upper:]'  # uppercase
echo "$var" | sed 's/old/new/'            # substitution
```

#### Other Forbidden Features
- `select` loops
- `let` arithmetic
- `(( ))` arithmetic expressions
- `&>` redirection
- `|&` pipe
- `function` keyword
- `local` variables (use subshells instead)
- `source` (use `.` instead)
- `which` (use `command -v` instead)

### Required Practices

#### String Comparison
```sh
# Always quote variables
if [ "$var" = "value" ]; then

# For existence checks
if [ -n "$var" ]; then  # not empty
if [ -z "$var" ]; then  # empty
```

#### Command Substitution
```sh
# Use $() not backticks
result="$(command)"
```

#### Error Handling
```sh
# Use set -eu at script start
set -eu

# Check command existence
if command -v tool >/dev/null 2>&1; then
    tool --version
fi
```

#### Loops
```sh
# Iterate over arguments
for arg in "$@"; do
    echo "$arg"
done

# Read lines
while IFS= read -r line; do
    echo "$line"
done < file.txt
```

### Testing

Before committing:

1. Run `make lint` to check all scripts
2. Test with dash: `dash -n script.sh`
3. Test functionality with both sh and bash

### Validation Tools

- **ShellCheck**: `shellcheck --shell=sh --severity=style script.sh`
- **dash**: `dash -n script.sh`
- **Makefile**: `make lint` runs both checks

### Common Patterns

#### Parsing Arguments
```sh
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -v|--verbose)
            verbose=1
            shift
            ;;
        --)
            shift
            break
            ;;
        -*)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
        *)
            break
            ;;
    esac
done
```

#### Default Values
```sh
# Use parameter expansion
var="${VAR:-default}"
```

#### Temporary Files
```sh
# Create safely
tmpfile="$(mktemp)" || exit 1
trap 'rm -f "$tmpfile"' EXIT INT TERM
```

### Resources

- [POSIX Shell Command Language](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html)
- [Dash as /bin/sh](https://wiki.ubuntu.com/DashAsBinSh)
- [ShellCheck Wiki](https://www.shellcheck.net/wiki/)