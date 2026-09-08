# Resource admission

The shell CLI owns one admission store per receiving host's `HYDRA_HOME`, shared
by all projects using that home. A client capacity snapshot never grants a slot.
The receiving authority checks current reservations and policy under one lock.

## Implementation status

The admission primitive is implemented. Automatic integration with spawn, exec,
workflows, and remote task owners is still in progress; configuring this primitive
alone does **not yet limit those execution paths**. Roadmap item 4 remains open.

## Receiver policy

```sh
# Host slots, slots per project, disk floor KiB, pending queue bound, host labels
hydra admission configure 4 2 1048576 128 linux,gpu
hydra admission status --json
```

Concurrency limits of zero mean unlimited. The project limit applies separately
to each project identity. Disk is measured on the admission store filesystem.
Labels are explicit operator assertions, not hardware or executable probes.
Use `-` for no labels. A queue limit of zero refuses new requests. Policy changes
affect new grants and never revoke existing reservations. A lower limit can leave
existing reservations above the new limit; no further slots are granted until
capacity permits. Policy is private receiver configuration and is never sourced
as shell code or taken from a submitted repository.

## Owner interface

```sh
hydra admission request task-example project-example 60 linux
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

## Primitive verification

`sh tests/test_admission.sh` checks the public CLI with concurrent submitters,
unique FIFO sequences, project limits, disk floors, labels, queue bounds and
expiry, unknown ownership, cancellation, conflicting IDs, malformed records,
links, and lock contention. Runtime integration acceptance remains outstanding.
