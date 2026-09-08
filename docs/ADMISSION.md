# Resource admission

The shell CLI owns one admission store per receiving host's `HYDRA_HOME`, shared
by all projects using that home. A client capacity snapshot never grants a slot.
The receiving authority checks current reservations and policy under one lock.
The CLI resolves its executable and home before workers change directories.
The home and admission directory must be owned by the current user and must not
be writable by the group or other users. A home symlink resolves to the same store.

## Runtime paths

Local exec workers, gates, spawn, resume, and spawn queue processing use the same
admission authority. Workflow steps and remote tasks use these paths.

Every exec worker obtains its own slot before launching its command, including
each worker selected by `exec --all --jobs N`. `HYDRA_ADMISSION_QUEUE_SECONDS`
sets its queue deadline (default 60 seconds, maximum 7 days), separately from the
command timeout. `HYDRA_ADMISSION_LABELS` supplies required comma-separated labels.
Rejected or expired admission reports exit code 125 without invoking the command.
Each worker records `admission.json` and, on completion, `admission-release.json`
beside its execution evidence. Losing an execution owner retains its claim even
if its command later exits. Explicitly reconcile termination before releasing it.

Head creation reserves before worktree setup. Interactive agent instances keep
their slot until explicit teardown, and store its ID in their instance's
`admission-id`. Plain shell heads release their startup slot after creation; later
exec commands acquire independent slots. Admission counts managed operations and
agent instances, not arbitrary processes launched manually in shell panes. Resume
acquires a new reservation before creating a worktree or terminal. A failed setup
with uncertain effects retains an `unknown` reservation for reconciliation.
An absent terminal alone does not release an old instance's claim during resume.
Existing `HYDRA_MAX_SESSIONS` and interactive spawn-queue behavior still constrain
head creation; they cannot grant capacity past the receiver's admission policy.

## Receiver policy

```sh
# Host slots, slots per project, disk floor KiB, pending queue bound, host labels
hydra admission configure 4 2 1048576 128 linux,gpu
hydra admission status --json
hydra admission status --summary
# Read-only inspection through an explicitly selected registered remote:
hydra fleet admission build -- status --summary
hydra fleet admission build -- inspect task-example
```

Concurrency limits of zero mean unlimited. The project limit applies separately
to each project identity. Disk is measured on the admission store filesystem.
Labels are explicit operator assertions, not hardware or executable probes.
Use `-` for no labels. A queue limit of zero refuses new requests. Policy changes
affect new grants and never revoke existing reservations. A lower limit can leave
existing reservations above the new limit; no further slots are granted until
capacity permits. Policy is private receiver configuration and is never sourced
as shell code or taken from a submitted repository.

An existing policy must contain all five fields exactly once. Empty, partial or
malformed policy files block admission; defaults apply only when no policy exists.

## Owner interface

```sh
hydra admission request task-example project-example 60 linux
hydra admission inspect task-example
hydra admission claim task-example
hydra admission cancel task-example
hydra admission unknown task-example
hydra admission release task-example --confirmed
```

Request returns a versioned JSON envelope containing the state and reason. It
binds an ID to project, required labels, and queue duration; reuse with changed
inputs fails. Repeating an unchanged request observes its existing identity and
does not extend its deadline. A reservation is an admission decision, **not an
execution deduplication lock**. An execution owner must separately ensure a task
is launched only once before using a reservation, as remote task launch already
does. Never use a reserved response as permission to repeat uncertain execution.

The queue uses a sequence assigned under the host lock. Only its oldest entry may
claim the next slot; a project-limited entry can hold up later projects. Queue
entries expire at their original deadline, or on a backward clock observation.
Expiry is reconciled on each admission operation. Claim does not launch work:
the existing execution owner is responsible for polling, deadline and cancellation
handling. There is no new scheduler daemon.

Cancellation removes only waiting requests. Running or uncertain work retains its
claim until termination is confirmed. `unknown` explicitly labels an unresolved
reservation; it continues counting against host and project limits. Release is an
explicit owner assertion that execution has ended, requiring `--confirmed`.
Age, dead PIDs, lost connections, or expired leases never release reservations.

## Remote tasks

The existing receiver launch lock and durable launch claim still control execution
deduplication. Before detaching an owner, the receiver requests a preparation slot
using the accepted task ID and the original mapped project identity. A full queue
fails without creating a workspace. A waiting task remains `starting`, with its
queue state and reason in `runtime.admission`; status never turns that observation
into permission to launch again.

The initial queue deadline is acceptance time plus the specification's
`queue_seconds`. Repeated start or submit does not extend it. The detached owner
polls the shell authority and cancellation request. Startup time begins only after
that initial reservation is granted. Preparation releases its slot before heads
and commands acquire their own slots, so workflow coordinators do not hold a slot
while waiting for their workers. Subsequent operation waits have the same maximum
queue duration, bounded also by their enclosing startup or execution deadline.

Receiver-created Git metadata binds the isolated workspace to the original mapped
project for admission. Its child reservation IDs carry the task ID as a prefix.
Thus separate clones cannot evade the mapped project's limit. Resuming a suspended
task checks this binding; older suspended tasks acquire it from their immutable
acceptance record. The existing execution project identity remains unchanged for
worktree state, logs, artifacts, and collection.

Task capability names prefixed with `label.` request operator labels: for example,
`"capabilities": ["exec", "label.linux", "label.gpu"]`. The prefix is removed when
matching the receiver's configured labels. Requirements apply to preparation and
later workers. Built-in capability checks still reject unsupported operations.

Confirmed managed-command cancellation reconciles only that task's child claims.
An unconfirmed stop or missing owner retains reservations. A task status response
can therefore show `outcome_unknown` while an execution reservation remains
`reserved`; it still consumes capacity. Inspect the task and its reservation before
manual release. `admission_cleanup: incomplete` means reconciliation could not be
confirmed and capacity may remain held.

## Storage and recovery

`$HYDRA_HOME/admission/policy` holds restricted key/value policy. Each
`<id>.request` is one restricted-token record: project, state, request time,
deadline, FIFO sequence, required labels, and reason. Records are replaced by
same-directory rename while holding the `lock` directory. Partial or linked
records fail closed. Terminal records retain the request binding; no automatic
history purge is provided in this slice.

Lock acquisition is bounded. A writer crash can leave `lock` behind; ordinary
stale session-lock cleanup does not remove it. Inspect and stop admission writers
before manually removing that empty directory, then inspect records before
resuming. Removing the writer lock does not release execution reservations.
This shell store follows Hydra's atomic filesystem replacement model; it does
not claim power-loss durability beyond the host filesystem's guarantees.

Status records its observation timestamp and reports zero age at capture. A
consumer computes subsequent freshness from that timestamp. CPU and memory are
not admission enforcement. Capacity snapshots must not be used as reservations.
`status --summary` omits individual records and marks `requests_omitted: true`,
keeping capacity response size bounded even when terminal history grows. Fleet
head snapshots include this summary for initialized hosts. Full status and
`inspect ID` expose individual queue and reservation records. Remote admission
inspection is read-only; configure receiver policy on the receiving host.

## Acceptance — 8 September 2026

Resource admission through implementation commit `6972f99` passed local macOS
qualification. Remote cases use a controlled SSH boundary with real receiving
processes, Git, tmux, and isolated host state. This qualification covers admission
behavior; live-provider and separate-host qualification retain their existing scope.

| Requirement | Acceptance evidence |
| --- | --- |
| Atomic host-wide FIFO and bounded queue | Twelve simultaneous CLI submitters produce one reservation, eleven queued entries, and unique sequences; queue overflow is refused. |
| Host/project concurrency | Real local commands across projects cannot overlap with one host slot; two remote task clones share the original project's one-slot limit and cannot overlap. |
| Disk floors and capability labels | CLI disk-floor refusal, missing-label refusal, and a matching-label remote execution. |
| Stale observations cannot grant capacity | A remote zero-use snapshot is captured before capacity is occupied; later submission queues against the receiving authority. |
| Bounded queue age and cancellation | Original deadlines survive repeated start; expiry and queued cancellation occur without workspace creation; running cancellation confirms termination and reconciles task-scoped claims. |
| Unknown ownership retains claims | Killing local and remote execution owners leaves reservations held; repeated remote start refuses replay. |
| Head, gate, and queue integration | Startup/resume stop before effects when blocked; interactive instances retain slots; gates and queue processing use the same authority. |
| Inspectable capacity and freshness | Public local/remote status and inspect expose state, reasons, counts, labels, timestamps, and bounded summaries. CPU/memory do not grant slots. |

Verification completed successfully with `make lint`, `make test`,
`make test-fleet`, and `make sanitize-fleet` (UBSan on macOS). Final release-build
remote acceptance and focused head/exec admission checks also passed after review.
The build and public fleet help check passed after the final help-text update.
The subsequent C quality gate passed after extracting admission preparation,
cleanup, queue-expiry, and inspection helpers: no cognitive-complexity regressions
against the checked-in baseline. Existing size and analyzer advisories remain;
the acceptance result does not describe the repository as warning-free.
Fleet transport and full remote task acceptance passed again after that refactor.

`sh tests/test_admission.sh` checks the public CLI with concurrent submitters,
unique FIFO sequences, project limits, disk floors, labels, queue bounds and
expiry, unknown ownership, cancellation, conflicting IDs, malformed records,
links, and lock contention. `sh tests/test_admission_execution.sh` exercises real
exec commands across two projects, verifies they cannot overlap at a one-slot
host limit, and checks queue cancellation/expiry, label refusal, normal release,
and a killed worker's retained reservation. `sh tests/test_admission_heads.sh`
checks startup/resume before effects, an interactive fixture agent's lifetime,
gate refusal and release, and the existing queue processor's real spawn path.
`tests/task_admission_cases.sh`, sourced by `tests/test_task_acceptance.sh`, exercises
the real receiver through the fixture's SSH boundary: stale capacity, queue bounds,
queued cancellation, expiry before clone, capability labels, and concurrent tasks
sharing the original project's limit. The task suite also checks that losing an
owner retains its execution claim and confirmed cancellation releases child claims.
