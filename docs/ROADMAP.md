# Hydra Roadmap

> - **Status:** canonical outstanding-work backlog
> - **Snapshot:** 4 September 2026
> - **Current release:** `v2.0.0` stable local orchestration interface
> - **Release planning:** versions are assigned from compatibility impact when backlog work is ready
> - **Related:** [README](../README.md) · [CHANGELOG](../CHANGELOG.md) ·
>   [Release policy](VERSIONING.md) · [Contracts](CONTRACTS.md) ·
>   [2.0 release checklist](RELEASE_CHECKLIST_2.0.0.md)

## Purpose

Hydra should be the most inspectable way to coordinate parallel coding agents across
real Git worktrees and tmux sessions, without requiring a cloud account, background
daemon, database, or provider-specific runtime.

This file contains only outstanding work and the policies that constrain it.
Implemented behavior belongs in the changelog and focused contract documentation;
completed roadmap items are removed rather than retained as checked history.

2.0.0 is the final version assigned in advance. After 2.0, work is selected from one
backlog and released when a coherent feature or meaningful change is ready. The
version number is chosen at release time from compatibility impact.

## Product and engineering guardrails

- The POSIX shell CLI remains the authoritative mutation path.
- C remains optional and must not be required for core session management.
- Hydra requires no daemon, database, model router, or provider-specific runtime.
- State remains inspectable with ordinary filesystem and shell tools.
- Git and tmux remain authorities for repository and terminal state.
- Native frontends delegate mutations to the shell CLI instead of duplicating policy.
- Backwards compatibility is retained only when a current requirement demands it;
  2.0 removes superseded compatibility paths cleanly.
- Mutating commands have bounded failure behavior and an explicit recovery story.
- Machine-readable interfaces use versioned success and error schemas.
- Repository configuration is never executed without an explicit trust decision.
- Hydra does not silently copy secrets, auto-merge without policy and approval, or
  present heuristic agent observations as authoritative state.
- Performance claims require reproducible measurements.

## Post-2.0 backlog

Items below have no assigned release number. When work is selected, define the
smallest coherent scope and its acceptance boundaries, then release it when ready.
Priority may change with observed use.

### Fleet pilot

Observe and coordinate trusted Hydra installations over SSH using stable local
contracts, without introducing a server or shared runtime database.

#### Candidate scope

- [ ] Add remote aliases that defer host resolution to OpenSSH.
- [ ] Add a remote handshake exposing Hydra version, protocol versions,
      capabilities, project mappings, and native availability.
- [ ] Add explicit bootstrap of a pinned compatible shell Hydra on a trusted host;
      Git and tmux remain prerequisites.
- [ ] Add bounded-parallel, read-only fleet list and doctor aggregation.
- [ ] Add remote attach through ordinary `ssh -t ... tmux attach` behavior.
- [ ] Add explicit remote spawn, signal, cancel, and workflow operations only for
      compatible capabilities.
- [ ] Add per-host timeouts, offline state, partial results, and cancellation.
- [ ] Reuse SSH multiplexing when configured or explicitly enabled.
- [ ] Add a native TUI fleet view only after the CLI protocol is stable.
- [ ] Add export/import for declarative project configuration and historical run
      bundles, excluding live tmux mappings and host-local locks.
- [ ] Add wake/reconcile behavior after sleep or network loss.

#### Acceptance boundaries

- Host-key failures, authentication failures, timeouts, partial outages, and version
  mismatch are distinct machine-readable outcomes.
- One offline host cannot block local or other-host visibility beyond its timeout.
- Remote mutations fail closed when the required capability is unavailable.
- No live state file is treated as authoritative on two hosts.
- Remote arguments survive spaces and metacharacters without shell injection.
- Sleep and reconnect reconcile state without losing local work.

### Candidate features

- [ ] File-backed dynamic task pools after static workflows demonstrate the need.
- [ ] Best-of-N arenas over existing gate and integration primitives.
- [ ] Issue-decomposition templates over the workflow engine.
- [ ] External heads and schedules after local lifecycle contracts stabilize.
- [ ] Comparison views for two or more heads.
- [ ] Historical ghost heads as recovery shortcuts.
- [ ] Accessible event-announcer views.
- [ ] Git-bundle transport for air-gapped hosts.
- [ ] Optional TUI enhancements such as mouse input, drag layouts, workflow graphs,
      themes, sparklines, and richer visualizations.
- [ ] Provider adapter packaging after a dated capability probe and stable hook
      fixtures exist for a supported provider.

### Experimental ideas

These enter the candidate backlog only after a bounded prototype demonstrates user
value, security boundaries, and a simpler implementation than composing existing
Hydra primitives.

- exact context-window or cost routing across heterogeneous agents;
- provider-specific cognition dashboards;
- automatic issue decomposition without a reviewed plan;
- dirty-file shadow-head synchronization;
- Git-as-consensus fleet registries;
- multi-user presence, inbox federation, and handoff tokens;
- external cloud heads;
- natural-language command parsing;
- arbitrary plugin marketplaces.

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
- [workflows.md](workflows.md) for workflow and integration behavior;
- [NATIVE_CORE.md](NATIVE_CORE.md) and [NATIVE_TUI.md](NATIVE_TUI.md) for optional
  native behavior;
- [SECURITY.md](SECURITY.md) for current trust boundaries.
