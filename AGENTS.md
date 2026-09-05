# AGENTS.md

## Cursor Cloud specific instructions

Hydra has a POSIX shell CLI (`bin/hydra`) that orchestrates one tmux
session + one git worktree per branch ("head"). Library code lives in `lib/*.sh`;
tests are POSIX shell scripts in `tests/*.sh` plus C tests in `tests/c/`.
Optional native helpers are built with `make build-core build-tui build-fleet`;
fleet additionally needs JSON-C development files and pkg-config at build time.

### Toolchain
- Required to run/lint/test: `git`, `tmux` (>= 3.0), `dash`, `shellcheck`, GNU `make`.
  `/bin/sh` is `dash` on this VM, which is exactly what the POSIX-compliance checks
  assume, so tests run under dash by default.
- Optional (improve UX, not required for tests): `fzf` (interactive `switch`/`tui`),
  `gh` (GitHub issue/PR features), and an AI CLI such as `claude`/`aider`/`gemini`.

### Lint / Test / Run (standard commands, see `Makefile` and `README.md`)
- Lint: `make lint` — runs ShellCheck (`--shell=sh --severity=style`) plus `dash -n`
  syntax checks on every shell file.
- Test: `make test` — runs each `tests/test_*.sh` with `sh`. Passing runs still print
  `Error: ...` lines: those are expected error-path assertions, not failures. Judge
  success by the exit code and the `Failed: 0` summaries.

### Running the app from source (non-obvious caveats)
- Run it directly as `bin/hydra <command>`; it auto-detects `lib/` relative to the
  binary, so no install is needed. `HYDRA_ROOT=/workspace` forces library discovery
  if you invoke it from elsewhere.
- `hydra spawn` creates a real tmux session and an identity-scoped git worktree.
  Use `hydra path <branch>` to locate it.
  To avoid creating worktrees/branches inside this repo, run spawn/kill demos inside a
  throwaway `git init` repo in a temp dir.
- For non-interactive automation set `HYDRA_NONINTERACTIVE=1` (skips confirm prompts)
  and `HYDRA_SKIP_AI=1` (does not try to launch an AI CLI on spawn). Runtime state
  lives in `$HYDRA_HOME/state/v2` (default `~/.hydra/state/v2`); see `docs/STATE.md`.
