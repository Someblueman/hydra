# ADR 0001: Shell lifecycle foundation

Status: Accepted for Hydra 1.6.0

## Decision

Hydra uses generated, opaque `project_*`, `head_*`, and `instance_*` identifiers.
The project identifier is stored in the repository's Git common directory at
`hydra/project-id`, so it is shared by linked worktrees, survives a repository move,
and is not copied by a normal clone. Heads keep their identifier across regeneration;
every spawn, regenerate, or resume creates a new instance identifier.

State v2 is a directory tree rooted at `$HYDRA_HOME/state/v2`. Projects and heads
are directories keyed only by validated identifiers. Each human-readable scalar is
stored in its own newline-terminated file, and variable-length history is JSONL.
Writers use an adjacent temporary file followed by rename. This is simpler and less
ambiguous in POSIX shell than an escaped multi-field record and gives tasks,
instances, provenance, and history durable homes without inventing a parser.

The host-local runtime state is separate from repository configuration. Project
configuration may describe profiles and bootstrap policy, but it is inert until the
project is trusted. Default worktrees are namespaced by project and head identity;
the stored worktree path is authoritative after creation.

Events use the Events v1 JSONL envelope. They are serialized under the same `mkdir`
lock protocol used by all shell writers. A lock directory contains owner PID, host,
creation time, and operation. Local stale cleanup requires matching-host evidence
and a dead PID; age alone never authorizes removal. Atomic rename and append streams
use separate lock objects.

## Consequences

- Two repositories may use the same branch label without sharing state or paths.
- Branch text and other user input never become state directory names.
- Migration from the seven-field map creates a backup before selecting schema 2 and
  can be rolled back with that generated backup.
- A prior instance's events remain historical evidence but cannot satisfy current
  instance state.
- No C writer is introduced in 1.6.0; any future native writer must implement this
  exact directory-lock protocol and pass the shared fixtures.

## Alternatives considered

A single versioned record with percent-escaped fields was prototyped against the
same fixtures. It remained atomic as one renamed file, but every scalar read required
decoding, duplicate keys needed another rule, and task/history growth forced either
multi-line escaping or a second format. The per-head directory preserves fixtures
containing spaces and punctuation as literal scalar values, rejects embedded
newlines, and makes a complete head visible with one directory rename. The directory
layout therefore has the smaller shell parser and clearer interrupted-write boundary.
