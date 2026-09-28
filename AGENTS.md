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
- One test file: `make test-one T=<name>` runs `tests/test_<name>.sh` (also accepts
  `T=test_<name>` or `T=tests/test_<name>.sh`) exactly as `make test` does and prints
  its log. Pass the same `BUILD_DIR` as your other make runs. Do not run
  `sh tests/test_*.sh` directly: cases need `BUILD_DIR` plus the binaries the runner
  derives from it (`HYDRA_FLEET_BIN`, `HYDRA_TUI_BIN`, `HYDRA_CORE`,
  `HYDRA_SHELL_EXEC`) and stop with a "... is required" message without them, rather
  than falling back to a possibly stale `./build`. The equivalent manual form is
  `BUILD_DIR="$PWD/build" sh scripts/run-shell-test.sh build/test-logs <name> tests/test_<name>.sh`
  after building the `make test` prerequisites.
- `scripts/run-shell-test.sh` gives every case a private `TMPDIR` and `TMUX_TMPDIR`
  and removes them afterwards (failed cases keep only their log under
  `$BUILD_DIR/test-logs/`), and unsets `TMUX`/`TMUX_PANE`, so running from inside
  tmux neither skips checks nor touches your server.

### Running the app from source (non-obvious caveats)
- Run it directly as `bin/hydra <command>`; it auto-detects `lib/` relative to the
  binary, so no install is needed. `HYDRA_ROOT=/workspace` forces library discovery
  if you invoke it from elsewhere.
- `hydra spawn` creates a real tmux session and an identity-scoped git worktree.
  Use `hydra path <branch>` to locate it.
  To avoid creating worktrees/branches inside this repo, run spawn/kill demos inside a
  throwaway `git init` repo in a temp dir. A private `HYDRA_HOME` does not isolate
  worktrees: the worktree root is recorded in the repository's git common dir (shared
  by every worktree of this checkout), so a spawn from here lands beside the real repo
  and is orphaned when that `HYDRA_HOME` is deleted.
- For non-interactive automation set `HYDRA_NONINTERACTIVE=1` (skips confirm prompts)
  and `HYDRA_SKIP_AI=1` (does not try to launch an AI CLI on spawn). Runtime state
  lives in `$HYDRA_HOME/state/v2` (default `~/.hydra/state/v2`); see `docs/CONTRACTS.md`.
