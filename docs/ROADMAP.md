# Hydra Roadmap

> - **Status:** canonical outstanding-work backlog
> - **Snapshot:** 24 November 2026
> - **Release:** `v5.0.0` (21 November 2026), a reliability and performance release
> - **Release planning:** versions are assigned from compatibility impact when backlog work is ready
> - **Related:** [README](../README.md) · [CHANGELOG](../CHANGELOG.md) ·
>   [Contracts](CONTRACTS.md) · [Release definition of done](#release-definition-of-done)

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

This file lists outstanding work and the policies that constrain it. Delivered work
is not described here: its history is in git (`docs/ROADMAP.md` at `c82547c` is the
last version that narrates it) and in [CHANGELOG.md](../CHANGELOG.md), and its
current behaviour is in [CONTRACTS.md](CONTRACTS.md), [FLEET.md](FLEET.md) and
[workflows.md](workflows.md). A shipped item stays below only while some acceptance
still needs a human, a real host or a real provider; local fixture qualification
never closes those.

After 2.0.0 work is selected from one backlog and released when a coherent feature
or meaningful change is ready. The version number is chosen at release time from
compatibility impact.

## Next target: live and human acceptance

Most open product and fleet work has shipped; what remains is mostly acceptance that
only a person, a real host or a real provider can give. By what each item needs:

- **A person at the screen:** U1 to U9 (U5's follow-ups need a real run), U12, U13, U15, U17 to U19 and
  I4 (details in each item). The automated checks are described in
  [TUI-REVIEW.md](TUI-REVIEW.md).
- **A real host or provider:** U10 (the operator VPS run), U11 (remote
  attach, reconnect, history), T2 and T3 (hosts without tmux), item 6 (placement
  trials), item 8 (cron, systemd, launchd, macOS, several hosts), H4 to H6 (a live
  network, inventory and key rotation), statistics from real remote hosts.
- **Measurement:** item 10 (a second Linux trial, macOS, frozen budgets, the
  efficiency slice) and 9E (a planning benefit on a live-agent workload).
- **A maintainer decision:** standalone kettle publication.

These priorities do not establish a release date; compatibility impact still
determines the next version.

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
  Spawn queues, workflow scheduling and task pools stay on one admission and
  execution path.
- Public changes follow the compatibility and deprecation policy in
  [CONTRACTS.md](CONTRACTS.md), migration, and rollback requirements. Replace
  internal interfaces in place; do not create competing state authorities or
  speculative compatibility layers.
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
- Qualification is host- and provider-specific. A fixture, a local sign-in or a
  controlled SSH failure does not qualify another provider or a real host, and no
  document may claim a qualification that was not observed.

## Outstanding backlog

Items below have no assigned release number. When work is selected, define the
smallest coherent scope and its acceptance boundaries, then release it when ready.
Priority may change with observed use. The numbered priorities reuse fleet
transport and one workflow execution authority as coordination expands across hosts;
numbering is not execution order.

### Immediate product priority: first-use and agentic workflow

User feedback from installed v2.5.0 on 13 September reopened product acceptance for
onboarding, workspace presentation and the real agentic journey. Existing
implementation and tests are evidence for their recorded scope; they do not close
the gaps below.

The intended entry is `hydra` in a repository: describe an objective to an agent,
review and revise its plan, explicitly approve execution, follow work and respond
to questions, then inspect the changed files and verified result. `hydra tui` must
also be a usable entry with no existing heads. Users should not need to understand
initialization, spawn, worktree identities or environment variables to begin.
Hydra is one control centre for local and remote agentic work; provider terminals,
execution location and transport are resources within that experience.

The item texts keep the vocabulary of the tab-based control centre. Since 4.0.0
Work replaces Heads, Details, Coordination, Overview, Workflows and the Workspace
layouts, Inbox replaces Attention and Recovery, Hosts is `3` and Stats is `4`
(see "What moved" in [USAGE.md](USAGE.md#what-moved)). Each item below is open
until the acceptance it states has been observed; "Open" says what is still
missing. What the automated checks cover is in [TUI-REVIEW.md](TUI-REVIEW.md); the
installed-build journeys for the individual items were removed from
`make test-acceptance` and are covered by the focused tests the
[changelog](../CHANGELOG.md#unreleased) lists.

- [ ] **U1 — Keep ordinary onboarding out of the source tree.** Store local
      registration, preferences and runtime state outside the repository by default.
      Make shared repository configuration an explicit opt-in, with a clear purpose
      and preview. Preserve existing configuration and migrate durable contracts
      deliberately; hiding generated files with ignore rules is not the desired
      solution. Acceptance: opening, configuring and reopening Hydra in a clean
      existing repo leaves its files and `git status` unchanged until the user
      requests actual project work or explicitly chooses shared configuration.
      Existing dirty work and ignore rules are preserved. Repository commands still
      require explicit trust before execution; fewer setup steps must not imply
      blanket trust. Open: no human step; covered by `tests/test_init_source_tree.sh`,
      `tests/test_onboarding.sh` and `tests/test_project_trust.sh`, awaiting the
      maintainer's acceptance.
- [ ] **U2 — Clean terminal entry and human-readable context.** Deliver the
      required environment before the interactive prompt without typing setup
      commands into the user's conversation or shell history. Present concise
      project and branch context; keep exact identities and paths in inspectable
      details. Acceptance: a fresh shell or real agent session opens with usable
      input and clean output at ordinary terminal widths. No bootstrap exports
      appear in the visible transcript or history; agents and subprocesses still
      receive the correct environment. Check initial spawn and reconnect with
      actual terminals. Open: real terminal applications (macOS Terminal, iTerm2);
      PTY coverage is in `tests/test_spawn_bootstrap.sh` and `tests/test_tmux.sh`.
- [ ] **U3 — Start the workspace before creating work.** `hydra` and `hydra tui`
      discover the current repository and offer an agent conversation or a clear
      in-app next action when no heads exist. Resolve provider selection and any
      missing authentication in context; create execution resources when the
      requested work needs them. Keep explicit CLI commands for expert automation.
      Acceptance: from an uninitialized repository, reach a working conversation
      without first running `init`, `spawn`, exporting variables, or constructing a
      plan file by hand. Missing providers and authentication have actionable
      states; entering the UI alone does not authorize agent work or repo commands.
      An interactive task launch (`spawn --profile codex --prompt`) enters or stays
      in the control centre with the selected agent visible inside it; direct
      terminal attachment is an explicit expert choice, preserving documented
      noninteractive contracts. Acceptance includes starting from both the UI and
      an interactive CLI launch without opening another terminal to recover Hydra
      navigation. Open: a live provider's sign-in and missing-login states.
- [ ] **U4 — Match the reviewed workspace mockups in actual use.** The two user
      references show (1) "HYDRA / PLAN TOGETHER": narrow project navigation, a
      prominent interactive agent conversation, adjacent plan and dependency review
      and contextual revise, validate and approve controls; and (2) a monitoring
      layout with project navigation, central run graph and test output, and a
      persistent agent conversation for intervention. Match their hierarchy,
      spacing, restrained cyan, amber and green status colors, clear borders and
      focus, readable text, and concise contextual controls. Their illustrative
      content is a design reference, not execution evidence or a request to
      implement every pictured menu. Acceptance: compare live installed-build
      captures with both references during planning, running, waiting for input and
      failure, at normal narrow and wide sizes including 80x24 and 140x40, with a
      usable compact fallback. The objective, agent input destination, progress and
      next action remain clear; raw IDs, repeated keyboard hints and diagnostic
      fields do not dominate. Open: the human comparison. `make visual-review`
      produces the captures and `make test-visual-lint` the layout checks
      ([TUI-REVIEW.md](TUI-REVIEW.md)); the aesthetic judgement is the reviewer's.
- [ ] **U5 follow-ups — observe in a later real run.** U5 (the agentic product
      journey) was accepted on 27 September 2026. Still to observe: answering an
      agent question during execution, a failed check with supported recovery, and
      leaving and returning mid-run.
- [ ] **U6 — Stable rendering without flicker.** Avoid visible clearing and
      repainting of unchanged content and preserve cursor, input, selection and
      scroll during updates. Acceptance: observe the installed UI while idle,
      streaming agent output, polling local and remote state, switching panes and
      resizing. Retain a terminal recording to assess flicker; static screenshots
      and passing rendering tests cannot establish this result. Open: perceptual
      review in a real terminal. `make test-acceptance` measures repaint counts and
      retains the recordings ([TUI-REVIEW.md](TUI-REVIEW.md#flicker-make-test-acceptance));
      repaint counts bound flicker but cannot show it.
- [ ] **U7 — Readable details and actionable recovery.** Present objective, agent,
      progress, checks, changes and pending decisions in clearly grouped, wrapped
      sections; keep raw IDs, source paths and confidence metadata in optional
      diagnostics. Give a plain-language account of what stopped, what is known
      about retained work, and an appropriate in-app inspection or recovery action.
      Do not infer lost work or task failure solely from a missing terminal.
      Acceptance: the user can explain the problem and choose the next action
      without decoding an internal status or consulting CLI help; destructive
      actions remain separate from restoring access to work. Open: judging the
      wording as plain language (`tests/test_native_tui.sh` covers the content).
- [ ] **U8 — Consistent, discoverable navigation.** One interaction model across
      work, details, inbox and agent panes: Tab/Shift-Tab for pane focus, arrows for
      selection, Enter to open or activate, Esc to return, with visible focus and
      contextual help. Make the boundary between agent input (which receives Tab)
      and exiting agent focus discoverable and preserve provider input behavior.
      Avoid unrelated single-letter modes and hidden prefix sequences. Acceptance:
      exercise forward and backward navigation, dialogs and attached agents with
      actual key events; displayed hints must match behavior in each context. Open:
      first-time discoverability by human first use (`tests/kettle/test_attach_cycles.c`
      covers the key events).
- [ ] **U9 — Useful overview and coordination.** Answer what is running, what needs
      attention, what changed and what finished, with direct routes to conversation
      and evidence; explain assignments, dependencies, blockers and handoffs for the
      selected objective. For a single agent or no workflow, say so and offer a
      relevant next action instead of an empty dashboard. Say why information is
      unavailable and distinguish unknown values from zero; never invent progress or
      coordination from process liveness. Acceptance: inspect real empty,
      single-agent and multi-agent work, including a blocked dependency, and find
      the required decision or result without reading raw counters. Open: whether
      the summaries answer a real user's question.
- [ ] **U10 — Guided remote setup inside Hydra.** The user selects an SSH
      destination, inspects connectivity and requirements, reviews a proposed
      installation, and authorizes Hydra to provision a compatible remote runtime;
      no manual remote Hydra installation or package building as the normal journey.
      Build on the qualification and pinned-bootstrap contracts: platform matching,
      verified bytes, explicit host-key trust, scoped installation. Guide provider
      sign-in and remote project selection (or explicitly authorized source setup)
      separately; SSH access establishes neither. Onboarding should offer agent
      installation and detect per-user install locations. Acceptance: onboard a
      supported host with no Hydra installation through the control centre, launch
      a real agent task and reconnect to it. Also test an existing installation,
      missing prerequisites, failed sign-in and interrupted setup with clear
      progress and safe continuation. Preserve unrelated installs, credentials and
      work; never silently copy secrets or accept changed keys. Automated:
      `make test-u10-loopback` (gated in CI) onboards a throwaway account behind a
      private loopback sshd through the Hosts screen and covers each of those cases
      with a fixture agent ([FLEET.md](FLEET.md#guided-remote-setup)). Open: the
      operator VPS run. From an installed build, `hydra tui`, open Hosts (`3`), add
      `ubuntu@<vps>`, type `yes` for the fingerprint read on the VPS, approve the
      plans, sign in to a real provider there, then `hydra fleet spawn <name>
      --project /abs/project -- task --profile <agent>` and `hydra tui --location
      remote` to find it. A pinned release build needs no
      `HYDRA_REMOTE_SETUP_BINARY`. Upgrading a host that an older Hydra set up
      (`hydra remote provision NAME`, [FLEET.md](FLEET.md#upgrading-a-host)) is
      qualified over the fake SSH boundary of `tests/test_remote_provision.sh` only.
- [ ] **U11 — One workspace across local and remote execution.** Local and remote
      work share navigation, agent interaction, task details, attention and review,
      with location as a visible filter; users need not discover `fleet tui`. Keep
      host identity and connectivity clear at action time and retain exact instance
      checks, host capabilities and execution ownership. Acceptance: follow
      simultaneous local and remote tasks, converse with either agent, inspect
      results and handle remote disconnection without switching applications or
      learning another keymap. Stale remote observations must not authorize actions
      or make ongoing work appear successfully completed. `hydra tui` does this
      with fixtures and a fake `hydra` (`make test-tui`, `make test-tui-pty`). Open:
      a live remote host: real SSH attach, reconnect after a real disconnection,
      and remote history scrolling against a host tmux (including a non-default
      prefix; remote panes scroll through `Ctrl-B [`, which needs the default
      prefix). Remote tasks are not yet nodes of the workspace navigation tree.
- [ ] **U12 — Keep action confirmation and results inside the control centre.**
      Confirm the selected target and consequences in an in-app dialog, execute
      through the authoritative CLI, and show progress and a concise result in
      place. On success, update navigation and move focus predictably to surviving
      work without an extra acknowledgement. Keep detailed output on demand;
      failures explain what remains and offer a next step. Preserve dirty-work
      protection, current-session safeguards and exact target validation; an in-app
      confirmation must not become blanket force authorization. Acceptance: remove a
      stopped head and an active head, cancel confirmation, encounter dirty work,
      and exercise a failed or partially successful bulk action. The UI remains the
      interaction surface, reflects each outcome accurately, and never requires a
      shell detour or a return-to-UI keypress after success. Open: no human step;
      covered by `tests/test_kill.sh`, awaiting the maintainer's acceptance.
- [ ] **U13 — Shared navigation and meaningful statistics.** Statistics keeps the
      common navigation, the selected work and a clear return path, and covers head
      sessions as well as recorded workflow runs. Activity is inferred from tmux
      window output while a control centre is open and is labelled as inferred;
      unobserved time is never called quiet, unknown values are `--`, "no history"
      is told apart from "filtered out", and empty charts are not drawn. Headless and
      remote heads are deliberately not inferred; their owners report exact status.
      Acceptance: navigate to and from statistics during standalone agent work and a
      recorded workflow, with both empty and populated history, without losing
      context. Open: tokens and cost for interactive sessions (they need each
      provider's own session records), activity while no control centre is running,
      the workflow-runs page's own empty charts, and the redesign of the screen.
- [ ] **U15 — Clear purposes for heads, overview and coordination.** Heads help
      select and manage agent work; overview summarizes objectives, progress,
      decisions and results; coordination explains actual assignments,
      dependencies and handoffs. Consolidate redundant views where no distinct user
      task justifies them; empty coordination explains why there is nothing to
      coordinate. Label uncommitted changes separately from the task's full diff
      against its base, so a clean worktree does not imply no delivered change.
      Acceptance: follow a standalone task through commit, then a multi-agent
      objective through a blocked handoff; each retained view answers a distinct
      useful question with current evidence. Open: whether each view's answer is
      useful.
- [ ] **U17 — Conversation-to-plan integration without manual JSON.** Agent-authored
      planning is part of the conversation: discuss an objective, generate a
      structured draft, revise it and display steps, dependencies, checks and
      execution scope alongside that conversation. Hydra manages the draft's
      association with the task and guides policy choices; file import and export
      stay an expert option. Acceptance: create and revise an executable plan from
      a real conversation without asking the user to write JSON, locate draft files
      or translate agent prose. Validate and show the exact revision before explicit
      execution approval; changed plans invalidate old approval. A conversational
      proposal or standalone agent completion must not masquerade as a validated
      workflow. `make test-usability` walks this path with a labelled fixture
      provider. Open: a live provider authoring a plan.
- [ ] **U18 — Automated usability journeys and discoverability evaluation.** One
      local command produces a report with reproducible actions, captures and
      independent outcome checks against a prefix-installed product; known UX
      defects fail their desired-behavior checks. Human visual review and live
      qualification stay explicit, never replaced by fixture passes or an AI score.
      `make test-usability` runs the first-use and planning walkthrough
      ([TUI-REVIEW.md](TUI-REVIEW.md#first-use-walkthrough-make-test-usability)).
      Its goal-driven discoverability trials and the journeys for clean entry,
      navigation, attachment, removal and recovery were removed from the runner;
      native tests cover those areas. Open: showing that a first-time person (not a
      scripted evaluator) can find the controls for those tasks from what the screen
      says, and the live-provider, real-terminal and remote-onboarding runs.
- [ ] **U19 — Explain features where users encounter them.** Establish consistent
      user-facing language and explain each feature's purpose, when it helps, and
      the next action in its entry point and empty state (workflow, head, plan, gate,
      coordination view, Fleet), with a concrete example, such as implement a change,
      run tests, wait for review, and when a single agent conversation is enough.
      Distinguish an agent's proposed plan from an approved executable workflow, and
      a live session from completed work. Explain consequences before approval,
      interruption or removal. Prefer familiar labels where they clarify; keep
      expert terms in technical details and CLI documentation. Use concise inline
      explanations and optional deeper help rather than a mandatory tutorial or
      permanent walls of text, and keep them aligned with shipped behavior.
      Acceptance: a first-time user can explain what a workflow does, decide whether
      their task needs one, start the appropriate path and understand its states
      without leaving the control centre. Open: human first-use review; the
      comprehension trials that asserted the copy were removed with the simulated
      discoverability layer.

### Candidate features

Select these priorities in dependency order, with independently useful scope.
Remote submission and collection are documented in [FLEET.md](FLEET.md) and
[CONTRACTS.md](CONTRACTS.md); local objective planning is in
[workflows](workflows.md). Priorities keep their original numbers so existing
references remain meaningful.

#### Provider qualification (adapter conformance and headless execution)

Workflow data, durable approval, retry, the adapter contract and headless
execution are documented in [workflows](workflows.md) and [CONTRACTS.md](CONTRACTS.md).
Live qualification is a separate, host-level check: retain each unqualified
live-provider check until actual execution, independent result checks and
observed-process cancellation are recorded on its claimed host. Fixture tests and
local authentication do not close another provider's remote requirement.

The operator Ubuntu VPS has qualified remote shared tasks for Codex, Pi, OpenCode,
a plain prompt, Claude Code and Cursor Agent (September 2026; Cursor only with the
`--trust` profile fix, which is not in 2.7.0). Antigravity's remote task is closed
as an upstream provider issue
([antigravity-cli#219](https://github.com/google-antigravity/antigravity-cli/issues/219));
re-run it if that is resolved. No provider check is currently open.

#### T. Optional tmux for headless and remote execution

Decision: tmux is optional for headless execution and required for interactive
heads. Local commands and headless adapters run without tmux; the remaining
milestones extend that to remote hosts. See [Contracts](CONTRACTS.md) for the
affected contracts and
[Execution hosts without tmux](FLEET.md#execution-hosts-without-tmux) for the
reproducible loopback qualification and the real-host checklist. A host that has
only passed those loopback cases is not qualified.

- [ ] **T2 — Remote execution and planning without tmux.** Remote exec and headless
      workflow steps use the detached task owner. Admission, bootstrap, doctor,
      installation and capability negotiation require tmux only for terminal
      operations. Unsupported remote or terminal capabilities fail before launch,
      rather than silently changing the requested execution mode. Acceptance: on
      hosts without tmux installed, submit a command and an available authenticated
      headless adapter, disconnect, reconnect, collect exact outputs, and consume
      them in a dependent step. Run the corresponding local compiled plan through
      public interfaces. Exercise duplicate submission, lost start response, owner
      death, cancellation and bounded logs; uncertain work is never replayed and
      retained reservations are not released by terminal absence. Open: the same
      steps on a real, separately authenticated host without tmux and with a
      signed-in headless adapter.
- [ ] **T3 — Distributed qualification and operator access.** The two-host fan-out,
      validation, join, composition and final check run with run, step and attempt
      visibility through the CLI and TUI without attach; interactive terminal
      access stays an explicit capability, and viewing logs is not attachment to a
      headless process's stdin. Acceptance: the complete distributed scenario
      succeeds with tmux absent on execution hosts. Coordinator restart and lost
      acknowledgments preserve task identity; stale results cannot advance
      dependents. Interactive spawn, attach, messaging, transcript and teardown
      regressions pass with tmux installed. Open: the same scenario across real
      remote hosts.

Host service management is a conditional extension of these milestones. Add systemd
or launchd integration only for an explicit logout or reboot recovery requirement,
reusing the same owner and reconciliation records. Qualify those failure boundaries
separately; service restart must not replay uncertain work. No permanent daemon,
replacement terminal multiplexer, automatic failover or isolation guarantee is
required to complete T2 and T3.

#### 9. Planning, node contracts, and verified outcomes

Outcome obligations, handoff contracts, structured evidence, task acceptance,
hierarchical patterns, staged experiments, manifest maps, selective repair and
bounded expansion (`hydra workflow plan expand`) are delivered within local
qualification; see [workflows](workflows.md) and [CONTRACTS.md](CONTRACTS.md). Not
supported, and not qualified by rejection: runtime membership changes and late
conditional evaluation. The open work is 9E.

##### 9E. Explainable planning and measured plan quality

Delivered: reusable decomposition patterns with explanations, `plan evaluate`
(decomposition, fixed-graph scheduling and placement kept separate, ranges and
unknowns preserved), `plan observed` and `plan decision` (selection reasons replayed
byte for byte).

- [ ] Maintain independently specified and held-out task cases plus known incorrect
      artifacts. Compare current and contract-aware planning under matched scope,
      tools and budgets; separate the effects of planning, validation and
      scheduling. Measure false acceptance and rejection, verified outcomes,
      time and cost, human correction, repeated work and estimate calibration by
      task class. State: `make test-plan-eval` validates the held-out oracle and runs
      one serial and fork-join scheduling comparison; it does not run candidate
      planners. Human
      corrections and provider cost have never been observed (unknown), and no
      per-class estimate calibration exists.

Acceptance: explain every required node and reproduce deterministic comparisons from
recorded inputs. Report stochastic variability and held-out outcome errors, not only
plan validity or throughput. Qualify semantic improvements with observed results and
explicit limits; planning benefit must exceed its overhead on the workload where
improvement is claimed. Open: a semantic planning benefit over its overhead on a
real workload with live agents, with recorded human corrections and supplied
estimates. Deterministic fixtures exercise no stochastic variability, and nothing
claims the benefit.

#### 10. Performance baselines, idle efficiency, and change locality

Begin measurement before changing refresh or scheduling policy. This milestone
measures Hydra itself (item 9D's performance tasks are workloads Hydra executes).
Method: schedule inputs independently of render completion, use monotonic timestamps
and tagged stimuli, record clock uncertainty, attribute agent, tmux and observer work
separately, and treat unsupported counters as unknown
([performance research][src-performance]).

Tools today: `make bench-i1` (individual deterministic-worker cells, see
[performance-harness.md](performance-harness.md)) and `make perf-attention`. The
first and only baseline was a Linux x86_64 container trial on 17 October 2026 (one
trial, deterministic stand-in agents, a shared 4-core container, 1, 10 and 50
worktrees); its bundle and the matrix runner that produced it were removed and are in
git history. What it found and what still depends on it:

- Every worktree was scanned on every refresh whether or not it changed. 4.0.0
  bounded the scan to the selected head, worktrees whose index or `HEAD` changed and
  a fixed rotation of the others; no before and after trial has been run.
- At 50 heads cancel took 2.0 to 3.9 s or missed its 4 s deadline, no declared
  change appeared in the update window, and 50 headless heads never became usable
  (the native client's shell data adapter timed out). Not repeated since the 4.0.0
  and 5.0.0 changes to the data path.
- Runs ended `observation_unavailable` when ten supervised headless runs coincided;
  fixed (status is recorded only on change, a lost observation never stops the
  worker). The host admission lock also refused 1 of 10 and 14 of 20 simultaneous
  `exec` starts (`state_unavailable`) in a side experiment; not re-measured.
- The numeric budgets were provisional, set at 1.5 times the observed maximum on that
  container, and were never frozen.

Target: **with no changes, Hydra should do almost no work; with one changed
worktree, work should be mostly confined to that worktree.** This is an acceptance
objective, not a claim about the current implementation.

Open requirements, each with what is still missing:

- [ ] Repeatable baselines at **1, 10 and 50 real worktrees with active agents**:
      quiescent, one-changing, all-changing and sustained-output cases, separating
      deterministic stand-ins from authenticated live-agent trials. Extend the
      existing benchmark and PTY infrastructure rather than adding a performance
      service; keep the 5/20/100-head checks until replaced with equivalent
      coverage. Missing: a repeat trial, 50 headless heads, macOS, live-agent trials.
- [ ] **Idle cost** after settling: CPU time per second, normalized CPU percentage,
      wakeups per second and subprocess launches per minute across Hydra and its
      helpers, with agent, tmux and observer overhead attributed separately, plus
      scans, redraws, filesystem operations and remote requests. Missing: redraw
      counts, unshimmed filesystem operations, remote requests, macOS.
- [ ] **Interaction latency** from scheduled input injection to the matching
      rendered response (median, p95, p99 where the sample count supports them,
      observed maximum, missed deadlines) for navigation, search, resize and cancel
      under refresh and output load. PTY frame completion is not input-to-pixel
      latency. Missing: enough trials for tails, macOS.
- [ ] **Update latency** from a timestamped repository or agent change to its
      matching display state, split into detection, collection, model update and
      render, for selected and unselected worktrees, bursts and remote
      disconnect and reconnect, with remote clock uncertainty bounded. Missing: the
      stage split, unselected worktrees, bursts, remote reconnect and clock bounds.
- [ ] **Fleet scaling** across the matrix: per-role and per-worktree CPU, resident
      and peak memory, process and descriptor counts, scans, remote traffic,
      time-to-usable and tails; record agent activity, repository size, placement
      and admission limits, and never report queued work as concurrently active
      agents. Missing: remote traffic, tails, 50 headless heads, macOS.
- [ ] **Output pressure** at declared bytes per second, line sizes, producer counts
      and durations with preview open and closed: input latency, backlog and age,
      peak memory, dropped or coalesced updates (the renderer does not expose them;
      unknown), producer blocking and recovery time, with authoritative results and
      evidence preserved. Missing: 50 headless heads, simultaneous starts at 10 or
      more heads, disconnect and reconnect under pressure, macOS.
- [ ] **Change locality** per worktree: compare no-change cost with the cost of
      changing exactly one worktree at each scale; unchanged worktrees should not
      incur repeated Git scans, helper launches or output capture because a peer
      changed. Identify shared Git metadata dependencies and bounded reconciliation
      separately; do not achieve low cost by hiding stale state; prove missed-event
      and reconnect recovery for any event notification, invalidation or cache.
      Missing: the before and after trial of the 4.0.0 bounded scan.
- [ ] Store exact revisions, builds, workload seeds, raw timestamped samples,
      environment and tool versions, attribution boundaries and observer overhead;
      separate startup from steady state and alternate baseline and candidate
      trials. Choose numeric idle, tail-latency, memory, recovery and locality
      budgets from the supported-platform baselines and freeze them before
      evaluating optimizations. Retain stalls and uncertainty; never treat a
      short-run maximum as a guaranteed worst case or a missing counter as zero.
      Missing: a macOS baseline, the decision that freezes the budgets, and a
      runner to replace the removed one.
- [ ] One and three attached clients, local interactive, local headless and remote
      headless, and disconnect and reconnect under output pressure; attribute PTY
      parsing, generated and transmitted frames and per-client rendering; record
      unsupported combinations. Qualify I2 attention signals against annotated
      blocked, question, approval, working, done-but-unseen, idle and unknown
      cases, reporting false positives and missed signals separately (`make
      perf-attention` found none on its corpus). Attention hints are not accepted
      outcomes. Missing: remote headless (needs a live verified fleet host),
      reconnect under pressure, per-client frame attribution.

Acceptance: publish reproducible baseline evidence for every matrix cell on macOS and
Linux, with explicit unsupported measurements and separate live-agent coverage. The
measurement slice completes with recorded budgets and correctness checks; it has a
provisional Linux baseline and completes when a macOS baseline exists and the
budgets are frozen. The efficiency slice completes only when repeated before and
after trials meet those budgets, demonstrate near-idle behavior and bounded
unrelated-worktree cost, and preserve freshness, cancellation, recovery and result
integrity under output load; it has not started. No performance improvement is
claimed. Tmux control-mode polling is a bounded historical lead
([prototype][src-tmux-prototype]): evaluate it here only against reconnect, restart,
partial frames, sustained CPU and memory, resize and platform parity.

#### 6. Load balancing across eligible hosts

`hydra fleet task submit --placement auto` places new, unassigned work across
eligible hosts; explicit pinning remains available. It is documented in
[FLEET.md](FLEET.md#automatic-placement). Constraints still in force: receiver
admission is final; a definitive refusal may be re-placed but an ambiguous dispatch
stays assigned for reconciliation; accepted work that has not started can be
withdrawn and re-placed only under a new assignment generation, and running agents
are never migrated; there is no shared provider budget authority, so Hydra does not
claim to enforce account or provider limits fleet-wide; a task's external side
effects need their own idempotency or fencing contract, and lease expiry alone never
authorizes duplicate execution.

- [ ] Heterogeneous-host trials compare explicit placement and simple balanced
      placement using queue wait, time to verified result, throughput, transfer
      bytes and starvation, with workload and observation traces so replay yields
      identical decisions. Racing submitters, stale capacity, drain, lost
      reservation and withdrawal responses and late results are covered locally
      behind a controlled SSH boundary (`tests/test_placement.sh`,
      `tests/c/test_placement.c`); the performance comparison needs real hosts and
      has not been run. Queue-delay, duration and transfer-cost estimates are typed
      but unused, `validator_reserve_slots` defaults to reserving nothing because no
      workload has shown starvation, and no performance improvement is claimed.

#### 7. Remote execution visibility, diagnostics, and bounded retention

Run and host overview, attempt detail with ordered events and resumable logs,
controls with visible acknowledgments, recovery visibility, metric export and
retention are delivered within local qualification ([CONTRACTS.md](CONTRACTS.md),
[FLEET.md](FLEET.md)). Boundaries retained for open work: human activity outside
recorded events, historical unknown incidents replaced by newer observations, wire
traffic and provider usage remain unmeasured, and missing coverage never becomes a
measured zero. Retention quotas bound the maintenance projection, not concurrent
admission or whole-disk use. This surface adds no daemon, terminal server,
scheduler or automatic coordinator failover.

- [ ] Statistics from real remote hosts (receipt telemetry, remote cohorts) and any
      provider cost figures remain operator observations; the protocol does not
      record transport time, and missing measurements never appear as zero.

#### I. Interactive parity and review navigation

I1 (saved multi-machine interaction and direct attach), I2 (attention rollups and
next-action navigation) and I3 (compact review queue and external evidence
references) are locally complete; the attention and review routes are in
[CONTRACTS.md](CONTRACTS.md#tui-protocols-and-parity). Seen state never approves a
gate, and opening an external reference cannot promote, push, merge or substitute
provider status for Hydra verification.

- [ ] **I4 — Visual design and usability of attention and review.** Bring the
      attention queue and detail, evidence review, identity and reference views and
      Fleet navigation up to the visual standard of the native workspace, planning
      and operational pages, and extend that to the run, result, failure and
      statistics screens. Reuse the established typography, spacing, borders,
      themes, selection and focus treatments. Lead with a readable task name,
      status, reason for attention and next action; group checks, artifacts, diffs
      and references into scannable sections with useful previews, keeping full
      hashes and identity fields in explicit detail views. Handle long names and
      paths deliberately, make keyboard actions discoverable, and design loading,
      empty, stale, unavailable and failed states with the same care as populated
      views. Preserve exact-subject binding, read-only review and the separation of
      seen state from approval. Acceptance: compare before and after captures from
      the running native UI with the existing workspace and planning pages, using
      realistic task names, checks, diffs and artifacts, at 80x24 and wider, in
      dark, light and no-color modes. Review every state for hierarchy, alignment,
      contrast, wrapping, clipping and focus, and walk through finding an item,
      understanding why it needs attention, reviewing its evidence and returning
      to the queue with selection preserved. Passing tests or placeholder-filled
      screenshots alone are insufficient. Open: the user's review of the
      `make visual-review` contact sheet (hierarchy and visual quality; the palette
      contrast rows in `scripts/visual_review/lint-known.tsv` are a palette decision
      left to that review) and the interactive walk-through
      ([TUI-REVIEW.md](TUI-REVIEW.md#screen-captures-and-layout-lint-make-visual-review)).
      The source pass shipped in 3.0.0; `tests/test_native_tui_i4.sh` compares fresh
      captures with those in `docs/evidence/i4/`.

#### 8. Dynamic task pools and schedules

`hydra pool` (file-backed pools, one coordinator, unique claims, bounded work,
retry budgets, stale-owner rejection) and `hydra schedule` (host-timer entries for
cron, systemd and launchd that submit through `hydra workflow run` and `hydra
exec`) are delivered with local qualification (`tests/test_pool.sh`,
`tests/test_pool_recovery.sh`, `tests/test_schedule.sh`). They add no scheduler or
daemon, and reboot recovery is an explicit opt-in that never replays in-flight work.

- [ ] Qualify a live cron, systemd or launchd installation, macOS, and workers on
      more than one host (owner-pid staleness is evaluated on the recording host
      only; other hosts rely on lease expiry). Acceptance: duplicate triggers and
      competing workers do not create duplicate task claims, work growth stays
      bounded, cancellation propagates, and restart recovery does not invent
      completion or replay uncertain actions.

#### H. Host discovery, qualification, and staged onboarding

Discovery is an inventory and qualification feed, not a trust authority or an
automatic enrollment mechanism: it never accepts keys, creates aliases, installs
Hydra or grants execution permission. H1 to H6 are delivered within local fixture
qualification: `fleet discover` and `fleet qualify` (with opt-in `ssh-config`,
Ansible INI and DNS-SD sources and snapshot diffs), reviewed enrollment, the `O`
onboarding view on the Hosts screen, and `fleet revoke` for final, scoped revocation
([FLEET.md](FLEET.md)). Scope guardrails: do not auto-scan arbitrary networks,
auto-accept host keys, silently replace aliases after key rotation, copy secrets,
auto-trust repositories, submit remote work as a side effect of discovery, add a
permanent daemon or database, or claim that source reachability proves host health.
Shared multi-user inventory and automatic failover stay out until their authority and
reconciliation contracts are separately approved.

- [ ] H4 live: observe one live mDNS network, a real Ansible-managed inventory and
      `avahi-browse` on a real LAN. VPN and cloud-tag clients remain snapshot-only.
- [ ] H5 live: onboard a real multi-host fleet through the operator view and observe
      its responsiveness on a slow network.
- [ ] H6 live: a real host key rotation on a live host.

All three need a separately authorized operator run; none is claimed.

## Native workspace and standalone kettle track

Kettle is dependency-free C terminal infrastructure with Hydra as its first
consumer. It owns reusable rendering, layout, input routing and terminal screen
interpretation; Hydra owns agent and workflow semantics and authoritative actions.
Keep OS-specific terminal lifecycle and PTY handling in small adapters, add no
third-party dependencies, and preserve the shell-only path, the CLI and state
contracts, and tmux authority for Hydra heads. The standalone shell example does not
introduce a competing Hydra execution owner. See the
[kettle module guide](../src/kettle/README.md), the
[terminal contract](../src/kettle/TERMINAL.md) and the
[ownership and compatibility boundaries](../src/kettle/STANDALONE.md).

- [ ] Standalone kettle release. Standalone component and PTY checks passed on
      Darwin arm64 (Apple Clang, UBSan) and Linux aarch64 (GCC, ASan and UBSan) on
      8 September 2026, and `make test-kettle-export` was re-run on Linux x86_64 for
      3.0.0; other platforms are unqualified. Publication needs a separate release
      decision from the maintainer: local readiness does not authorize repository
      creation, pushing or publication.

Full terminal compatibility, a broad widget catalogue and remote execution changes
require their own bounded scope.

## Simplification alongside feature work

- [ ] Keep workflow syntax deliberately restricted. Use versioned fields and files
      for dataflow; make any structured-parser dependency an explicit toolchain
      decision rather than growing ad hoc YAML or shell interpolation.
- [ ] Present historical heads as inspectable history with explicit restore, not as
      live workers. Preserve the dashboard; expand views only for a concrete
      operator need rather than requiring decorative frontend parity.

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
- a narrow MCP interface over the public CLI for authorized task and context access;
- A2A adapters for independently operated agent services when SSH fleet is insufficient;
- bounded best-of-N recipes over existing tasks, gates and integration primitives;
- provider or cloud-manager execution or review integrations beyond I3's read-only
  references: first select a concrete workflow and credential and ownership boundary;
- team-shared discovery inventory: first define per-user authorization, approver
  identity and conflicting trust decisions; H4 to H6 retain per-user state;
- replacement server-owned PTYs or live runtime handoff: require a demonstrated
  limitation of existing tmux attach and an explicit lifecycle and migration decision.

Provider cognition dashboards, exact cross-provider context and cost routing,
automatic unreviewed decomposition, dirty-file shadow synchronization, Git-as-consensus
registries, natural-language command parsing, and arbitrary plugin marketplaces are
outside planned scope. Multi-user federation and automatic coordinator failover
require a separate ownership and authentication design and demonstrated demand.

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

## Where delivered work is recorded

- [CHANGELOG.md](../CHANGELOG.md) for shipped features and compatibility changes;
- [CONTRACTS.md](CONTRACTS.md) for local interfaces, state, trust boundaries,
  compatibility rules, remote submission, disconnected execution and verified
  collection;
- [FLEET.md](FLEET.md) for the fleet capability;
- [workflows.md](workflows.md) for workflow and integration behavior;
- [TUI-REVIEW.md](TUI-REVIEW.md) and [performance-harness.md](performance-harness.md)
  for the TUI review runs and the measurement tooling.

[src-performance]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/research/performance-baselines.md
[src-tmux-prototype]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/research/tmux-control-mode-prototype.md
