# Workflow data and approval waits

Workflow schema 1 accepts an optional `data: handoff.json` field. This path is
relative to the workflow definition. The JSON manifest uses schema version 1,
with named `inputs` and per-step `inputs` and `outputs`. Unknown fields fail
validation. `hydra workflow validate` checks references against the finite DAG
before a run is created.

These features use the existing optional `hydra-fleet` helper for bounded JSON,
file snapshots, and fingerprints. Build it with `make build-fleet` (C99 and JSON-C),
or install the prebuilt fleet helper. The shell CLI owns execution, state
transitions, approvals, and recovery. Existing workflows without data or approval
waits, and core session management, retain shell-only operation.

```json
{
  "schema_version": 1,
  "inputs": {
    "request": {"path": "request.json", "type": "object", "max_bytes": 65536}
  },
  "steps": {
    "produce": {
      "inputs": {"request": {"input": "request"}},
      "outputs": {
        "report": {"path": "report.json", "type": "object", "max_bytes": 65536}
      }
    },
    "consume": {
      "inputs": {"report": {"step": "produce", "output": "report"}}
    }
  }
}
```

`consume` must explicitly include `produce` in its `needs` list. Names start with
a lowercase letter and contain at most 64 lowercase letters, digits, underscores,
or hyphens. Each input/output map has at most 64 entries. The manifest is limited
to 64 KiB and data-bearing workflows to 256 steps.

Every declaration has a relative `path`, a `type`, and a positive `max_bytes`.
`file` preserves arbitrary bytes up to 512 KiB. `object`, `array`, `string`,
`number`, and `boolean` require exactly one UTF-8 JSON value of that type and are
limited to 64 KiB. Nonfinite numbers, trailing data, and embedded NUL bytes in JSON
are rejected. JSON null is not a declared type. Required outputs are never inferred
from process exit or provider completion.

Workflow inputs default to `"source": "repository"`, relative to the repository
root. An explicit `"source": "task"` reads only from `HYDRA_TASK_INPUT_DIR`, the
selected input directory supplied by remote task execution. Such an input fails
when that directory is unavailable. Optional input `sha256` fields pin expected
bytes. Absolute paths, traversal, symlinks in selected paths, and special files are
refused. The workflow definition and manifest must be included in remote task
source snapshots; selected task inputs remain separate from that source.

Before publishing a run, Hydra snapshots and validates its inputs. Before an exec
step starts, it verifies and copies the referenced snapshots into a private
attempt directory. Commands receive:

- `HYDRA_WORKFLOW_INPUTS_DIR`: input files named by the step's input names.
- `HYDRA_WORKFLOW_OUTPUTS_DIR`: where commands write declared output paths.

After exit zero, required outputs are snapshotted and validated. Only then may
the step succeed. `outputs.json` records exact types, sizes, and SHA-256 digests;
`artifacts/<name>` contains the sealed bytes. Dependents receive those snapshots.
Changed or missing sealed bytes fail verification before the dependent command
runs. Resume revalidates the recorded manifest, root inputs, and completed outputs.
These are integrity checks, not isolation from another process running as the same
OS user. Private scratch files and provider side effects are outside these manifests.

## Durable approval requests

An `approval-wait` step has `idempotent: false`, `retry: 0`, and required `args.head`
and `args.name`. Optional `args.message` describes the review; `args.timeout` sets
an expiry in seconds from request creation (1-86400). Omitting it creates a request
without expiry. The step cannot supply `args.by` to issue its own decision.

When no runnable work remains, the coordinator records `waiting-approval` and
exits with status **3**. No background coordinator is needed to retain the request.
Use the public commands:

```sh
hydra workflow requests run_ID --json
hydra workflow decide run_ID step_REQUEST_ID approve --by reviewer
hydra workflow resume run_ID
```

Use `reject` instead of `approve` to refuse the action. Recording a decision never
executes work; resume is explicit. Cancellation also works while suspended.

Each request binds the resolved workflow, base commit, current head and instance,
tracked and untracked worktree evidence, completed dependency attempts, declared
artifacts, and the named gate's current evidence when present. The request exposes
its binding hash and an inspectable `binding.tsv`. Worktree fingerprints use Git
with external diff/textconv disabled and bounded command output. Ignored files,
system tools, and external services are not part of the worktree fingerprint;
put evidence that requires an exact binding in the declared data manifest.

Decision records are immutable and include the request, binding, action, time,
local OS user ID, `source=local-cli`, and an optional descriptive actor label.
This records an explicit local operator decision under the host's account trust
boundary; it does not authenticate a human merely from `--by`. A workflow's existing
`approve` kind is an automatic policy-issued gate approval and records
`decision-source=workflow-policy` in its attempt. It cannot satisfy an
`approval-wait` request.

Resume rejects changed definitions or corrupted data. If the reviewed head or gate
evidence changes, or the request expires, resume preserves the old request as
`stale` or `expired` and creates a fresh request. The new request needs its own
decision. Direct dependents recheck approval bindings and expiry immediately before
dispatch; changed evidence cannot release them just because a decision was recorded
earlier. A late change reports `recovery-required`; it does not replay an uncertain
command. Approval does not replace a verification gate or make provider completion
an authoritative result.

## Remote approval waits

A received workflow that suspends for approval records task state
`waiting_approval` and keeps its existing workspace and workflow run. It does not
seal a terminal result. Inspect and decide through the registered host:

```sh
hydra fleet task requests HOST --id TASK_ID
hydra fleet task decide HOST --id TASK_ID --request REQUEST_ID \
  --decision approve --trust-spec SPEC_SHA256 --by reviewer-label
hydra fleet task resume HOST --id TASK_ID --trust-spec SPEC_SHA256
```

A decision never starts execution. Resume requires the exact accepted spec digest,
claims the existing owner lock, and resumes the recorded run. Completed steps are
preserved. Active execution time consumes the original execution budget; approval
waiting time does not. Owner logs append within the original byte limit. A lost
resume response requires status reconciliation, not a new task submission. Loss of
an active owner remains `outcome_unknown`; it does not authorize replay.

Decisions are made by the receiving host's local CLI under the authenticated SSH
account. The recorded OS UID is that account; `--by` is only a label. The approval
binding is checked on decision and resume. Changed evidence requires a new request
and decision. `hydra fleet task cancel HOST --id TASK_ID` can cancel a suspended
workflow without signaling the PID of its former coordinator. Independent agent
sessions still require their own shutdown evidence before cancellation is confirmed.

## Generated distributed-plan inputs

Accepted objective plans using schema 2 can declare `{"validation":"plan"}` or
`{"repair":"plan"}` as a step input reference. These values are generated by the
coordinator, selected by name in the task recipe, and packaged as ordinary input
files. They require an accepted compiled plan; a standalone workflow has no plan
validation or repair context. See [distributed task workflows](WORKFLOW_TASKS.md)
for the value formats, validation joins and repair bounds.

## Opt-in handoff contracts

Data schema 2 adds exact producer/consumer contracts, units, required fields,
explicit lossless conversions, composition predicates and candidate provenance
bindings. See [Bounded handoff contracts](HANDOFF_CONTRACTS.md). Schema 1 retains
its existing byte/type/receipt contract.
