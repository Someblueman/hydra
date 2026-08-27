# Hydra v1.5.2 Release Notes

**Release date:** 2026-08-26

## Highlights

Hydra 1.5.2 is a five-minute, agent-optional first run. A clean macOS or Linux
user can install to a writable prefix without `sudo`, verify the install, create
a disposable head in a throwaway repository, and clean it up — with or without
an AI CLI.

## Install and discovery

- `PREFIX=$HOME/.local ./install.sh` (or `make install PREFIX=$HOME/.local`)
- Layout: `$PREFIX/bin/hydra` and `$PREFIX/lib/hydra`
- `install.sh` and `make install` share one contract, run `hydra version`, and
  print the exact binary and library paths
- Matching uninstall for the same `PREFIX`; `DESTDIR` staging is supported
- Run-from-source remains a supported evaluation path (`./bin/hydra`)
- Library discovery: `HYDRA_ROOT`, source `../lib`, PREFIX `../lib/hydra`,
  legacy `/usr/local/lib/hydra`

## First-run readiness

- `hydra doctor` reports install layout, git/tmux versions, writable
  `HYDRA_HOME`, repository/worktree readiness, and detected agent CLIs
- Every doctor and onboarding failure includes a `Next:` recovery action
- Spawn without an agent on `PATH` fails closed and points at `HYDRA_SKIP_AI=1`
- README Quick Start is a replayable throwaway-repo tour with user-level
  shell completion install

## Upgrade

```sh
git pull
PREFIX=$HOME/.local ./install.sh   # or: make install PREFIX=$HOME/.local
hydra version                      # should show 1.5.2
hydra doctor
```

No migration steps required for `~/.hydra` state files. Default `PREFIX` is
still `/usr/local` if you prefer `sudo ./install.sh`.

## Test Coverage

Run `make lint` and `make test`. Dedicated gates: `make test-install` and
`make smoke-onboarding`. Passing runs may still print `Error: ...` lines from
expected error-path assertions.
