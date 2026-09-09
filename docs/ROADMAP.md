# Hydra Roadmap

> - **Status:** canonical outstanding-work backlog
> - **Snapshot:** 8 September 2026
> - **Current release:** `v2.3.0` distributed workflows and resource admission
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
  current interactive head contract while separating terminal attachment from
  headless execution. The tmux-optional milestones below extend the existing task
  owner; they do not introduce another scheduler or remove interactive sessions.
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
The observation milestones in priority 7 can start against current remote tasks
before tmux removal; they build on existing distributed execution and do not wait
for load balancing.
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

### Feature-branch integration — 9 September 2026

The `codex/hydra-next-wave` branch integrates T1 terminal-independent execution,
9A outcome obligations, and V1 remote visibility, including their C quality fixes.
This is local implementation status, not publication or full distributed/provider
qualification. The acceptance requirements below remain the delivery contract;
remaining qualification and integration evidence are recorded separately.

Next implementation wave: **T2**, **9B**, and **H1**. These extend the integrated
terminal, obligation, and fleet contracts independently. V2 remains the next
visibility slice; T3 and H2 follow their stated dependencies. This sequencing does
not authorize live provider campaigns, host enrollment, or publication implicitly.

Research for follow-on decisions: [interactive parity](research/interactive-parity-report.md)
and its [decision brief](research/interactive-parity-decision-brief.md), plus
[host discovery and onboarding](research/host-discovery-onboarding-report.md)
and its [decision brief](research/host-discovery-onboarding-decision-brief.md).
The reports describe the audited baseline and retain their research-date context;
the H track below incorporates the authorized onboarding recommendations.

#### T. Optional tmux for headless and remote execution

Decision: make tmux optional for headless execution, retaining it for interactive
heads. This is planned behavior, not a change to current dependency requirements.
See [the updated analysis](research/tmux-optional-execution.md) for the current
implementation, affected contracts, and design tradeoffs. Resource admission and
finite distributed execution are implemented; T1 and T2 extend that execution to
hosts without tmux, and T3 qualifies the same distributed scenario in that mode.
These are delivery milestones, not assigned release versions.

- [ ] **T1 — Terminal-independent workspace and execution identity.** Separate
      workspace, trust, identity, and provenance creation from terminal launch in
      the shell mutation path. Support a headless execution with no terminal;
      preserve existing interactive spawn/attach behavior. Define the durable
      representation and compatibility treatment before changing session fields.
      Update lifecycle, cleanup, result readers, and CLI/TUI observations together;
      an absent terminal must not mean a dead worker. Keep cancellation distinct
      from workspace deletion and preserve dirty work.
      Acceptance: a local command and headless adapter execute and produce verified
      artifacts without invoking tmux; status and teardown work for both execution
      modes. Existing state and interactive workflows remain readable and usable,
      with migration/rollback checks where the durable contract changes.
- [ ] **T2 — Remote execution and planning without tmux.** Route remote exec and
      headless workflow steps through T1 and the existing detached task owner.
      Make admission, bootstrap, doctor, installation, and capability negotiation
      require tmux only for terminal operations. Extend plan schema/validation and
      lowering explicitly; preserve published spawn semantics and existing compiled
      artifact bindings. Unsupported remote or terminal capabilities fail before
      launch, rather than silently changing the requested execution mode.
      Acceptance: on hosts without tmux installed, submit a command and an available
      authenticated headless adapter, disconnect, reconnect, collect exact outputs,
      and consume them in a dependent step. Run the corresponding local compiled
      plan through public interfaces. Exercise duplicate submission, lost start
      response, owner death, cancellation, and bounded logs; uncertain work is never
      replayed and retained reservations are not released by terminal absence.
- [ ] **T3 — Distributed qualification and operator access.** Use T2 for the
      implemented two-host fan-out, validation, join, composition, and final check
      described in [Distributed DAGs](DISTRIBUTED_DAG.md). Reuse
      priority 7's run/step/attempt visibility through CLI/TUI without requiring attach.
      Interactive terminal access remains an explicit capability; viewing logs is
      not attachment to a headless process's stdin.
      Acceptance: the complete distributed scenario succeeds with tmux absent on
      execution hosts. Coordinator restart and lost acknowledgments preserve task
      identity; stale results cannot advance dependents. Re-run interactive spawn,
      attach, messaging, transcript, and teardown regressions with tmux installed.

Host service management is a conditional extension of these milestones. Add systemd
or launchd integration only for an explicit logout/reboot recovery requirement,
reusing the same owner and reconciliation records. Qualify those failure boundaries
separately; service restart must not replay uncertain work. No permanent daemon,
replacement terminal multiplexer, automatic failover, or isolation guarantee is
required to complete T1–T3.

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

- [x] Add pinned producer output and consumer input schemas, cardinality, units,
      versions, identity/provenance requirements, and supported pre/postconditions.
      Begin with exact schema matching and a small supported predicate set; reject
      unsupported constraints rather than claiming arbitrary schema implication.
- [x] Validate declared compatibility at compilation and actual values at
      materialization. Values that violate the declared contract, including empty
      objects, missing fields, wrong units/candidates, or missing required evidence,
      must stop the consumer before it executes.
- [x] Distinguish data, evidence, effect/order, resource, and provenance relations in
      the planning representation. Lower ordering to the existing DAG; resource
      mutexes need not create arbitrary permanent ordering, and descriptive lineage
      links are not all execution prerequisites.
- [x] Bind candidate manifests to the relevant collected commit/source tree,
      dependency/build inputs, artifacts, and configuration. Support relational
      checks across inputs and recheck invariants after composition; checking one
      declared file does not automatically validate the complete candidate.
- [x] Use explicit validated conversion nodes for permitted transformations. Extend
      declared read/write and effect-conflict reasoning where useful, and label
      which constraints the executor actually enforces. Declarations alone do not
      establish isolation, determinism, or safe repetition of external effects.

Acceptance: negative handoff cases fail before consumer submission; compatible
cases pass through the public CLI. Include a structurally valid object with missing
semantic fields, a unit mismatch, stale candidate identity, a lossy conversion,
and individually valid components whose composition violates a shared invariant.

Implementation: [bounded data-schema-2 contracts](HANDOFF_CONTRACTS.md) preserve
legacy compiled acceptance bindings. Exact schemas, required fields/units,
pre/postconditions, lossless integer conversions and candidate manifests are
checked through existing sealed-artifact and receipt boundaries. Typed execution
relations must match existing DAG dependencies; resource/provenance annotations
are descriptive and unsupported mutex enforcement is rejected. Local acceptance
and its explicit limits are recorded in [9B evidence](evidence/handoff-contracts-9b.md).
External-host/live-provider qualification remains separate; no arbitrary schema
implication, isolation or semantic correctness is claimed.

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

#### 10. Performance baselines, idle efficiency, and change locality

Begin measurement alongside item 9, before changing refresh or scheduling policy;
this is not blocked by 9F or load balancing. Item 9D concerns performance tasks
executed by Hydra; this milestone measures Hydra itself. Feed fleet overhead and
contention evidence into item 6 and expose useful counters through item 7.
See [measurement design and research](research/performance-baselines.md).

Target: **with no changes, Hydra should do almost no work; with one changed
worktree, work should be mostly confined to that worktree.** This is an acceptance
objective, not a claim about the current periodic snapshot implementation.

- [ ] Establish repeatable baselines at **1, 10, and 50 real worktrees with active
      agents**. Separate quiescent, one-changing, all-changing, and sustained-output
      cases; distinguish deterministic agent stand-ins from authenticated live-agent
      trials. Extend existing benchmark/PTY infrastructure rather than introducing
      a separate performance service. Retain existing 5/20/100-head checks for
      their narrower scope until explicitly replaced with equivalent coverage.
- [ ] Measure **idle cost** after settling: CPU time per second, clearly normalized
      CPU percentage, wakeups per second, and subprocess launches per minute across
      Hydra and its helpers. Attribute agent, tmux, and observer overhead separately;
      also record scans, redraws, filesystem operations, and remote requests.
- [ ] Measure **interaction latency** from scheduled input injection to the matching
      rendered response: median, p95, p99 where supported by sample count, observed
      maximum, and missed deadlines. Include navigation, search, resize, and cancel
      under refresh and output load. Distinguish PTY frame completion from visible
      terminal presentation; do not label the former input-to-pixel latency.
- [ ] Measure **update latency** from a timestamped repository or agent change to
      its matching display state. Separate detection, collection, model update,
      and render delay; include selected and unselected worktrees, bursts, and
      remote disconnect/reconnect. Bound remote clock uncertainty explicitly.
- [ ] Measure **fleet scaling** across the matrix: total and per-worktree CPU,
      resident/peak memory, process and descriptor counts, scans, remote traffic,
      startup/time-to-usable, and interaction/update tails. Record agent activity,
      repository size, host placement, and admission limits; queued work must not
      be reported as concurrently active agents.
- [ ] Measure **output pressure** at declared bytes/second, line sizes, producer
      counts, and durations, with preview open and closed. Track input latency,
      update backlog/age, peak memory and growth, dropped/coalesced display updates,
      producer blocking, and time to recover after output stops. Preserve authoritative
      task results and required evidence even when display updates are coalesced.
- [ ] Qualify **change locality** using per-worktree attribution: compare no-change
      cost and the incremental cost of changing exactly one worktree at each scale.
      Unchanged worktrees should not incur repeated Git scans, helper launches, or
      output capture because a peer changed. Identify shared Git metadata dependencies
      and bounded reconciliation separately; do not achieve low cost by hiding stale
      state. Explore event notification, invalidation, batching, and bounded caches
      only where profiles support them; prove missed-event and reconnect recovery.
- [ ] Store exact revisions, builds, workload seeds, raw timestamped samples,
      environment/tool versions, attribution boundaries, and observer overhead.
      Separate startup from steady state and alternate baseline/candidate trials.
      Choose numeric idle, tail-latency, memory, recovery, and locality budgets from
      the initial supported-platform baselines and product needs, then freeze them
      before evaluating optimizations. Retain stalls and uncertainty; never treat
      a short-run maximum as a guaranteed worst case or missing counters as zero.

Acceptance: publish reproducible baseline evidence for every matrix cell on macOS
and Linux, with explicit unsupported measurements and separate live-agent coverage.
The measurement slice completes with recorded budgets and correctness checks;
the efficiency slice completes only when repeated before/after trials meet those
budgets, demonstrate near-idle behavior and bounded unrelated-worktree cost, and
preserve freshness, cancellation, recovery, and result integrity under output load.
No performance baseline or improvement is claimed by this roadmap addition.

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

#### 7. Remote execution visibility, diagnostics, and bounded retention

Organize visibility around runs, tasks, and execution attempts, with hosts,
workspaces, provider conversations, and optional terminals as related resources.
An agent is a managed process, not an OS-isolated container. Terminal replacement
alone does not provide execution visibility or isolation. These milestones can
start before T1–T3 using the existing remote task owner and observation endpoints;
apply them to the implemented [distributed runs](DISTRIBUTED_DAG.md). Extend item
9's obligation/evidence explanations as those milestones land. V1–V4 are planned
operator visibility and qualification work, not claims of new runtime behavior.

- [ ] **V1 — Run and host overview with explicit observation freshness.** Define a
      versioned receiver snapshot with task/run/step/attempt identity, assigned host,
      workspace, agent profile, execution owner, state, pending requests, and
      observation timestamps. Show effective configuration and concrete waiting
      reasons: admission, dependency, authentication, approval, or reconciliation.
      Include the unsatisfied contract/obligation, exact subject, missing evidence,
      and next useful action as item 9 supplies those records.
      Keep connection health and observation age distinct from last-known execution
      state. The receiver owns execution observations; the coordinator owns graph
      and assignment decisions. Views do not become another state authority.
      Acceptance: CLI and TUI identify the owner, waiting reason, and next action
      without raw-state inspection. Disconnect one host while others stay reachable:
      its cached state is visibly stale, with last-confirmed time, and is never
      relabeled failed or confirmed running merely because transport was lost.
- [ ] **V2 — Attempt detail, ordered events, and resumable logs.** Reuse existing
      run/step/attempt log selectors and byte offsets; add a bounded, versioned event
      observation contract with stable sequence/cursor semantics. Define reconnect,
      duplicate-event, retention-gap, and stream-reset behavior. Expose attempt
      history, artifact inventory, provider observations, and approval requests.
      Present process exit, result collection, verification verdict, and approval
      separately. Start with bounded polling; streaming is optional when measured
      responsiveness or traffic justifies it. Logs do not imply interactive stdin
      access; attach remains a capability of an actual terminal.
      Acceptance: reconnect resumes logs and event observation from recorded cursors
      without silently omitting transitions or presenting duplicate transitions.
      Missing retained history is explicitly reported. A successful process with
      missing artifacts or failed verification cannot appear as an accepted result.
- [ ] **V3 — Controls with visible acknowledgments.** Present cancellation requested,
      delivered, and confirmed stopped as distinct stages. Bind approvals and other
      mutations to the exact task/attempt or candidate they concern; preserve existing
      authorization and mutation paths. Unknown cancellation remains visible. Keep
      cancellation separate from workspace deletion and retain unresolved admission
      claims. Disable actions that cannot establish a current safe target, and make
      host authentication/approval requests actionable rather than hidden waits.
      Acceptance: lose a cancellation response, reconnect, and show the receiver's
      recorded outcome without duplicate effects or a false stopped state. Reject
      stale approvals and ensure workspace cleanup cannot discard active/dirty work.
- [ ] **V4 — Recovery visibility qualification.** Exercise the complete public CLI
      and TUI path through submission, execution, SSH loss, reconnect, cancellation,
      and collection. Reconcile the original attempt before deciding on further
      execution; loss of a heartbeat or connection does not authorize replacement
      workers. Reuse task identities and durable outcomes rather than infer success
      from a quiet terminal or provider conversation restore.
      Acceptance: disconnect and restart the observing client while work continues;
      recover the same task/attempt, event history, log position, and exact result.
      Separately kill the execution owner and retain `outcome_unknown` without replay.
      Repeat across the existing two-host execution path; one unavailable host
      cannot block observation of another, and stale results cannot advance
      dependent work.

- [ ] Measure queue delay, time to verified result, unknown outcomes, recovery
      success, manual interventions, and transfer size. Make metric export optional
      and report provider usage only when available.
- [ ] Define retention and archive policies for submission keys, event metadata,
      logs, and artifacts without discarding evidence required by active recovery
      or accepted outcome claims. Preserve referenced raw evidence and contract
      versions for the declared audit/reuse window; disclose expired evidence.
      Keep retention gaps distinguishable from empty output or absent events.
- [ ] Add accessible event-announcer and comparison views over the same evidence.

Acceptance: operators can explain what is running, where, why it is waiting, how
fresh that information is, and whether a requested action took effect. Retention
stays bounded while preserving active recovery and the documented deduplication
window. V1–V4 provide the observation surface used by T3; they do not require a new
permanent daemon, terminal server, scheduler, or automatic coordinator failover.

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

#### H. Host discovery, qualification, and staged onboarding

Add a candidate-to-operation path around the existing explicit SSH fleet. Discovery
is an inventory and qualification feed, not a new trust authority or an automatic
enrollment mechanism. Keep the shell CLI as the mutation path, the existing
host-key and handshake contracts as the qualification boundary, and receiver-owned
remote-task state as the recovery authority. This track can begin against the
current fleet implementation.

- [ ] **H1 — Candidate discovery and read-only qualification.** Import effective
      OpenSSH aliases and explicitly selected static inventory records without
      copying private keys or rewriting SSH configuration. Normalize endpoints,
      preserve source provenance/freshness, assign stable candidate IDs, and
      deduplicate conservatively; a hostname, address, provider ID, or mDNS name
      is not a verified host identity. Add a machine-readable candidate/result
      schema and a read-only probe that uses BatchMode, strict host-key checking,
      bounded time/output, and the existing Hydra handshake/capability checks.
      Unknown or changed host keys stop for operator review; discovery never
      accepts a key, creates a fleet alias, installs Hydra, changes PATH, maps a
      project, or submits a task.
      Acceptance: a prepared alias (including a jump host) produces deterministic
      candidate and qualification output; unknown/changed keys, authentication
      failure, unreachable hosts, protocol mismatch, and missing capability have
      distinct typed results; a mixed ten-host run retains every row and evidence;
      discovery and probing leave aliases, remote state, and installations
      unchanged.

- [ ] **H2 — Reviewed qualification and explicit enrollment.** Add a reviewable
      operation intent using existing state/CLI mechanisms; do not introduce a
      second execution authority or require a new digest-plan subsystem. Bind each
      selected host to its candidate/source identity, accepted host-key
      fingerprint, Unix principal/target, required protocol/capability, project
      mapping, and (when requested) the exact pinned package digest and remote
      prefix. Apply only after explicit operator confirmation. Reuse the current
      staged, hash-verified bootstrap and fleet-init/trust boundaries; do not copy
      credentials, private keys, or repository trust implicitly. Record per-host
      progress and partial failure. A lost response to a mutation is
      outcome_unknown and must be reconciled against the receiver with the same
      identity before any retry; never blind-replay an uncertain action.
      Acceptance: one-host onboarding proves the selected key, package bytes,
      prerequisites, project/path decision, and resulting alias/state agree with
      the reviewed intent; a ten-host mixed apply preserves successes and typed
      failures; interruption, duplicate submission, owner loss, and lost response
      tests reconcile without duplicate effects or inferred success; a changed
      key, package, target, project, or policy requires renewed review.

- [ ] **H3 — Bounded scale and opt-in source adapters.** Extend the same candidate
      contract to selected mDNS/DNS-SD, VPN/provider, cloud-tag, and
      configuration-management inventory sources only after H1 is useful. Treat
      source records as untrusted, timestamped metadata; keep provider
      credentials outside candidate state and make source scope/cache behavior
      explicit. For 50–100 candidates, import snapshots locally and qualify in
      deterministic batches no larger than the current 16-host bounded observer;
      retain per-host progress, stale/conflict evidence, and resume/reconcile
      semantics. A TUI or richer batching view is a presentation layer over these
      records, not another authority.
      Acceptance: fixture-backed adapters reject malformed/secret-bearing input;
      source disappearance, stale data, duplicate identities, and key conflicts
      remain visible; a 50-host run can resume without rerunning completed
      mutations; a 100-host import is bounded and does not silently widen the
      selected set. No provider membership or discovery record grants execution
      permission.

Dependencies and coordination: H1 depends only on the current explicit fleet
aliases, strict OpenSSH policy, and versioned handshake. H2 reuses the current
package/bootstrap, fleet-init/trust, and remote-task contracts documented in
[FLEET.md](FLEET.md), [SECURITY.md](SECURITY.md), and
[REMOTE_TASKS.md](REMOTE_TASKS.md); it may proceed without a new planner or
daemon. H3 is follow-on work after H1/H2 and should not broaden the trust boundary.
T1, 9A, and V1 implementations are integrated on the feature branch; T2 remains
the next remote-execution milestone. T1/T2 become dependencies only for
tmux-independent/headless enrollment, 9A is relevant if
onboarding obligations are later compiled into objective plans, and V1 can supply
freshness/attempt presentation when its observation contract lands. This H track
does not mark any of those milestones complete, reclassify them as delivered, or
rewrite their acceptance.

Scope guardrails: do not auto-scan arbitrary networks, auto-accept host keys,
silently replace aliases after key rotation, copy secrets, auto-trust repositories,
submit remote work as a side effect of discovery, add a permanent daemon/database,
or claim that source reachability proves host health. Keep shared multi-user
inventory and automatic failover outside this slice until their authority and
reconciliation contracts are separately approved.


## Native workspace and standalone termviz track

This track develops dependency-free C terminal infrastructure, using Hydra as its
first real consumer and eventual standalone release as the direction. Work in the
existing visualization worktree. Build milestones in order; integrate a real shell
before expanding the widget catalogue. Existing visualization architecture and
limits are documented in [VISUALIZATION.md](VISUALIZATION.md) and the
[termviz module guide](../src/termviz/README.md).

Termviz owns reusable rendering, layout, input routing, and terminal screen
interpretation. Hydra owns agent/workflow semantics and authoritative actions.
Keep OS-specific terminal lifecycle and PTY process handling in small adapters,
separate from the portable core. Add no third-party dependencies. Preserve the
shell-only path, current CLI/state contracts, and tmux authority for Hydra heads;
the standalone shell example does not introduce a competing Hydra execution owner.

The workspace foundation and first embedded-shell milestone are implemented.
See [workspace acceptance](WORKSPACE_ACCEPTANCE.md) for reproducible checks and
the [terminal contract](../src/termviz/TERMINAL.md) for the qualified subset.

### Milestone 3: useful Hydra workspace

- [x] Establish a reviewed visual target with clear project -> head -> run navigation,
      prominent selected work, concise status summaries, and contextual actions.
      Keep raw identifiers and detailed provenance available on demand; avoid
      repeated hints and diagnostic fields as the primary workspace content.
- [x] Make an interactive agent pane a first-class part of the workspace. Users
      should converse with Codex or another qualified CLI about an objective,
      inspect its proposed plan alongside the conversation, request revisions,
      and return to the same agent session while work is running.
- [x] Use conversation-first planning (concept A) as the default, with a discoverable
      toggle to the comprehensive plan overview (concept B) and back. Expand the
      dependency graph, validation and approval context on demand while keeping the
      agent pane available. Preserve the agent session, unsent input, plan revision,
      selection and pane scroll positions; restore prior focus when returning.
      Use the monitoring/intervention layout (concept C) during execution, with the
      same conversation and selected run available across layouts.
      Connect the implemented statistics layout (concept D) to these workspace
      layouts; allow the agent pane to be revealed without losing statistics
      filters or conversation.
- [x] Present agent-authored plans through the existing plan workflow: objective,
      steps, dependencies, inputs/outputs, checks, validation errors and current
      revision. Distinguish a conversational proposal from a validated executable
      plan and from a running workflow. Surface explicit approval of the exact
      plan being executed; revisions invalidate any prior approval.
- [x] Design planning, execution and recovery layouts around the same selected
      project/run context. Preserve draft and session context when changing panes;
      make focus and whether input reaches an agent or Hydra unambiguous.

Acceptance: review concrete narrow/wide layout examples, then use the workspace to
ask an agent for a plan, request a meaningful revision, inspect validation and the
changed dependency graph, and explicitly approve that version through the existing
shell-authoritative workflow. Merely receiving an agent message never starts work.
Draft, validated, awaiting approval, running and failed states are visibly distinct.
Toggle from conversation to comprehensive overview and back during plan revision;
verify that unsent input, session identity, selected step, scroll and focus survive.
The overview toggle changes presentation only, never approval or execution state.
The existing planning implementation is integrated into this branch; see the
[planner recipe](PLANNER_RECIPE.md) and [compilation contract](PLAN_COMPILATION.md).
The native workspace now loads draft/policy files with P, validates with V, shows
revision-aware complete previews and dependencies, and preserves layout state
across A/B/C/D. Native dialogs continue agent I/O. E accepts the exact displayed
digest, rechecks the revision and delegates execution to a durable owner through
the existing engine; UI exit and duplicate launch refusal are covered by a real
workflow test. C now follows selected run/step evidence and delegates explicit
request decisions, resume and cancellation to the existing CLI. Local navigation
groups recorded runs under matching head branches and opens selected evidence;
unmatched runs remain under the project. Visual review and the representative
agent-authored workflow passed; see [the real acceptance record](WORKSPACE_REAL_ACCEPTANCE.md).

### Milestone 4: integrated working terminals

- [x] Attach interactive panes to existing Hydra sessions while preserving tmux
      ownership. Keep the planning conversation distinct from worker sessions and
      retain session identity when switching or reconnecting.
- [x] Qualify real Codex and other selected agent CLIs, expanding terminal behavior
      only for observed needs. Exercise interactive prompts, paste, scrolling,
      resize, full-screen output, interruption and permission requests.
- [x] Support multiple terminal panes and switching between agents. Show attention
      indicators backed by actual observations; distinguish waiting for user input,
      unknown state, disconnected transport and process exit.

Acceptance: author/revise a plan in the interactive agent pane, switch to a worker's
existing session, interact with it, then return to the original conversation. Resize
and reconnect without duplicate execution, lost session identity or damaged terminal
state. Record tested CLI versions and unsupported behavior explicitly.

The workspace now supports two visible existing tmux clients per A/B/C layout,
with four cached clients, independent scrollback and focus, and retained unsent
drafts across layout changes. Narrow layouts reveal one focused agent at a time.
Agent attention remains explicitly unknown without exact instance observations;
recorded exit/failure, stale data and client disconnection have separate labels.
Workflow input requests remain identified in C. Codex CLI 0.153.3 is qualified in
the recorded local configuration; no additional CLI was selected.

### Milestone 5: operational views

- [x] Integrate workflow graphs, host status, selected-step output, verification
      evidence, failures and recovery actions into the workspace's navigation and
      detail panes. Preserve useful existing overview/graph views.
- [x] Distinguish observed progress from declared intent, stale or missing data,
      blocked dependencies and requests for a decision. Offer the next applicable
      action with enough context to assess its effect.
- [x] Extend the local [statistics view](STATISTICS.md) with queue delay, total
      execution duration, recorded independent verification time and owner recovery
      counts. Minimal lifecycle scalars define future measurements; unsupported
      historical values remain unknown. Filters, coverage, percentiles, trends,
      freshness and contributing evidence are qualified through real workflows.
- [ ] Add remote workflow cohorts when the fleet protocol supplies reliable evidence.
- [x] Show resource utilization and provider token/cost totals only where reliable
      measurements exist. Preserve source, freshness, sample counts and explicit
      unknowns; do not add telemetry collection merely to populate the view.
- [x] Preserve agent session/input and shared selected work when switching
      between statistics, planning and monitoring layouts. Current statistics
      filters, run/step/host drill-down and workspace return are implemented.
- [x] Keep the interactive agent pane available to discuss a blocked step or revise
      future work, with explicit scope and approval before execution changes.

Acceptance: trace a failed step from the graph to its output and evidence, understand
why dependents are waiting, inspect host/data freshness, and perform the supported
recovery action through the existing CLI. No fabricated progress or inferred success.
For D, reconcile displayed aggregates with their underlying records for a selected
time range, including failed, missing and stale observations. Verify filtering,
drill-down and return navigation; missing measurements never appear as zero.

Implemented in C: recorded run switching, dependency selection, bounded step
output, request/binding inspection, fresh artifact verification with tamper refusal,
and explicit approve/reject/resume/cancel controls. The controls retain the engine's
separate decision/resume boundary and do not restart terminal runs. PTY tests cover
an existing approval-wait workflow and preserve an attached shell's unsent draft
through decisions and UI closure. Local project/head/run navigation is connected.
Agent-authored planning and recovery are qualified in the recorded exercise; the
compiled-plan schema is unchanged. Its approval-wait boundary was exercised through
a separately authorized existing workflow, not added to compiled plans.

Local statistics are implemented and qualified at 40x10, 80x24 and 140x40.
Recorded verification timing is historical evidence; result retrieval independently
checks current artifacts. Remote workflow cohorts and provider/resource telemetry
remain future work and require explicit protocol and measurement contracts.

### Milestone 6: interaction and visual polish

- [x] Refine spacing, information density, restrained semantic colors, selection and
      focus, pane titles, contextual controls and keyboard discoverability.
- [x] Add useful search/filtering and deliberate empty, loading, disconnected,
      failed and unavailable states. Preserve readable monochrome/ASCII behavior.
- [x] Adapt layouts to available space, with focus/zoom for the agent conversation,
      discoverable hidden panes, and retained selection and independent scrolling.

Acceptance: inspect and interact with planning, running, waiting-for-input and failed
workflows at 40x10, 80x24 and 140x40. Users can identify selected work, current status,
input destination and next action without consulting raw IDs or overflowing text.
Compare actual terminal captures against the reviewed visual target; component tests
alone do not satisfy visual or interaction acceptance.

### Milestone 7: real-workflow acceptance

- [x] Run a representative multi-agent task from objective discussion and agent-
      authored plan through revision, explicit approval, execution, intervention,
      verification and result inspection entirely through the workspace.
- [x] Exercise a real request for user input, a failed check and supported recovery,
      plus detach/reconnect. Fix observed workflow friction within this scope.
- [x] Record reproducible terminal evidence and operator findings, distinguishing
      automated checks from hands-on usability acceptance and untested combinations.

Acceptance: the operator can tell what is happening, communicate with the planning
agent and workers, make a required decision, recover a failure and inspect verified
results without losing context or creating another execution owner. Complete this
workflow before describing the interface as product-ready.

### Milestone 8: standalone library readiness

Local source extraction, an independent Makefile, preserved licenses and the
standalone component/PTY acceptance path are implemented. Run
`make test-termviz-export`; see [ownership and compatibility boundaries](../src/termviz/STANDALONE.md).
Darwin 25.6.0 arm64 with Apple Clang 17 and UBSan passed. Linux aarch64 on
LinuxKit 6.12.67 with Debian GCC 12.2 passed standalone component/PTY checks and
ASan/UBSan on 8 September 2026. Other platform runtime qualification remains
unavailable. The caller-owned core and optional POSIX boundary are preserved;
this establishes local source readiness, not a standalone release.

- [x] Stabilize the API from real Hydra and standalone use, document storage/process
      ownership and compatibility limits, and prepare an independently buildable
      source tree with examples and tests. Compatibility remains explicitly
      unpublished and source consumers must rebuild when updating.
- [x] Qualify the supported platforms and package/license boundaries. Keep Hydra
      semantics outside termviz and publish only after a separate release decision.

Full terminal compatibility, a broad widget catalogue, new telemetry collection and
remote execution changes require their own bounded scope. Local implementation and
qualification do not authorize repository creation, pushing or publication.

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
