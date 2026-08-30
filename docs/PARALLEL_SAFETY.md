# Parallel safety

Hydra 1.7 adds coordination records and guarded integration for repositories with
many active heads. Claims and scopes communicate intent and detect mistakes; they
are not filesystem or process sandboxes.

## Claims, scopes, and collisions

Create time-bounded claims for paths a head expects to read or change:

```sh
hydra claim add feature-api --path 'lib/api/*' --access write \
  --reason 'API refactor' --expires-at 1790000000
hydra claim list --json
```

Scopes may be recorded at spawn or changed later. `scope check` classifies every
changed path as `writable`, `read-only`, or `out-of-scope` and exits nonzero for the
latter two classifications.

```sh
hydra spawn feature-api --scope-read 'docs/*' --scope-write 'lib/api/*'
hydra scope check feature-api --json
hydra collision feature-api feature-ui --json
```

Collision findings are deliberately labeled `claim`, `overlap`,
`predicted-conflict`, or `observed-conflict`. An overlap is never reported as a
certain merge conflict. `predicted-conflict` comes from file-level textual merge
prediction; `observed-conflict` comes from Git's commit merge simulation or an
already-unmerged index entry.

## Resources and gates

Resource allocation is project-scoped and locked, so concurrent heads cannot claim
the same port or environment name. Successful teardown releases owned allocations
and claims; failed cleanup leaves inspectable records for recovery.

```sh
hydra resource allocate feature-api --port http=3000-3999 \
  --compose-project hydra-api --database hydra_api
hydra resource env feature-api
hydra gate run feature-api --name acceptance -- make test
hydra gate approve feature-api --name acceptance --by maintainer \
  --reason 'reviewed captured output'
```

Command success and human approval are separate records. An approval applies only to
the exact commit and worktree status hash that was verified.

## Context, synchronization, and landing

Context packs include only named types. File selections become hashed manifest
entries; they are not silently copied. Likely secret-bearing paths are rejected.

```sh
hydra context create feature-api --diff --file lib/api/client.sh \
  --note 'Review error handling' --history 5 --artifact build/report.txt
hydra sync feature-api --from main --gate acceptance --dry-run
hydra land feature-api --into main --gate acceptance --dry-run
```

`sync` and `land` require clean worktrees and a current approved gate, simulate the
merge, and archive the pre-operation commit plus a Git bundle. Conflicts are aborted
back to the clean pre-operation commit. `land` tears down the source head after a
successful merge unless `--keep-head` is selected.

## Disk and worktree recovery

`hydra du` separates worktree and durable-state usage. `hydra gc` is a dry run unless
`--apply` is passed and preserves dirty or untracked work by default. The only
policies are `orphaned`, `stopped`, and `archives`; dirty worktree removal also needs
the separate `--include-dirty` acknowledgement.

Worktree doctor exposes Git's own lock, unlock, move, repair, and prune operations.
Move requires a stopped session and clean worktree. Repair and prune default to dry
run.
