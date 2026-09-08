# Hydra Roadmap

> - **Status:** canonical outstanding-work backlog
> - **Snapshot:** 8 September 2026
> - **Current release:** `v2.2.1` safety, trust, and terminal fixes
> - **Release planning:** versions are assigned from compatibility impact when backlog work is ready
> - **Related:** [README](../README.md) · [CHANGELOG](../CHANGELOG.md) ·
>   [Release policy](VERSIONING.md) · [Contracts](CONTRACTS.md) ·
>   [Release definition of done](#release-definition-of-done)

## Purpose

Hydra should let a user describe an objective, distribute the work, and receive an
integrated result checked against their requirements: a working feature, research
report, design, or another declared deliverable. The general pattern is decompose,
execute, compose, and verify. Evidence supports the deliverable; a decision or a
collection of successful subtasks does not replace it.

Keep that experience simple across local and trusted remote hosts, using existing
agents and ordinary tools. Submit exact inputs, disconnect, then inspect and collect
the result. The DAG and recovery machinery should explain execution without making
users design a distributed system for each objective.

The same task and workflow should run with any worker that satisfies its required
capabilities. Agent agnosticism does not imply identical provider features or
portable private conversation history. Hydra owns execution, recovery, and evidence;
provider interaction belongs behind explicit adapters.

This file contains only outstanding work and the policies that constrain it.
Implemented behavior belongs in the changelog and focused contract documentation;
completed roadmap items are removed rather than retained as checked history.

2.0.0 is the final version assigned in advance. After 2.0, work is selected from one
backlog and released when a coherent feature or meaningful change is ready. The
version number is chosen at release time from compatibility impact.

## Product and engineering guardrails

- The POSIX shell CLI remains the authoritative mutation path.
- C remains optional and must not be required for core session management.
- Hydra requires no cloud account, database, model router, provider-specific
  runtime, or always-on daemon. An active run may have a host-owned supervisor;
  unattended schedules and reboot recovery need an explicit execution owner.
- State remains inspectable with ordinary filesystem and shell tools.
- Git and tmux remain authorities for repository and terminal state. Preserve the
  current head contract while adding task identity above heads and instances;
  introduce another execution backend only for a demonstrated requirement.
- Native frontends delegate mutations to the shell CLI instead of duplicating policy.
- Public changes follow [VERSIONING.md](VERSIONING.md), including deprecation,
  migration, and rollback requirements. Replace internal interfaces in place;
  do not create competing state authorities or speculative compatibility layers.
- Mutating commands have bounded failure behavior and an explicit recovery story.
- Machine-readable interfaces use versioned success and error schemas.
- Repository configuration is never executed without an explicit trust decision.
- Hydra does not silently copy secrets, auto-merge without policy and approval, or
  present heuristic agent observations as authoritative state.
- Each execution has one authoritative owner. A disconnected client or expired
  lease is not permission to repeat uncertain work on another host. Submission
  deduplication does not make arbitrary external side effects exactly once.
- Declared outcomes, process liveness, provider observations, verification results,
  and approval remain separate. Claims and scopes are not operating-system isolation.
- Performance claims require reproducible measurements.

## Outstanding backlog

Items below have no assigned release number. When work is selected, define the
smallest coherent scope and its acceptance boundaries, then release it when ready.
Priority may change with observed use. The numbered priorities reuse fleet
transport and one workflow execution authority as coordination expands across hosts.

### Candidate features

Select these priorities in dependency order, with independently useful scope.
The implemented remote submission and collection interface is documented in
[Remote tasks](REMOTE_TASKS.md), with [qualification evidence](REMOTE_TASK_ACCEPTANCE.md).
Local objective planning is implemented; see the [planner recipe](PLANNER_RECIPE.md)
and [delivery qualification](evidence/plan-qualification.md). Remaining priorities
keep their original numbers so existing references remain meaningful. Item 9 is
placed ahead of item 6 following the planning and validation review; numbering is
not execution order. Completed item 5 is recorded under delivered behavior.

#### 2. Adapter conformance and headless execution: remaining live qualification

The workflow data, durable approval, retry, adapter-contract, and headless execution
implementation is documented in [Workflow data](WORKFLOW_DATA.md) and
[Agent contract](AGENT_CONTRACT.md). The [acceptance record](WORKFLOW_AGENT_ACCEPTANCE.md)
contains the local harness matrix, remote qualifications, and failure-test evidence.

- [ ] Complete Claude Code's remote shared task after native host sign-in: verify
      exact prompt delivery, recorded-session recall, and observed-process
      cancellation. Its local qualification has passed. Remote sign-in and
      qualification were explicitly deferred on 6 September 2026 until native host
      sign-in is available; they are not counted as passed.

- [ ] Complete Cursor Agent's live prompt, recorded-session recall, and cancellation
      checks after sign-in; its CLI probe and builtin conformance pass, but the
      current local CLI is unauthenticated.
- [ ] Qualify Antigravity and Cursor on the shared remote task once native host
      authentication is available. Antigravity's local result-byte preservation,
      recorded recall, and observed-process cancellation have passed.

Antigravity (`agy`), Cursor Agent (`cursor`), and OpenCode (`opencode`) now have
implemented interactive and headless profiles. See [supported agents](PROFILES.md)
for their exact capabilities and authentication boundaries.

Acceptance: retain each unqualified live-provider check until actual execution,
independent result checks, and observed-process cancellation are recorded on its
claimed host. The original Codex/Pi/OpenCode/plain remote task remains qualified;
fixture tests and local authentication do not close another provider's remote
requirement. Claude remains explicitly deferred rather than blocking the other
implemented profiles.

#### 9. Planning, node contracts, and verified outcomes

Prioritize this work before automatic host placement. The finite distributed DAG,
required validation joins, bounded whole-graph repair, and scheduling replay are
[implemented and qualified](evidence/distributed/qualification.md). Retain that
execution substrate and the existing public CLI; strengthen what is planned,
compiled, handed off, and accepted as a completed outcome.

Milestone IDs are stable references, not release numbers. Begin with **9A**, then
**9B–9D**. Start the evaluation baseline and explanations in **9E** alongside that
work; qualify its comparisons on the task pilots. Item **6** follows the contract
and outcome pilots, while **9F** is a later, workload-driven extension and does not
block basic load balancing. Relevant item **7** diagnostics can accompany each slice.

##### 9A. Satisfiable outcome obligations and compiler diagnostics

- [ ] Correct the reproduced orphan-check inconsistency: public plan validation
      currently admits a check with no assigned requirement, while its report must
      claim at least one requirement owned by that check. Reject this contradiction
      before execution without preventing legitimate multi-obligation checks. See
      the [report ownership rules](../src/fleet/plan/plan_report.c).
- [ ] Represent each mandatory outcome with an intent reference, exact subject,
      observable criterion, evaluation method, required evidence, applicable
      environment, completion rule, and limitations. Allow a requirement to have
      distinct behavior, failure-handling, performance, or other obligations.
- [ ] Check that every required obligation has a reachable, satisfiable evaluation
      path for the right candidate. Reject orphan or circular evidence dependencies
      and missing joins; distinguish structural proof, runtime obligations, and
      semantic judgments about whether criteria actually capture the objective.
- [ ] Preserve assumptions and unresolved questions explicitly. Give authors
      diagnostics with field paths, obligation IDs, and counterexamples; never
      silently weaken an objective or acceptance criterion to obtain a valid plan.

Acceptance: public CLI fixtures reject orphan checks and impossible evaluation
paths, while valid multiple-obligation cases compile. A changed performance objective
with unchanged text-content checks is identified by semantic review, not misreported
as something the structural compiler can prove. Start a fixed set of valid and
misleading plans across feature, performance, and research tasks for later milestones.

##### 9B. Producer and consumer contracts at every handoff

Depends on 9A's obligation model; reuse sealed artifacts and existing receipt checks.

- [ ] Add pinned producer output and consumer input schemas, cardinality, units,
      versions, identity/provenance requirements, and supported pre/postconditions.
      Begin with exact schema matching and a small supported predicate set; reject
      unsupported constraints rather than claiming arbitrary schema implication.
- [ ] Validate declared compatibility at compilation and actual values at
      materialization. Values that violate the declared contract, including empty
      objects, missing fields, wrong units/candidates, or missing required evidence,
      must stop the consumer before it executes.
- [ ] Distinguish data, evidence, effect/order, resource, and provenance relations in
      the planning representation. Lower ordering to the existing DAG; resource
      mutexes need not create arbitrary permanent ordering, and descriptive lineage
      links are not all execution prerequisites.
- [ ] Bind candidate manifests to the relevant collected commit/source tree,
      dependency/build inputs, artifacts, and configuration. Support relational
      checks across inputs and recheck invariants after composition; checking one
      declared file does not automatically validate the complete candidate.
- [ ] Use explicit validated conversion nodes for permitted transformations. Extend
      declared read/write and effect-conflict reasoning where useful, and label
      which constraints the executor actually enforces. Declarations alone do not
      establish isolation, determinism, or safe repetition of external effects.

Acceptance: negative handoff cases fail before consumer submission; compatible
cases pass through the public CLI. Include a structurally valid object with missing
semantic fields, a unit mismatch, stale candidate identity, a lossy conversion,
and individually valid components whose composition violates a shared invariant.

##### 9C. Structured evidence and validation of the validator

Depends on 9A–9B; extend the accepted report contract with explicit versioning.

- [ ] Bind each evidence record to its obligation, subject manifest, verifier
      identity/recipe, invocation, environment, observations, and hashed raw evidence.
      Preserve counts of executed, failed, and skipped cases, limitations, and any
      required reviewer decision. Retain prose as explanation, not as a replacement
      for required measurements or observations.
- [ ] For machine-checkable obligations, derive verdicts through trusted adapters
      from accepted predicates and sealed observations. Check the actual candidate
      and executed tests; provenance establishes identity, not semantic correctness,
      and logs still depend on the trustworthiness of their collection path.
- [ ] Qualify validators using relevant positive and negative controls: omitted work,
      stale subjects, missing measurements, dropped failures, inverted assertions,
      unsupported claims, and selected mutations. Treat mutation/coverage results
      as bounded evidence of test strength, not a universal completion score.
- [ ] Keep execution status, evidence validity, and domain outcome distinct in the
      result model and UI. Final acceptance inspects every mandatory obligation on
      the assembled candidate; successful leaf tasks cannot substitute for it.
- [ ] For assessments, retain the rubric, source/evidence locators, disagreement,
      and acceptance authority. Calibrate LLM judges on known cases and test ordering
      sensitivity where relevant; more judges, different models, or different hosts
      do not by themselves establish independent evidence.

Acceptance: a correctly bound narrative-only PASS cannot satisfy an obligation
requiring measurements. A missing, failed, or inconclusive verifier cannot become
success. Known wrong artifacts fail for their intended reasons, raw evidence can
be independently inspected or recomputed, and changed acceptance definitions cannot
silently inherit earlier verdicts. Existing subject/recipe integrity checks remain.

##### 9D. Task-specific acceptance through the public workflow

Depends on 9A–9C. Use one shared contract/evidence model with focused recipes; avoid
separate schedulers, universal ontologies, or a DAG node for every test assertion.

- [ ] **Features:** bind user-visible behavior and failure boundaries; run the public
      CLI/UI against the assembled candidate, check interfaces and regressions, and
      exercise interruption/recovery where relevant. Reuse existing harnesses and
      appropriate property/metamorphic checks; negative controls must expose missing
      behavior even when the process exits successfully.
- [ ] **Performance:** bind baseline, candidate, workload, environment/toolchain,
      units, warm-up, trial structure, failures/exclusions, analysis, and stopping
      rules. Preserve raw samples, respect measurement exclusivity, and use an
      uncertainty method appropriate to the metric and dependent observations.
      Distinguish target established, target not established, and invalid/insufficient
      measurement; repeated searching must not cherry-pick a favorable confirmation.
- [ ] **Research:** bind questions, source/data provenance, transformations, claim
      locations, competing explanations, methods, and limitations. Distinguish
      exploration from confirmation and invalid instrumentation from a substantive
      negative result. Use source audits, reproducible analyses, or proof checkers
      as appropriate; review whether formalized statements match the actual question.
- [ ] Make completion depend on the requested outcome. A valid negative experiment
      can complete an investigation without satisfying an optimization target or
      supporting a hypothesis. Repair invalid work, not a scientifically inconvenient
      answer; a new hypothesis or acceptance method needs a visible new version.

Acceptance: complete one real feature, one performance task, and one research
question through the public CLI with independent outcome checks. Include a feature
absent despite green superficial tests, insufficient/no-improvement performance
evidence, an unsupported research claim, and a valid negative research result.
Fixture transport success or agent agreement alone does not qualify these outcomes.

##### 9E. Explainable planning and measured plan quality

Start the baseline with 9A; use 9B–9D contracts and pilots to qualify the tools.

- [ ] Add reusable bounded decomposition patterns and explanations linking each
      node/dependency to an outcome, input, evidence need, or effect constraint.
      Keep small cohesive tasks small; the cost of planning and composition counts.
- [ ] Extend the existing CLI to inspect obligations, explain dependencies, compare
      candidate plans, test contract examples, and show invalidation after changes.
      Command names and schema details are selected during implementation, not
      promised here as existing interfaces.
- [ ] Separate decomposition quality, fixed-graph scheduling, and host placement.
      Compare admissible alternatives by time/cost to a verified result, critical
      path, resource/transfer requirements, validation effort, and rework risk.
      Preserve ranges and unknown estimates; do not invent calibrated probabilities
      from an agent's confidence or claim globally optimal arbitrary work plans.
- [ ] Keep the planning agent a candidate generator and external checks explicit.
      Record the reason for selected/rejected alternatives, compiler/policy versions,
      and the inputs to deterministic analysis. Estimates do not relax hard budgets.
- [ ] Maintain independently specified and held-out task cases plus known incorrect
      artifacts. Compare current and contract-aware planning under matched scope,
      tools, and budgets; separate effects of planning, validation, and scheduling.
      Measure false acceptance/rejection, verified outcomes, time/cost, human
      correction, repeated work, and estimate calibration by task class.

Acceptance: explain every required node and reproduce deterministic comparisons from
recorded inputs. Report stochastic variability and held-out outcome errors, not only
plan validity or throughput. Reject every declared deterministic corruption case;
qualify semantic improvements with observed results and explicit limits. Planning
benefit must exceed its overhead on the workload where improvement is claimed.

##### 9F. Selective repair and bounded graph expressiveness

Select after contract/evidence pilots demonstrate a useful workload. Preserve the
finite execution DAG, original coordinator, and unknown-outcome reconciliation.

- [ ] First support hierarchical patterns lowered into static graphs and staged
      experiments whose findings inform a separately compiled next plan. Add maps
      over admitted finite manifests and conditional branches only with explicit
      cardinality, skipped-output semantics, and fixed join membership.
- [ ] Qualify selective repair of affected dependency closures. Reuse an artifact
      only when its input, source, recipe, contract, environment, effect assumptions,
      and evidence dependencies remain valid. Current whole-plan check binding
      needs an explicit versioned reuse policy; merely skipping nodes is insufficient.
- [ ] Require fresh affected checks after repair. Different bytes do not prove a
      meaningful correction. Preserve valid unrelated work where qualified, but
      never treat an uncertain external effect or a rerun of an LLM as cached truth.
- [ ] If staged plans are insufficient, admit proposed expansions through the same
      compiler and authority checks. Bound total graph growth, work, artifact bytes,
      and repair attempts; version expansions and freeze joins before scheduling.
      Keep new scientific questions distinct from retries of an unchanged claim.

Acceptance: changes invalidate exactly the evidence covered by the declared dependency
model; unknown dependencies or unsupported effects make work ineligible for reuse.
Test changed inputs, contracts,
environments and acceptance rules, branch skips, empty/bounded maps, interrupted
repair, exhausted budgets, and unknown remote outcomes. No skipped branch may create
false completion, and no expansion may enlarge authority or duplicate uncertain work.

Research basis (8 September 2026): the review combined current Hydra code and bounded
probes with [CWL typed workflows](https://www.commonwl.org/v1.2/Workflow.html),
[LLM-Modulo's external critics](https://arxiv.org/html/2402.01817v2),
[test-oracle research](https://philmcminn.com/publications/barr2015.pdf),
[controlled performance evaluation](https://users.elis.ugent.be/~leeckhou/papers/oopsla07-stat.pdf),
[exploration versus confirmation](https://psychologicalsciences.unimelb.edu.au/__data/assets/pdf_file/0007/2888098/The-preregistration-revolution.pdf),
and [build-system reuse models](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems-final.pdf).
These motivate the milestones; they do not prove general agent-plan soundness,
measured Hydra optimization gains, or a need to adopt another runtime. Preserve
published plan/report schemas and durable records through explicit versioning.

#### 6. Load balancing across eligible hosts

Extend resource admission with automatic placement of **new, unassigned work**.
Explicit host pinning remains available. Implement this after assignment recovery
and receiver reservations are qualified, plus item 9's contract and outcome pilots
(9A–9D) and measured planning baseline (9E). It need not wait for selective repair
(9F) or dynamic task pools. Preserve measurement exclusivity and required validator
separation when choosing eligible hosts.

- [ ] Filter hosts by authorized destination, project mapping, platform/toolchain,
      adapter capabilities, resource requirements, freshness, and any validator
      separation requirement. An idle incompatible host is not eligible.
- [ ] Start with capacity-normalized reserved slots for comparable task classes,
      bounded queues, and stable tie-breaks. Then evaluate queue-delay estimates,
      measured task duration, and artifact-transfer cost as evidence warrants.
      CPU utilization alone is insufficient for agents waiting on remote APIs;
      account/provider limits shared across hosts need an explicit shared budget
      authority before Hydra claims to enforce them fleet-wide.
- [ ] Record candidate hosts, observations, ranking, policy version, and reservation
      outcome. Receiver admission remains final. Reconsider placement after a
      definitive refusal; an ambiguous dispatch remains assigned for reconciliation.
- [ ] Add queue aging or bounded fair sharing where workloads demonstrate starvation.
      Balance producer and validator demand so fan-out does not indefinitely delay
      validation. Measure useful completions rather than pursuing equal CPU usage.
- [ ] Consider rebalancing accepted but not-started work only with durable withdrawal
      that prevents its old receiver from starting it, followed by confirmed release
      and a new assignment generation. A lost withdrawal response blocks transfer.
      Do not migrate running agents or replay uncertain effects as load balancing.
- [ ] Before any broader reassignment, define enforced ownership generations and
      stale-update rejection at receivers, result admission, and promotion. External
      side effects require their own idempotency/fencing contract; lease expiry alone
      must not authorize duplicate execution.

Acceptance: heterogeneous-host trials compare explicit placement and simple balanced
placement using queue wait, time to verified result, throughput, transfer bytes, and
starvation. Record workload and observation traces; replay yields identical decisions.
Race concurrent submitters, stale capacity, node drain, lost reservation/withdrawal
responses, and late results. Admission limits and artifact bindings always hold;
performance improvement is claimed only where repeated measurements support it.

Research basis (6 September 2026): Kubernetes separates
[filtering, scoring, and reservation](https://kubernetes.io/docs/concepts/scheduling-eviction/scheduling-framework/).
[Dask scheduling](https://distributed.dask.org/en/latest/scheduling-policies.html)
considers worker load and data locality. Its
[transactional work stealing](https://distributed.dask.org/en/latest/work-stealing.html)
checks that work has not started before moving it, but still documents duplicate
execution risks during worker/network failures. Hydra should borrow the placement
ideas while retaining its stricter unknown-outcome boundary. These are design
recommendations; no Hydra balancing prototype or performance qualification exists.

#### 7. Run diagnostics and bounded retention

Explain what needs attention through existing CLI and TUI surfaces. Extend item
9's obligation/evidence explanations as those milestones land; useful diagnostics
need not wait for load balancing.

- [ ] Add effective configuration, "why waiting?", host/attempt timelines, artifact
      inventories, and explicit stale-observation labels. Include the unsatisfied
      contract/obligation, exact subject, missing evidence, and next useful action.
- [ ] Measure queue delay, time to verified result, unknown outcomes, recovery
      success, manual interventions, and transfer size. Make metric export optional
      and report provider usage only when available.
- [ ] Define retention and archive policies for submission keys, event metadata,
      logs, and artifacts without discarding evidence required by active recovery
      or accepted outcome claims. Preserve referenced raw evidence and contract
      versions for the declared audit/reuse window; disclose expired evidence.
- [ ] Add accessible event-announcer and comparison views over the same evidence.

Acceptance: an operator can identify a blocked task's owner, reason, and next action
without reading raw state files. Retention stays bounded while preserving active
recovery and the documented deduplication window.

#### 8. Dynamic task pools and schedules

Select this work only when real workloads need newly discovered tasks or persistent
queues that finite workflows cannot express cleanly. Build on item 9F's admitted
expansion and join semantics rather than introducing a second graph compiler.

- [ ] Add file-backed pools with one coordinator, unique task claims, bounded
      outstanding work, cancellation/retry budgets, and stale-owner rejection.
      Reuse task execution and admission rather than adding another scheduler.
      Seal bounded expansion manifests before scheduling; freeze join membership
      and cap graph growth, artifact bytes, and repair rounds.
- [ ] Start schedules through host timers invoking the same submission API. Define
      missed-run and duplicate-trigger behavior and the always-on owner needed for
      unattended scheduling; make reboot recovery an explicit opt-in contract.

Acceptance: duplicate triggers and competing workers do not create duplicate task
claims. Work growth remains bounded, cancellation propagates, and restart recovery
does not invent completion or replay uncertain actions.

## Simplification alongside feature work

- [ ] Keep spawn queues, workflow scheduling, and future pools on one admission and
      execution path. Avoid separate policy implementations in native frontends.
- [ ] Keep workflow syntax deliberately restricted. Use versioned fields and files
      for dataflow; make any structured-parser dependency an explicit toolchain
      decision rather than growing ad hoc YAML or shell interpolation.
- [ ] Present historical heads as inspectable history with explicit restore, not as
      live workers. Preserve the dashboard and shell-only TUI; expand views only for
      a concrete operator need rather than requiring decorative frontend parity.

There is no usage evidence yet to justify deleting a major shipped feature. Review
actual workflows and maintenance cost before proposing removal. Revisit storage or
implementation language only when required atomicity, bounded recovery, or measured
workload performance cannot be satisfied by the current design.

## Conditional extensions and exclusions

Each extension needs a bounded use case and acceptance proof before entering the
prioritized backlog:

- explicit dirty-source snapshots, including selected untracked and binary files,
  without silently committing or altering the caller's branch;
- optional isolated execution profiles, prioritized earlier if untrusted code is
  required; advisory scopes alone do not provide isolation;
- ACP session adapters after pinning a protocol version and proving interoperability;
- a narrow MCP interface over the public CLI for authorized task/context access;
- A2A adapters for independently operated agent services when SSH fleet is insufficient;
- bounded best-of-N recipes over existing tasks, gates, and integration primitives;
  reviewed decomposition patterns are now scoped in item 9E.

Provider cognition dashboards, exact cross-provider context/cost routing, automatic
unreviewed decomposition, dirty-file shadow synchronization, Git-as-consensus
registries, natural-language command parsing, and arbitrary plugin marketplaces are
outside planned scope. Multi-user federation and automatic coordinator failover
require a separate ownership/authentication design and demonstrated demand.

## Release definition of done

Every release satisfies the applicable items below; unrelated backlog work is not
pulled into the release.

- [ ] Scope and compatibility impact are explicit.
- [ ] Shell-only behavior remains functional unless the release deliberately changes
      a documented contract and provides migration.
- [ ] Native and shell parity is proven for every accelerated command.
- [ ] Failure, cancellation, interruption, and recovery behavior is tested.
- [ ] Security and trust changes are documented and tested.
- [ ] Performance claims cite reproducible measurements.
- [ ] Applicable install, upgrade, uninstall, packaging, and source workflows pass.
- [ ] Documentation, CLI help, completions, and examples agree.
- [ ] One end-to-end scenario preserves its exact commands and resulting evidence.
- [ ] Hosted checks, tags, artifacts, and the release object resolve to the same
      qualified commit.

Passing unit tests alone is supporting evidence, not release acceptance.

## Delivered behavior

Delivered work is intentionally absent from this roadmap. Use these records instead:

- [CHANGELOG.md](../CHANGELOG.md) for shipped features and compatibility changes;
- [CONTRACTS.md](CONTRACTS.md), [STATE.md](STATE.md), [EVENTS.md](EVENTS.md), and
  [AUTOMATION.md](AUTOMATION.md) for current local interfaces;
- [FLEET.md](FLEET.md) and [FLEET_ACCEPTANCE.md](FLEET_ACCEPTANCE.md) for the
  implemented fleet capability and historical pilot qualification;
- [REMOTE_TASKS.md](REMOTE_TASKS.md) and [REMOTE_TASK_ACCEPTANCE.md](REMOTE_TASK_ACCEPTANCE.md)
  for remote submission, disconnected execution, verified collection, and qualification;
- [workflows.md](workflows.md) for workflow and integration behavior;
- [DISTRIBUTED_DAG.md](DISTRIBUTED_DAG.md), [WORKFLOW_TASKS.md](WORKFLOW_TASKS.md),
  and [two-host qualification](evidence/distributed/qualification.md) for completed
  item 5: finite distributed execution, validation joins, bounded repair and replay;
- [NATIVE_CORE.md](NATIVE_CORE.md) and [NATIVE_TUI.md](NATIVE_TUI.md) for optional
  native behavior;
- [SECURITY.md](SECURITY.md) for current trust boundaries.
