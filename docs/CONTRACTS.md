# Hydra 1.5 public contracts

This document records which 1.5.0 behaviors remain public through 1.5.1.
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

## State map

Runtime file: `$HYDRA_HOME/map` (default `~/.hydra/map`).

Seven space-separated fields per line:

```text
branch session ai_tool group timestamp deps pr_number
```

Optional fields use `-` as a placeholder. Timestamp is a Unix epoch in
seconds. `hydra regenerate` preserves every field except the tmux session
name.

## Locks

Locks are empty directories `$HYDRA_HOME/locks/<name>.lock` created with
`mkdir`. There is no `pid` file in 1.5.1. Stale locks are directories whose
mtime is older than one minute (`find -mmin +1`). Doctor and cleanup use that
same format.

## Atomic replacement

Writers create a temp file in the destination directory (`mktemp_adjacent`)
and `mv` it into place so the rename stays on one filesystem.

## JSON

`list --json`, `status --json`, `recv --json`, `queue --json`, and
`group status --json` emit JSON objects/arrays.

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

## Not covered

State v2, instance IDs, events, native helpers, and non-root `PREFIX` install
are later releases. Pre-2.0 internal library layout may change.
