# Remaining-work integration — 10 September 2026

This continuation implements local task/run evidence retention, the missing
recorded receiver/action/transport metrics, and separately admitted staged plans.
It also records a six-candidate planner pilot. **9E remains partially qualified**:
the pilot identifies an interface-conformance difference that disappears after a
post hoc protocol adaptation. It does not establish a planning benefit.

The implementation is committed locally as `d3e9813`.
The [evidence index](remaining-work-20260910.json) binds that commit,
source and runtime hashes, completed checks, retained run identities and raw
outputs. This extends the earlier [workstream integration](workstream-integration-20260910.md);
that earlier report describes its own source revision and limits.

## Qualified behavior

| Scope | What the operator can do | Evidence and limits |
| --- | --- | --- |
| Retention | Preview or apply local receiver/workflow evidence quotas; preserve audit windows and referenced/pinned evidence; expire eligible payloads with durable manifests while retaining submission identity | Fourteen focused cases on default and UBSan builds. Copied real accepted workflows preserve all six original receiver bindings, reuse six duplicate submissions, create zero new acceptances, preserve nonempty workspace bytes and disclose expired results. Workspaces are excluded from quotas; external claims require explicit pins. |
| Metrics | Export latest recorded receiver unknown states, explicit approval/cancel/resume counts, and consumed transport stdin plus captured stdout with known/unknown coverage | Seven focused cases, exporter version/bounds checks, partial-consumption C coverage, and reconciliation of two real workflows across native JSON, TSV and public JSON. Counts do not cover off-CLI human actions or historical unknown incidents; bytes are not network wire traffic. |
| Staged planning | Use a checked stage-1 finding to generate a separately compiled fixed stage-2 graph with at most eight admitted members | Both stages complete through the real public CLI for normal, empty, all-skipped, colliding-name and maximum-member cases; the normal case also passes under UBSan. Changed local findings and stale source are refused. Runtime graph expansion remains unsupported and is not required by these finite cases. |
| Planner evaluation, partial | Reproduce frozen finite candidate outputs and inspect independently bound public checker reports | Six candidates cover nine held-out cases and eighteen distinct incorrect artifacts. All six actual public outcomes match the direct checks. Fixed graph/local placement and a post-generation oracle correction limit interpretation; human correction, provider cost and calibration remain unmeasured. |

The [retention](../RETENTION.md), [metrics](../OBSERVABILITY.md) and
[staged-plan](../PLAN_STAGED.md) documents describe the commands and boundaries.
Statistics TSV version 3 exports JSON version 2; saved TSV version 2 retains its
JSON version 1 contract, and mixed comparisons identify each side's version.

## Real outcome checks

The preserved repair workflow is `run_ffa58353740fd6fea841`. Retention qualification
operates on two copies of its receiver/coordinator state, not its original home.
Before expiry and while pinned its result passes. After unpinning and eligible
expiry, the result explicitly reports `evidence_expired`. Original submission keys
still reconcile to the same six task IDs; changed specifications remain conflicts.
The copy check includes a nonempty uncommitted workspace sentinel.

That repair workflow records six attempts, 36 transport calls, one explicit resume,
zero latest receiver unknown states and 168,123 transport-process bytes. The UBSan
lost-ack workflow `run_cb2eb00b56e170330ac1` records two attempts, ten calls, one
resume, zero latest unknown states and 35,605 bytes. Independently summed saved
records agree with all three export paths. Zero latest unknown states does not erase
the fixture's earlier lost acknowledgement; historical incident counts are outside
this metric's scope.

Staged qualification retains twelve passing public stage results across six test
invocations. Every invocation separately compiles and explicitly admits both plans,
checks exact result membership, and refuses both a changed local finding and stale
manifest input before stage 2 can compile. The maximum case actually executes eight
members. The unit cases additionally reject malformed/duplicate JSON, bounds and
forged semantic bindings, including booleans presented as integer results.

## Planner measurement limits and remaining acceptance

The [pilot report](../../examples/planning/evaluation/README.md) gives the complete
descriptive counts and timings. Each contract-aware candidate accepts nine of nine
cases; each current candidate initially accepts zero because it uses named-file
input/output maps. The post hoc interface adapter makes all three current candidates
accept all nine. This diagnoses interface conformance, not semantic planner quality.
Every evaluation rejects the eighteen declared incorrect artifacts. Repetition of
those controls is not additional independent corruption coverage.

| Remaining 9E criterion | Available evidence | Missing evidence |
| --- | --- | --- |
| Compare decomposition, scheduling and placement separately | Deterministic inspection/comparison tools and complete structural explanations; the pilot fixes graph and placement | Matched admissible alternatives on a representative workload with critical path, resources, transfer, validation and rework measurements |
| Record candidate selection and external checks | Frozen candidate/plan/checker/source hashes, explicit compiler admission and independent public checks | A predeclared selection rule and decision record for a genuine comparison; labels alone are not a selection rationale |
| Measure held-out outcomes and costs by task class | Three cases each for finite feature, research and manifest classes; six frozen generations; declared corruptions; descriptive CPU/wall and public timings | Human correction records, provider cost, comparable estimate calibration, repeated-work analysis and independent repeated trials |

Only five generation durations were recorded; current-1 cannot be reconstructed.
The original held-out v1 hash case used fabricated hashes without bytes. V2 repaired
that unsatisfiable case after generation but before any candidate evaluation. Both
versions remain committed; v2 is not described as frozen before generation.
Public elapsed measurements ran alongside fleet qualification and cannot establish
isolated overhead or speedup. No additional planner complexity is justified by this
pilot. Completing the remaining measurements needs a fresh independently designed
comparison and actual workload/cost/correction records; no missing value is replaced
with a fabricated zero or inferred benefit.

## Verification and publication

Final acceptance status and exact hashes are recorded in the evidence index. The
completed checks include shell and fleet integration, focused native/UBSan tests,
statistics UI/export coverage, lint and the repository C quality gate. The C gate
allows existing advisories; passing it does not mean zero analyzer warnings.
The final fleet run uses unchanged runtime sources and binaries. During that run,
the existing retention, metrics and staged tests were added as fleet prerequisites
so CI also runs them; those new prerequisites passed separately through Make. This
is not represented as a single `make test-all` run.

The original H2 reviewer independently inspected retention/metrics/staged boundaries
and ran fourteen retention cases, seven metric cases, lint and whitespace checks.
It found no actionable implementation defect. A separate criterion mapping from the
same reviewer confirmed the remaining 9E gaps. The leader reviewed the integrated
changes, retained fixtures and evidence limits; review agreement is not substituted
for the recorded executions.

Integration is local on `codex/release-next`. Main and the local `origin/main`
tracking reference remain `2c307c82`; the remote was not fetched. No push, main merge,
release, operational-host enrollment or provider campaign occurred. Unrelated
`.gmcs/` and `output/` contents and existing worktrees are preserved. The full
selected roadmap assignment is not represented as complete while 9E remains open.
