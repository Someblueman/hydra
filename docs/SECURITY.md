# Security and trust model

Hydra runs local commands with the invoking user's permissions. It separates trust
decisions and protects coordination records, but it is not an operating-system
sandbox. A trusted hook, setup command, agent executable, or explicitly authorized
shell string can do anything that user can do.

## Trust boundaries

- Repository `.hydra` configuration and hooks are untrusted until `hydra init
  --trust` records their exact content hash for this host. Any relevant content
  change invalidates that decision. `.hydra/local.yml` is host-local, ignored by
  Git, mode 0600, and excluded from the repository trust hash.
- Built-in profiles resolve one known executable name. Custom profiles require an
  existing absolute executable path and are recorded as user-declared. Hydra does
  not infer provider hooks from executable presence.
- Task text is stored in mode-0600 head state and delivered as one quoted argument.
  Events and provenance contain its hash and byte count, never its content.
- Adapter input is capped at 8 KiB, must be canonical schema v1, and must correlate
  to the current instance. Malformed, version-skewed, and stale input is rejected
  without changing lifecycle state.

## Command execution

`hydra exec` uses an argument vector by default. `--shell` is a separate mode and
requires both `--allow-shell` and a currently trusted project configuration. Output
is capped, stored privately, and recorded as an `exec.completed` event; it is never
treated as an agent message, lifecycle outcome, gate result, or approval. See
[OPERATIONS.md](OPERATIONS.md).

## Secrets and retained data

The default teardown policy stores no pane transcript. `redacted` is bounded and
removes common assignment and API-token forms, but redaction is best effort and is
not a substitute for avoiding secrets in terminal output. `full` is an explicit raw
content opt-in. Transcripts, messages, receipts, exec captures, tasks, state, and
lock metadata are mode 0600 under mode-0700 directories. Notifications contain only
the branch and lifecycle event name; message bodies are not copied into events or
notifications.

Hooks receive only documented non-secret `HYDRA_*` identity and path variables.
Setup and hook output streams to the caller rather than being silently persisted.
Users should still treat worktree paths, branch names, diffs, and agent output as
sensitive project metadata.

## Recovery and integrity

Mutating writers use adjacent temporary files plus rename and the shared directory
lock protocol. Stale-lock cleanup requires a matching host and a dead owner PID;
age alone is insufficient. State migration creates a generated backup before schema
activation, and rollback accepts only a backup under the current `HYDRA_HOME`.

Hydra protects against accidental cross-project state collisions, stale instance
evidence, shell interpolation, and unlocked concurrent writes. It does not defend
against a malicious process running as the same user, a compromised executable, or
host filesystem administrators.
