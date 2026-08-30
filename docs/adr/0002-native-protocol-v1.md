# ADR 0002: Read-only native command protocol v1

Status: Accepted for Hydra 1.7.0

## Decision

`hydra-core` is an optional read-only helper. Shell remains the only authority for
state mutation, lifecycle policy, Git integration, configuration execution, hooks,
and teardown.

Protocol v1 uses an argv command and paths as inputs. The helper writes exactly one
JSON document followed by one newline to stdout, writes diagnostics only to stderr,
and returns zero only when the requested read completed and validated. It never
reads commands or JSON requests from stdin.

The accepted commands are:

- `--protocol-version`: print `1` and a newline;
- `--version`: print the core and protocol version;
- `capabilities`: print the helper's read-only capability document;
- `validate-state <state-v2-root>`: validate the supported state v2 records;
- `validate-events <events.jsonl>`: validate event schema and sequence;
- `json-string <text>`: encode one canonical JSON string;
- `snapshot <state-v2-root>`: emit the public snapshot JSON envelope.

All output strings use the same byte-preserving JSON escaping contract as the shell
implementation. Snapshot records are sorted by project ID and head ID so shell and
native bytes are directly comparable.

## Dispatch and failure boundary

Native dispatch is explicit through `hydra snapshot --native`. Hydra checks protocol
version before dispatch and bounds helper execution. Absence, protocol skew, timeout,
crash, nonzero exit, malformed JSON, and unreadable execution all produce a named
diagnostic and execute the shell snapshot path. Native output never becomes input to
a shell mutation.

The helper accepts state schema 2 and event schema 1 only. Unknown state files and
directories are ignored; malformed required records fail closed. Protocol or schema
changes require a new accepted protocol version and exact parity fixtures before
shell integration.

## Distribution

The native artifact is optional. Source installs build it only when requested;
prebuilt installation verifies an adjacent SHA-256 checksum, architecture metadata,
and protocol version before atomic replacement. Removing or rolling back the helper
cannot make shell-only Hydra unusable.

## Consequences

The first native slice is intentionally narrow: capability reporting, state and
event validation, canonical JSON string encoding, and read-only snapshot aggregation.
There is no native writer, YAML parser, workflow policy, public ABI, or TUI in 1.7.0.
