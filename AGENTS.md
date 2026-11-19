# AGENTS.md

## Working in this repository

Hydra has a POSIX shell CLI (`bin/hydra`) that orchestrates one tmux
session + one git worktree per branch ("head"). Library code lives in `lib/*.sh`;
tests are POSIX shell scripts in `tests/*.sh` plus C tests in `tests/c/`.
Optional native helpers are built with `make build-core build-tui build-fleet`;
fleet additionally needs JSON-C development files and pkg-config at build time.
`hydra tui` has no shell fallback: without the `build-tui` helper it prints how to
get one and exits 1 (`hydra list` and `hydra switch` work without it).

### Toolchain
- Required to run/lint/test: `git`, `tmux` (>= 3.0), `dash`, `shellcheck`, GNU `make`,
  a C99 compiler (`cc`), JSON-C development files (`libjson-c-dev`) and `pkg-config`:
  `make test` and `make test-fast` build all three native helpers first.
  Tests run with `sh`; where `/bin/sh` is `dash` (Debian, Ubuntu) that is exactly
  what the POSIX-compliance checks assume.
- Optional (improve UX, not required for tests): `fzf` (interactive `switch`),
  `gh` (GitHub issue/PR features), and an AI CLI such as `claude`/`aider`/`gemini`.
  `python3` is needed only by the bench-i1, perf-matrix, plan-eval, usability,
  acceptance and visual-review targets.

### Lint / Test / Run (standard commands, see `Makefile` and `README.md`)
- First check for a pull request: `make test-fast` — lint, a fixed selection of shell
  cases and the native unit tests (`CONTRIBUTING.md`).
- Lint: `make lint` — runs ShellCheck (`--shell=sh --severity=style`) plus `dash -n`
  syntax checks on every shell file.
- Test: `make test` — runs with `sh` every `tests/test_*.sh` that is not listed in
  `SHELL_TEST_EXCLUDES` in `scripts/shell-tests.mk`; other targets such as
  `test-fleet`, `test-parity` and `test-tui` run the excluded files. Passing runs still print
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
  tmux neither skips checks nor touches your server. A case that leaves processes
  running whose arguments name its private `TMPDIR` fails with a `LEAK` report
  (the processes are then stopped); stop what a case starts on every exit path,
  for example with `test_stop_processes_naming` from `tests/helpers.sh`.
- Native warnings: `make -j4 check-opt-levels` compiles every `src/**/*.c` at `-O0`,
  `-Og`, `-O1`, `-O2`, `-O3` and `-Os` with warnings as errors and lists every
  failing site; the ordinary build only shows what `-O2` warns about.
- Sanitizers: `make sanitize` (or `make sanitize-<lane>`) builds and tests in
  `$BUILD_DIR/sanitize`, one tree shared by every lane, so pass the same `BUILD_DIR`
  as your other make runs. `make clean` removes `$BUILD_DIR`.

### Running the app from source (non-obvious caveats)
- Run it directly as `bin/hydra <command>`; it auto-detects `lib/` relative to the
  binary, so no install is needed. `HYDRA_ROOT=<path to this checkout>` forces library
  discovery if you invoke it from elsewhere.
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
