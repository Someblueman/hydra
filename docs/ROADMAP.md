# Hydra Roadmap

> Hydra's delivery roadmap, informed by review of the original plan, the current
> 1.5.0 repository, an independent roadmap review, and the project's intended POSIX
> shell plus optional C architecture.
>
> - **Status:** canonical
> - **Snapshot:** 25 August 2026
> - **Baseline:** `v1.5.0` at `52adf6f`
> - **Related:** [README](../README.md) · [CHANGELOG](../CHANGELOG.md) ·
>   [Contributor instructions](../AGENTS.md)

---

## 1. Purpose

Hydra should become the most inspectable way to coordinate parallel coding agents
across real Git worktrees and tmux sessions, without requiring a cloud account, a
background daemon, a database, or a provider-specific agent runtime.

The roadmap prioritizes five outcomes:

1. **Trustworthy lifecycle:** Hydra can distinguish running, idle, agent-reported,
   verified, approved, failed, and abandoned work.
2. **Safe parallel development:** Hydra detects overlap, preserves work, verifies
   results, and integrates branches through recoverable workflows.
3. **Inspectable coordination:** state, events, manifests, messages, and decisions
   remain understandable with ordinary filesystem and shell tools.
4. **Optional native performance:** C improves read-heavy aggregation and terminal
   interaction without taking ownership of orchestration policy.
5. **Low-friction adoption:** A new user can install, verify, create, enter, and
   clean up a first head without root access, an agent CLI, or hidden setup.

This is a delivery roadmap, not a promise that every proposed feature will ship.
Items marked **experimental** require evidence before receiving a version commitment.

---

## 2. Strategic Position

Hydra is not primarily a tmux wrapper and should not attempt to become an AI agent
runtime. Its product boundary is the living development environment:

```text
project
  -> head identity
      -> Git branch and worktree
      -> tmux session and panes
      -> agent profile and launch instance
      -> messages, signals, gates, artifacts, and events
      -> optional workflow run membership
```

The defensible differentiation is the combination of:

- heterogeneous local agent support;
- human-readable, file-backed coordination;
- evidence-bearing completion rather than session disappearance;
- cross-head Git intelligence and safe integration;
- recovery after crashes, stale sessions, and interrupted workflows;
- provider independence and a no-daemon core;
- an optional native mission-control interface over the same state.

Worktree isolation by itself is no longer a moat. Codex, Claude Code, Cursor, and
other tools increasingly provide isolated parallel work. Hydra should win on the
coordination, verification, recovery, and integration layers around those worktrees.

The CLI is also Hydra's agent interface. Documented commands, stable exit behavior,
versioned JSON, machine-readable capabilities, and per-head environment identity are
the universal contract. Optional adapters improve lifecycle fidelity; they do not
replace the CLI.

---

## 3. Product and Engineering Principles

These principles apply to every milestone.

### 3.1 Core principles

- The POSIX shell CLI remains the authoritative mutation path.
- C is optional and must not be required for core session management.
- No Hydra daemon is required. A foreground TUI, workflow runner, or watch command
  may be long-lived while the invoking process is alive.
- State remains inspectable with `cat`, `find`, `ls`, and `tail`.
- Git and tmux remain authorities for Git and terminal state. Hydra does not parse
  private Git storage or implement a tmux server protocol of its own.
- New behavior is implemented once. Native frontends delegate mutations to the
  shell CLI rather than duplicating spawn, kill, workflow, or integration policy.
- Backwards compatibility is preserved only where a current requirement explicitly
  demands it. Pre-2.0 migrations should favor a simpler correct model.
- Every mutating command has a bounded failure mode and a recovery story.
- Every machine-readable interface has versioned success and error schemas.
- Agent integrations extend the CLI and event contracts; they do not create a second
  orchestration API or make provider-specific state authoritative.
- Performance claims require reproducible measurements.

### 3.2 Explicit non-goals

Hydra will not:

- become a model router, token broker, or provider-specific conversation runtime;
- infer exact agent cognition from terminal text;
- silently copy secrets or arbitrary ignored files into worktrees;
- auto-merge changes without explicit policy and approval;
- execute untrusted repository configuration without a trust decision;
- build a proprietary remote protocol, distributed consensus system, or replicated
  database for fleet mode;
- require C, a compiler, `fzf`, `gh`, or a particular agent CLI for the core path;
- claim that replay reproduces nondeterministic agent behavior;
- expose a stable public `libhydra` ABI unless a later accepted requirement calls
  for one.

---

## 4. Current Baseline

### 4.1 Shipped: 1.5.0

Hydra 1.5.0 is released. The current repository contains:

- a single POSIX shell command dispatcher in [`bin/hydra`](../bin/hydra);
- modular command, state, tmux, TUI, Git, maintenance, and workflow-adjacent shell
  libraries in [`lib/`](../lib);
- one tmux session and one Git worktree per head;
- seven-field, space-delimited global state in `~/.hydra/map`;
- tags, groups, dependencies, queues, templates, messages, GitHub helpers, doctor,
  cleanup, and shell TUI behavior;
- shell lint and tests through the current [`Makefile`](../Makefile) and
  [CI workflow](../.github/workflows/ci.yml).

The repository does not yet contain `src/`, `tests/c/`, a C build target, native
artifacts, native-aware installation, or native CI.

### 4.2 Known hardening work carried into 1.5.1

- [x] Wire message cleanup into kill and cleanup paths.
- [x] Align maintenance stale-lock detection with the actual `mkdir` lock format.
- [x] Make every state writer use the same lock; never mutate after lock acquisition
      fails.
- [x] Create replacement files on the same filesystem as their destination before
      atomic rename.
- [x] Update shell completion for all commands shipped through 1.5.0.
- [x] Fix the known `hydra status` invalid-duration arithmetic path.
- [x] Preflight dirty-worktree handling before killing tmux or removing state so a
      failed worktree deletion cannot leave an untracked orphan.
- [x] Preserve group, dependency, pull-request, timestamp, and future metadata when
      regenerating a session.
- [x] Make queue ordering honor priority before request time, with stable FIFO order
      within one priority.
- [x] Escape every required JSON control character and add byte-oriented fixtures.
- [x] Give broadcast and startup delivery an explicit pane/transport target; never
      type into an agent prompt merely because that pane is active.
- [x] Define and correct the shell TUI row contract: the producer currently emits
      eight tab-separated fields while readers document or consume fewer fields and
      use a literal `\t` as `IFS` rather than an actual tab.
- [x] Add focused tests for maintenance, state-cache invalidation, messages, and TUI
      row parsing before using them as native parity oracles.
- [x] Fix the malformed `.gitignore` entry and stop tracking runtime state in the
      repository.
- [x] Document exactly which 1.5.0 behaviors are considered public contracts.

### 4.3 Current onboarding friction

- The documented installer and Make target assume root access and hard-code
  `/usr/local` rather than accepting a user-writable prefix.
- Installation verification checks that the executable exists but does not exercise
  `hydra version` or prove that the installed library path resolves.
- The Quick Start immediately spawns a head, while spawn defaults to `claude` unless
  the user discovers and sets `HYDRA_SKIP_AI=1`.
- `hydra doctor` checks core dependencies and state consistency but does not yet
  diagnose installation layout, writable state paths, repository readiness, or
  agent availability as one first-run readiness report.
- No fresh-home install/uninstall and disposable-repository smoke test protects the
  documented onboarding path.

---

## 5. Foundation Contracts

No native writer, workflow engine, replay system, or fleet mutation should ship
until the contracts in this section are accepted and tested.

### 5.1 Project, head, and run identity

Branch names are useful labels but insufficient identifiers. Global branch-keyed
state can collide across repositories, and a regenerated session can accidentally
consume stale signals from an earlier process.

Hydra will define:

| Identity | Meaning | Required properties |
| --- | --- | --- |
| `project_id` | One logical repository/project | Stable within a machine; explicitly mapped across fleet hosts |
| `head_id` | One Hydra-managed line of work | Stable across tmux regeneration; not derived only from branch text |
| `instance_id` | One concrete agent/tmux launch | New for each spawn or regenerate |
| `run_id` | One workflow or orchestration run | Unique and immutable |
| `step_id` | One workflow step within a run | Stable within the resolved workflow |
| `event_id` | One append-only event | Unique enough for ordering and deduplication |

#### Identity acceptance gates

- [ ] Two repositories with the same branch name cannot overwrite each other's
      state, locks, messages, signals, queues, or worktree paths.
- [ ] Regenerating a head creates a new `instance_id` and invalidates stale transient
      status without losing head history.
- [ ] Every event, signal, gate result, and workflow step can be correlated to its
      project, head, instance, and run where applicable.
- [ ] Filesystem names use encoded IDs rather than unsanitized user input.
- [ ] Identity derivation and cross-host mapping are captured in a short architecture
      decision record before implementation.

### 5.2 State layout and migration

State v2 ships with the next minor lifecycle release; it is not hidden in a patch
series or deferred to 2.0.

The selected layout must:

- namespace runtime state by project;
- retain human-readable scalar records;
- support atomic replacement and interrupted-write recovery;
- avoid ambiguous whitespace parsing;
- distinguish desired state from observed tmux/Git state;
- separate host-local runtime state from shareable repository configuration;
- include an explicit schema version;
- provide durable homes for the task, resolved launch and resume recipe, instance
  identity, lifecycle status, and bounded history associated with a head;
- key filesystem paths by project and encoded head identity rather than raw branch
  names;
- support a one-command backup, migration, verification, and rollback path from the
  current seven-field map.

The exact physical representation is an implementation decision. Before choosing
it, fixture tests must compare at least a versioned escaped-record design with a
per-head directory design. The simplest representation that is safe in POSIX shell
wins.

#### State acceptance gates

- [ ] `hydra state verify` identifies corruption without mutating state.
- [ ] `hydra state migrate --dry-run` reports the exact planned changes.
- [ ] Migration writes a recoverable backup before changing authoritative state.
- [ ] Interrupted writes leave either the old or new complete record, never a
      partially authoritative record.
- [ ] Shell-only Hydra can read and mutate the chosen state without C.
- [ ] Native and shell readers agree on all accepted and rejected fixtures.

### 5.3 One interoperable lock protocol

Shell and C must use the same lock representation. A shell `mkdir` lock and a C
`fcntl` lock do not coordinate with each other.

Initial policy:

- continue using atomic lock directories for cross-language coordination;
- record owner PID, host, creation time, operation, and optional head/run identity;
- never continue a state mutation after lock acquisition fails;
- make stale-lock removal evidence-based rather than age-only where process identity
  can be checked;
- use separate lock objects when atomic rename replaces a data-file inode;
- specify timeouts, interrupt cleanup, reentrancy, and crash recovery;
- add concurrent shell/shell and shell/C stress tests before any native writer ships.

Advisory OS locks may be reconsidered only after all writers have migrated together.

### 5.4 Event contract

Events are append-only JSON Lines with a versioned envelope. Human-readable text
views are derived output, not the authoritative record.

Illustrative envelope:

```json
{
  "schema_version": 1,
  "event_id": "evt_...",
  "occurred_at": "2026-08-25T12:00:00Z",
  "project_id": "project_...",
  "head_id": "head_...",
  "instance_id": "instance_...",
  "run_id": null,
  "type": "gate.completed",
  "actor": { "kind": "hydra", "id": "local" },
  "payload": { "gate": "tests", "result": "passed", "exit_code": 0 }
}
```

The event specification must define:

- schema evolution and unknown-field handling;
- per-writer atomicity and maximum record size;
- ordering, sequence numbers, and clock-skew behavior;
- partial-record detection and repair;
- rotation, retention, export, and privacy controls;
- duplicate detection where commands may be retried;
- redaction rules for prompts, environment variables, paths, and terminal output.

### 5.5 Lifecycle, completion, and evidence model

Completion is not a boolean and is not inferred from a missing tmux session.

Hydra tracks three separate channels:

| Channel | Examples | Authority |
| --- | --- | --- |
| Declared outcome | `done`, `failed`, `blocked` | A human, agent adapter, gate, or Hydra command identified by actor and instance |
| Observed status | output active/quiet, agent process exited, pane dead | tmux, Git, and bounded probes with observation time |
| Liveness | tmux session and process presence | tmux and the operating system |

Observed quiet is never declared success, and declared success does not prove that
required gates passed.

| State/evidence | Meaning | Terminal? |
| --- | --- | --- |
| `running` | Process or session is currently present | No |
| `idle_observed` | Pane output has not changed within a configured interval | No |
| `agent_reported` | Agent/profile reported a requested outcome | No |
| `process_exited` | Agent process or tmux session ended | No |
| `gate_passed` | A deterministic verification command passed | Depends on policy |
| `human_approved` | A named human gate was approved | Depends on policy |
| `completed` | The run's declared completion policy is satisfied | Yes |
| `failed` | A required step or gate failed without an available retry | Yes |
| `cancelled` | An authorized actor stopped the run | Yes |
| `abandoned` | Ownership expired or recovery was explicitly declined | Yes |

Signals include their source, actor, `instance_id`, optional evidence references,
and creation time. Workflow dependencies declare the evidence they require, such as
`agent_reported`, `gate_passed`, or `human_approved`.

The lifecycle contract also defines:

- signal behavior across kill, regenerate, resume, respawn, and dependency checks;
- source precedence and freshness when hooks and tmux observations disagree;
- stale-state rules after missed hooks, process exit, or tmux restart;
- invalidation of transient state whenever `instance_id` changes;
- explicit terminal outcomes for user cancellation and recoverable teardown failure.

### 5.6 Run provenance

Each workflow, arena, integration, or other multi-step operation writes a resolved
run manifest containing:

- starting commit/ref and worktree path;
- Hydra, Git, tmux, native-helper, and agent versions;
- resolved agent profile and exact launch arguments;
- hashes of trusted configuration and workflow definitions;
- inputs, step graph, retry policy, and completion policy;
- gate commands, exit codes, durations, and evidence references;
- produced artifacts and their hashes;
- terminal outcome and approval records;
- redaction metadata without secret values.

Replay replays or visualizes recorded Hydra events and decisions. It does not promise
to reproduce model output, network responses, or external side effects.

---

## 6. Native C Architecture

### 6.1 Boundary

C is used where a long-lived process, byte-safe parser, terminal state machine, or
parallel read aggregator materially improves the product.

| Area | Owner now | Native role |
| --- | --- | --- |
| Spawn, kill, switch, regenerate | POSIX shell | None; native UI delegates via argument-safe process execution |
| Hook and setup execution | POSIX shell | None |
| Workflow and integration policy | POSIX shell | Optional pure graph/query helpers later |
| State reads and indexing | Shell authoritative | Read-only native parser and index |
| State writes | POSIX shell | No native writer in 1.x; reconsider only after a measured need |
| Events, signals, messages | POSIX shell | Native read, validation, filtering, and indexing only in 1.x |
| tmux observation | tmux + shell fallback | Long-lived control-mode client and one-shot formatted queries |
| Pane capture/activity | tmux + shell fallback | Event stream, `window_activity`, bounded buffers, selective capture; hashes only as a last-resort fallback |
| JSON output | Shell fallback | Byte-safe serializer and schema validation |
| TUI input/rendering | Shell fallback | `termios`, escape parser, layout, cell diff, `wcwidth` handling |
| Git intelligence | Git CLI | Bounded parallel `git -C` calls and porcelain parsing |
| YAML/config policy | POSIX shell initially | No second parser until semantics stabilize and profiling justifies it |
| SSH configuration | OpenSSH | Use `ssh -G`; do not implement an SSH config parser |

### 6.2 Artifact model

The initial native distribution contains two executables backed by an internal
static archive:

```text
src/
  libhydra/        internal C library; no public ABI promise
  core/            hydra-core one-shot read/query helper
  tui/             hydra-tui foreground event loop and renderer
```

- `hydra-core` serves versioned snapshot, capture, validation, and later read-only
  diagnostic commands.
- `hydra-tui` links `libhydra` in-process; it does not invoke `hydra-core` for every
  refresh.
- `hydra-tui` invokes the shell `hydra` executable for mutations using `posix_spawn`
  or `fork`/`exec` with an argument vector, never by concatenating a shell command.
- `libhydra` remains internal. The supported boundary is the command protocol.
- Release binaries may depend on platform system libraries but no third-party
  runtime library. “Fully static” is not a cross-platform requirement.

### 6.3 tmux primitives before native code

The shell baseline should first replace repeated per-head subprocess loops with tmux
and Unix primitives that already provide the required semantics:

- one formatted `tmux list-panes -a` snapshot containing session/window/pane IDs,
  `window_activity`, `pane_current_command`, and `pane_dead`;
- `tmux pipe-pane` for bounded, policy-controlled pane history;
- `tmux wait-for` as a wake-up accelerator after checking durable Hydra state;
- `tmux set-environment` for non-secret `HYDRA_*` head identity and paths;
- `gh --jq`, `ssh -G`, Git porcelain, and bounded shell fan-out before custom parsers
  or thread pools are considered.

`session_activity` is not used as a proxy for detached pane output. Pane hashing and
periodic capture remain fallback observations only. `wait-for` notifications never
replace persisted outcomes because tmux server state can disappear.

### 6.4 tmux control mode

The native TUI should prototype tmux control mode before designing a high-frequency
polling loop. The prototype must evaluate:

- asynchronous session/window/pane notifications;
- `%output` and bounded per-pane scrollback;
- format subscriptions for state that changes over time;
- flow control and recovery through selective `capture-pane`;
- tmux version and capability probing;
- reconnect after tmux server restart;
- polling fallback when control mode is unavailable or unreliable.

Control mode does not replace tmux. It maintains one long-lived tmux client process
and parses tmux's documented text protocol.

### 6.5 Shell/native command protocol

Every `hydra-core` command supports:

- `--protocol-version` or a capability command;
- public JSON output with an explicit schema version;
- one strictly specified escaped-tabular output for POSIX shell consumption;
- stdout containing records only and stderr containing diagnostics only;
- stable exit categories: success, invalid input, unsupported protocol, unavailable
  dependency, corrupt state, transient failure, and internal failure;
- a bounded timeout when invoked by the shell;
- fixtures for empty, malformed, permission-denied, stale, and concurrent state.

The shell must fall back only for absence, unsupported capability, or an explicitly
classified transient native failure. Corrupt native output must produce a visible
warning before fallback; it must never be silently accepted.

### 6.6 Native TUI minimum viable scope

The first native TUI contains only:

- list and detail views;
- keyboard navigation and search;
- selected-pane preview;
- status/event summary;
- spawn, switch, kill, and regenerate actions delegated to the shell CLI;
- terminal resize handling;
- safe exit and shell-TUI fallback.

The shell TUI remains a maintained basic fallback for essential status and actions.
Parity is required for source data, schemas, and delegated actions, not for every
native layout or visualization. Noninteractive and CI callers use CLI JSON rather
than either TUI.

Mouse input, drag resizing, animated graphs, themes, command-language parsing,
sparklines, and advanced visualization are post-MVP.

### 6.7 Terminal acceptance

Before native-by-default dispatch:

- [ ] SIGINT, SIGTERM, SIGHUP, normal exit, and internal error restore terminal state.
- [ ] `TERM=dumb`, missing color support, non-TTY stdin/stdout, and broken pipes fail
      cleanly or dispatch to the shell fallback.
- [ ] Resize races and narrow terminals are covered.
- [ ] UTF-8, invalid byte sequences, combining characters, and wide characters do
      not corrupt the renderer or memory.
- [ ] Escape-sequence ambiguity, bracketed paste, mouse input, and timeouts are
      bounded even when optional features are disabled.
- [ ] Pane output is treated as untrusted terminal data and cannot inject commands
      into Hydra's input parser.
- [ ] A deterministic headless fixture mode renders a fixed number of frames at an
      explicit terminal size without depending on platform-specific `script(1)`.

---

## 7. Security and Trust Model

The roadmap adds repository configuration, agent launch recipes, steering, workflow
commands, hooks, and SSH. Each is a code-execution boundary.

### 7.1 Configuration trust

- User configuration is trusted as the current user.
- Repository configuration requires an explicit per-project trust decision before
  commands, hooks, setup, agent wrappers, or workflows execute.
- A changed trusted configuration hash requires renewed confirmation for newly
  introduced executable behavior.
- Read-only metadata can be inspected before trust is granted.
- Machine-readable commands provide a noninteractive trust policy rather than
  silently accepting repository code.

### 7.2 Agent launch safety

- Built-in profiles use fixed executable and argument arrays.
- Custom profiles reference an executable wrapper or an argv-safe declarative list;
  they are not interpolated shell snippets.
- CLI input never becomes an executable command string.
- Environment variables are passed by explicit name; secret values are not persisted
  in state, events, manifests, or diagnostic output.
- Profile checks are bounded executable probes such as `--version`, not arbitrary
  `check:` shell fragments.
- Regenerate resolves the current profile by name and records the new resolved argv
  in the instance manifest.
- Agent adapters are capability-probed and version-reported; missing or changed hook
  capabilities degrade to explicit lower-confidence observation rather than being
  silently assumed.

### 7.3 Workflow and hook safety

- Workflow `exec` and hooks inherit the repository trust decision.
- Commands have timeouts, captured exit status, bounded output, and documented
  interrupt behavior.
- Destructive Hydra actions remain confirmation-gated unless an explicit policy
  authorizes noninteractive execution.
- Adapter events are accepted only when their project, head, and `instance_id` match
  the active registration.
- Permission relays never auto-approve from an agent-authored request. A human or
  previously trusted human-authored policy must produce the decision record.
- Pane history and transcripts have explicit enablement, retention, size, redaction,
  and file-permission policies because terminal output may contain secrets.
- Artifact and handoff paths are confined to allowed roots, reject traversal, and
  record content hashes.

### 7.4 Fleet safety

- OpenSSH remains responsible for host keys, authentication, and connection policy.
- Hydra uses configured host aliases and `ssh -G`; it does not parse nested SSH
  configuration itself.
- Remote requests avoid interpolated shell command strings.
- Mutations require protocol/capability negotiation and fail closed on incompatible
  versions.
- Runtime state remains host-local; only declarative configuration and explicit
  bundles are synchronized.

---

## 8. Versioned Delivery Plan

### 8.1 1.5.1 — Shell Baseline Hardening

**Theme:** Make the shipped shell implementation a trustworthy parity oracle.

#### Scope

- [x] Complete the hardening list in section 4.2.
- [x] Add regression tests for status, lock failure, same-filesystem replacement,
      message cleanup, kill ordering, regenerate preservation, queue priority, JSON
      control bytes, explicit pane targeting, and TUI framing.
- [x] Document the current state fields and behavior before migration.
- [x] Replace repeated per-head tmux probes with formatted batch queries where this
      preserves the documented behavior.
- [x] Use `window_activity`, `pane_current_command`, and `pane_dead` as the primary
      shell activity/process observations; keep pane hashing only as a fallback.
- [x] Record baseline performance for list, TUI refresh, doctor, and pane capture at
      5 and 20 heads.
- [x] Add a supported-platform statement for the shell-only release.

#### Release gates

- `make lint` passes.
- `make test` passes under the required shell.
- No known state-corruption or unlocked-write path remains accepted behavior.
- A dirty worktree cannot be orphaned by normal kill failure.
- Regenerate preserves every accepted metadata field.
- Queue fixtures prove priority and FIFO behavior.
- JSON output accepts or safely rejects every byte fixture without producing invalid
  JSON.
- The shell TUI row schema has a contract fixture.
- Baseline measurements are reproducible; no speedup claims are made yet.

#### Deferred

- State migration, new lifecycle behavior, C code, workflows, and advanced TUI
  features.

### 8.2 1.5.2 — Frictionless First Run

**Theme:** Turn the existing shell product into a five-minute, agent-optional first
experience without introducing new lifecycle architecture.

#### Scope

- [x] Support installation to a writable `PREFIX` without root, with a matching
      uninstall path and equivalent behavior from `install.sh` and `make install`.
- [x] Document running directly from a source checkout as a supported evaluation
      path that does not require installation.
- [x] Verify installation by running `hydra version`, confirming library discovery,
      and reporting the exact installed paths.
- [x] Expand `hydra doctor` to report Git and tmux versions, writable `HYDRA_HOME`,
      install/library consistency, repository and worktree readiness, detected agent
      CLIs, and a concrete remediation for every failure.
- [x] Replace the Quick Start with a replayable five-minute tour: install or run from
      source, verify, create a disposable first head in a throwaway repository, list
      and enter it, then kill it and verify cleanup.
- [x] Make the existing shell-only path obvious for users with no agent installed,
      including the current `HYDRA_SKIP_AI=1` behavior.
- [x] Add shell-completion installation instructions for supported shells.
- [x] Rewrite first-run errors to name the failed precondition and the next command
      or documentation action the user should take.
- [x] Add fresh-home, fresh-prefix install/uninstall tests and a throwaway-repository
      onboarding smoke test that cannot create branches or worktrees in Hydra's own
      source repository.

#### Release gates

- A clean macOS or Linux user can install to a writable prefix without `sudo`.
- `install.sh`, `make install`, uninstall, and run-from-source instructions agree on
  binary and library discovery.
- With no agent CLI and no manual configuration, the documented path reaches a
  created head, enters or inspects it, and cleans it up in under five minutes.
- The smoke scenario leaves no Hydra tmux session, worktree, branch, or state debris.
- Every onboarding failure reports a specific recovery action.
- Every Quick Start command is exercised by a replayable test or release checklist.

#### Deferred

- `hydra init`, `spawn --dry-run`, a first-class `--no-agent` flag, automatic profile
  selection, generated project configuration, task injection, state v2, lifecycle
  changes, and C code.

### 8.3 1.6.0 — Identity, Lifecycle, and Evidence

**Theme:** Give every head durable identity, task, lifecycle, evidence, and recovery
semantics using the shell implementation first.

#### Scope

- [x] Accept the identity architecture decision.
- [x] Specify state v2, events v1, lifecycle v1, JSON success/error v1, and the shared
      lock protocol.
- [x] Implement state verify, backup, migration dry-run, migration, and rollback.
- [x] Namespace state and worktree identity by project.
- [x] Implement versioned JSONL events with verify, tail, filter, retention, and
      repair behavior.
- [ ] Implement instance-scoped declared outcomes, observed status, liveness, and
      explicit completion policies.
- [ ] Make dependencies name the evidence or state they require instead of treating
      session disappearance as success.
- [ ] Add `hydra wait` over durable state, using `tmux wait-for` only as an optional
      wake-up accelerator with rechecks and timeouts.
- [x] Add agent profiles with launch, prompt, resume, environment, capability, and
      lifecycle-adapter declarations.
- [ ] Ship capability-probed hook adapters only for verified agent surfaces; all
      adapters translate into the provider-neutral Hydra lifecycle/event schema.
- [ ] Add one generic adapter-ingest command that validates structured stdin,
      correlates the active instance, and emits generic lifecycle events.
- [x] Add `hydra agent list`, `show`, `doctor`, and `init`, plus
      `hydra capabilities --json` for agents and automation.
- [x] Add `hydra init` for project identity, trust, profiles, bootstrap, worktree-root,
      and local-ignore configuration; workflow initialization follows in 1.9.
- [x] Add `spawn --dry-run` and a first-class `--no-agent` mode so users can inspect
      setup and create a plain shell head without environment variables.
- [x] Select an explicit, configured, or positively detected agent profile—or none;
      never silently default to an unavailable agent executable.
- [x] Add task injection through `spawn --prompt`, `--prompt-file`, and issue-body
      input, recording the resolved task in head state.
- [ ] Add durable resume metadata plus `hydra resume`; regenerate creates a new
      instance while preserving head history.
- [ ] Add policy-controlled pane history, archival, and teardown behavior with size,
      retention, redaction, and permission controls.
- [ ] Stream bootstrap/setup output, export explicit non-secret `HYDRA_*` identity
      and path variables, and support dry-run plus pre/post teardown hooks.
- [ ] Add typed messages and safe-point steering with delivery receipts; inbox-only
      remains the universal fallback.
- [ ] Add rate-limited local notification sinks for named lifecycle events without
      requiring the TUI to be running.
- [ ] Add `hydra exec` for out-of-band commands across selected worktrees with
      bounded parallelism, captured results, and explicit trust behavior.
- [ ] Add `hydra diff`, `hydra review`, and `hydra list --git` over recorded base refs
      and Git porcelain.
- [ ] Add versioned JSON success and error output to automation-relevant commands.
- [ ] Add per-head provenance containing resolved profiles, tasks, versions, trusted
      configuration hashes, and lifecycle sources.
- [ ] Publish state, lifecycle, agent-adapter, security, and automation documentation
      with the release.

#### Release gates

- Shell-only installation and all shell tests pass without a compiler.
- Migration is reversible using a generated backup.
- Two repositories with identical branch names remain isolated.
- A stale event or signal from a prior instance cannot satisfy current work.
- Declared outcome, observed status, and liveness remain distinguishable in CLI and
  JSON; none is mislabeled as a future gate pass or human approval.
- Hookless agents retain a complete Tier-0/Tier-1 experience with explicit confidence
  labels.
- Adapter absence, version skew, malformed hook input, and missed hook events have
  deterministic fallback behavior.
- A clean user can run `hydra init`, inspect `spawn --dry-run`, and create a
  task-aware head with either an explicit agent profile or `--no-agent`.
- Task injection and resume work without typing into a pane mid-turn.
- Setup, teardown, transcript, and hook paths do not persist secrets by default.
- `hydra exec` cannot confuse out-of-band command execution with agent steering.
- A task -> spawn -> handoff -> declared outcome -> resume or teardown scenario has
  an inspectable event and provenance history.

#### Deferred

- All C code, native TUI work, general workflows, automated integration, and fleet
  operations.

### 8.4 1.7.0 — Parallel Safety and Native Core

**Theme:** Make many heads safe on one repository, then prove the smallest optional
native acceleration over stable contracts.

#### Scope

- [ ] Add `hydra claim` intent claims with owner, path pattern, access mode, reason,
      and expiry.
- [ ] Add scoped heads with injected scope instructions and a gate that identifies
      out-of-scope changes without pretending sparse checkout is a security boundary.
- [ ] Add collision analysis that distinguishes declared claims, changed-file
      overlap, textual conflict risk, and actual merge simulation.
- [ ] Add per-head environment profiles covering ports, compose project names,
      database names, and cleanup, with atomic allocation where shared resources are
      reserved.
- [ ] Add verification gates with captured evidence and explicit human approval
      gates.
- [ ] Add typed context packs containing selected diffs, manifests, notes, history,
      and artifact references without silently bundling secrets.
- [ ] Add safe `hydra sync` and `hydra land` primitives with preflight, gate, archive,
      teardown, dry-run, and recoverable failure behavior.
- [ ] Add `hydra du` and policy-driven `hydra gc` with dry-run and preservation of
      dirty or untracked work by default.
- [ ] Add worktree doctor actions around Git lock, unlock, move, repair, and dry-run
      prune behavior.
- [ ] Add `src/`, `tests/c/`, internal `libhydra`, and a read-only `hydra-core`
      scaffold in a minor release.
- [ ] Accept shell/native command protocol v1 before integrating the helper into any
      shell command.
- [ ] Add `make build-core`, `make test-c`, `make test-parity`, `make test-all`,
      sanitizer, and benchmark targets.
- [ ] Implement only capability reporting, state/event validation, canonical JSON,
      and read-only snapshot aggregation in the first native slice.
- [ ] Extend the 1.5.2 installer contract to native artifacts, adding offline/source,
      checksum, architecture, version-handshake, and rollback behavior.
- [ ] Add supported macOS and Linux native CI plus native-present, native-absent, and
      version-skew qualification.
- [ ] Prototype tmux control mode against bounded polling and publish reproducible
      reliability and performance findings before committing the TUI to it.
- [ ] Publish native architecture/distribution and parallel-safety documentation.

#### Release gates

- Collision output is labeled as claim, overlap, predicted conflict, or observed
  conflict; it never presents overlap as certainty.
- Scope gates detect accepted out-of-scope fixtures and report read-only versus
  writable scope distinctly.
- Resource allocations are unique under concurrent spawn and are released or
  recoverable after failure.
- `sync`, `land`, and `gc` preserve dirty/untracked work unless an explicit destructive
  policy was selected.
- Shell-only Hydra remains fully functional without a compiler or native artifacts.
- Native absence, old protocol, crash, timeout, malformed output, and permission
  failures have deterministic fallback behavior.
- Native artifacts have checksums and declare platform/system-library requirements.
- Read-only native output is schema-equivalent to the shell path.
- Benchmarks show a useful measured win on a named accepted path; otherwise native
  dispatch remains experimental.

#### Deferred

- Native state writes, native workflow policy, native YAML, a public `libhydra` ABI,
  the native TUI, general workflows, and fleet operations.

### 8.5 1.8.0 — Native Mission Control

**Theme:** Deliver a faster, calmer control surface over stable coordination data.

#### Scope

- [ ] Implement the terminal-safe native TUI MVP from section 6.6.
- [ ] Use tmux control mode if the prototype met reliability and performance gates;
      otherwise use measured bounded polling.
- [ ] Show declared, observed, liveness, stale, and unavailable status distinctly.
- [ ] Show event, signal, message, and gate summaries without inventing agent state.
- [ ] Add claims, collision, scope, queue, resource, Git-diff, and approval views over
      existing CLI/state contracts.
- [ ] Delegate all mutations to the shell CLI with argument-safe execution.
- [ ] Add command palette search over explicit local actions, not a natural-language
      command interpreter.
- [ ] Add a recovery board for stale locks, dead sessions, orphan worktrees, teardown
      failures, and interrupted lifecycle transitions.
- [ ] Surface existing notification and adapter capability status without making the
      TUI responsible for delivering lifecycle events.
- [ ] Add `hydra tui --native`, `hydra tui --basic`, capability diagnostics, and the
      deterministic headless fixture mode.
- [ ] Keep native dispatch opt-in through the first patch release.
- [ ] Publish native/basic TUI behavior, fallback, keymap, accessibility, and recovery
      documentation.

#### Release gates

- Terminal acceptance in section 6.7 passes on the supported platform matrix.
- Native and shell lists agree for clean, stale, malformed, and changing state.
- Native crashes do not corrupt state or leave the terminal unusable.
- Interactive latency and CPU usage meet measured budgets at 5, 20, and 100 heads.
- A manual accessibility review covers keyboard-only use, no-color mode, narrow
  terminals, and readable status language.
- Every dashboard state links to its inspectable source record and confidence.
- Basic shell TUI data and actions remain functional without native files; native
  render-only features do not require shell reimplementation.

#### Deferred

- Mouse, drag layouts, animations, workflow graph editing, fleet view, exact context
  or cost routing, and natural-language command parsing.

### 8.6 1.9.0 — Workflows and Verified Integration

**Theme:** Coordinate multi-step work and close the loop from parallel heads to one
verified change.

#### Workflow scope

- [ ] Add a strict, documented workflow schema and reject unsupported YAML.
- [ ] Add list, show, validate, dry-run, run, status, cancel, and resume commands.
- [ ] Support bounded `spawn`, `wait`, `exec`, `message`, `gate`, `approve`, `kill`,
      and parallel fan-out/join steps.
- [ ] Build workflow execution on the accepted `hydra wait`, `exec`, lifecycle,
      profile, gate, and resource contracts rather than reimplementing them.
- [ ] Persist a resolved workflow copy and run manifest before execution.
- [ ] Define atomic step transitions, run ownership, heartbeat/lease, stale-run
      recovery, and cancellation behavior.
- [ ] Define retry idempotency and require explicit policy for non-idempotent steps.
- [ ] Add resource limits, queue fairness, per-run parallelism, and disk safeguards.
- [ ] Add a file-backed dynamic task pool only after the static DAG path demonstrates
      a real need for workers to claim tasks at runtime.

#### Integration worktree

- [ ] Add `hydra integrate <run-or-group> --dry-run`.
- [ ] Create an isolated, disposable integration worktree from an explicit base.
- [ ] Preview merge order and conflicts before mutation.
- [ ] Apply changes only inside the integration worktree.
- [ ] Run required verification gates and produce an integration report.
- [ ] Require explicit promotion or approval before updating the target branch.
- [ ] Preserve failed integration state or remove it through a recoverable cleanup
      command chosen by the user.
- [ ] Add a guarded merge train only after single-run integration, `land`, conflict
      preview, provenance, and gate recovery meet their acceptance criteria.
- [ ] Publish workflow schema, automation, cancellation, recovery, and integration
      documentation with exercised examples.

#### Release gates

- Killing the workflow runner does not lose authoritative step outcomes.
- Resume never silently repeats a non-idempotent completed side effect.
- Cancellation reaches running children or reports exactly which children remain.
- Collision output is labeled as claim, overlap, predicted conflict, or observed
  conflict; it never presents overlap as certainty.
- An interrupted integration leaves the target branch unchanged.
- One issue -> parallel implementation/test/review -> gates -> integration can be
  demonstrated entirely from local, inspectable state.
- Merge-train failure identifies the exact candidate, gate, target ref, and recovery
  action without changing the target branch unexpectedly.

#### Deferred

- Best-of-N arenas, automatic issue decomposition, animated DAGs, external heads,
  schedules, and remote workflows.

### 8.7 2.0.0 — Stable Local Orchestration Interface

**Theme:** Commit to the contracts that survived real use.

#### Scope

- [ ] Remove superseded pre-2.0 commands and state compatibility paths.
- [ ] Publish stable JSON success/error schemas and protocol negotiation rules.
- [ ] Publish workflow, event, manifest, completion, and integration contracts.
- [ ] Publish the agent profile, adapter capability, task, resume, transcript, scope,
      and basic/native TUI contracts that survived real use.
- [ ] Define supported platform and upgrade policies.
- [ ] Define deprecation windows for post-2.0 public interfaces.
- [ ] Provide a complete migration guide from the last 1.x release.
- [ ] Publish a security and trust model covering local configuration, agents,
      workflows, artifacts, and the prerequisites for the fleet pilot.

#### Release gates

- One unchanged release commit passes shell-only, native, parity, migration,
  lifecycle, workflow, integration, and security qualification lanes.
- Release artifacts and checksums are produced from the qualified commit.
- Documentation examples are exercised in CI or a replayable release checklist.
- No foundational state migration is deferred into 2.0 merely to preserve an
  accidental 1.x format.

### 8.8 2.1.0 — Fleet Pilot

**Theme:** Observe and coordinate trusted Hydra installations over SSH using the
stable 2.0 contracts, without a server or shared runtime database.

Fleet begins as an opt-in pilot, not a compatibility promise.

#### Scope

- [ ] Add remote aliases that defer host resolution to OpenSSH.
- [ ] Add a remote handshake exposing Hydra version, protocol versions,
      capabilities, project mappings, and native availability.
- [ ] Add an explicit remote bootstrap that installs a pinned compatible shell Hydra
      on a trusted host when Hydra is absent; Git and tmux remain prerequisites.
- [ ] Add bounded-parallel, read-only fleet list and doctor aggregation.
- [ ] Add remote attach through ordinary `ssh -t ... tmux attach` behavior.
- [ ] Add explicit remote spawn, signal, cancel, and workflow operations only for
      compatible capabilities.
- [ ] Add per-host timeouts, offline state, partial-result reporting, and cancellation.
- [ ] Reuse SSH multiplexing when configured or explicitly enabled.
- [ ] Add a native TUI fleet view only after the CLI protocol is stable.
- [ ] Add export/import for declarative project configuration and historical run
      bundles; exclude live tmux mappings and host-local locks.
- [ ] Add a wake/reconcile command that rechecks sessions, runs, locks, and remote
      reachability after sleep or network loss.

#### Release gates

- Host-key failures, authentication failures, timeouts, partial outages, and version
  mismatch are distinct machine-readable outcomes.
- One offline host cannot block local or other-host visibility beyond its timeout.
- Remote mutations fail closed when the required capability is unavailable.
- No live state file is treated as authoritative on two hosts.
- Remote command arguments survive spaces and metacharacters without shell injection.
- A laptop sleep/reconnect scenario reconciles state without losing local work.

#### Deferred or experimental

- shadow heads that synchronize dirty files;
- presence beacons across people;
- inbox federation;
- handoff tokens between users;
- external cloud heads;
- air-gapped Git-bundle transport.

These require separate security and conflict models before receiving a release.

---

## 9. Agent Profiles and Head Lifecycle

Hydra remains agent-agnostic. Profiles and adapters translate optional provider
capabilities into Hydra's generic CLI, lifecycle, and event contracts without making
an agent's internal schema authoritative.

### 9.1 Integration tiers

| Tier | Capability | Contract |
| --- | --- | --- |
| 0 | tmux observation | Any CLI in tmux; process, pane, output activity, and liveness only |
| 1 | Launch and resume profile | Executable argv, version probe, prompt mode, resume recipe, environment-name allowlist |
| 2 | Lifecycle adapter | Optional capability-probed hooks translated into generic Hydra events and outcomes |
| 3 | Structured telemetry | Optional versioned status with source, freshness, privacy, and retention policy |
| 4 | Duplex transport | Experimental safe-point steering or permission relay with explicit human policy |

Tier 0 and Tier 1 are the universal experience. Heuristic pane activity is reported
as observed active/quiet, never as exact cognition. Higher-tier status always reports
its provider, capability version, source, and freshness.

### 9.2 Profile requirements

- duplicate profile keys are rejected;
- user and repository precedence is explicit;
- a plain shell head with no agent is a first-class launch mode, not an error or an
  undocumented environment-variable workaround;
- when no profile is explicit or configured, Hydra selects only a positively
  detected agent and otherwise offers the no-agent path instead of naming a missing
  default executable;
- executable resolution and arguments are shown by `hydra agent show`;
- repository profiles are inert until the project is trusted;
- built-in profiles can change only through a Hydra release;
- custom wrappers live at explicit paths and are never synthesized from CLI input;
- prompt delivery declares argument, file, or safe-point transport explicitly;
- resume recipes consume recorded provider/session identifiers without putting those
  identifiers into global branch-keyed state;
- adapter capabilities are probed and exposed by `hydra agent doctor`;
- unsupported adapter events degrade to Tier 0 or Tier 1 rather than failing spawn;
- regenerate records the resolved profile and creates a new instance identity.

### 9.3 Bootstrap and teardown requirements

Worktree bootstrap supports:

- dependency/setup commands from trusted configuration;
- a dry-run showing every command and copied path;
- explicit ignored-file inclusion patterns;
- a denylist for sensitive defaults such as credentials and private keys;
- a configurable worktree root and `hydra path` query, with stored paths used instead
  of recomputing them from the caller's current directory;
- per-head environment names, compose/database identifiers, and allocated ports;
- streamed setup output plus timeouts, logs, exit codes, and cleanup policy;
- explicit non-secret `HYDRA_PROJECT_ID`, `HYDRA_HEAD_ID`, `HYDRA_INSTANCE_ID`,
  branch, worktree, task, and state paths where applicable;
- `pre-kill`, teardown, and post-kill phases with dry-run and recoverable failure;
- idempotent retry or an explicit non-idempotent marker;
- a `HYDRA_SKIP_SETUP`-equivalent escape hatch that is recorded in provenance.

### 9.4 Task, history, and resume requirements

- Spawn accepts task text, a task file, or an issue body and records the resolved task
  before launching the agent.
- Pane history is optional or policy-controlled, bounded by size and retention, and
  archived before destructive cleanup when enabled.
- Resume uses the recorded profile recipe and provider/session identifier, creates a
  new `instance_id`, and retains the prior instance history.
- Kill checks worktree cleanliness and teardown preconditions before destroying the
  only live session or authoritative state reference.
- Recovery and ghost-head views are projections over retained task, history, Git,
  lifecycle, and resume records; they are not a separate authoritative state tree.

---

## 10. Testing and Qualification

### 10.1 Test layers

| Layer | Purpose | Examples |
| --- | --- | --- |
| Shell unit/integration | Authoritative user behavior | existing `tests/*.sh`, state, locks, kill/regenerate, queue, messages, lifecycle, workflows |
| Onboarding/packaging | Clean-user installation and first-head path | fresh `HOME`/`PREFIX`, run from source, install/uninstall, no-agent throwaway repository |
| C unit | Pure parsing, rendering, protocol, and data structures | `tests/c/test_*.c` |
| Contract/parity | Shell/native agreement | snapshot, JSON, errors, malformed inputs, fallback |
| Adapter contract | Provider input translated into generic lifecycle events | capability versions, malformed hooks, missed events, stale instances |
| PTY/TUI | Terminal lifecycle and interaction | scripted keyboard, resize, signals, non-TTY, deterministic headless frames |
| Concurrency | Locking and append safety | shell/shell and shell/C stress, N parallel spawns, task claims |
| Recovery | Interrupted state transitions | killed writer, stale run, partial event, broken tmux |
| Security | Trust and injection boundaries | hostile paths, config, argv, hooks, transcripts, SSH, artifact traversal |
| Fleet | Partial remote failure | mock SSH plus gated real-host smoke |

The 1.5.1 suite gains a repository-local `tests/bin/tmux` fixture for deterministic
shell behavior before native parity tests depend on it. Real-tmux integration remains
a separate gated lane.

### 10.2 Required build targets

```text
make lint          shell lint and syntax
make test          shell-only tests; compiler not required
make test-install  fresh-prefix install, verification, and uninstall
make smoke-onboarding  throwaway-repository, no-agent first-head path
make build-core    build hydra-core
make build-tui     build hydra-tui
make test-c        C unit tests
make test-parity   shell/native protocol parity
make test-all      shell, C, parity, and supported integration lanes
make sanitize      supported sanitizer builds and tests
make bench         reproducible benchmark scenarios
```

### 10.3 Native CI

Native CI covers supported macOS and Linux architectures where runners are
available. It includes:

- warning-clean builds with an explicit C/POSIX standard;
- debug sanitizer execution where supported;
- shell-only tests without native artifacts;
- native-present and native-absent installations;
- old/unsupported native protocol fixtures;
- shell/native version-handshake and release-tag mismatch fixtures;
- deterministic headless TUI fixtures at fixed dimensions;
- release dependency inspection with `otool -L` or `ldd`;
- checks that generated artifacts match the qualified source commit.

### 10.4 Benchmark policy

Benchmarks report, rather than assume:

- head count and visible-pane count;
- cold versus warm state;
- tmux, OS, architecture, and hardware;
- p50 and p95 wall time;
- subprocess/command count;
- CPU and resident memory for long-lived TUI operation;
- shell fallback and native results over the same fixture.

Release documentation may claim only measured results. A nonblocking trend job may
run weekly, but a regression budget for a named accepted native feature is a release
gate.

### 10.5 Fuzzing

Parser and terminal-input fuzzing begins when each parser lands, even while its
public semantics remain provisional:

- state and event record parsers;
- native command protocol;
- tmux control-mode decoding;
- ANSI/escape input;
- workflow validation.

Fuzzing is not a substitute for fixture-based shell/native parity.

---

## 11. Differentiating Feature Priorities

### 11.1 Commit to after foundations

1. **Evidence-bearing completion** — dependencies can require tests, review, or human
   approval rather than trusting session disappearance.
2. **Task-aware, resumable heads** — task, profile, instance, history, and recovery
   remain connected across tmux loss and regeneration.
3. **Cross-vendor lifecycle adapters** — verified optional hooks enrich the same
   provider-neutral state model used by hookless terminal agents.
4. **Run provenance** — every orchestration decision and artifact can be traced to
   its inputs and environment.
5. **Scoped collision and intent awareness** — show declared scope, claims, file
   overlap, and actual merge simulation as distinct signals.
6. **Safe integration worktrees** — convert parallel agent output into a verified,
   reviewable candidate without touching the target branch.
7. **Recovery board** — reconcile tmux, Git, locks, state, workflows, and integration
   after crashes or sleep.
8. **Event-driven native mission control** — a fast UI over the same inspectable
   state, not a separate hidden database.

### 11.2 Consider after observed demand

- best-of-N arenas over the gate and integration primitives;
- issue decomposition templates over the workflow engine;
- dynamic cross-vendor task pools after static workflows demonstrate the need;
- external heads and schedules after local lifecycle contracts stabilize;
- comparison views for two or more heads;
- historical ghost heads as recovery shortcuts;
- accessible event announcer views;
- Git-bundle transport for air-gapped hosts.

### 11.3 Keep experimental

- exact context-window or cost routing across heterogeneous agents;
- provider-specific cognition dashboards;
- automatic issue decomposition without a reviewed plan;
- dirty-file shadow-head synchronization;
- Git-as-consensus fleet registries;
- multi-user presence and handoff tokens;
- arbitrary plugin marketplaces.

Experimental items move into a release only after a bounded prototype demonstrates
user value, security boundaries, and a simpler implementation path than composing
existing Hydra primitives.

---

## 12. Release Definition of Done

Every release must satisfy all applicable items:

- [ ] Scope matches the version section; summary tables do not promise unspecified
      features.
- [ ] Required predecessor gates are complete.
- [ ] Shell-only behavior remains functional unless the release explicitly changes a
      documented contract and provides migration.
- [ ] Native and shell parity is proven for each accelerated command.
- [ ] Basic/native TUI parity is proven for source data and delegated actions; render
      parity is not required.
- [ ] State/schema migrations have dry-run, backup, verification, and rollback.
- [ ] Lifecycle transition tests cover kill, regenerate, resume, stale adapters,
      process exit, and instance replacement.
- [ ] Supported agent adapters publish a dated capability matrix and degrade cleanly
      when capabilities are absent or changed.
- [ ] Failure, cancellation, interruption, and recovery behavior is tested.
- [ ] Security/trust changes are documented and tested.
- [ ] Performance claims cite reproducible measurements.
- [ ] Install, upgrade, uninstall, and offline/source workflows are verified.
- [ ] Applicable onboarding releases pass from a clean home directory, a writable
      non-root prefix, and a machine with no agent CLI installed.
- [ ] CLI help, completions, README, architecture docs, and examples agree.
- [ ] First-run documentation is replayed exactly and cleanup leaves no session,
      branch, worktree, or state debris.
- [ ] One end-to-end acceptance scenario is captured with its exact commands and
      resulting evidence.
- [ ] No unrelated known defect is hidden by the new native path.

Passing unit tests alone is supporting evidence, not release acceptance.

---

## 13. Open Decisions

These decisions block specific milestones and should be resolved through short,
recorded architecture decisions.

| Decision | Blocks | Resolution evidence | Status |
| --- | --- | --- | --- |
| Installation prefix, layout, and version discovery | 1.5.2 onboarding | Fresh-prefix install/uninstall plus run-from-source parity | Accepted: `$PREFIX/bin` + `$PREFIX/lib/hydra`; relative discovery; `HYDRA_ROOT` first |
| Default agent detection and no-agent selection | 1.6 guided onboarding | Clean-home fixtures with zero, one, and multiple detected agent CLIs | Open |
| Project identity generation | State v2 | Collision, repo move, clone, and duplicate-branch fixtures | Accepted: opaque ID in Git common dir; ADR 0001 |
| Physical state v2 representation | Migration and native reader | Shell simplicity, atomicity, malformed-input fixtures | Accepted: per-head scalar directory; ADR 0001 |
| Pane-history default, retention, and redaction policy | Lifecycle release | Secret-bearing fixtures, permissions, disk growth, recovery value | Open |
| Agent adapter packaging and capability matrix | Lifecycle release | Verified vendor fixtures, trust boundaries, version-skew fallback | Open |
| Worktree-root default and stored-path migration | Lifecycle release | Multiple repositories, repo moves, wrong-cwd kill, recovery fixtures | Open |
| Supported native OS/architecture matrix | Native distribution | CI availability and release artifact tests | Open |
| tmux control mode versus bounded polling | Native TUI | Reliability and performance prototype | Open |
| Trusted repository configuration UX | Profiles/workflows/bootstrap | Threat model and noninteractive policy tests | Open |
| Workflow YAML subset | Workflow release | Schema fixtures, duplicate-key rejection, error quality | Open |
| Native TUI default threshold | 1.8 default switch | Crash rate, terminal matrix, parity, and performance evidence | Open |
| Fleet project equivalence across hosts | 2.1 fleet mutation | Explicit mapping and mismatch tests | Open |

These are bounded design gates, not permission for open-ended architecture work.

---

## 14. Immediate Implementation Queue

### Now: finish the shell baseline

1. Close the 1.5.1 defects and add focused regression tests. **Done in 1.5.1.**
2. Batch remaining tmux probes and establish the `window_activity`-based observation
   contract. **Done in 1.5.1.**
3. Capture benchmark fixtures for the current shell paths. **`make bench` in 1.5.1.**

### Next: remove first-run friction

1. Support non-root `PREFIX` installation, matching uninstall, and verified
   run-from-source behavior. **Done in 1.5.2.**
2. Expand `hydra doctor` into an actionable installation and readiness check.
   **Done in 1.5.2.**
3. Publish the five-minute throwaway-repository Quick Start with an obvious
   no-agent path and shell-completion setup. **Done in 1.5.2.**
4. Protect that path with fresh-home install and onboarding smoke tests.
   **Done in 1.5.2.**

### After that: ship the shell lifecycle foundation

1. Write and accept the identity, state, event, lifecycle, and lock decisions.
2. Implement state verification and reversible migration.
3. Ship declared outcome, observed status, liveness, events, and `hydra wait`.
4. Ship guided init, dry-run/no-agent spawn, profiles, capability-probed adapters,
   task injection, resume, and teardown.
5. Ship safe messaging, `hydra exec`, and Git review commands.
6. Qualify the complete hookless path before relying on richer adapters.

### Then: build product value on stable foundations

1. Ship scopes, claims, collision analysis, resource profiles, gates, sync/land, and
   cleanup policy.
2. Prove the smallest read-only native slice and its distribution pipeline.
3. Ship the terminal-safe native mission-control MVP.
4. Ship recoverable workflows and isolated integration.
5. Stabilize local contracts in 2.0, then pilot fleet in 2.1.

---

## 15. Version Summary

| Version | Theme | Headline acceptance outcome |
| --- | --- | --- |
| **1.5.0** | Shipped foundation | Modular shell implementation |
| **1.5.1** | Baseline hardening | Correct shell fallback and parity oracle |
| **1.5.2** | Frictionless first run | Non-root, agent-optional, five-minute onboarding |
| **1.6.0** | Identity, lifecycle, and evidence | Project-safe, task-aware, resumable shell coordination |
| **1.7.0** | Parallel safety and native core | Scoped multi-head safety plus optional read-only native proof |
| **1.8.0** | Native mission control | Fast, safe, opt-in control surface over stable lifecycle data |
| **1.9.0** | Workflows and integration | Recoverable DAGs and verified integration worktrees |
| **2.0.0** | Stable local interface | Qualified and documented local orchestration contracts |
| **2.1.0** | Fleet pilot | Capability-negotiated SSH coordination under partial failure |

---

## 16. Reference Notes

These external references inform implementation choices but do not create roadmap
requirements by themselves:

- [tmux Control Mode](https://github.com/tmux/tmux/wiki/Control-Mode) — documented
  text protocol, notifications, pane output, flow control, and format subscriptions.
- [Git worktree documentation](https://git-scm.com/docs/git-worktree.html) — stable
  porcelain output plus lock, move, prune, repair, and removal semantics.
- [Claude Code worktrees](https://code.claude.com/docs/en/worktrees) — current
  worktree isolation, bootstrap-file copying, cleanup, and recovery behavior.
- [Claude Code hooks](https://code.claude.com/docs/en/hooks) — current lifecycle,
  permission, task, stop, notification, and worktree hook surfaces.
- [Gemini CLI hook reference](https://github.com/google-gemini/gemini-cli/blob/main/docs/hooks/reference.md)
  — current command-hook lifecycle and structured input/output behavior.
- [Cursor worktrees](https://prod.cursor.com/docs/configuration/worktrees) — current
  worktree setup and review workflow.
- [Codex app announcement](https://openai.com/index/introducing-the-codex-app/) —
  parallel agent and worktree positioning.

The roadmap's differentiation claims should be refreshed before public release
because adjacent products change quickly.

---

## 17. Evidence Confidence

| Claim category | Confidence | Basis and limitation |
| --- | --- | --- |
| Current 1.5.0 baseline and missing native infrastructure | High | Direct inspection of the tagged repository, build, install, CI, shell libraries, and tests |
| Current installation and first-run friction | High | Direct inspection of README Quick Start, installer, Makefile, spawn defaults, doctor checks, and integration tests |
| Cross-repository identity, locking, parsing, and lifecycle gaps | High | Directly implied by current global branch-keyed state and mixed-format roadmap proposals |
| 1.5.1 kill, regenerate, queue, JSON, locking, and repository-hygiene defects | High | Direct inspection of the current shell implementation and repository state |
| Read-heavy and terminal-oriented C boundary | High | Matches the current subprocess-heavy TUI and avoids duplicate mutation policy |
| tmux formatted activity/process observation before C | High for feasibility; Hydra behavior requires fixtures | Current tmux exposes the required formats and local probing confirmed detached pane output updates `window_activity` rather than `session_activity` |
| Lifecycle adapters as an early optional integration tier | Medium-high | Official agent documentation demonstrates useful lifecycle hooks, but exact support and trust behavior are vendor/version-sensitive |
| tmux control mode as the preferred native prototype | High for feasibility; unproven for Hydra | Official tmux protocol supports the needed primitives; Hydra-specific reliability and performance still require a prototype |
| Release sequencing | Medium-high | Dependencies are architectural; exact release sizing depends on maintainer capacity and product priorities |
| Differentiation priorities | Medium | Grounded in current adjacent-product capabilities but market positioning is time-sensitive |
| Experimental feature value | Low until tested | These remain hypotheses and have no release commitment |
