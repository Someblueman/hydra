# Hydra v1.6.0 Release Notes

**Release date:** 2026-08-29

## Highlights

Hydra 1.6.0 gives each project, head, and agent instance durable identity and
evidence. Tasks can now be injected at spawn, lifecycle state can be inspected and
waited on without inferring success from a missing tmux session, and a head can be
resumed while retaining its prior instance history.

The implementation remains a single POSIX shell CLI. No compiler or background
daemon is required.

## Project-safe state and lifecycle

- State v2 namespaces heads by project and uses opaque head and instance IDs, so
  identical branch names in separate repositories do not collide.
- Versioned JSONL events retain inspectable lifecycle and coordination history.
- Declared outcome, observed status, and liveness are independent channels; none is
  presented as a passed gate or human approval.
- Dependencies name the durable evidence they require instead of treating a missing
  tmux session as success.
- `hydra wait` watches durable conditions with timeouts and stale-instance detection.
- `hydra resume` and regenerate create a new instance while retaining prior history
  and rejecting stale observations, messages, and receipts.

## Task-aware agents and coordination

- `hydra init` records project identity, profile defaults, local worktree policy, and
  an explicit trust decision for repository configuration.
- Built-in and custom agent profiles declare launch, prompt, resume, capability, and
  adapter behavior. `hydra agent list`, `show`, and `doctor` expose the result.
- `spawn --prompt`, `--prompt-file`, issue-body input, `--dry-run`, and `--no-agent`
  support inspectable task delivery without pane typing or a required AI CLI.
- Provider-neutral adapter input is bounded, schema-validated, and correlated to the
  current instance. Missing, malformed, version-skewed, and stale input cannot invent
  lifecycle progress.
- Typed inbox and safe-point messages have instance-scoped delivery receipts.
- Rate-limited local lifecycle notifications work without the TUI or a daemon.

## Operations and evidence

- `hydra exec` runs argv-safe commands across selected worktrees with bounded
  parallelism, per-command timeouts, private captured results, and explicit trust for
  shell-string mode.
- `hydra diff`, `hydra review`, and `hydra list --git` inspect changes relative to
  each head's recorded base commit.
- `hydra provenance` records resolved profiles, task hashes, tool versions, trusted
  configuration hashes, launch or resume mode, and lifecycle sources.
- Automation-relevant `--json` commands use a versioned success/error envelope.
- Teardown stores no transcript by default. Bounded redacted retention and raw
  retention are explicit policies, and trusted teardown hooks receive only documented
  non-secret identity variables.

## Upgrade from 1.5.x

Install the new version using the same prefix as the existing installation:

```sh
git pull
PREFIX=$HOME/.local ./install.sh   # or: make install PREFIX=$HOME/.local
hydra version                      # should show 1.6.0
hydra doctor
```

If `$HYDRA_HOME/map` contains existing heads, migrate it from inside the repository
that owns those mappings before initializing or creating new 1.6 heads:

```sh
hydra state verify
hydra state migrate --dry-run
hydra state migrate
```

Migration validates the complete legacy map and creates a backup before activating
state v2. Keep the printed backup path; if rollback is required, run:

```sh
hydra state rollback "$HOME/.hydra/backups/state-<timestamp>-<pid>"
```

An empty legacy map requires no migration. State v2 keeps a project-scoped
seven-field compatibility projection for remaining legacy display and session
helpers.

## Validation and documentation

Run the release gates from the repository root:

```sh
git diff --check
make lint
make test
```

Security, automation, operations, provenance, state, and lifecycle contracts are
documented under [`docs/`](docs/).

## Deferred

Native C helpers, a native TUI, general workflow automation, automated integration,
and fleet operations remain planned for later releases.
