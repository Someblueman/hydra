# Hydra public contracts

This document records which 1.5 behaviors remain public and the accepted 1.6
foundation contracts.
Internal function names, the in-process state cache, and TUI render details
are not contracts.

## CLI

- Commands dispatched by `bin/hydra` (including `group`, `send`, `recv`, `tail`,
  `broadcast`, `wait-idle`, `queue`, and `dashboard-exit`) are part of the
  user-facing command set. Shell completion covers that set.
- `hydra version` / `--version` print `Hydra version <semver>`.
- Mutating commands fail closed on lock acquisition failure: they print an
  error and do not write. They do not fall back to an unlocked write.
- `hydra kill` checks worktree dirtiness **before** killing tmux or removing
  the mapping. `--force` on `kill --all` only skips the confirmation prompt.
- `hydra workflow` accepts strict schema version 1 and exposes list, show, validate,
  dry-run, run, status, cancel, and resume. Every step declares idempotency.
- `hydra integrate` creates an isolated report/worktree and requires separate current
  approval before local promotion. `integrate train` runs gates after each immutable
  ordered candidate. Neither mode pushes.

## State

State v2 under `$HYDRA_HOME/state/v2/projects/<project_id>` is authoritative for
1.6 identity, heads, instances, tasks, lifecycle, events, messages, exec results,
and provenance. Opaque validated IDs, not user labels, are filesystem keys.

The legacy migration input is `$HYDRA_HOME/map` (default `~/.hydra/map`). Each
active v2 project also maintains a project-scoped `compat-map` projection for
remaining legacy readers. Both use seven space-separated fields per line:

Seven space-separated fields per line:

```text
branch session ai_tool group timestamp deps pr_number
```

Optional fields use `-` as a placeholder. Timestamp is a Unix epoch in
seconds. `hydra regenerate` preserves every field except the tmux session
name.

## Locks

Locks are directories `$HYDRA_HOME/locks/<name>.lock` created atomically with
`mkdir`. Each new lock records owner PID, host, creation time, and operation in
mode-0600 scalar files. A partially written owner record is still a held lock.
Cleanup removes a lock automatically only when its host matches this host and
its recorded PID is no longer live; age alone is not evidence of staleness.
Metadata-free legacy locks remain held until explicitly recovered.

## Atomic replacement

Writers create a temp file in the destination directory (`mktemp_adjacent`)
and `mv` it into place so the rename stays on one filesystem.

## JSON

Every command that accepts `--json` emits JSON envelope v1 with
`schema_version`, `ok`, `command`, and either `data` or `error`. This includes
`list`, `status`, `recv`/`receipts`, `queue`, `group status`, `init`,
`capabilities`, `lifecycle`, `wait`, `exec`, `diff`, `review`, and `provenance`.
JSON failures return nonzero. Unknown fields must be ignored.

`json_escape` operates on POSIX C strings (NUL cannot appear). It escapes
`"`, `\`, `\b`, `\t`, `\n`, `\f`, `\r`, and other C0 controls as `\u00XX`.
Bytes 0x20–0xFF other than `"` and `\` (including UTF-8 payload bytes) are
copied unchanged; a UTF-8 locale must not recode them. Callers must never
emit raw control characters inside JSON strings.

## TUI row schema

`tui_build_list` writes one row per head, eight TAB-separated fields:

```text
branch  session  ai  status  tag  activity  group  pr
```

`status` is `ALIVE` or `DEAD`. `activity` is `BUSY`, `IDLE`, or `-`.

Hydra 1.8 keeps that basic shell TUI and adds an internal escaped-tabular protocol
between `hydra tui --data` and `hydra-tui`. It begins with `HYDRA_TUI<TAB>2` and
contains bounded `H` head and `R` recovery records. Native dispatch is opt-in in
1.8.0. The native process delegates mutations to the shell executable with an argv;
the tabular adapter is not a general automation API.

## tmux send-keys

Startup commands and agent launch target `session:0.0`, not the active pane.

`hydra broadcast` sends to a non-agent shell pane when one exists. A live
agent process on `:0.0` is skipped even if the map stores `-`; after that
process exits and `:0.0` is a shell again, broadcast may use it. Only panes
whose command is a recognized shell are chosen automatically. If the only
pane is the agent (typical `default` layout), or the other panes are not
shells, broadcast refuses unless `--force` (then `:0.0`) or `--pane` is
given. A session-qualified `--pane` (`session:0.1`) applies only to that
session.

## Environment

Documented in the README: `HYDRA_HOME`, `HYDRA_ROOT`, `HYDRA_AI_COMMAND`,
`HYDRA_SKIP_AI`, `HYDRA_NONINTERACTIVE`, and the other `HYDRA_*` variables
listed there.

## Installation

Default prefix is `/usr/local`. Layout:

```text
$PREFIX/bin/hydra
$PREFIX/lib/hydra/*.sh
```

`DESTDIR`, when set, is prepended to those paths (staged `make install`).
Root is not required when `PREFIX` is writable. `install.sh` and `make install`
produce the same layout and verification output: they run the installed
`hydra version` and print the exact binary and library paths.

Library discovery order:

1. `$HYDRA_ROOT/lib` when `HYDRA_ROOT` is set and contains `git.sh`
2. `$HYDRA_BIN_DIR/../lib` (run from a source checkout)
3. `$HYDRA_BIN_DIR/../lib/hydra` (`PREFIX` install)
4. `/usr/local/lib/hydra` (legacy fallback)

Uninstall uses the same `PREFIX`/`DESTDIR` and removes only those installed
files. `--purge` may remove `HYDRA_HOME` / `~/.hydra`; it never touches the
source repository.

## 1.6 foundation

The accepted identity, state v2, Events v1, and lifecycle contracts are in
[`adr/0001-identity-state-events-locks.md`](adr/0001-identity-state-events-locks.md),
[`STATE.md`](STATE.md), [`EVENTS.md`](EVENTS.md), and
[`LIFECYCLE.md`](LIFECYCLE.md). State v2 is the active authority; the seven-field
project map is a compatibility projection only. Security, automation, operations,
and provenance boundaries are documented in [`SECURITY.md`](SECURITY.md),
[`AUTOMATION.md`](AUTOMATION.md), [`OPERATIONS.md`](OPERATIONS.md), and
[`PROVENANCE.md`](PROVENANCE.md). Native helpers remain deferred. Pre-2.0 internal
library layout may change.
