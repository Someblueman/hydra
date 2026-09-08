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

## Compiled plans with required evidence

Objective plan schema 2 uses task steps through the same `workflow plan compile`,
`show`, `run --accept`, `status` and `resume` interfaces as schema 1. The compiled
artifact binds each recipe and its resolved transport destination before any
execution. Recipes must fit the accepted hosts, tools, effects, artifact budget,
head count and total queue/startup/execution time budget. Each task starts from
the accepted source commit unless it explicitly selects a predecessor with `source_step`. The preview includes the exact recipe and transport.

A deliverable with `destination: intermediate` can name a work-step output;
`destination: run-artifact` still requires a compose step. Both need explicit
requirements and checks. Any consumer of a checked artifact must depend on all
its required validator steps. Those dependencies form the evidence join; missing
edges are a compilation error. The producer remains a direct dependency for its
file handoff.

A validator declares a generated input in the workflow data manifest:

```json
"validation": {"validation": "plan"}
```

Its task recipe includes `validation` in its selected inputs. The coordinator
writes an object mapping this step's check IDs to their accepted definition
SHA-256 values. The validator reads it at `$HYDRA_TASK_INPUT_DIR/validation` and
returns a version-2 [check report](PLAN_COMPILATION.md) in its declared object
output. The report binds the exact subject digest, validator digest, requirement
coverage and PASS/FAIL/INCONCLUSIVE verdict.

After verified collection, the coordinator evaluates every check owned by the
step before marking it successful. A missing, malformed, stale, failed or
inconclusive required report blocks downstream execution. Valid negative reports
remain sealed for inspection, with coordinator verdicts in `validation.json`.
Final result retrieval independently evaluates the reports again. Validator
scripts run from the accepted source commit, so producer commits cannot replace
their acceptance definitions.

Schema-1 local plans and reports remain supported. Schema-2 plans currently require
zero execution retries and zero repairs; bounded candidate repair is still outstanding.

The larger schema-2 qualification run `run_fcd37ffcd8338b3a8558` used a macOS
producer and a Linux VPS producer. Each was checked on the other host. Assembly
waited for both checks, committed the combined file on macOS, and a VPS validator
checked the assembled bytes. All three reports passed exact expected-content,
subject-digest, definition-digest and requirement-coverage checks. The combined
artifact SHA-256 was
`60dc14222713495f87b057cf6d1c5c4e7582125b80de2e7e973bdd6668d60c73`.
[Recorded task identities and verdicts](evidence/distributed/two-host-validation.json)
retain the explicit placements and accepted plan digest.

The same collected composition was exercised through the existing integration
CLI in a disposable collector repository: promotion without approval failed,
a target move after approval blocked promotion, and a fresh assembly and approval
promoted the checked bytes. [Integration identities](evidence/distributed/two-host-integration.json)
record the target commits and both attempts. No repository publication occurred.
These checks do not close the remaining repair and replay work.

### Derived Git source

A task may set `args.source_step` to a direct predecessor. After that producer
succeeds, the coordinator requires a verified collection containing exactly one
clean head. It creates the consumer package from that collected commit, retaining
the accepted recipe and destination. Missing, dirty or altered collections stop
execution before submission. The dispatch records the producer step, task ID,
collection ID, result digest, head ID and commit and rechecks them on recovery.

In plan schema 2 this source dependency also requires all the producer's required
validation joins. A `verify` step cannot select derived source: its executable
acceptance definitions remain in the original accepted commit. File inputs are
still handed off separately through declared, sealed artifacts.

The two-host source qualification `run_b7b8ebd8d52d130b6c04` assembled on macOS
from the VPS producer's collected commit after independent validation. The
composer checked a file through `git show HEAD:result.txt`, then committed the
combined candidate; a VPS validator checked the final bytes. The
[recorded source binding](evidence/distributed/two-host-source.json) identifies
both the producer result and the consumer package commit.
