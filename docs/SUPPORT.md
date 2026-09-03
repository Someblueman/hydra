# Platform and upgrade support

## Supported systems

Hydra 2.0 supports current macOS and Linux systems with:

- a POSIX `sh` (`dash` is used for compliance qualification);
- Git;
- tmux 3.0 or newer;
- GNU Make and ShellCheck for source qualification.

The shell CLI and basic TUI require no compiler. The optional native TUI requires a
C99 compiler when built from source; release artifacts are qualified on macOS and
Linux. Windows, WSL-specific behavior, BSD userlands without the documented tools,
and remote multi-host coordination are not supported contracts.

Optional `fzf`, `gh`, and agent executables add only their documented features.
Their absence must not prevent shell-only local orchestration.

## Installation support

Running from a source checkout and installing to a writable `PREFIX` are supported.
The default prefix is `/usr/local`; `DESTDIR` is supported for packaging. A source
install builds the native TUI when a qualified toolchain is present, while
`HYDRA_INSTALL_TUI=never` produces a shell-only installation.

Hydra supports upgrades from the immediately preceding stable release. An upgrade
that changes durable state must provide dry-run, backup, verification, interruption,
and rollback behavior. Skipping releases is supported only when each intervening
migration guide explicitly says so.

Hydra never edits Git worktree content during a state migration. Users remain
responsible for preserving local work before package replacement and for retaining
the generated state backup until verification completes.

See [MIGRATING_TO_2.0.md](MIGRATING_TO_2.0.md) for the 1.9-to-2.0 procedure.

## Qualification and fixes

Release qualification covers shell-only install, source/native install, lint,
state migration, lifecycle recovery, workflow/integration behavior, native/basic
parity, PTY behavior, sanitizer checks, and current macOS/Linux CI.

Security fixes are applied to the latest release. Older release lines receive fixes
only when maintainers explicitly announce a supported branch; there is no implied
long-term-support window.
