# Distributed workflow implementation contract

Status: implementation in progress for [roadmap item 5](ROADMAP.md). This document
defines the intended extension; it does not claim that remote DAG execution is
available. Existing workflow schema 1, compiled plans, task protocol 1 and their
persisted records remain published contracts.

## Execution and authority

Extend the existing finite workflow/planner path. Keep shell responsible for
process orchestration and use the native helper for structured distributed state,
package derivation, validation and scheduling decisions. Do not introduce a second
user-facing scheduler or require a background service.

One run has one coordinator on its original host. Before any dispatch, publish the
accepted graph, source and root-input digests, policy, explicit destinations,
recipes, validation definitions and attempt bounds. A remote execution belongs to
one graph step and one attempt. Persist its exact package and submission key
before sending it, then bind the returned receipt before consuming observations.
Changing a registered alias's endpoint must not silently move existing work:
record and verify the transport destination as well as the alias.

Downstream packages cannot contain future output bytes at initial admission.
Admission therefore binds the derivation recipe and permitted producer references.
Once those producers have verified results, derive the package from their sealed
artifacts, persist its exact digest and bytes, and only then dispatch. Derivation
may fill declared artifact inputs and source commits; it may not change the host,
work recipe, capabilities, acceptance definition or authorized effects.

The coordinator owns scheduling. Worker, validator and composer are execution
roles, not pure functions or separate services. An evidence join is a deterministic
coordinator decision requiring all declared checks. Its ordering uses the recorded
dependency graph and stable source-order tie breaks. Wall-clock deadlines and
remote observations must be recorded inputs to decisions; replay promises the
same decisions for those inputs, not identical outputs or execution timing.

## Recovery boundary

Persist each mutation atomically under exclusive run ownership. A second
coordinator invocation must not dispatch while the first owns the run. Recovery
revalidates the accepted definition, packages, receipts and collected artifacts.

| Recorded boundary | Next permitted action |
| --- | --- |
| No package yet | Resolve verified inputs and persist the derived package |
| Package and key persisted, receipt absent | Submit that exact package and key to the original destination to reconcile acceptance |
| Receipt bound, remote work pending | Observe the same task; do not allocate another execution |
| Remote process succeeded | Retrieve and independently verify its result and bindings |
| Collection verified | Apply required validation before releasing gated dependents |
| Transport unavailable or outcome unknown | Retain identity and wait for reconciliation or explicit recovery |

Lost acknowledgements do not consume execution retries. An execution retry is a
new, separately recorded attempt after authoritative failure and only where the
accepted policy permits replay. Semantic repair is a new candidate produced from
recorded failure evidence within the accepted repair budget. Neither mechanism
may replay an unresolved execution or reuse a verdict for changed bytes.

Deduplication is scoped to the receiving host's task store. Coordinator failover,
cross-host exactly-once execution and automatic reassignment of unresolved work
are outside this implementation.

## Artifacts and validation

Use existing fleet result verification and collection before downstream transfer.
Bind file handoffs to type, size and SHA-256; bind code handoffs to verified Git
objects and their collected provenance. Transfer through the coordinator initially.
Mutable branch names and a worker's claimed successful exit are not artifact
identity. Verify sealed bytes again when constructing a consumer package.

Distributed validation reports must have a versioned contract containing the
subject digest, validator-definition digest, covered requirement IDs, evidence and
a verdict of PASS, FAIL or INCONCLUSIVE. The definition digest covers the accepted
check recipe, rubric/criteria and source/input bindings needed to run that check.
Canonical encoding must make object member order irrelevant. Preserve the existing
published version-1 planning report contract when adding the distributed format.

Only PASS for every required check permits a gated transition. Missing reports,
invalid reports, mismatched digests, missing requirement coverage and validator
execution errors cannot be interpreted as PASS. Preserve negative and inconclusive
evidence for diagnosis and bounded repair rather than discarding it as a parse
failure. Agreement between agents does not replace declared acceptance checks.

Run acceptance recipes from their bound trusted source, with candidate bytes
provided separately. A producer's changes must not replace the acceptance harness.
Separate heads or sessions do not isolate processes running under the same OS
identity; the contract does not claim protection against a malicious same-user
process or against inadequately specified checks.

Composition creates a new artifact and therefore needs its own verification.
Code promotion then uses the existing candidate-bound approval and integration
path, including rechecking the target ref. No distributed verdict authorizes an
unreviewed change of integration target or an automatic push.

## Required qualification

Exercise the public CLI with two distinct receiving hosts: fan out producers,
validate their exact outputs independently, join evidence, assemble a candidate,
validate that candidate and use the bound integration path. A local transport
fixture can establish protocol behavior but cannot stand in for two-host evidence.

Inject coordinator termination before and after dispatch/receipt persistence and
drop an acceptance response after the receiver commits it. Resume must recover the
original task IDs, and receiver evidence must show no duplicate work. Test changed
alias mappings, changed packages and corrupt receipts as recovery failures.

Inject corrupt artifacts, stale subject and validator digests, negative and
inconclusive verdicts, missing requirements, validator crashes, producer changes
to check scripts, exhausted repairs and a moved integration target. Each must
block the affected transition or promotion. Replay recorded observation sequences
and compare scheduling decisions. Retain command results and identify unavailable
external checks explicitly; keep roadmap acceptance open until proved.
