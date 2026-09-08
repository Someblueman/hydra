# Distributed task steps

Workflow schema 1 accepts `kind: task` with exactly one argument,
`task_input: recipe`. The named input must reference a root input in the workflow
data manifest containing a draft [fleet task specification](REMOTE_TASKS.md). Build
`hydra-fleet` with `make build-fleet` on the coordinator and selected workers.
Each recipe explicitly selects `host: local` or a registered remote alias,
a project, an exact source commit, execution capabilities and finite limits.
Task steps require `retry: 0` and `idempotent: false`.

```yaml
  - id: produce
    kind: task
    needs: []
    retry: 0
    idempotent: false
    args:
      task_input: recipe
```

The data declaration names `recipe` as a root input. Other declared inputs can
reference root inputs or sealed outputs from predecessor steps. The recipe's
`inputs` list must name exactly those other inputs; its `outputs` paths must
match the data declaration's output paths. The receiver exposes packaged inputs
through `HYDRA_TASK_INPUT_DIR`. Source Git content is packaged separately from
these inputs. See [Workflow data](WORKFLOW_DATA.md) for the manifest format.

Before publishing a run, Hydra snapshots every task recipe and resolved
transport destination. Before a first submit, it persists the exact package,
submission key and binding in the attempt's `remote` directory. A response is
accepted only when its task ID, package digest and submission key match.
Collected artifacts must pass the existing task result verification before they
become sealed inputs for downstream steps. A successful process and artifact
collection do not by themselves establish semantic correctness.

`hydra workflow resume <run-id>` runs on the original coordinator host and home.
A native process lock permits one coordinator at a time, including after a
crash. A lost acknowledgement leaves `waiting-remote` (exit 3); resume reconciles
the original host, package and key without creating a new execution attempt.
Changing a registered alias does not redirect recorded work. Corrupt recorded
bindings require recovery. There is no coordinator failover or automatic
reassignment to another host.

Cancellation records intent and sends a cancellation request for the bound task.
If the receiver cannot confirm its state, the run remains `waiting-remote`;
it is not reported as cancelled. A later explicit resume reconciles the same
identity. Downstream work stays blocked while the predecessor is unresolved.

This task primitive currently carries files between nodes. Objective-plan
compilation, intermediate semantic validation gates, derived Git source handoff
and bounded candidate repair remain separate work under
[the distributed DAG contract](DISTRIBUTED_DAG.md).

## Qualification, 2026-09-08

The public workflow CLI completed a real Linux VPS producer followed by a macOS
consumer in `run_5ded930196dec1df4745`. The coordinator independently collected
and sealed `Linux\n`; the local consumer emitted `Linux\nDarwin\n` with SHA-256
`54d05eda5f9578d5da4f6c019fe440f4e83d4d881c8d4f0534574ad27d0ca457`.
The two accepted task IDs were
`task_5d6daa62a9c72909e9d5f44282a6f0bde4385799c46ba4d4185204303b3a8924`
and `task_02105801d1bd9de232a8e81c5070de1728e14b9f23e6371ad59f1153c7282ce6`.
The macOS coordinator used UBSan; the VPS used the native Linux build with JSON-C
0.17. This establishes cross-host file handoff, not full roadmap acceptance.

`make test-fleet` includes public CLI task handoff, lost submit acknowledgement,
concurrent coordinator rejection, coordinator/observer crash recovery, tampered
dispatch, moved placement, and confirmed/unconfirmed cancellation cases. Recovery
checks compare the original dispatch and receipt and count receiver acceptances.
The wrong-key case also recomputes the file checksum, exercising contextual
identity verification separately from file-integrity verification.
