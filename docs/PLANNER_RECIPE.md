# Author, compile and deliver an objective

The planner is an ordinary agent using the public CLI. Hydra does not call a
model during validation or compilation. Start with `hydra workflow plan schema`;
it returns the JSON Schema, including policy and verification-report definitions.
Planning requires the optional native helper (`make build-fleet`). Existing
schema-1 workflows and the shell-only core keep their existing interfaces.

## Planning protocol

1. Read the objective and the operator-supplied policy. Write explicit criteria,
   final deliverables, assumptions, and any unresolved material questions. Do not
   invent authorization or remove requirements to make validation pass.
2. Choose a useful decomposition. A final deliverable must have a `compose` step;
   each requirement must link to a check of that exact deliverable. Work and
   verification may use different heads. Prefer independent executable tests for
   code and a separate assessment for research or design. A passing agent exit is
   not a verification verdict.
3. Write a bounded JSON document (256 KiB, at most 64 steps). Use local `spawn`
   and `exec` recipes. Each head must be created in the plan; existing heads are
   rejected at admission. Retries and repair budgets are zero in this version.
4. Declare named inputs and outputs using [Workflow data](WORKFLOW_DATA.md).
   Artifact consumers must directly depend on their producer. `prompt_input`
   names a declared input. For profile execution, `result_file` must name a
   declared output whose `path` is the same name. Profile text is written
   verbatim; some adapters concatenate text emissions. For a structured result,
   require one JSON object with no progress prose or Markdown fences, placing
   findings inside its evidence field.
5. Use source-relative files for `context`; snapshot external references first.
   Keep executable scripts and worker prompts in the source repository, commit
   them, then compile from that clean source. Heads start at its HEAD. Keep the
   planning document, policy and compiled output outside the source checkout when
   changing them would otherwise change its fingerprint.
6. Run `hydra workflow plan validate plan.json policy.json`. Read `diagnostics`
   and `coverage`; revise the proposal and repeat. Unknown fields, missing
   coverage, cycles, write conflicts, unsafe handoffs, unsupported capabilities,
   unresolved questions and over-budget or unauthorized scope fail validation.
7. Run `hydra workflow plan compile plan.json policy.json compiled.json`. The
   destination must be new. Identical explicit inputs produce identical bytes;
   object member ordering does not affect the result. Source content, declared
   input hashes, context files, adapter definitions, policy and compiler version
   are bound into the artifact.
8. Review `hydra workflow plan show compiled.json` (or `--json`). Acceptance is
   `hydra workflow plan run compiled.json --accept <sha256>`, using the digest
   reported by compilation or preview. Obtain operator acceptance when existing
   authorization does not cover the exact plan. An agent-written policy file is
   not itself an authorization grant.
9. Consume the run ID. Use the existing `hydra workflow status <run-id> --json`,
   `cancel`, and `resume` commands. Retrieve completed, reverified artifacts with
   `hydra workflow plan result <run-id>`. Inspect the actual final output against
   the original objective; do not substitute child completion or a receipt for
   that assessment.

## Plan and policy envelopes

Both declare `hosts`, `tools`, `effects`, `writes`, `parallelism`,
`timeout_seconds`, `artifact_bytes`, `max_heads`, `disk_mb`, `retry_budget` and
`repair_budget`. The plan must fit the supplied policy. `hosts` is `["local"]`;
effects are `worktree` and `execute`. Tools name the executable in `argv[0]` or
`profile:<name>`. Declared writes use `head:path` or `head:*`; overlapping writes
need dependency ordering. The policy may allow a parent directory or all paths
in a named head. These declarations are reviewable scope, **not OS isolation**:
review scripts, agent permissions, and any tools they call before accepting.

The wall-time budget includes scheduling and execution. The compiler also checks
the sum of exec timeouts, all input/output size limits, and head count. Artifact
limits cover declared root inputs, outputs and context snapshots; they are not a
limit on arbitrary repository writes or process output. `disk_mb` is the existing
runner's free-space floor; the plan must meet or exceed the policy's floor.
No operation is automatically replayed.

`argv` must fit the lossless subset of the published workflow YAML reader. Empty
arguments, control characters and YAML constructs are rejected with
`unsupported_argument`. Put complex shell code in a committed script and invoke
it with an ordinary argument list. The compiler does not infer a script's hidden
dependencies: declare inputs and keep recipes in the bound repository.

## Verification reports and final delivery

Each check has a `method` (`executable` or `assessment`), a human-readable
`definition`, a `verify` step, an input bound to its composed deliverable, and an
object output named by `report`. A verify step declares no repository writes.
An executable check uses an argv recipe. An assessment may use an independent
agent with a rubric and the sealed artifact. Human judgment requires a separate
authorized stage in this version.

The report format is:

```json
{
  "schema_version": 1,
  "verdict": "pass",
  "subject_sha256": "<SHA-256 of the exact delivered input bytes>",
  "requirements": ["requirement-id"],
  "evidence": "What was actually checked, including limitations"
}
```

Use `fail` for a negative assessment. Final success requires every declared
deliverable, every check's passing report, coverage of its linked requirements,
matching subject hashes, and intact sealed artifacts. A zero exit with a
negative or malformed report fails the run. Artifacts and reports are retained
under the existing run directory; `plan result` returns their paths and digests.

Coverage proves explicit traceability, not that the decomposition or rubric is
sufficient. Check the delivered feature in its integrated environment; read a
research report and examine its supporting evidence. When findings require new
work, author and accept a new static plan. There is no dynamic DAG expansion,
automatic repair, remote placement, load balancing or failover in this version.
