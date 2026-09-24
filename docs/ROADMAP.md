# Hydra Roadmap

> - **Status:** canonical outstanding-work backlog
> - **Snapshot:** 24 September 2026
> - **Release:** `v2.7.0` reliability baseline and guided planning
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

This file contains outstanding work and the policies that constrain it. Delivered
implementation is recorded in the [changelog](../CHANGELOG.md) and focused
contracts. A milestone appears here only while it has open work; a short delivery
note keeps the qualification boundaries its open neighbours rely on. Those notes
do not close live-host or provider acceptance.

Detailed qualification records were removed from the repository on 11 September
2026. Links marked *(historical)* resolve to [that source revision][hist] and
describe only the revisions they name.

2.0.0 is the final version assigned in advance. After 2.0, work is selected from one
backlog and released when a coherent feature or meaningful change is ready. The
version number is chosen at release time from compatibility impact.

## Next target: installed-build acceptance

Release 2.7.0 shipped the September 2026 reliability audit fixes together with
in-app conversation launch, head-associated agent proposals with guided local
policy, invalidation on revision and exact-digest approval (U3/U17), and the first
installed usability runner (U18); see the [2.7.0 changelog](../CHANGELOG.md#270---2026-09-24).
Its local qualification: `make test-usability` covers five installed journeys at
80/140 columns, including checked delivery; one existing-login Claude planning task
published and validated a proposal within a five-minute cap; input/resize/return
and nested tmux attachment have deterministic acceptance; and independent native
capture checks Unicode, line drawing and ANSI color. These local qualifications
do not close the full acceptance criteria of U3, U17 or U18.

Next, complete the remaining real-provider, output and review acceptance: the
local part of U5 plus U14, U16 and I4, including styled diffs, long-output
comparisons, actual-terminal human review and goal-only discoverability. Existing
Fleet behavior retains its gates; one real-host workflow still needs explicit
qualification. New scheduling, task pools, automatic placement and remote
onboarding remain outside this target. These priorities do not establish a
release date; compatibility impact still determines the next version.

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
- Public changes follow the compatibility and deprecation policy in
  [CONTRACTS.md](CONTRACTS.md),
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

### Immediate product priority: first-use and agentic workflow

User feedback from installed v2.5.0 on 13 September reopens product acceptance
for onboarding, workspace presentation and the real agentic journey. Select this
work ahead of further feature expansion. Existing implementation and test results
remain evidence for their recorded scope; they do not close the usability gaps
below or establish that the current experience meets the reviewed mockups.

The intended entry is `hydra` in a repository: describe an objective to an agent,
review and revise its plan, explicitly approve execution, follow work and respond
to questions, then inspect the changed files and verified result. `hydra tui` must
also be a usable entry with no existing heads. Users should not need to understand
initialization, spawn, worktree identities or environment variables to begin.
Hydra is one control centre for local and remote agentic work. Provider terminals,
execution location and transport are resources within that experience, not separate
products the user must assemble. The follow-up installed-build feedback below is
an acceptance blocker, not optional cosmetic polish.

- [ ] **U1 — Keep ordinary onboarding out of the source tree.** In `dkvm`,
      `hydra init --no-agent --trust` created an untracked `.hydra/` directory.
      Store local registration, preferences and runtime state outside the repository
      by default. Make shared repository configuration an explicit opt-in, with a
      clear purpose and preview. Preserve existing configuration and migrate durable
      contracts deliberately; hiding generated junk with ignore rules is not the
      desired solution. The reported `.gmcs/` directory is a separate existing
      artifact; do not attribute it to Hydra or remove it as part of this work.
      Acceptance: opening, configuring and reopening Hydra in a clean existing repo
      leaves its files and `git status` unchanged until the user requests actual
      project work or explicitly chooses shared configuration. Existing dirty work
      and ignore rules are preserved. Repository commands still require explicit
      trust before execution; fewer setup steps must not imply blanket trust.
- [ ] **U2 — Clean terminal entry and human-readable context.** Spawn currently
      echoes a long `export HYDRA_PROJECT_ID=... HYDRA_HEAD_ID=...` bootstrap command,
      with wrapped internal paths and identities dominating the terminal. Deliver
      the required environment before the interactive prompt without typing setup
      commands into the user's conversation or shell history. Present concise
      project/branch context; keep exact identities and paths in inspectable details.
      Acceptance: a fresh shell or real agent session opens with usable input and
      clean output at ordinary terminal widths. No bootstrap exports appear in the
      visible transcript or history; agents and subprocesses still receive the
      correct environment. Check initial spawn and reconnect with actual terminals.
- [ ] **U3 — Start the workspace before creating work.** `hydra` and `hydra tui`
      should discover the current repository and offer an agent conversation or a
      clear in-app next action when no heads exist. Resolve provider selection and
      any missing authentication in context; create execution resources when the
      requested work needs them. Keep explicit CLI commands for expert automation.
      Acceptance: from an uninitialized repository, reach a working conversation
      without first running `init`, `spawn`, exporting variables, or constructing
      a plan file by hand. Missing providers and authentication have actionable
      states; entering the UI alone does not authorize agent work or repo commands.
      Interactive task launch should enter or stay in the control centre with the
      selected agent visible inside it; the reported `spawn --profile codex --prompt`
      path currently takes the user straight into Codex instead. Make direct terminal
      attachment an explicit expert choice, preserving documented noninteractive
      contracts. Acceptance includes starting from both the UI and an interactive
      CLI task launch without opening another terminal to recover Hydra navigation.
- [ ] **U4 — Match the reviewed workspace mockups in actual use.** The two user
      references show (1) “HYDRA / PLAN TOGETHER”: narrow project navigation, a
      prominent interactive agent conversation, adjacent plan/dependency review
      and contextual revise/validate/approve controls; and (2) a monitoring layout
      with project navigation, central run graph and test output, and a persistent
      agent conversation for intervention. Match their hierarchy, spacing, restrained
      cyan/amber/green status colors, clear borders and focus, readable text, and
      concise contextual controls. Their illustrative content is a design reference,
      not real execution evidence or a request to implement every pictured menu.
      Acceptance: compare live installed-build captures with both references during
      planning, running, waiting for input and failure. Review at normal narrow/wide
      terminal sizes, including 80x24 and 140x40, with a usable compact fallback.
      The objective, agent input destination, progress and next action remain clear;
      raw IDs, repeated keyboard hints and diagnostic fields do not dominate.
- [ ] **U5 — Test the agentic product journey.** Replace the manual shell-head lab
      as the primary user evaluation guide with one small, meaningful agent task
      in a real repository. Start Hydra, ask for a bounded change with observable
      acceptance criteria, discuss and revise an agent-authored plan, approve the
      exact revision, observe execution, answer a real question, inspect a failed
      check and supported recovery, then review the resulting diff and test evidence.
      Include leaving and returning without losing the conversation or run context.
      Acceptance: perform this through the installed application with an authenticated
      agent and real outputs, recording user friction and remaining gaps. The user
      does not manually author the implementation or assemble workflow machinery.
      Keep shell-only fixtures and isolated failure probes as engineering checks;
      their success cannot substitute for this product acceptance exercise.
- [ ] **U6 — Stable rendering without flicker.** The user reports distracting
      flicker during normal TUI operation. Diagnose the live render/refresh path;
      avoid visible clearing and repainting of unchanged content, and preserve
      cursor, input, selection and scroll during updates. Acceptance: observe the
      installed UI while idle, streaming agent output, polling local/remote state,
      switching panes and resizing. Retain a terminal recording to assess flicker;
      static screenshots and passing rendering tests cannot establish this result.
- [ ] **U7 — Readable details and actionable recovery.** The supplied detail view
      is an undifferentiated diagnostic dump. Present objective, agent, progress,
      checks, changes and pending decisions in clearly grouped, wrapped sections;
      keep raw IDs, source paths and confidence metadata in optional diagnostics.
      Replace `dead-session / try-hydra / hydra doctor` with a plain-language account
      of what stopped, what is known about retained work, and an appropriate in-app
      inspection or recovery action. Do not infer lost work or task failure solely
      from a missing terminal. Acceptance: the user can explain the problem and
      choose the next action without decoding an internal status or consulting CLI
      help; destructive actions remain separate from restoring access to work.
- [ ] **U8 — Consistent, discoverable navigation.** The user reports that Tab pane
      switching does not work as expected despite visible hints. Establish one
      interaction model across workspace, details, overview, coordination and agent
      panes: Tab/Shift-Tab for pane focus, arrows for selection, Enter to open or
      activate, and Esc to return, with visible focus and contextual help. Resolve
      how agent input receives Tab versus how the user exits agent focus; make that
      boundary discoverable and preserve provider input behavior. Avoid requiring
      users to memorize unrelated single-letter modes or hidden prefix sequences.
      Acceptance: exercise forward/backward navigation, dialogs and attached agents
      with actual key events; displayed hints must match behavior in each context.
- [ ] **U9 — Useful overview and coordination.** The reported views show sparse
      counters, empty charts and unexplained `unavailable` labels without explaining
      the task. Overview should answer what is running, what needs attention, what
      changed and what finished, with direct routes to conversation and evidence.
      Coordination should explain assignments, dependencies, blockers and handoffs
      for the selected objective. For a single agent or no workflow, explain that
      state and offer a relevant next action instead of an empty dashboard. Identify
      why information is unavailable and distinguish unknown values from zero;
      never invent progress or coordination from process liveness. Acceptance:
      inspect real empty, single-agent and multi-agent work, including a blocked
      dependency, and find the required decision/result without reading raw counters.
- [ ] **U10 — Guided remote setup inside Hydra.** Adding a machine should let the
      user select an SSH destination, inspect connectivity and requirements, review
      a proposed installation, and authorize Hydra to provision a compatible remote
      runtime. Do not require manual remote Hydra installation or package building
      as the normal onboarding journey. Build on the existing qualification and
      pinned-bootstrap contracts, including platform matching, verified bytes,
      explicit host-key trust and scoped installation. Guide provider sign-in and
      remote project selection or explicitly authorized source setup separately;
      SSH access does not establish provider authentication or repository presence.
      Acceptance: onboard a supported host with no Hydra installation through the
      control centre, launch a real agent task and reconnect to it. Also test an
      existing installation, missing prerequisites, failed sign-in and interrupted
      setup with clear progress and safe continuation. Preserve unrelated installs,
      credentials and work; do not silently copy secrets or accept changed keys.
- [ ] **U11 — One workspace across local and remote execution.** Unify ordinary
      and Fleet UI navigation, agent interaction, task details, attention and review.
      Represent local/remote as a visible location and filter within the same control
      centre; users should not need to discover `fleet tui` to see remote work.
      Keep host identity and connectivity clear at action time and retain exact
      instance checks, host-specific capabilities and execution ownership. Existing
      CLI automation contracts can remain while the interactive experience is unified.
      Acceptance: follow simultaneous local and remote tasks, converse with either
      agent, inspect results and handle remote disconnection without switching
      applications or learning another keymap. Stale remote observations must not
      authorize actions or make ongoing work appear successfully completed.

- [ ] **U12 — Keep action confirmation and results inside the control centre.**
      Killing a head currently leaves the TUI for a shell confirmation and teardown
      transcript, then requires “Press Enter to return to Mission Control” even
      after success. Confirm the selected target and consequences in an in-app
      dialog, execute through the authoritative CLI, and show progress and a concise
      result in place. On success, update navigation and move focus predictably to
      surviving work without an extra acknowledgement. Keep detailed output available
      on demand; failures should explain what remains and offer a relevant next step.
      Preserve dirty-work protection, current-session safeguards and exact target
      validation; an in-app confirmation must not become blanket force authorization.
      Acceptance: remove a stopped head and an active head, cancel confirmation,
      encounter dirty work, and exercise a failed or partially successful bulk action.
      The UI remains the interaction surface, accurately reflects each outcome, and
      never requires a successful-action shell detour or a return-to-UI keypress.

- [ ] **U13 — Shared navigation and meaningful statistics.** Statistics currently
      drops the other tabs and shows an empty recorded-workflow dashboard while an
      agent session exists. Retain the common navigation, selected work and a clear
      return path. Explain which activity is covered: a standalone agent session is
      not automatically a recorded workflow run. Show relevant available activity,
      distinguish no history from filters excluding data or unsupported measurements,
      and avoid large empty charts and unexplained `unavailable` fields. Acceptance:
      navigate to and from statistics during standalone agent work and a recorded
      workflow, with both empty and populated history, without losing context.
- [ ] **U14 — Faithful, readable agent output.** The supplied output capture contains
      repeated question marks replacing characters, long unwrapped lines and flattened
      diffs. Preserve supported Unicode, terminal styling, line structure and readable
      code/diff presentation through capture and rendering. Keep live terminal input
      distinct from a read-only transcript; provider shortcut hints in captured text
      must not imply they work in the transcript viewer. Acceptance: compare real
      provider output with Hydra's view using Unicode, colored diffs, long lines,
      scrolling and resize. No corrupted glyph runs, raw control sequences or missing
      content; unsupported terminal behavior has an honest, readable fallback.
- [ ] **U15 — Clear purposes for heads, overview and coordination.** Heads should
      help select and manage agent work; overview should summarize objectives,
      progress, decisions and results across that work; coordination should explain
      actual assignments, dependencies and handoffs. Consolidate redundant views
      where no distinct user task justifies them. Empty coordination must explain
      why there is nothing to coordinate instead of presenting an unexplained blank.
      The screenshot also shows an agent reporting a committed two-file change while
      the summary says zero changed files. Inspect this mismatch: label uncommitted
      changes separately from the task's full diff against its base, so a clean
      worktree does not imply no delivered change. Acceptance: follow a standalone
      task through commit, then a multi-agent objective through a blocked handoff;
      each retained view answers a distinct useful question with current evidence.
- [ ] **U16 — Attachment that can always be left cleanly.** The user reports being
      unable to dismiss attachment; the screenshot shows duplicated agent content,
      large dotted regions and competing terminal/workspace presentation. Diagnose
      attach, sizing and screen restoration using the real terminal combination;
      do not treat the screenshot as proof of a particular underlying cause. Provide
      a visible way to leave agent input and close its view while retaining the task.
      Keep one coherent workspace, accurate agent identity and predictable focus.
      Acceptance: repeatedly attach, leave input, close the pane, switch layouts,
      resize and reattach locally and remotely. No stuck input capture, duplicate
      views, residual screen regions or lost work; closing a client does not kill
      its execution owner. Verify leaving attachment with actual user input.
- [ ] **U17 — Conversation-to-plan integration without manual JSON.** The current
      plan page asks the user to load draft and policy files. Make agent-authored
      planning part of the conversation: discuss an objective, generate a structured
      draft, revise it and display steps, dependencies, checks and execution scope
      alongside that conversation. Hydra should manage the draft's association with
      the task and guide policy choices. Keep file import/export as an expert option.
      Acceptance: create and revise an executable plan from a real conversation
      without asking the user to write JSON, locate draft files or manually translate
      agent prose. Validate and show the exact revision before explicit execution
      approval; changed plans invalidate old approval. A conversational proposal or
      standalone agent completion must not masquerade as a validated workflow.

The nine follow-up findings map to U3 (control-centre launch), U6 (flicker), U7
(details and recovery), U8 (keybindings), U9 (coordination and overview), U10 (remote
onboarding), and U11 (one local/remote UI). Extend U5's real-task acceptance to include
onboarding a remote machine and following local and remote work in that same UI.
The subsequent six findings are covered by U13 (statistics), U14 (output), U15
(coordination and overview), U16 (attachment), and U17 (conversation-to-plan flow).

- [ ] **U18 — Automated usability journeys and discoverability evaluation.**
      Implement the staged [usability evaluation plan](USABILITY-TESTING.md), reusing
      existing PTY infrastructure against a prefix-installed product. First automate
      clean entry, navigation, attachment exit and head removal with retained failure
      evidence. Add bounded goal-driven screen-only trials and separate real-provider
      and remote-onboarding qualification. Acceptance: one local command produces
      a report with reproducible actions, captures and independent outcome checks;
      known UX defects fail their desired-behavior checks. Human visual review and
      live qualification remain explicit, not replaced by fixture passes or an AI score.

- [ ] **U19 — Explain features where users encounter them.** Users cannot be
      expected to know what a workflow, head, plan, gate, coordination view or Fleet
      means. Establish consistent user-facing language and explain each feature's
      purpose, when it helps, and the next action in its entry point and empty state.
      For example: “A workflow is a saved sequence of steps Hydra runs and tracks
      for a task. Steps can run agents, execute tests or wait for your approval;
      independent steps can run in parallel.” Show a concrete example such as
      implement a change -> run tests -> wait for review, and explain when a single
      agent conversation is sufficient. Distinguish an agent's proposed plan from
      an approved executable workflow, and a live session from completed work.
      Prefer familiar labels where they clarify the concept; preserve expert terms
      in technical details and CLI documentation as needed. Explain consequences
      before actions such as approval, interruption or removal. Use concise inline
      explanations, contextual examples and optional deeper help rather than a
      mandatory tutorial, jargon glossary as the only explanation, or permanent
      walls of instructional text. Keep explanations aligned with shipped behavior.
      Acceptance: a first-time user can explain what a workflow does, decide whether
      their task needs one, start the appropriate path and understand its states
      without leaving the control centre. Extend U18 with goal-driven comprehension
      trials that do not give the evaluator feature names or shortcuts; verify that
      its chosen action fits the goal, alongside human first-use review.

Delivery boundary: the items above remain outstanding requirements. Source changes
have shipped as follows; each item stays open until the installed-build acceptance
in U5 and U18 has been observed. Close items with observed first-use, visual and
agentic acceptance from the installed build, and update the getting-started and
evaluation guides to the delivered flow as part of that work.

| Item | Shipped source changes |
| --- | --- |
| U1, U2, U4, U6, U7, U8, U9, U13, U19 | [2.6.0](../CHANGELOG.md#260---2026-09-14) |
| U3 | CLI half in 2.6.0; in-app conversation launch and interactive spawn/resume opening inside Hydra in 2.7.0 |
| U12 | in-app removal in 2.6.0; untracked-file protection in 2.7.0 |
| U14 | DEC special graphics and a labelled read-only Details excerpt in 2.7.0 |
| U15 | view consolidation in 2.6.0 |
| U16 | nested tmux attachment with deterministic input/resize/return checks in 2.7.0 |
| U17 | head-associated proposals, guided local policy, invalidation and exact-digest approval in 2.7.0 |
| U18 | initial deterministic installed-journey runner in 2.7.0 |
| U5, U10, U11 | none yet |

### Candidate features

Select these priorities in dependency order, with independently useful scope.
The observation milestones in priority 7 can start against current remote tasks
before tmux removal; they build on existing distributed execution and do not wait
for load balancing.
The implemented remote submission and collection interface is documented in
[FLEET.md](FLEET.md) and [CONTRACTS.md](CONTRACTS.md).
Local objective planning is implemented; see [workflows](workflows.md)
and its exact-digest procedure. Remaining priorities
keep their original numbers so existing references remain meaningful. Item 9 is
placed ahead of item 6 following the planning and validation review; numbering is
not execution order. Completed item 5 is recorded under delivered behavior.

#### 2. Adapter conformance and headless execution: remaining live qualification

The workflow data, durable approval, retry, adapter-contract, and headless execution
implementation is documented in [workflows](workflows.md) and [CONTRACTS.md](CONTRACTS.md).
Live provider qualification remains a separate host-level check.

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
implemented interactive and headless profiles. See [agent profiles and inputs](USAGE.md#agent-profiles-and-inputs)
for capabilities and authentication boundaries.

Acceptance: retain each unqualified live-provider check until actual execution,
independent result checks, and observed-process cancellation are recorded on its
claimed host. The original Codex/Pi/OpenCode/plain remote task remains qualified;
fixture tests and local authentication do not close another provider's remote
requirement. Claude remains explicitly deferred rather than blocking the other
implemented profiles.

### Research sources and open follow-ups

Research for follow-on decisions is retained at its source revision:
[interactive parity report][src-interactive-report], [interactive parity decision
brief][src-interactive-brief], [host discovery report][src-host-report], [host
discovery decision brief][src-host-brief], [performance measurement research][src-performance],
[optional-tmux analysis][src-tmux], [tmux control-mode prototype][src-tmux-prototype]
and [workflow agent acceptance][src-workflow-acceptance]. They describe the audited
baseline in their research-date context; this roadmap is the canonical backlog.
Recommendations already delivered (V1–V4, H1–H3, I1–I3, 9E tools and 9F finite
reuse) are summarized in their milestones below. The remaining recommendations map
to open work:

| Source finding | Open destination and next action |
| --- | --- |
| Terminal attachment remains separate from the durable execution owner ([optional tmux][src-tmux]). | [T2](#t-optional-tmux-for-headless-and-remote-execution) live qualification, then [T3](#t-optional-tmux-for-headless-and-remote-execution). Next: qualify an authenticated host without tmux, then the two-host scenario; interactive heads retain tmux. |
| Control-mode polling is a bounded historical lead ([tmux prototype][src-tmux-prototype]). | Item 10 may evaluate it; reconnect, restart, partial frames, sustained CPU/RSS, resize and platform parity remain unqualified. Next: measure the supported matrix before selecting a change. |
| Scheduled input, coordinated omission, monotonic clocks, attribution and counter limits ([performance research][src-performance]). | [Item 10](#10-performance-baselines-idle-efficiency-and-change-locality). Next: establish 1/10/50-worktree baselines with agent/tmux/observer attribution and explicit unknown counters before budgets or parity claims. |
| Discovery supplies untrusted coordinates and needs typed, private candidate state ([host discovery][src-host-report]). | [H4](#h-host-discovery-qualification-and-staged-onboarding). Next: select one live source, then implement and qualify bounded acquisition and snapshot diffs. |
| Onboarding needs provenance, freshness, identity and typed progress ([host discovery][src-host-brief]). | [H5](#h-host-discovery-qualification-and-staged-onboarding). Next: implement and qualify review, filters, detail and interrupted multi-host progress. |
| Changed keys and source conflicts require explicit revocation and review ([host discovery][src-host-report]). | [H6](#h-host-discovery-qualification-and-staged-onboarding). Next: implement and qualify scoped revocation and reviewed key rotation without cancelling or replaying tasks. |
| Real Codex, Pi, OpenCode and plain-prompt VPS checks passed; Claude remote qualification was deferred ([workflow acceptance][src-workflow-acceptance]). | Item 2 retains its open provider matrix. An operator-provided Ubuntu VPS is available; prepare its tmux-absent environment and verify the current package and provider status. |
| Structural plan inspection and finite comparisons are implemented; broader planning-quality evaluation remains open. | [9E](#9e-explainable-planning-and-measured-plan-quality). Next: define and measure matched held-out work before claiming planning benefit. |
| Runtime membership/expansion, PTY server replacement, cloud-manager integration and shared inventory remain conditional ([optional tmux][src-tmux], [host discovery][src-host-brief]). | 9F's open expansion item and the conditional extensions below. Next: revisit only with an explicit authority, workload and acceptance decision. |

[hist]: https://github.com/Someblueman/hydra/tree/66a5baad011d50b17949f981b807f31627c59250/docs

[src-interactive-report]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/research/interactive-parity-report.md
[src-interactive-brief]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/research/interactive-parity-decision-brief.md
[src-host-report]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/research/host-discovery-onboarding-report.md
[src-host-brief]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/research/host-discovery-onboarding-decision-brief.md
[src-performance]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/research/performance-baselines.md
[src-tmux]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/research/tmux-optional-execution.md
[src-tmux-prototype]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/research/tmux-control-mode-prototype.md
[src-workflow-acceptance]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/WORKFLOW_AGENT_ACCEPTANCE.md

[h-t2]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/T2_ACCEPTANCE.md
[h-integration]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/next-wave-integration.md
[h-dag]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/DISTRIBUTED_DAG.md
[h-dag-qual]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/distributed/qualification.md
[h-compile]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/PLAN_COMPILATION.md#outcome-obligations-9a
[h-handoff]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/HANDOFF_CONTRACTS.md
[h-9b]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/handoff-contracts-9b.md
[h-9d-feature]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/9d-feature-outcome.md
[h-9d-perf]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/9d-performance-outcome.md
[h-9d-research]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/9d-research-outcome.md
[h-inspect]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/PLAN_INSPECTION.md
[h-patterns]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/PLAN_PATTERNS.md
[h-cleanup]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/python-cleanup-20260910/README.md
[h-reuse]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/PLAN_REUSE.md
[h-manifest]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/PLAN_MANIFEST.md
[h-staged]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/PLAN_STAGED.md
[h-observability]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/OBSERVABILITY.md
[h-recovery]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/recovery-visibility-v4.md
[h-retention]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/RETENTION.md
[h-remaining]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/remaining-work-20260910.md
[h-discovery]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/HOST_DISCOVERY.md
[h-h3]: https://github.com/Someblueman/hydra/blob/66a5baad011d50b17949f981b807f31627c59250/docs/evidence/h3-source-batches.md

#### T. Optional tmux for headless and remote execution

Decision: make tmux optional for headless execution, retaining it for interactive
heads. T1 is locally qualified; T2 has controlled acceptance but still needs live-host
qualification. Interactive heads retain their tmux requirement.
See [Contracts](CONTRACTS.md) for the current implementation and affected
contracts. Resource admission and
finite distributed execution are implemented; T1 and T2 extend that execution to
hosts without tmux, and T3 qualifies the same distributed scenario in that mode.
These are delivery milestones, not assigned release versions.

T1 — terminal-independent workspace and execution identity — is delivered: local
commands and headless adapters execute and produce verified artifacts without
tmux, and status and teardown work for both execution modes (*historical*:
[headless and interactive regressions][h-t2], [combined integration][h-integration]).

- [ ] **T2 — Remote execution and planning without tmux.** Route remote exec and
      headless workflow steps through T1 and the existing detached task owner.
      Make admission, bootstrap, doctor, installation, and capability negotiation
      require tmux only for terminal operations. Extend plan schema/validation and
      lowering explicitly; preserve published spawn semantics and existing compiled
      artifact bindings. Unsupported remote or terminal capabilities fail before
      launch, rather than silently changing the requested execution mode.
      Implementation and controlled acceptance: [T2 record][h-t2] *(historical)*.
      Live authenticated-host qualification remains required.
      Acceptance: on hosts without tmux installed, submit a command and an available
      authenticated headless adapter, disconnect, reconnect, collect exact outputs,
      and consume them in a dependent step. Run the corresponding local compiled
      plan through public interfaces. Exercise duplicate submission, lost start
      response, owner death, cancellation, and bounded logs; uncertain work is never
      replayed and retained reservations are not released by terminal absence.
- [ ] **T3 — Distributed qualification and operator access.** Use T2 for the
      implemented two-host fan-out, validation, join, composition, and final check
      described in [Distributed DAGs][h-dag] *(historical)*. Reuse
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
[implemented and qualified][h-dag-qual] *(historical)*. Retain that
execution substrate and the existing public CLI; strengthen what is planned,
compiled, handed off, and accepted as a completed outcome.

Milestone IDs are stable references, not release numbers. **9A–9D** are delivered;
the open work is **9E**'s measured planning evaluation and **9F**'s bounded expansion.
Item **6** follows 9E's measured baseline, while 9F is a workload-driven extension
and does not block basic load balancing.

##### 9A–9D. Outcome obligations, handoff contracts, structured evidence and task acceptance

Delivered and locally qualified within their documented scope; see
[workflows](workflows.md) and [CONTRACTS.md](CONTRACTS.md).

- **9A** represents mandatory outcomes as satisfiable obligations with compiler
  diagnostics and rejects orphan checks and impossible evaluation paths
  (*historical*: [compiler obligations][h-compile], [integration acceptance][h-integration]).
  Semantic review of whether criteria capture an objective remains separate.
- **9B** checks producer/consumer handoffs with [bounded data-schema-2 contracts][h-handoff]
  at compilation and materialization (*historical*: [9B evidence][h-9b]). No arbitrary
  schema implication, isolation, semantic correctness or external-host/live-provider
  qualification is claimed; resource/provenance annotations are descriptive.
- **9C** binds structured evidence with report schema v3 and qualifies validators
  with positive and negative controls. No LLM judge is implemented, so judge
  calibration and ordering sensitivity are not claimed.
- **9D** passed [feature][h-9d-feature], [performance][h-9d-perf] and
  [research][h-9d-research] pilots *(historical)* through the public CLI, including
  declared negative controls. Results apply to the supplied feature, one synthetic
  performance workload and the finite research dataset.

##### 9E. Explainable planning and measured plan quality

Start the baseline with 9A; use 9B–9D contracts and pilots to qualify the tools.

Delivered: reusable bounded decomposition patterns with node/dependency
explanations, and CLI inspection, explanation, comparison and invalidation tools.

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

Local implementation: [plan inspection][h-inspect] *(historical)* and
[finite patterns][h-patterns] *(historical)* provide complete structural explanations,
deterministic bound comparisons, public serial/fork/join outcomes and six
incorrect-artifact controls. They do not establish stochastic planner quality,
estimate calibration or a planning benefit that exceeds overhead. The
[archived finite task-class pilot](../examples/planning/evaluation/README.md) records six
frozen candidates, nine independently specified cases and eighteen incorrect artifacts
at its exact historical source commit. Its superseded active runner has been retired.
Its interface-conformance difference disappears in a separate post hoc protocol
diagnostic. Broader planning-benefit, human-correction and calibration measurements
remain open; these results do not justify a more elaborate planner.

A [real Python cleanup workload][h-cleanup] *(historical)*
replaced duplicated precompilers with native C and retired the toy runner while
preserving useful independent tests. Its matched serial/parallel executions both
passed (106.33/106.69 seconds), qualifying real planning, execution and independent
verification on this maintenance workflow. Those timings are descriptive: the
cleanup was not expected to benefit from parallelism, and speedup is not an
acceptance criterion or a deficiency in this qualification. The record preserves
planning overhead, failed attempts and unknown provider cost. Broader semantic
planning-quality, stochastic-variability and calibration research remains open.

##### 9F. Selective repair and bounded graph expressiveness

Select after contract/evidence pilots demonstrate a useful workload. Preserve the
finite execution DAG, original coordinator, and unknown-outcome reconciliation.

Delivered: hierarchical patterns lowered into static graphs, staged experiments,
finite manifest maps and conditional branches with fixed join membership, and
qualified selective repair that requires fresh affected checks.

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

Local qualification: [versioned sealed-artifact reuse][h-reuse] *(historical)* preserves
qualified original attempts, reruns affected checks, and revalidates proofs after
interrupted repair. The supported policy is local artifact-only execution under
explicit complete-dependency assumptions. [Finite manifest maps][h-manifest] *(historical)*
lower at most eight members and boolean selection into fixed joins, including
checked empty/all-skipped outputs. [Staged findings][h-staged] *(historical)* drive a
separately compiled static graph only after their source, selection, results and
public acceptance are reverified. Real two-stage outcomes cover empty, skipped,
colliding-name and maximum-cardinality maps. Runtime membership changes, late conditional
evaluation and dynamic expansion remain unsupported; rejection does not qualify
those graph forms.

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
Measurement qualification remains open. The [performance research][src-performance]
sets the method: schedule inputs independently of render completion, use monotonic
timestamps and tagged stimuli, record clock uncertainty, attribute agent/tmux/
observer work separately, and treat unsupported counters as unknown.

The [local measurement harness](performance-harness.md) provides reproducible
readiness checks and individual deterministic-worker cells. Its availability
does not complete the matrix or establish performance budgets below.

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
- [ ] Include one and three attached clients, local interactive/local headless/
      remote headless cases, and disconnect/reconnect under output pressure.
      Attribute PTY parsing, generated/transmitted frames and per-client rendering;
      record unsupported combinations explicitly. Qualify I2 attention signals
      against annotated blocked/question/approval, working, done-but-unseen, idle
      and unknown cases, reporting false positives and missed signals separately.
      These measurements do not make an attention hint an accepted outcome.

The I2/I3 local acceptance packet records reproducible behavior and bounded local
fixture evidence in [attention-review-acceptance.md](attention-review-acceptance.md).
Its accepted bounded measurements retain denominators, attribution, and
late/missed observations; item 10's full matrix, idle budgets, and platform/T2/T3
qualification remain open. These measurements are not a baseline, parity claim,
or item 10 completion.

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

Delivered within local qualification: V1 run and host overview with explicit
observation freshness, V2 attempt detail with ordered events and resumable logs,
V3 controls with visible acknowledgments, V4 recovery visibility qualification,
optional metric export, retention/archive policies and accessible event-announcer
and comparison views. See [CONTRACTS.md](CONTRACTS.md) and [FLEET.md](FLEET.md)
(*historical*: [recovery qualification][h-recovery], [metrics][h-observability],
[retention][h-retention], [remaining-work acceptance][h-remaining]).

Boundaries retained for open work: human activity outside recorded events,
historical unknown incidents replaced by newer observations, wire traffic and
provider usage remain unmeasured, and missing coverage never becomes a measured
zero. Retention quotas bound the maintenance projection, not concurrent admission
or whole-disk use; external claims need explicit local pins. T3 and item 10 reuse
this observation surface; it adds no permanent daemon, terminal server, scheduler
or automatic coordinator failover.

#### I. Interactive parity and review navigation

Follow-up to the [interactive parity report][src-interactive-report].
Extend the current attached-terminal and V1–V4 interfaces; keep the existing task
owner, tmux terminal lifecycle and shell mutation path. The locally qualified
operator workflows below retain those execution and mutation boundaries.

I1 (saved multi-machine interaction and direct attach), I2 (attention rollups and
next-action navigation) and I3 (compact review queue and external evidence
references) are locally complete. I1 was qualified with two native clients across
actual Mac/VPS hosts. I2/I3 evidence and limits are recorded in
[attention-review-acceptance.md](attention-review-acceptance.md); item 10 retains the
broader platform/scaling matrix and numeric budgets. Seen state never approves a
gate, and opening an external reference cannot promote, push, merge or substitute
provider status for Hydra verification.

- [ ] **I4 — Visual design and usability of attention and review.** Bring the
      attention queue/detail, evidence review, identity/reference views and Fleet
      navigation up to the visual standard of the existing native workspace,
      planning and operational pages. The I2/I3 functional checks do not establish
      visual quality; the PR #84 screenshot review identified this as unfinished.
      Reuse the established typography, spacing, borders, themes, selection and
      focus treatments so these screens feel like part of the same application.
      Lead with a readable task name, status, reason for attention and next action.
      Group checks, artifacts, diffs and references into scannable sections with
      useful previews; keep full hashes and technical identity fields available
      in explicit detail views. Balance information density and whitespace, handle
      long names/paths deliberately, and make keyboard actions easy to discover.
      Design loading, empty, stale, unavailable and failed states with the same
      care as populated views. Preserve exact-subject binding, read-only review
      and clear separation of seen state from approval or acceptance.
      Acceptance: compare before/after captures from the running native UI with
      the existing workspace/planning pages, using realistic task names, checks,
      diffs and artifacts. Cover 80×24 and wider layouts, dark/light themes and
      no-color mode. Visually review every new view and state for hierarchy,
      alignment, contrast, wrapping, clipping and focus. Walk through finding an
      item, understanding why it needs attention, reviewing its evidence and
      returning to the queue with selection preserved. Completion requires user
      review of the actual screens as well as relevant interaction checks;
      passing tests or placeholder-filled screenshots alone are insufficient.

I1 builds on V1–V4 and existing interactive attach; tmux-free distributed
qualification stays in T2/T3. I2/I3 reuse lifecycle, outcome and review contracts.
Select independently useful slices, with item 10 measuring their overhead.

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

H1 (candidate discovery and read-only qualification, as `fleet discover` /
`fleet qualify`), H2 (reviewed qualification and explicit enrollment) and H3
(bounded scale and opt-in source adapters) are delivered within local
qualification (*historical*: [contract and acceptance][h-discovery],
[H3 local qualification][h-h3]). H3 covers four snapshot formats, 100 distinct
selected targets, 16-host probe/apply batches, private progress, interruption and
lost-response reconciliation. No live host, provider, vendor discovery client or
provider credential path is qualified; discovery never accepts keys, creates
aliases, installs Hydra or grants execution permission.

- [ ] **H4 — Opt-in live inventory acquisition and snapshot diffs.** Acquire one
      selected mDNS/DNS-SD, VPN, cloud or configuration-management source through
      its normal client/API and feed the existing candidate importer. Choose the
      first source and account/network scope when selecting implementation. Keep
      credentials outside records, bound acquisition time/bytes/rate, and expose
      cache age plus added/removed/changed/conflicting candidates before selection.
      Acceptance: stale cache, unavailable credentials, partial responses, source
      disappearance and identity conflicts preserve provenance and do not widen
      selection or mutate enrollment intent. Fixture coverage and separately
      authorized live-source qualification are reported independently.

- [ ] **H5 — Onboarding operator view and workflow qualification.** Add filters,
      candidate details, snapshot changes and per-host apply/resume/reconcile
      progress over H1–H3 records, delegating actions to the existing CLI.
      Acceptance: with a prepared alias/key/credentials/package, reach one-host
      review in at most three explicit commands and one confirmation; show the
      exact actions and bindings before apply. CLI/JSON/TUI agree on identity and
      typed state. Mixed ten-host and interrupted 50-host runs retain every row,
      success and unknown outcome; 100-host views remain bounded and responsive.
      No UI retry replays an uncertain mutation or silently bootstraps a host.

- [ ] **H6 — Scoped revocation and reviewed key rotation.** Define explicit local
      revocation for candidate/source, transport, package and project decisions,
      retaining who/when/reason and the superseded binding under existing state
      conventions. Re-import cannot revive a revoked identity silently. A changed
      key shows old/new fingerprints and observation provenance; acceptance uses
      the operator's OpenSSH process and requires fresh enrollment review.
      Acceptance: revoked bindings block new operations and invalidate pending
      intents; alias reuse, endpoint changes and source refresh cannot bypass the
      block. Existing accepted tasks/evidence remain inspectable. Revocation alone
      never cancels tasks, uninstalls packages, deletes remote state or rewrites
      known_hosts; those actions retain their own explicit authorization.

Dependencies: H4–H6 build on the H1–H3 records, the existing strict OpenSSH policy,
versioned handshake, package/bootstrap, fleet-init/trust and remote-task contracts
in [FLEET.md](FLEET.md) and [CONTRACTS.md](CONTRACTS.md). T1/T2 become dependencies
only for tmux-independent/headless enrollment, and 9A only if onboarding
obligations are later compiled into objective plans. This track does not change
the status or acceptance of those milestones.

Scope guardrails: do not auto-scan arbitrary networks, auto-accept host keys,
silently replace aliases after key rotation, copy secrets, auto-trust repositories,
submit remote work as a side effect of discovery, add a permanent daemon/database,
or claim that source reachability proves host health. Keep shared multi-user
inventory and automatic failover outside this slice until their authority and
reconciliation contracts are separately approved.

## Native workspace and standalone termviz track

This track develops dependency-free C terminal infrastructure, using Hydra as its
first real consumer and eventual standalone release as the direction. Existing
visualization architecture and limits are documented in the native helper
behavior in [workflows.md](workflows.md) and the [termviz module guide](../src/termviz/README.md).

Termviz owns reusable rendering, layout, input routing, and terminal screen
interpretation. Hydra owns agent/workflow semantics and authoritative actions.
Keep OS-specific terminal lifecycle and PTY process handling in small adapters,
separate from the portable core. Add no third-party dependencies. Preserve the
shell-only path, current CLI/state contracts, and tmux authority for Hydra heads;
the standalone shell example does not introduce a competing Hydra execution owner.

Milestones 1–8 are implemented: the workspace foundation and embedded shell
(see the [terminal contract](../src/termviz/TERMINAL.md) for the qualified subset),
the Hydra workspace with conversation-first planning, comprehensive overview,
monitoring and statistics layouts (A–D), integrated working terminals, operational
views, interaction and visual polish, a recorded real-workflow exercise, and local
standalone source readiness (`make test-termviz-export`; see
[ownership and compatibility boundaries](../src/termviz/STANDALONE.md)). Codex CLI
0.153.3 is the qualified agent in the recorded local configuration.

Installed-build feedback on 13 September reopened product, visual and real-workflow
acceptance for the workspace (Milestones 3, 6 and 7). Those gaps are tracked by
[U1–U19](#immediate-product-priority-first-use-and-agentic-workflow); the earlier
implementation checks do not establish that the experience is product-ready.

- [ ] Add remote workflow cohorts to statistics when the fleet protocol supplies
      reliable evidence. Provider/resource telemetry requires explicit protocol and
      measurement contracts; missing measurements never appear as zero.
- [ ] Standalone termviz release. Darwin 25.6.0 arm64 (Apple Clang 17, UBSan) and
      Linux aarch64 (Debian GCC 12.2, ASan/UBSan) passed standalone component/PTY
      checks on 8 September 2026; other platforms remain unqualified. Publication
      needs a separate release decision; local readiness does not authorize
      repository creation, pushing or publication.

Full terminal compatibility, a broad widget catalogue, new telemetry collection and
remote execution changes require their own bounded scope.

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
- provider/cloud-manager execution or review integrations beyond I3's read-only
  references: first select a concrete workflow and credential/ownership boundary;
- team-shared discovery inventory: first define per-user authorization, approver
  identity and conflicting trust decisions; H4–H6 retain per-user state;
- replacement server-owned PTYs or live runtime handoff: require a demonstrated
  limitation of existing tmux attach and an explicit lifecycle/migration decision.
  The interactive-parity research does not select this runtime replacement.

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
- [CONTRACTS.md](CONTRACTS.md) for current local interfaces, state, trust boundaries,
  compatibility rules, remote submission, disconnected execution and verified
  collection;
- [FLEET.md](FLEET.md) for the implemented fleet capability;
- [workflows.md](workflows.md) for workflow and integration behavior, finite
  distributed execution, validation joins, bounded repair, replay and optional
  native behavior;
- [attention-review-acceptance.md](attention-review-acceptance.md) and
  [performance-harness.md](performance-harness.md) for current local qualification
  and measurement tooling;
- [the pre-consolidation documentation][hist] for historical qualification records.
