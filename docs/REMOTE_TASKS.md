# Remote task packages (unreleased)

This unreleased implementation provides package preparation, durable submission,
detached command/workflow execution, and task status. Launch requires explicit
authorization of the specification digest. Cancellation, public log retrieval,
and result collection remain under implementation. The roadmap item remains open.

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
locally. Submission checks the receiving host's project mapping and dependencies.

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
Log bounds apply to each captured stream; artifact limits remain declarations
until collection is implemented. The runner enforces queue, startup, and execution
deadlines; cancellation is still under implementation.
Each preparation/inspection Git subprocess has a 60-second deadline.

Output package files are private (mode 0600) and must not already exist. Reuse the
same prepared package for future submission retries: preparing again can produce
different Git pack bytes and therefore a different digest. No automatic retry or
mutation is performed by either command.

## Durable acceptance and reconciliation

Initialize the destination project using the remote installation and state
directory selected by the alias. Review its configuration before explicitly
trusting it for execution; submission does not copy a local trust decision.

```sh
hydra fleet init build --project /srv/project -- --no-agent --json
hydra fleet task submit build --input /tmp/task-package.json --key build-change-1 \
  --trust-spec SHA256_FROM_PREVIEW
hydra fleet task status build --id task_ID_FROM_RECEIPT
```

The selected alias must match the prepared specification. The receiver requires
an existing registered project mapping, working Git/tmux/Hydra executables, and
supported required capabilities. This slice recognizes the existing local `exec`,
`workflow`, `git`, and `tmux` capabilities. Other required names fail explicitly;
executable detection does not qualify provider prompt delivery or resume.
The handshake advertises task protocol 1 with `task-accept`, `task-start`, and
`task-status`.

Acceptance atomically publishes a nonempty private directory under
`$HYDRA_HOME/fleet/tasks/task_ID`, containing the validated `package.json`, immutable
`acceptance.json`, and initial `state.json`. Files and directory entries are synced
before acknowledging acceptance. The receipt binds the task ID, specification
digest, submission key, canonical receiving project path/identity, and recorded
acceptance time. Initial runtime reads `state: accepted` and
`launch_intent: pending`. This is acceptance evidence, not activity or completion.
Task metadata is separate from head/instance and workflow-attempt state.

Keys are scoped to **one receiving host and its selected `HYDRA_HOME`, across all
projects**. They contain 1–128 letters, digits, dots, underscores, or hyphens and
start with a letter or digit. Reusing a key with the same validated specification
returns the original receipt, including after the original project disappears.
Changing the specification, including its destination, returns
`submission_conflict`. Concurrent submitters publish only one acceptance; losing
an acknowledgment does not grant permission to create another task. A handle is
host-qualified: the same key on another host/state directory is a separate task.

If a dispatched submission loses its response, the client reports
`outcome_unknown`. Repeat the **same package and key** to reconcile. No automatic
mutation retry occurs. Handshake failures mean this call did not dispatch a
submission; they do not erase any earlier acceptance. Status during an outage
reports the transport failure without claiming a terminal task state. Submission
uses the specification's transport deadline independently for handshake and
request; status and standalone start use 5 seconds for each. A transport timeout
does not stop a detached task owner.

Acceptance records and keys currently have no automatic expiry. Retain them for
the lifetime of recovery and result collection. Deleting this state loses the
deduplication guarantee; it is not an execution retry mechanism. Incomplete or
corrupt published state returns `recovery_required` and is never replaced by a
repeat submission. An unpublished `.accept.*` directory is not an accepted task.
The receiver rejects symlinked task storage and directories writable by another
user. These protections preserve metadata integrity; tasks are not an operating
system sandbox.

## Receiver-owned execution

`submit --trust-spec HASH` authorizes the exact transferred code, task, and Hydra
configuration, and starts the receiving-host owner before returning its response.
The decision is recorded in the receiving task's `launch.json`; no local project
trust file or credentials are imported. Without the flag, submission only stages
acceptance. To launch a previously accepted task after reviewing it:

```sh
hydra fleet task start build --id task_ID --trust-spec SPEC_SHA256
```

A permanent launch claim is synced before forking. The detached owner retains a
kernel file lock through startup and execution; its descendants do not retain the
lock after exec. Repeated start requests return the existing task or report its
uncertainty. They never remove the claim or repeat an execution. SSH disconnect
does not signal the detached owner. If the owner dies before recording a terminal
state, status reports `outcome_unknown` alongside the recorded state. Reboot or
lost-owner recovery never silently resumes or restarts external work.

The owner creates a private checkout from the verified bundle and initializes a
dedicated Hydra project/worktree root. It uses the public shell CLI for `init`,
`spawn`, `exec`, `provenance`, and `workflow run`. The originally mapped project and
its dirty work remain untouched. Bundle materialization disables Git system/global
configuration and hooks; execution restores the receiving host's Git configuration.
Credentials remain host-local. Selected inputs are read-only-by-convention regular
files under the task's private `inputs/`; commands receive their absolute directory
as `HYDRA_TASK_INPUT_DIR`. Input contents are never treated as environment settings.

Command tasks create one no-agent head and retain its provenance plus the actual
exec run response in `provenance.json` and `attempt.json`. Workflow tasks retain the
actual workflow run ID; its existing attempt/gate records remain authoritative.
`succeeded` means the declared command-exit or workflow-success policy passed; it
does not imply all agent sessions stopped, a human approved changes, or changes
were integrated. Missing execution evidence is not invented.

Queue time runs from recorded acceptance to launch, with clock reversal refused.
Startup uses one monotonic deadline across checkout, initialization, and head
creation. Execution has its own monotonic deadline around the shell CLI process
group. `queue_deadline`, `startup_deadline`, and `execution_deadline` are separate
failure reasons. The owner records bounded `stdout` and `stderr` files, an exit
status, and available run identity. These are inspectable files pending public log
retrieval. Cancellation and artifact/result collection are not yet qualified.
