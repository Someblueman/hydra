# From objective to executable work

For offline node/dependency explanations, bound estimates and deterministic
candidate comparisons, see [Plan inspection](PLAN_INSPECTION.md).

Status: local plans and schema-2 distributed execution, required validation,
source handoff, bounded repair and scheduling replay implemented, 8 September 2026.
The public `hydra workflow plan` interface provides schema discovery, validation,
compilation, preview, accepted execution and verified result retrieval. See the
[planner recipe](PLANNER_RECIPE.md) for runnable commands and the
[qualification evidence](evidence/plan-qualification.md) for delivery checks.
See [distributed task plans](WORKFLOW_TASKS.md) for explicit placement,
intermediate validation, derived Git source and bounded candidate repair. Existing
[static workflows](workflows.md) and
[workflow data](WORKFLOW_DATA.md) retain their published contracts.

## Product contract

An objective should end in its requested deliverable: working code, a design,
research report, or another explicit artifact. Decompose, execute, compose, and
verify is the reusable pattern. Planning decisions and evidence help produce that
result; they are not substitutes for delivery.

An agent is suited to interpreting intent and proposing useful work. A deterministic
compiler is suited to checking the resulting explicit plan. Do not describe natural
language as deterministically compilable into a correct decomposition. Different
agents may propose different sound approaches, and a structurally valid plan may
still misunderstand the objective.

## Interface: a declarative plan through the CLI

Use a bounded, versioned JSON planning document as the initial authoring format.
It is a small declarative DSL, with a published schema, not a programming language.
An agent writes the document and uses the public CLI to inspect and validate it.
Humans may edit the same document. Do not build a separate graph-construction RPC
for each node or a provider-specific planner into the execution engine.

Existing workflow schema 1 is a restricted YAML execution format. Preserve that
published interface. JSON is already used for workflow data and native bounded
parsing. Lower planning documents into existing workflow operations where possible;
add explicit versioned capabilities for new operations. Do not silently strip
unsupported fields or implement a second scheduler behind the planning format.

The implemented local interaction is:

```text
agent reads objective, context, and plan schema
    -> writes plan.json
CLI validates plan.json -> structured diagnostics and requirement coverage
agent revises plan.json or asks about a material unresolved requirement
CLI compiles plan.json -> immutable resolved plan and readable preview
user or previously authorized policy accepts the exact execution scope
runner executes the accepted compiled plan -> final deliverable and checks
```

Agents can still author schema-1 workflows and use `hydra workflow validate`,
`dry-run`, `run`, and `status --json`. The additional `workflow plan` commands are
`schema`, `validate <plan> <policy>`, `compile <plan> <policy> <new-output>`,
`show <compiled> [--json]`, `run <compiled> --accept <sha256>` and
`result <run-id>`. The planner recipe teaches this protocol without becoming
another authorization source. The optional native helper is required.

## Minimum planning contract

| Part | Information the agent must make explicit |
| --- | --- |
| Objective | Requested outcome, context references, assumptions and open questions |
| Deliverables | Stable IDs, artifact types, required final outputs and destination |
| Requirements | Stable IDs, acceptance criteria, evaluation method and deliverable links |
| Work | Step IDs, dependencies, inputs, outputs, worker/tool recipe and write scope |
| Composition | How contributions become the final feature, design, report or other result |
| Verification | Exact artifact under evaluation, check definition, required verdict/evidence |
| Execution envelope | Allowed hosts/tools/effects, parallelism, time and artifact limits, retry/repair budgets |

### Outcome obligations (9A)

Schema 1 and schema 2 remain the published plan versions. 9A is an additive
closed extension: a plan may include a root `obligations` array without changing
the plan or compiled-artifact version. Existing plans and compiled artifacts do
not acquire invented obligations; the CLI exposes a clearly marked derived
legacy projection instead. An explicit obligation is bound into the canonical
plan and therefore into the compiled SHA-256 and acceptance binding.

Each explicit record has a unique `id`, `requirement`, `intent_ref` (`objective`
or a declared `context:<path>`), exact `subject` (`deliverable`, `step`, and
`output`), observable `criterion`, `evaluation` (`method` and `check`),
`required_evidence`, envelope-scoped `environment`, a bounded completion rule
(`pass` or `verdict=pass` in the current workflow), and `limitations`. Multiple
obligations may reference one requirement. Required report evidence includes
the exact subject digest, verdict, and evidence explanation; an obligation ID in
`required_evidence` is an explicit join and joins must be acyclic.

The compiler proves only structural satisfiability: the exact candidate output
exists, the selected check produces the required report, and the verifier is
reachable from the subject through declared dependencies. Orphan checks,
orphan obligations, missing joins, wrong subjects, unreachable validators and
circular evidence are rejected with a field path, obligation ID, and bounded
counterexample. Structural proof is distinct from runtime evidence and from a
semantic judgment that the criterion captures the objective. Performance and
research domains, or objectives that introduce performance language, remain
`semantic_review_required`; a text/content check is never reported as a proven
performance result.

Use `hydra workflow plan obligations <compiled.json> --json` for the small,
read-only projection consumed by operator views. It reports structural status,
semantic-review status, exact subject, evaluation and missing/required evidence
without becoming another mutation or evidence authority.

Some criteria need executable tests; others require evidence assessment or explicit
human judgment. Record that distinction. A design review can be an intermediate
output, but a feature objective must still reach implementation and whole-feature
verification. Do not infer success from a reviewer narrative or provider exit alone.

Require declared writes to shared mutable targets to be ordered or rejected.
Separate workspaces may overlap file paths legitimately; composition must then have
an explicit conflict policy. Write declarations support checks, not OS isolation.
The existing runner is repository-based. Initially research and design workflows
can use a repository as their workspace and emit file artifacts; repository-free
execution would require a separate deliberate runtime contract.

## Deterministic compilation stages

1. **Parse and normalize.** Reject unknown fields, invalid types, duplicate IDs and
   excess size. Resolve supplied references and use deterministic ordering.
2. **Check coverage.** Every mandatory requirement maps to a final deliverable and
   evaluation; every required final output has a producing path. Report unresolved
   material questions. Coverage is traceability, not proof of adequate interpretation.
3. **Build the graph.** Check cycles, dependencies, artifact producers, types and
   bounds. Ensure final composition and verification are connected to their inputs.
4. **Check execution policy.** Validate permitted effects, write conflicts, host/tool
   constraints and bounded resources. Do not accept an agent's `idempotent: true`
   as proof that arbitrary side effects are safe to repeat. Apply trusted operation
   policy; uncertainty must not create automatic retries.
5. **Lower and bind.** Emit a versioned resolved artifact containing execution steps,
   requirement/check mappings, pinned source and input digests, policy and compiler
   version. Existing runtime files are projections of this artifact, not independent
   editable authorities. Reject unimplemented capabilities before dispatch.
6. **Preview.** Show deliverables, coverage, work placement or eligibility constraints,
   effects, budgets and approval boundaries. Execution consumes exactly those bindings.

Compilation never calls a model or launches work. If capability observations affect
resolution, supply and record them as inputs; recheck admission at dispatch. Later
load balancing can choose a host from recorded eligibility constraints without
pretending real-time host choice is fixed at compile time. Reproducibility means
identical explicit inputs and compiler version produce identical resolved artifacts.

An edited objective, recipe, source, input, or accepted plan requires a new bound
artifact. Reuse completed work only when its actual dependencies, evidence and policy
remain valid. Compilation does not import trust or grant publication permission.

## Staged work when the graph is not yet knowable

For a feature with unresolved architecture, first execute a bounded design/prototype
stage. Its outputs inform the next implementation plan. For research, a first pass
may expose a question requiring an experiment. Both can use another static plan
before runtime graph expansion exists.

Later, a planning step may emit a bounded proposed subgraph. Treat it as data until
it passes the same compiler and authorization checks. Record a new expansion version,
retain previous evidence, freeze membership before joins, and enforce total work and
repair limits. Worker suggestions cannot expand destinations or permissions. Stop
with a clear unresolved question when the budget or authority is insufficient.

## Build order and evidence of completion

| Slice | What becomes possible | Required evidence |
| --- | --- | --- |
| 1. Contract and diagnostics | Agent authors a plan and receives useful errors | Valid feature/research examples; missing criteria, unknown fields and duplicate IDs rejected |
| 2. Compiler and preview | Plan becomes a stable local execution artifact | Repeatable compilation; cycle, handoff, conflict and unsupported-operation failures |
| 3. Bound local execution | Approved plan produces its final deliverable | Feature integrates and passes checks; research report is produced and assessed; stale plans refused |
| 4. Distributed execution and admission | Same model runs across explicit hosts | Original task reconciliation, exact artifact transfer, capacity races and whole-result verification |
| 5. Load balancing | Eligible new work is placed according to observed capacity | Stable decisions over recorded observations, receiver limits, fairness and measured benefit |
| 6. Bounded replanning | Findings lead to checked follow-up work | Expansion limits, fresh authorization where needed, fixed joins and preserved completed evidence |

For the planner itself, evaluate whether it captures requirements, identifies material
ambiguities, uses valid handoffs, and reaches the final deliverable. Keep those
semantic assessments separate from deterministic compiler tests. Test the complete
public path with at least one feature and one research objective; a schema-only demo
cannot qualify objective delivery.

## Explicit terminal mode (T2, unreleased)

Schema 1 spawn recipes accept an optional `args.terminal_mode` with values
`interactive` and `headless`. An omitted field preserves interactive spawn and
the prior compiled representation. Explicit headless recipes lower to the existing
workflow spawn operation with `--headless --no-agent`; following command or adapter
exec steps operate on that terminal-free head. The mode is part of the accepted
compiled digest, normalized workflow, and runtime graph binding. Changing it after
acceptance requires a new compilation and digest. Unknown modes or mode fields on
non-spawn recipes are rejected.

Schema 2 task recipes use the detached receiver's headless execution. Their task
specifications may declare `["exec", "execution-headless"]` to require T2 receivers;
`["exec"]` remains readable with its existing digest. All source, artifact,
placement, tool, and budget bindings remain enforced. A headless adapter in a
local schema 1 plan remains an explicit profile exec recipe; remote adapter
execution uses the existing workflow-task adapter contract.
