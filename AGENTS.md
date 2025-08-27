# Repository Guidelines

## Project Structure & Module Organization
- `bin/hydra`: POSIX `/bin/sh` entrypoint.
- `lib/*.sh`: Modular helpers (`git.sh`, `tmux.sh`, `layout.sh`, `state.sh`, `completion.sh`, `dashboard.sh`, `github.sh`).
- `tests/*.sh`: POSIX test scripts (`test_*.sh`).
- `assets/`: Images for README/docs; no runtime code.
- `scripts/`: Developer tooling (e.g., `install-hooks.sh`).
- `.github/workflows/`: CI for linting, syntax, and tests.
- Runtime state lives in `~/.hydra` (e.g., `~/.hydra/map`).

## Build, Test, and Development Commands
- `make lint`: Run ShellCheck and `dash -n` for POSIX compliance.
- `make test`: Execute all tests in `tests/*.sh`.
- `make dev-setup`: Install local dev hooks if available.
- `make install`: Install to `/usr/local/bin` and `/usr/local/lib/hydra`.
- `make help`: List available targets. Example: `bin/hydra --help` to inspect commands.

## Coding Style & Naming Conventions
- Shell: strict POSIX (`#!/bin/sh`, `set -eu`), validate with `dash`.
- Indentation: 4 spaces; quote all expansions; no Bashisms.
- Functions: `lower_snake_case` (e.g., `spawn_single`); globals: `UPPER_SNAKE_CASE` with `HYDRA_` prefix.
- Files: library modules are lowercase `.sh`; keep concerns small and focused.
- Prefer `case`/`getopts`-style parsing; avoid `eval` and process substitution. Example: use `grep -c` instead of `grep | wc -l`.

## Testing Guidelines
- Place unit tests in `tests/` named `test_*.sh`; use the existing `assert_*` helpers.
- Isolate state: set `HYDRA_HOME="$(mktemp -d)"` and `HYDRA_NONINTERACTIVE=1` in tests; clean up temp dirs.
- Avoid hard tmux dependencies in unit tests; gate integration behavior on `$TMUX` or stub where possible.
- Run locally with `make test`; CI runs ShellCheck, dash syntax checks, and tests across shells/OS.

## Commit & Pull Request Guidelines
- Follow Conventional Commits: `feat`, `fix`, `chore`, `docs`, `refactor`, `test`, optional scope (e.g., `fix(dashboard): ...`).
- Keep subject imperative and concise (<72 chars); link issues in body (`Closes #123`).
- PRs should include: clear description, rationale, tests (or notes on coverage), and screenshots/GIFs for dashboard/layout changes.
- CI respects `[skip ci]` for quick docs-only changes; use sparingly.

## Security & Configuration Tips
- Validate inputs using existing helpers (`validate_branch_name`, `validate_worktree_path`); never `rm -rf` unvalidated paths.
- Quote variables, avoid globbing surprises, and prefer explicit paths.
- When running from source inside worktrees, set `HYDRA_ROOT=/path/to/hydra` or `make install` to ensure library discovery.
