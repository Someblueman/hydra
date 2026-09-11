# Contributing to Hydra

## Build and verify

The shell CLI in `bin/hydra` owns lifecycle mutations; optional C helpers live in
`src/`. Runtime state is project-scoped state v2 under `$HYDRA_HOME/state/v2`.
See [contracts](docs/CONTRACTS.md) before changing CLI, JSON, or durable records.

Shell development needs Git, tmux 3.0+, Make, dash, and ShellCheck. Native builds
need a C99 compiler; fleet additionally needs pkg-config and JSON-C development
files. Run from source with `bin/hydra`; no installation is necessary.

```sh
make test-fast                         # first local PR check
make lint test                         # shell checks
make build-core build-tui build-fleet   # optional native helpers
make test-tui test-tui-pty sanitize-tui # focused native UI checks
make test-fleet sanitize-fleet         # focused fleet checks
make test-all                          # combined, parity, install and onboarding
make sanitize                          # supported core/TUI/fleet sanitizers
```

Choose focused checks for a localized change; run applicable broader acceptance for
contract, lifecycle, security, or native-memory changes. Expected error-path tests
may print errors: use exit codes and failure summaries. Exercise spawn/kill examples
in a disposable Git repository with isolated `HYDRA_HOME`, preserving real work.
For the first-run path, use `make smoke-onboarding`.

Start with `make test-fast` for the selected fast subset. Use focused targets for
localized edits and `make test-all`, `make sanitize`, and `make quality-c` for the
full local qualification set when a change crosses those boundaries. CI runs the
fast subset as `pr-gate` on pull requests targeting `main`, `develop`, or
`release/**`; pushes to those branches and manual dispatch run the full `release-gate`.

Keep changes cohesive and source/test files under 500 lines. Document behavior under
Unreleased in the changelog. Versions are assigned at release time from compatibility
impact; passing local checks is not a release.

## Where to add functionality

Start with the owning domain and its public contract. Native sources under
`src/fleet/` and `src/tui/` are discovered recursively by the build and C analysis;
private headers are tracked through compiler dependency files.

| Change | Entry point and implementation | Focused verification |
|---|---|---|
| Shell command | `lib/cli.sh`, its `lib/cmd_*.sh` handler, and the domain library; declare shared dependencies in `lib/deps.sh` | `make lint` and the affected `tests/test_*.sh` |
| Native TUI action | `src/tui/actions.c` palette table: label, literal command/subcommand, and selection scope; interactive prompts stay in action code | `make test-tui test-tui-pty`; argv cases in `tests/c/test_tui_palette.inc` |
| Native TUI data or presentation | `model.c` parses records, `adapter.c` fetches snapshots, `selection.c` selects heads, `render.c` draws, and `input.c` handles keys | TUI fixture and PTY suites; `make sanitize-tui` for native changes |
| Plan operation | `src/fleet/plan/plan_cli.c` operation table and the corresponding plan module | `make test-fleet`, especially workflow plan/approval acceptance |
| Agent provider | `src/fleet/agent/`: profile/probe, decode, stream observation, and run coordination have separate owners | `make test-fleet`, especially agent execution/auth acceptance |
| Remote task | `src/fleet/task/`: CLI parsing, acceptance, launch/control, execution, collection, and result verification | `make test-fleet sanitize-fleet` |
| Transport or subprocess behavior | `src/fleet/transport/` owns SSH/protocol operations; `support/process.c` owns process capture and deadlines | Fleet and sanitizer suites; preserve cancellation and uncertainty semantics |

Keep one owner for resource cleanup and state transitions. Put new work beside
that owner; extract a helper when it has a clear input/output contract. Add private
headers only for interfaces used across translation units. Ordinary palette
actions need a table entry and an argv test, without a new dispatch branch.

Run `make quality-c` for native changes. Its reviewed per-function ceilings catch
increases; lower an allowance when simplification reduces the measured score.
The quality baseline is retained in `docs/quality/cognitive-complexity.tsv`; it records
reviewed per-function ceilings and is consumed by `make quality-c`. Passing this gate
does not replace behavioral tests. Prefer the next concrete feature to another blanket
decomposition pass.

## POSIX Compliance

Hydra's runtime shell scripts use POSIX `/bin/sh` and pass ShellCheck and dash
validation. Native helpers use C99; generated Bash, Zsh, and Fish completions use
their target shell syntax.

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

1. Run `make lint` (ShellCheck and dash syntax checks).
2. Run the affected shell tests under `/bin/sh`.
3. Use the broader checks above when the changed boundary requires them.

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
# Create the temp file on the same filesystem as the destination, then rename.
# Bare mktemp usually lands in /tmp; mv across devices is not atomic.
tmpfile="$(mktemp_adjacent "$dest")" || exit 1
# write to "$tmpfile"...
atomic_replace "$dest" "$tmpfile" || { rm -f "$tmpfile"; exit 1; }
```

### Resources

- [POSIX Shell Command Language](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html)
- [Dash as /bin/sh](https://wiki.ubuntu.com/DashAsBinSh)
- [ShellCheck Wiki](https://www.shellcheck.net/wiki/)
