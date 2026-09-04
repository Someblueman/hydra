# Migrating from Hydra 1.9 to 2.0

Hydra 2.0 removes the seven-field map compatibility projection. Hydra 1.9 state v2
records contain the durable data needed by 2.0; migration verifies those records,
backs them up, and removes only the obsolete projections.

## Before upgrading

1. Finish or stop active mutations. Do not run spawn, resume, kill, workflow, event
   repair/retention, or integration promotion during migration.
2. Preserve local work in every worktree. Migration never deletes a worktree, tmux
   session, head record, event, transcript, message, or integration report.
3. From each repository you use with Hydra, record `hydra list --json` and run:

   ```sh
   hydra state verify
   hydra state migrate --dry-run
   ```

The dry run must report that authoritative state v2 verifies. A divergent
`compat-map` aborts migration; inspect the named project and head instead of editing
or deleting either format blindly.

## Upgrade

Install Hydra 2.0, then run:

```sh
hydra state migrate
hydra state verify
hydra list --json
hydra tui --capabilities --json
```

The migration prints its generated backup path. Keep that path until the upgraded
installation and active repositories have been checked. Re-run the command after an
interruption: if the projections were already removed, verification still succeeds
and a new no-op-safe backup is created.

Expected changes:

- `$HYDRA_HOME/state/v2` remains and is the only runtime authority;
- project `compat-map` files and `$HYDRA_HOME/map` are removed after backup;
- stopped head history remains visible to state, event, lifecycle, and recovery
  tools but is absent from the active `list` view;
- plain `hydra tui` starts native mission control when a qualified binary is
  installed and visibly falls back to `hydra tui --basic` otherwise.

## Verify local work and evidence

For every important active head, compare its branch, session, worktree path, profile,
task, current instance, and event verification with the pre-upgrade output:

```sh
hydra list --json
hydra path HEAD
hydra lifecycle HEAD --json
hydra provenance HEAD --json
hydra events verify
```

No command in this sequence pushes, merges, or removes local work.

## Roll back for a 1.9 downgrade

Stop Hydra mutations and use the exact backup printed by migration:

```sh
hydra state rollback "$HOME/.hydra/backups/state-YYYYMMDDTHHMMSS-PID"
hydra state verify
```

Rollback fails closed if a state or event writer owns a lock. On success it restores
the state tree and the 1.9 projections, after which reinstalling Hydra 1.9 can use
them. Do not continue using Hydra 2.0 and 1.9 concurrently against one `HYDRA_HOME`.
