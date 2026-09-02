# Hydra state v2

State v2 lives under `$HYDRA_HOME/state/v2` with mode 0700 directories and 0600
records. `schema-version` contains `2`. Projects are stored below
`projects/<project_id>` and heads below `heads/<head_id>`; both path components are
validated opaque identifiers.

Each head has scalar records for branch, session, profile, group, creation time,
dependencies, PR, desired state, completion policy, and current instance. Instance
directories retain launch history. Task, lifecycle, resume, transcript, and
provenance records extend the same head rather than creating another authority.

State v2 is authoritative in 1.6. Each project also has a seven-field `compat-map`
projection used by legacy display and session helpers that have not yet moved to
scalar reads. The projection is project-scoped, lock-protected, and not shareable
state. New durable task, identity, path, instance, event, exec, and provenance data
live only in v2 records. `$HYDRA_HOME/map` remains migration input when schema 2 has
not been activated; it is not the cross-project authority after activation.

Commands:

```sh
hydra state verify
hydra state backup
hydra state migrate --dry-run
hydra state migrate
hydra state rollback "$HOME/.hydra/backups/state-..."
```

Verification is read-only. Migration accepts the legacy seven-field map only when
every record is well formed, creates a backup first, then writes complete per-head
records using same-filesystem replacement. Rollback accepts only a generated backup
under the current `HYDRA_HOME`.

Head directories remain after teardown so instance, event, transcript, receipt, and
provenance history can be inspected. The project compatibility map lists only active
heads. Regenerate and resume retain the head ID, create a new instance ID, and mark
the prior instance as superseded.

Workflow runs live below `projects/<project_id>/workflows/runs/<run_id>`. A run is
published atomically only after its resolved definition, graph, manifest, bindings,
initial step states, and empty event stream exist. Per-step attempt directories retain
stdout, stderr, exit status, and the authoritative attempt. Integration reports live
below `projects/<project_id>/integrations/<run_id>` and retain the immutable candidate
manifest, initial target, gates, merge output, verification evidence, approval, and
recovery action. Cleanup removes only the recorded disposable worktree; it preserves
the report.
