# Hydra 2.0 public contracts

Hydra 2.0 commits to the local interfaces below. Internal shell function names,
module layout, renderer details, caches, and on-disk temporary files are not public
contracts.

## Compatibility and negotiation

- `hydra version` and `--version` print `Hydra version <semver>`.
- CLI syntax documented by `hydra help` is public. An incompatible removal requires
  a major release and the deprecation policy in [VERSIONING.md](VERSIONING.md).
- Machine interfaces reject unsupported schema or protocol versions. They do not
  guess, silently downgrade, or accept a different format as a fallback.
- Readers ignore unknown JSON object fields within a supported schema version.
- The shell CLI is the only mutation authority. Native processes receive bounded,
  versioned input and invoke public shell commands with an argument vector.

## JSON envelope v1

Every documented `--json` command emits exactly one JSON object and exits nonzero on
failure. Success has:

```json
{"schema_version":1,"ok":true,"command":"list","data":{}}
```

Failure has:

```json
{"schema_version":1,"ok":false,"command":"list","error":{"code":"invalid_input","message":"...","recovery":"..."}}
```

`schema_version`, `ok`, `command`, and the matching `data` or `error` member are
required. Error `code`, `message`, and `recovery` are required strings. Envelope v1 is
used by `init`, `capabilities`, `list`, `status`, `group status`, `recv`, `receipts`,
`queue`, `lifecycle`, `wait`, `exec`, `diff`, `review`, `provenance`, `workflow`,
`collision`, `resource`, `gate`, `du`, and `snapshot` where their help documents
`--json`.

`json_escape` accepts POSIX C strings (there is no NUL). It escapes JSON syntax and
C0 controls and preserves other UTF-8 bytes.

## Durable state v2

`$HYDRA_HOME/state/v2` with `schema-version` equal to `2` is the only runtime state
authority. Projects, heads, instances, workflow runs, integration reports, messages,
claims, resources, gates, and provenance use validated opaque IDs as path keys.
Human labels are scalar values, never path identity. See [STATE.md](STATE.md).

The seven-field global map and project `compat-map` are not 2.0 runtime formats.
`hydra state migrate` may read verified 1.9 projections solely to retire them after a
backup. Rollback restores those files only so a user can downgrade to 1.9.

Writers use project- or record-scoped directory locks and adjacent-file rename.
Failure to acquire a lock is a failed mutation; there is no unlocked write path.
Head history remains after teardown with `desired-state=stopped`.

## Events, lifecycle, and completion

- Events are append-only JSON Lines schema v1 with ordered sequence numbers,
  correlated project/head/instance IDs, and repair/retention under the event lock.
- Declared outcome, observed provider status, and tmux liveness are independent.
- Completion policies are `declared-done`, `observed-exit-zero`, and `either`.
  Session disappearance alone is not task completion.
- Resume retains the head ID, creates a new instance, and marks the old instance as
  superseded. Stale-instance adapter input is rejected.
- Teardown defaults to no transcript. `redacted` and `full` are explicit policies;
  retained transcripts are bounded and instance-scoped.

See [EVENTS.md](EVENTS.md), [LIFECYCLE.md](LIFECYCLE.md), and
[MESSAGING.md](MESSAGING.md).

## Profiles, tasks, adapters, and scopes

- Built-in and custom profile fields, confidence labels, and resolution order are
  defined in [PROFILES.md](PROFILES.md).
- Task text is resolved before launch, stored privately, and delivered as one quoted
  argument. Events contain only its hash and byte count.
- Adapter input is bounded canonical JSON schema v1 and must name the current
  instance. Capability confidence never upgrades an observation into authority.
- Read/write scopes and expiring claims are advisory coordination constraints. Gate
  approval is a separate exact binding to verification evidence.

## Workflows and integration

Workflow definition schema v1 is the stable finite-DAG contract. Definitions use the
documented restricted YAML subset, every step declares idempotency, argv is the
default execution form, and shell strings require both `allow_shell: true` and a
current repository trust decision. Durable manifests bind resolved definitions,
inputs, attempts, outputs, events, cancellation, and recovery.

Integration manifests bind the target, initial target ref, ordered immutable
candidates, gates, merge output, verification result, approval, and recovery action.
Promotion revalidates those bindings under a project lock, updates only a local ref,
and never pushes. See [workflows.md](workflows.md).

## TUI protocols and parity

The basic TUI row is seven tab-separated fields:

```text
branch  session  profile  status  activity  group  pr
```

The internal native adapter begins with `HYDRA_TUI<TAB>2`, followed by bounded `H`
head and `R` recovery rows whose fields contain no tabs or newlines. Unsupported
protocol versions fail closed. This adapter is not a general automation API.

Plain `hydra tui` is native-first with a visible `hydra tui --basic` fallback. Both
retain navigation, search, refresh, preview, switch, spawn, group assignment,
dashboard, regenerate, confirmed kill, and help behavior. Native mutations execute
the public shell CLI with explicit argv and never write Hydra state directly. See
[NATIVE_TUI.md](NATIVE_TUI.md).

## Process, install, and platform contracts

Startup and agent launch target `session:0.0`. Broadcast automatically selects only
a recognized shell pane; `--force` or `--pane` is required to target anything else.

Source and prefix installs provide `bin/hydra`, `lib/hydra/*.sh`, and an optional
qualified `hydra-tui`. Core shell operation requires POSIX `sh`, Git, and tmux 3.0 or
newer on supported macOS and Linux systems. See [SUPPORT.md](SUPPORT.md).

## Fleet pilot (unreleased)

[Fleet protocol 1](FLEET.md) is an optional C/OpenSSH coordinator. Its one-request
stdin JSON boundary negotiates capabilities before mutations, transports argv
without shell interpolation, and delegates head/workflow mutations to the local
shell CLI. Fleet's alias, package, and inert bundle stores are distinct from live
state v2. Lost mutation responses never cause automatic replay.
