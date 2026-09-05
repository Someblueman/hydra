# Hydra Roadmap

> - **Status:** canonical outstanding-work backlog
> - **Snapshot:** 5 September 2026
> - **Current release:** `v2.0.0` stable local orchestration interface
> - **Release planning:** versions are assigned from compatibility impact when backlog work is ready
> - **Related:** [README](../README.md) · [CHANGELOG](../CHANGELOG.md) ·
>   [Release policy](VERSIONING.md) · [Contracts](CONTRACTS.md) ·
>   [Release definition of done](#release-definition-of-done)

## Purpose

Hydra should be the most inspectable way to orchestrate engineering work locally
and across trusted remote hosts, using interchangeable agents, real Git worktrees,
and optional terminal interaction. Sending work remotely should be as ordinary as
running it locally: submit a task and an exact code snapshot, disconnect, then
inspect and collect a verified result.

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

## Post-2.0 backlog

Items below have no assigned release number. When work is selected, define the
smallest coherent scope and its acceptance boundaries, then release it when ready.
Priority may change with observed use. The numbered priorities build on fleet
transport rather than expanding the pilot into a distributed scheduler.

### Candidate features

Select these priorities in dependency order, with independently useful scope.
Develop the bounded adapter contract and harness qualification alongside workflow
inputs; neither needs to wait for the full approval-wait capability. Extend
headless execution as the shared task and workflow contracts become ready.
The implemented remote submission and collection interface is documented in
[Remote tasks](REMOTE_TASKS.md), with [qualification evidence](REMOTE_TASK_ACCEPTANCE.md).

#### 1. Workflow inputs, outputs, and durable approval waits

Extend remote tasks' selected input files into the finite workflow contract without
building a general expression or templating language.

- [ ] Add named file and small structured inputs, declared output manifests, and
      explicit references between steps. Validate required outputs, types, sizes,
      paths, and digests before releasing dependents.
- [ ] Add a durable approval-wait step with a request ID, bound action/evidence,
      decision, expiry where needed, and explicit resume. Distinguish a policy-issued
      approval from a human decision; a supplied actor label is not authentication.
- [ ] Extend bounded retries with failure classes and backoff. Recover from durable
      attempt evidence; do not repeat an uncertain non-idempotent action.

Acceptance: one agent produces an artifact and another receives that exact artifact.
Missing or changed outputs block dependents. Approval survives coordinator restart
but becomes stale when its bound action or evidence changes. Resume preserves
completed attempts and reports unresolved side effects.

#### 2. Adapter conformance and headless execution

Make agent interchangeability testable while retaining launch-only and no-agent
workers. Scheduler decisions use capabilities, not provider names. Claude Code,
Codex, Pi, and OpenCode are explicit qualification targets, not interchangeable
claims of support based only on executable detection.

- [ ] Publish a small versioned adapter contract and fixtures for task input,
      normalized observations, output bounds, safe-point delivery, cancellation,
      permission requests, and exact session resume where supported.
- [ ] Support declarative executable and argument-vector configuration, task-prompt
      delivery, and session-resume invocation wherever those declarations suffice.
      Bind resume to a recorded session identity; "most recent session" is not exact
      resume. Avoid shell-string templates and a general expression language.
- [ ] Keep small provider adapters only for behavior requiring translation, such
      as structured events or permission requests. Adding a harness must not require
      new provider-specific branches in Hydra's core lifecycle or workflow policy.
- [ ] Record executable version and probe date, distinguishing declared, probed,
      and observed capabilities. Fail before dependent work when a required
      capability is missing; never infer hooks from executable availability.
- [ ] Add structured, noninteractive execution through the existing supervision
      path first. Keep provider flags and event decoders outside workflow policy.
- [ ] Qualify Claude Code, Codex, Pi, OpenCode, and a plain script against the same
      local and remote acceptance task. Verify prompt delivery, cancellation, and
      exact session resume where supported, including malformed/partial events and
      stale instances. Record unsupported capabilities explicitly; launch-only
      support does not satisfy a prompt or resume requirement.
- [ ] Keep raw provider payloads and transcripts opt-in with bounded retention.
      Treat exact cost limits and usage reporting as optional capabilities;
      unavailable values remain unknown.

Acceptance: add another harness through a profile declaration and, only where
needed, a small adapter plus conformance fixtures, without modifying Hydra's core
lifecycle or workflow policy. Changing a compatible profile does not require
changing workflow logic. The named harnesses pass the shared task for their
supported capabilities; missing required capabilities fail before dependent work.
Unsupported resume fails explicitly, malformed output cannot alter authoritative
state, and provider completion alone never passes a verification gate.

#### 3. Resource admission and simple placement

Start with explicit hosts and FIFO admission. Add automatic placement only after
capacity and capability information can explain each decision.

- [ ] Add host/project concurrency limits, disk floors, capability labels, queue age,
      and bounded backpressure. Reserve host-wide resources at the receiving host,
      rather than trusting a stale client snapshot or project-local allocation alone.
- [ ] Add eligible-host selection with inspectable reasons for placement or refusal.
      Keep model/cost routing outside the scheduler's initial scope.
- [ ] Before reassignment, define ownership generations and stale-update rejection.
      Lease expiry alone must not duplicate unresolved external work.

Acceptance: concurrent submitters cannot exceed the host's admission limit; stale
capacity observations cannot overbook it. An incompatible or full host explains why
work is queued or refused. An offline host does not trigger an unsafe duplicate.

#### 4. Run diagnostics and bounded retention

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

#### 5. Dynamic task pools and schedules

Select this work only when real workloads need newly discovered tasks or persistent
queues that finite workflows cannot express cleanly.

- [ ] Add file-backed pools with one coordinator, unique task claims, bounded
      outstanding work, cancellation/retry budgets, and stale-owner rejection.
      Reuse task execution and admission rather than adding another scheduler.
- [ ] Start schedules through host timers invoking the same submission API. Define
      missed-run and duplicate-trigger behavior and the always-on owner needed for
      unattended scheduling; make reboot recovery an explicit opt-in contract.

Acceptance: duplicate triggers and competing workers do not create duplicate task
claims. Work growth remains bounded, cancellation propagates, and restart recovery
does not invent completion or replay uncertain actions.

## Simplification alongside feature work

- [ ] Align contributor instructions, CLI help, and examples with current state and
      native-build contracts. Remove language equating terminal idleness with task
      completion, and keep the first-run example internally consistent.
- [ ] Teach `--profile` as the ordinary selector, `exec` for commands, and inboxes
      for steering. Keep `broadcast` and `wait-idle` as expert terminal utilities,
      outside correctness-sensitive workflows. Follow the public deprecation policy
      before removing any option; this roadmap does not deprecate shipped commands.
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
- cross-host DAG steps after source/result transfer and coordinator recovery work;
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
  implemented fleet pilot awaiting release;
- [REMOTE_TASKS.md](REMOTE_TASKS.md) and [REMOTE_TASK_ACCEPTANCE.md](REMOTE_TASK_ACCEPTANCE.md)
  for remote submission, disconnected execution, verified collection, and qualification;
- [workflows.md](workflows.md) for workflow and integration behavior;
- [NATIVE_CORE.md](NATIVE_CORE.md) and [NATIVE_TUI.md](NATIVE_TUI.md) for optional
  native behavior;
- [SECURITY.md](SECURITY.md) for current trust boundaries.
