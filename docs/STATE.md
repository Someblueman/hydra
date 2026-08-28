# Hydra state v2

State v2 lives under `$HYDRA_HOME/state/v2` with mode 0700 directories and 0600
records. `schema-version` contains `2`. Projects are stored below
`projects/<project_id>` and heads below `heads/<head_id>`; both path components are
validated opaque identifiers.

Each head has scalar records for branch, session, profile, group, creation time,
dependencies, PR, desired state, completion policy, and current instance. Instance
directories retain launch history. Task, lifecycle, resume, transcript, and
provenance records extend the same head rather than creating another authority.

During the shell-command migration, each project also has a seven-field `compat-map`
projection used by unchanged 1.5 readers. It is project-scoped and is not shareable
state. New durable task, identity, path, instance, and event data live only in the v2
head records.

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
