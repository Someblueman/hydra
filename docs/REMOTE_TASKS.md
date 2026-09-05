# Remote task packages (unreleased)

This is the source-preparation slice of remote task submission. `prepare` and
`inspect` work locally. Receiver acceptance, detached execution, deduplication,
cancellation, and result collection remain under implementation; preparing a
package does not submit or launch work. The roadmap item remains open.

## Prepare and preview

Choose an exact commit with `git rev-parse HEAD`, an explicit registered host alias,
and the receiving project path. Write a JSON specification, replacing `COMMIT`
with the full lowercase commit ID:

```json
{
  "schema_version": 1,
  "host": "build",
  "project": "/srv/project",
  "source": {"commit": "COMMIT"},
  "work": {"kind": "exec", "argv": ["make", "test"]},
  "inputs": ["context/task.txt"],
  "outputs": ["test-results.json"],
  "capabilities": ["exec"],
  "completion": "command-exit",
  "limits": {
    "transport_seconds": 30,
    "queue_seconds": 60,
    "startup_seconds": 60,
    "execution_seconds": 120,
    "cancellation_seconds": 10,
    "log_bytes": 65536,
    "artifact_bytes": 65536
  }
}
```

```sh
hydra fleet task prepare --source /path/to/repository \
  --spec /path/to/task.json --output /tmp/task-package.json
hydra fleet task inspect --input /tmp/task-package.json
```

The preparation response previews the host, project, exact commit, selected input
paths, byte counts and SHA-256 hashes, output declarations, limits, package size,
and specification digest. It omits the actual transferred file contents. Inspect
checks the specification digest, bundle checksum and Git validity, exact source
commit, input checksums, and paths. It does not contact the destination or certify
that host's capabilities or trust. Git and either `shasum` or `sha256sum` are needed
locally. Receiver dependency checks belong to the acceptance slice.

For an existing finite workflow use
`"work": {"kind": "workflow", "path": ".hydra/workflows/build.yml"}` and
`"completion": "workflow-success"`. The path must be a regular file in the exact
source commit. Preparation does not execute it or grant repository trust.

## Transfer and binding contract

Package schema 1 contains `spec`, `spec_sha256`, `bundle_hex`, and `input_hex`, plus
`schema_version`. The normalized specification includes a source
`bundle_sha256` and an input manifest of `{path, sha256, bytes}` records in the
same order as `input_hex`. Its SHA-256 covers compact JSON emitted after schema
validation in canonical field order; input, output, capability, and argv array
order is significant. Unknown object fields fail validation. Consumers validate
and normalize before comparing the specification digest. Checksums detect changes;
they are not signatures or a substitute for SSH host authentication.

The bundle contains the exact commit and reachable Git history under one ref,
`refs/heads/task-source`. No other branch refs, hooks, Git configuration, environment
values, credentials, or unselected working-tree files are copied. **Committed
history is included**; choose source history appropriate for the trusted destination.
Dirty tracked files remain untouched and are excluded unless explicitly selected
as inputs. Input paths are relative to the source directory and can select
untracked files. Only regular files are accepted; no path component may be a
symlink. Binary input bytes are preserved. Inputs are separate package payloads,
not modifications to the source commit.

All paths reject traversal, absolute paths, empty components, control characters,
backslashes, colons, `.git` components, and the reserved `.hydra-task` directory.
Source containing the reserved directory, submodules, or Git LFS pointers fails
preflight. Submodule/LFS materialization is not implemented. A missing workflow
file, unavailable commit, or failed Git command also fails preparation. Source
preparation uses an isolated temporary bare repository without checkout or hooks;
temporary files are removed on normal success and failure.

Specifications are at most 64 KiB; packages at most 4 MiB. Selected inputs total at
most 512 KiB across at most 64 files. The bundle's hex representation is at most
3 MiB during preparation. Outputs have at most 64 declared paths, argv at most 128
strings of 4096 bytes, and capabilities at most 32 names. Log/artifact limits each
range from 1 byte to 512 KiB. Transport/cancellation limits range from 1 to 300
seconds; queue/startup/execution limits from 1 second to 7 days. These execution
limits are bound declarations in this slice, not yet enforced by a task runner.
Each preparation/inspection Git subprocess has a 60-second deadline.

Output package files are private (mode 0600) and must not already exist. Reuse the
same prepared package for future submission retries: preparing again can produce
different Git pack bytes and therefore a different digest. No automatic retry or
mutation is performed by either command.
