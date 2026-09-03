# Hydra state v2

State v2 is Hydra 2.0's only runtime state authority. It lives below
`$HYDRA_HOME/state/v2`, uses mode-0700 directories and mode-0600 records, and has a
`schema-version` file containing `2`.

## Identity and layout

Projects live at `projects/<project_id>` and heads at
`projects/<project_id>/heads/<head_id>`. Project, head, instance, run, event, claim,
and pack identifiers are validated opaque IDs. Human branch names and other labels
are scalar record values rather than filesystem keys.

Each head records its branch, tmux session, profile, group, creation time,
dependencies, PR, desired state, completion policy, worktree, task, scopes, base
reference, current instance, and retained instance history. Events, messages,
receipts, transcripts, execution results, gates, and provenance extend that head.
Workflow runs and integration reports are project-scoped siblings of `heads`.

Active readers select heads whose `desired-state` is `running` or `stopping`.
Teardown records `stopped` but retains history for inspection and recovery. Resume
keeps the head ID, creates a new instance ID, and records the predecessor link.

## Mutation and verification

Scalar writes reject embedded newlines, create a mode-0600 adjacent temporary file,
and rename it into place. Mutations acquire the relevant project or record directory
lock and fail closed on contention. There is no cache or flat-file projection in the
runtime read/write path.

```sh
hydra state verify
hydra state backup
hydra state migrate --dry-run
hydra state migrate
hydra state rollback "$HOME/.hydra/backups/state-..."
```

Verification is read-only and validates the state root, IDs, required scalar files,
instances, and event streams. Backup copies the full state tree and, when present,
the inactive global 1.x map into a private generated directory.

## Upgrade from 1.9

Hydra 1.9 already wrote state v2 but also maintained project `compat-map`
projections, and some installations may retain `$HYDRA_HOME/map`. In 2.0 these are
migration inputs only.

`hydra state migrate --dry-run` verifies state v2 and checks that every 1.9
projection row exactly matches its durable head. It reports planned removals without
writing. `hydra state migrate` then creates a backup, removes verified project
projections and the inactive global map, and verifies state v2 again. A malformed,
projection-only, or divergent row aborts before deletion.

Rollback accepts only a generated backup under the current `HYDRA_HOME`, requires
all state and event locks to be idle, stages the restore, and swaps the state tree.
It restores 1.9 map files for an intentional downgrade; Hydra 2.0 itself ignores
them. See [MIGRATING_TO_2.0.md](MIGRATING_TO_2.0.md) for the operator sequence.

## Durable workflow records

A workflow run is published only after its resolved definition, graph, manifest,
bindings, initial step states, and empty event stream exist. Per-attempt directories
retain stdout, stderr, exit status, and authoritative-attempt selection.

Integration reports retain their immutable candidate manifest, initial target,
gates, merge output, verification evidence, approval, and recovery action. Cleanup
removes only a recorded disposable worktree and preserves the report.
