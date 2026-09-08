# Distributed task workflows

Hydra runs a finite DAG across explicitly selected fleet hosts, using one durable
coordinator on its original host. Build `hydra-fleet` with `make build-fleet` on
the coordinator and selected workers. Register SSH destinations with `hydra remote
add`; task recipes select `host: local` or a registered alias and a project.

## Task steps

Workflow schema 1 accepts `kind: task`, a required `task_input` argument and an
optional `source_step`. The named task input references a root input containing a
draft [fleet task specification](REMOTE_TASKS.md). Each recipe specifies an exact
source commit, capabilities and finite limits. Task steps require `retry: 0` and
`idempotent: false`; receipt reconciliation does not create an execution retry.

```yaml
  - id: consume
    kind: task
    needs: [produce]
    retry: 0
    idempotent: false
    args:
      task_input: recipe
      source_step: produce
```

Other declared inputs reference root inputs or sealed predecessor outputs. The
recipe's `inputs` list names exactly these other inputs; its `outputs` paths match
the data declarations. The receiver exposes packaged inputs at
`HYDRA_TASK_INPUT_DIR`. See [workflow data](WORKFLOW_DATA.md) for file declarations.

Before publishing a run, Hydra binds every recipe and transport destination.
Before submission, it persists the derived package, digest and submission key in
`steps/STEP/attempt-N/remote/dispatch.json`. The receipt must match that package
and key. Verified collection precedes artifact sealing and downstream handoff.
Neither successful process exit nor successful collection establishes correctness.

## Recovery and cancellation

Use `hydra workflow status RUN --json` and `hydra workflow resume RUN` from the
original coordinator host and home. An OS process lock permits one coordinator,
including after a crash. Lost acknowledgements produce `waiting-remote` (exit 3):
resume reconciles the same host, package and key. Changing an alias's endpoint does
not redirect recorded work. Corrupt bindings or an unapproved attempt number
require recovery. Deduplication is scoped to the receiving host's task store.

Cancellation sends a request for the bound task. If the receiver cannot confirm
termination, the run remains `waiting-remote`; later resume reconciles that same
identity. Dependents remain blocked. There is no coordinator failover, automatic
reassignment, dynamic task pool or load balancing.

## Plans and required validation

Objective plan schema 2 uses `workflow plan compile`, `show`, `run --accept` and
`result`, with the existing workflow status/cancel/resume commands. It supports
explicitly placed `exec` task recipes with `work`, `verify` and `compose` roles.
Compilation binds recipes, destinations and the accepted source, and checks hosts,
tools, effects, artifact limits, time and head budgets against the supplied policy.
Schema-1 local plans and their version-1 reports remain supported.

A deliverable with `destination: intermediate` can name a work-step output;
`destination: run-artifact` requires a compose step. Both need requirements and
checks. Consumers of checked artifacts or derived source must depend on every
required validator. Missing validation joins are compilation errors.

Each validator declares an input such as:

```json
"validation": {"validation": "plan"}
```

The recipe includes this name in its selected inputs. Its generated file maps
check IDs to accepted definition SHA-256 values. The validator returns a
[version-2 check report](PLAN_COMPILATION.md) binding the exact subject digest,
validator-definition digest, covered requirements, evidence and a
PASS/FAIL/INCONCLUSIVE verdict. The definition digest binds the check's accepted
recipe, requirements, source and input declarations.

After collection, every check owned by the step must pass before its dependents
can advance. Missing, malformed, stale, failed or inconclusive reports block the
join. Negative evidence remains sealed, with coordinator verdicts in the attempt's
`validation.json`. Final result retrieval independently checks the evidence again.

Validators always execute from the original accepted source commit. They cannot
select `source_step`, so producer commits cannot replace their acceptance scripts.
This is a provenance boundary, not isolation from malicious processes under the
same OS identity. The compiler proves declared coverage, not the adequacy of a
human's or agent's chosen acceptance criteria.

## Derived Git source

`source_step` selects a direct predecessor's collected commit. The coordinator
requires a successful producer with exactly one clean head, a verified collection,
and a receipt matching the producer's bound dispatch. It records the producer
step, task ID, collection ID, result digest, head ID and commit in the consumer's
dispatch and rechecks these on recovery. Missing, dirty or altered provenance stops
submission. File inputs still travel separately as declared, sealed artifacts.

## Bounded semantic repair

Schema-2 plans may accept `repair_budget` from 0 through 10; execution
`retry_budget` stays zero. The compiler reserves the complete graph's head count,
summed queue/startup/execution limits and declared output bytes for every allowed
round; context and root-input reservations are counted once. The original
wall-clock deadline remains fixed across repair and coordinator restarts.

With a nonzero repair budget, every producer and composer needs a generated input:

```json
"feedback": {"repair": "plan"}
```

Its recipe selects `feedback` and reads the file at
`$HYDRA_TASK_INPUT_DIR/feedback`. The object contains `schema_version: 1`, the
current `attempt` (starting at 1), and `previous`. Initially `previous` is null;
later it contains the accepted plan digest and prior failed/inconclusive reports,
keyed by check ID, including their exact subject and definition digests.

A repair begins only after all current steps are terminal and every failed step
has valid negative semantic evidence. Missing/stale reports, validator process
errors, corrupt state and unresolved remote work do not trigger repair. The
coordinator records that evidence, then starts a new round of the same graph with
new attempt directories and submission keys. It retains all old attempts. A
partial reset is completed from `repair-pending.json` before launching work.

The accepted recipes implement the actual repair. A previously rejected artifact
must change its digest, and all validators run again; an unchanged rejected
candidate cannot advance even with a new PASS report. Composition produces a new
candidate requiring its own verification. Budget exhaustion leaves the run failed.
Approved Git promotion still uses the existing candidate-bound integration flow,
including a fresh target-ref check; a validation report never authorizes a push.

## Scheduling replay

`hydra workflow replay RUN` is read-only. It checks the graph binding and hash
chain in `schedule.jsonl`, recomputes each next-step choice, and returns the
recorded choices and journal digest. It does not contact workers or execute work.

The native selector consumes the recorded state vector, cancellation intent,
observed time, fixed deadline and graph parallelism. Graph source order breaks
ties, and required dependencies must all be successful. Repeated unchanged states
and choices are coalesced. Disk checks and receiver admission still guard launch.
Identical recorded observations yield identical choices; this does not promise
identical worker output or completion timing. A corrupt or partial journal blocks
scheduling. Runs created before this journal existed cannot be replayed.

## Qualification

The [qualification record](evidence/distributed/qualification.md) maps the public
CLI two-host graph, repair, replay, recovery, invalid evidence and integration
checks to their retained evidence. The implementation contract is
[distributed workflows](DISTRIBUTED_DAG.md).
