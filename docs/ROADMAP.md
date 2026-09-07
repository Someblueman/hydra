# Hydra Roadmap

> - **Status:** canonical outstanding-work backlog
> - **Snapshot:** 8 September 2026
> - **Current release:** `v2.2.1` correctness and trust hardening
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
The implemented remote submission and collection interface is documented in
[Remote tasks](REMOTE_TASKS.md), with [qualification evidence](REMOTE_TASK_ACCEPTANCE.md).
Local objective planning is implemented; see the [planner recipe](PLANNER_RECIPE.md)
and [delivery qualification](evidence/plan-qualification.md). Remaining priorities
keep their original numbers so existing references remain meaningful.

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

#### T. Optional tmux for headless and remote execution

Decision: make tmux optional for headless execution, retaining it for interactive
heads. This is planned behavior, not a change to current dependency requirements.
See [the updated analysis](research/tmux-optional-execution.md) for the current
implementation, affected contracts, and design tradeoffs. T1 and T2 precede the
headless distributed acceptance in priority 5; resource admission can proceed in
parallel. These are delivery milestones, not assigned release versions.

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
- [ ] **T3 — Distributed qualification and operator access.** Use T2 for priority
      5's two-host fan-out, validation, join, composition, and final check. Expose
      run/step/attempt logs and owner state through CLI/TUI without requiring attach.
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

#### 4. Resource admission

Start with explicit hosts and FIFO admission. Build this boundary alongside the
first distributed DAG slice; automatic placement depends on it.

- [ ] Add host/project concurrency limits, disk floors, capability labels, queue age,
      and bounded backpressure. Reserve host-wide resources atomically at the
      receiving host, rather than trusting client observations or project-local
      allocation alone. Define reservation release and retain unresolved ownership.
- [ ] Expose capacity, reservations, queue state, and observation freshness for
      inspection and later placement. Treat CPU/memory observations as signals,
      not enforcement or proof that another task can safely start.

Acceptance: concurrent submitters cannot exceed the host's admission limit; stale
capacity observations cannot overbook it. An incompatible or full host explains why
work is queued or refused. Queue deadlines and cancellation have bounded behavior;
unknown execution does not silently release its claim.

#### 5. Distributed DAG execution and independent validation

Extend the existing finite workflow DAG across explicitly selected hosts. Producers
create artifacts, validators examine those exact artifacts, and a deterministic
policy step combines their evidence. Composition workers integrate code, reconcile
designs, or synthesize reports; their new deliverables must be validated again.
These are workflow roles, not separate scheduler services. The headless execution
path uses T1–T2; its tmux-free two-host qualification closes T3 as well.

- [ ] Add individual remote steps through the existing fleet task interface, with
      one durable coordinator per run. Persist the resolved graph, source/input
      digests, policy, host assignment, exact package, and submission key before
      dispatch. Bind the receipt once known and verify completion before advancing
      dependents. Recover on the same coordinator host first.
- [ ] Connect verified result collection to downstream task inputs and source
      commits. Start with transfer through the coordinator; preserve immutable
      artifact bindings and authorize derived tasks within the run's explicit
      destinations and work recipes. Mutable branch names are not handoff identity.
- [ ] Define versioned validation reports bound to artifact and validator-definition
      digests. Separate process success from PASS/FAIL/INCONCLUSIVE verdicts. Require
      all designated checks to pass initially; missing evidence and agent agreement
      alone cannot satisfy a correctness gate. Protect the acceptance harness from
      producer edits; separate sessions do not provide OS isolation.
- [ ] Validate the assembled candidate, then use existing bound approval and
      integration checks. Repairs create new candidates and bounded attempts;
      previous validation does not authorize changed bytes.
- [ ] Reconcile lost responses against the original host, package, and key. Keep
      reconciliation, execution retry, and semantic repair distinct. Host-scoped
      deduplication cannot prevent a second execution on another host. Defer
      automatic coordinator failover and unresolved-task reassignment.

Acceptance: a two-host fan-out, independent validation, evidence join, assembly,
and combined-candidate check pass through the public CLI. Coordinator restart and
lost acknowledgments preserve original task identities without duplicate execution.
Bad artifacts, stale verdicts, validator errors, and a moved integration target
prevent promotion. Replaying the same recorded observations yields the same
scheduling decisions; this does not promise identical agent outputs or timings.

Research basis (6 September 2026): the original
[MapReduce model](https://research.google/pubs/mapreduce-simplified-data-processing-on-large-clusters/)
combines values by key; validation is more naturally a separate DAG stage.
[Temporal's architecture](https://github.com/temporalio/temporal/blob/main/docs/architecture/README.md)
separates deterministic workflow decisions from effectful activities.
[in-toto](https://in-toto.io/docs/getting-started/) supplies a precedent for linking
steps through exact materials and products. These inform the design, not new runtime
dependencies or claims that provenance proves correctness.

#### 6. Load balancing across eligible hosts

Extend resource admission with automatic placement of **new, unassigned work**.
Explicit host pinning remains available. Implement this after assignment recovery
and receiver reservations are qualified; it need not wait for dynamic task pools.

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

Explain what needs attention through existing CLI and TUI surfaces.

- [ ] Add effective configuration, "why waiting?", host/attempt timelines, artifact
      inventories, and explicit stale-observation labels.
- [ ] Measure queue delay, time to verified result, unknown outcomes, recovery
      success, manual interventions, and transfer size. Make metric export optional
      and report provider usage only when available.
- [ ] Define retention and archive policies for submission keys, event metadata,
      logs, and artifacts without discarding evidence required by active recovery.
- [ ] Add accessible event-announcer and comparison views over the same evidence.

Acceptance: an operator can identify a blocked task's owner, reason, and next action
without reading raw state files. Retention stays bounded while preserving active
recovery and the documented deduplication window.

#### 8. Dynamic task pools and schedules

Select this work only when real workloads need newly discovered tasks or persistent
queues that finite workflows cannot express cleanly.

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

### Milestone 1: interactive workspace foundation

- [ ] Add retained frames and incremental output: an unchanged frame emits no
      cell updates, and a localized edit does not clear/repaint the entire screen.
- [ ] Add nested rectangular layout with minimum sizes, clipping, draggable splits,
      independent scroll positions, and defined behavior when space is insufficient.
- [ ] Route keyboard and mouse input through explicit focus and painted hit regions.
      Each pane receives input and resize events and draws into a clipped surface.
- [ ] Support UTF-8 text and styled spans with documented display-width behavior,
      including wide/combining characters and malformed input. Bound text storage;
      do not claim universal emoji/font shaping compatibility.
- [ ] Deliver a standalone workspace demo with a navigation tree and two independently
      scrolling content panes. Integrate the same foundation into Hydra's native UI.

Acceptance: actual PTY checks exercise keyboard/mouse focus, divider dragging,
independent scrolling, and resize at 40x10, 80x24, and 140x40. No pane paints outside
its bounds; tiny layouts remain navigable. Capture output to prove unchanged-frame
and localized-update behavior. Inspect rendered narrow/wide examples and verify
exact terminal restoration on normal exit, interruption, and relevant failures.
The standalone build contains no Hydra dependencies.

### Milestone 2: one embedded real shell

- [ ] Implement a bounded terminal screen model and streaming escape-sequence parser,
      independent of layout and process creation. Document the supported sequence
      and mode subset; handle fragmented, malformed, and unsupported input safely.
- [ ] Support the cursor, erase, style, wrap, scrolling-region, primary/alternate-screen,
      and input-mode behavior required by the qualified shell workflow. Keep scrollback
      bounded and prevent child output from invoking host-terminal side effects.
- [ ] Connect one interactive shell through a small POSIX PTY adapter in the workspace
      demo. Route focused input, propagate resize, drain output without blocking UI
      interaction, and report child exit/error with explicit process ownership.
- [ ] Verify shell input, styled output, scrollback, a deterministic full-screen test
      child, focus changes while output streams, resize propagation, and cleanup.
      Make the embedded surface reusable by Hydra without replacing head ownership.

Acceptance: launch a real local shell in an embedded pane, execute commands and
observe their output and exit status, resize and confirm the child terminal size,
switch focus while it produces output, and return from alternate-screen content.
PTY and parser tests cover chunk boundaries, malformed sequences, bounded history,
child exit, and interruption without leaked owned children or damaged outer terminal
settings. Qualify locally on macOS, preserve POSIX portability, and report untested
platforms. Run relevant full Hydra checks, supported sanitizers, standalone builds,
and actual rendered inspection for both milestones; record reproducible evidence.

### Later: agent compatibility and standalone publication

- [ ] Run real agent CLIs to drive terminal compatibility, then expand to multiple
      embedded terminal panes and attention indicators backed by actual observations.
- [ ] Stabilize the API from Hydra and standalone use, document ownership and limits,
      and prepare an independently buildable repository with examples and tests.

Full terminal compatibility, a broad widget catalogue, remote execution changes,
new telemetry collection, and repository publication are outside the first two
milestones. Extraction and publication require a separate release decision;
implementation qualification is not permission to push or publish.

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
- bounded best-of-N recipes and reviewed issue-decomposition templates over existing
  tasks, gates, and integration primitives.

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
- [NATIVE_CORE.md](NATIVE_CORE.md) and [NATIVE_TUI.md](NATIVE_TUI.md) for optional
  native behavior;
- [SECURITY.md](SECURITY.md) for current trust boundaries.
