# Static workflows

For named artifacts and durable operator decisions, see [Workflow data and approval
waits](WORKFLOW_DATA.md).

Hydra workflow schema `1` describes a finite, locally executed DAG. Repository
definitions live at `.hydra/workflows/<id>.yml` (or `.yaml`).
They are covered by `hydra init --trust`; changing any file below `.hydra` makes
repository workflows untrusted until reviewed and trusted again. An explicit file
path may also be passed to `show`, `validate`, or `dry-run`.

```yaml
version: 1
id: review-change
description: Review a change in parallel
parallelism: 2
resources:
  disk_mb: 20480
  max_heads: 4
steps:
  - id: create
    kind: spawn
    needs: []
    retry: 0
    idempotent: false
    args:
      branch: review-change
      profile: default
  - id: tests
    kind: exec
    needs: [create]
    retry: 2
    idempotent: true
    args:
      head: review-change
      argv: [make, test]
```

IDs are lowercase, start with a letter, contain at most 64 letters, digits,
single `-` or `_` separators. `parallelism` is 1-16, `disk_mb` 1-1048576,
`max_heads` 1-64, and `retry` 0-10. A retry is only valid when `idempotent` is
true. Supported kinds and required arguments are: `spawn` (`branch`), `wait`
(`head`), `exec` (`head` and `command` or `argv`), `message` (`head`, `message`), `gate`
(`head`, `name`, and `command` or `argv`), `approve` (`head`, `name`, `by`), `approval-wait` (`head`, `name`), and
`kill` (`head`). Optional delegation arguments are `group`, `profile`, `reason`,
`completion_policy`, `timeout`, `force`, and `allow_shell`. `command` and `argv`
are mutually exclusive.

This is deliberately a restricted YAML subset: two-space indentation, mappings,
step sequences, scalar values, and inline scalar lists only. Tags, anchors,
aliases, merge keys, block scalars, flow mappings, multiline sequences, tabs,
and escape sequences are rejected. Unknown keys are errors.

Use `hydra workflow list`, `show`, `validate`, and `dry-run` to inspect a definition.
Normalized output and previews use a stable dependency-first topological order
(source order breaks ties). Inspection does not create heads, worktrees, or run
state. `hydra workflow run <id|path>` creates the durable run manifest first, then
executes the accepted `spawn`, `wait`, `exec`, message, gate, approval, and kill
commands. Repository definitions are never executed unless the current `.hydra`
tree matches the trust recorded by `hydra init --trust`.

Each run is stored beneath the current project's state directory with its resolved
definition, immutable bindings, `manifest.tsv`, `graph.tsv`, per-step attempts and
stdout/stderr, and an ordered `events.jsonl`. `hydra workflow status <run-id>` reads
that recorded state; `--json` emits schema version 1. The runner honors
`parallelism`, serializes spawn steps around Git's worktree mutation boundary,
refuses execution below `resources.disk_mb`, and never exceeds the declared retry
count. Every step must declare `idempotent`. Only idempotent failures may retry;
an interrupted non-idempotent step with no authoritative result becomes
`recovery-required` rather than being replayed.

Optional step fields `retry_on: [failure, timeout, interrupted, configuration]`
and `retry_backoff: 2` select retry classes and an initial delay in seconds
(0-86400). Delays double for each subsequent failed attempt, capped at 86400
seconds. An empty `retry_on: []` disables retries. Omitting these fields retains
immediate retries for all failure classes, subject to the retry count and
idempotency declaration. Exit 124 is `timeout`, 126/127 are `configuration`,
statuses above 128 and missing completion evidence are `interrupted`, and other
nonzero exits are `failure`. Exec steps request the underlying command status
using `hydra exec --exit-code`; this option requires exactly one selected head.
An interrupted attempt that cannot retry is `recovery-required`, since its side
effects remain unresolved. Non-idempotent attempts are never automatically retried.

Each attempt records `failure-class` and, when retryable, an absolute `retry-at`
deadline. Backoff does not occupy a worker slot. Restart preserves that deadline,
completed attempts, and the remaining retry budget; cancellation cancels pending
retries. Resume also verifies the runtime graph against the resolved definition.

`hydra workflow cancel <run-id>` records intent before signalling running command
trees and reports any residual child. `hydra workflow resume <run-id>` accepts a
stale or recovery-required run only when its project, base commit, and resolved
definition still match. Completed attempts are authoritative and are not repeated.

## Verified integration and merge trains

`hydra integrate <run-or-group> --base <ref> --target <branch> --dry-run` reports
immutable candidate commits, recorded and computed merge bases, claims, path
overlaps, predicted conflicts, gates, and the planned disposable worktree without
changing a ref. Replace `--dry-run` with `--execute` and repeat `--gate <command>`
to assemble and verify in an isolated worktree. The target remains unchanged until
both of these explicit steps succeed:

```sh
hydra integrate approve run_ID --by reviewer
hydra integrate promote run_ID
```

`hydra integrate train ...` uses the same report, worktree, locking, approval, and
promotion path but runs the configured gates after each ordered candidate. A train
stops at the first stale binding, observed conflict, gate failure, cancellation, or
disk refusal. `hydra integrate status run_ID` names the failure class, exact
candidate or gate, expected and observed target commits, preserved worktree/report,
and recovery command. Interrupted, cancelled, gate-failed, and resource-refused
runs can use `hydra integrate resume run_ID`; completed merges are not replayed.
Other failures require `hydra integrate cleanup run_ID --apply` and a new preview.
Promotion revalidates the manifest, every candidate, the verified worktree, approval,
and target ref before a local fast-forward. Hydra never pushes as part of integration.

Headless agent steps use `args.profile`, `prompt_file` or `prompt_input`, optional
`requires`, and an optional declared `result_file`. They run through the same
supervised exec path. See [Agent contract v1](AGENT_CONTRACT.md) for the finite
profile schema, exact-session resume, capability evidence, output bounds, and
retention, and [Workflow data](WORKFLOW_DATA.md) for sealed handoff and remote
approval waits. Provider completion does not satisfy a verification gate.
