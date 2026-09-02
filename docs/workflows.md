# Static workflows

Hydra workflow schema `1` describes a finite DAG and is inspection-only in this
release. Repository definitions live at `.hydra/workflows/<id>.yml` (or `.yaml`).
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
(`head`), `exec` (`command` or `argv`), `message` (`head`, `message`), `gate`
(`head`, `name`, and `command` or `argv`), `approve` (`head`, `name`, `by`), and
`kill` (`head`). Optional delegation arguments are `group`, `profile`, `reason`,
`completion_policy`, `timeout`, `force`, and `allow_shell`. `command` and `argv`
are mutually exclusive.

This is deliberately a restricted YAML subset: two-space indentation, mappings,
step sequences, scalar values, and inline scalar lists only. Tags, anchors,
aliases, merge keys, block scalars, flow mappings, multiline sequences, tabs,
and escape sequences are rejected. Unknown keys are errors.

Use `hydra workflow list`, `show`, `validate`, and `dry-run`. Normalized output
and previews use a stable dependency-first topological order (source order breaks
ties). These commands do not execute or persist workflow runs.
