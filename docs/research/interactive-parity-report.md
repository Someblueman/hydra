# Hydra interactive parity research

**Snapshot:** 9 September 2026  
**Hydra source audited:** `main` at [`2c307c82e97b948ecd6965d8d33e7079a3701eca`](https://github.com/Someblueman/hydra/tree/2c307c82e97b948ecd6965d8d33e7079a3701eca)  
**Published Hydra tag:** `v2.3.0` at `d21a651a51873d0504c8c23c94598ca7e59f15f0`  
**Scope:** read-only product and source research. No Hydra code, benchmark, release, deployment, or remote state was changed.

## Executive conclusion

Hydra is already stronger than the products it resembles at one particular boundary: it treats execution identity, declared outcome, provider observation, process liveness, verification, approval, and unknown outcomes as separate contracts. It also has a real local/remote workflow substrate: Git worktrees, tmux-backed interactive heads, headless provider profiles, finite DAGs, durable approval waits, bounded retries, SSH task submission, result snapshots, and guarded integration.

The parity gap is therefore not “add a nicer agent sidebar.” It is the operator surface around work that is already running. Herdr is the closest reference for that surface: a server owns real PTYs, clients attach and detach, agent state rolls up to workspaces, blocked/working/done/idle are visible, agents can drive the socket API, and multiple SSH machines can be viewed from one client. The commercial manager products instead optimize isolated worktrees, cloud execution, artifacts, and pull-request review. They are complementary references, not drop-in runtime replacements.

The safe follow-on recommendation is to keep Hydra’s evidence boundary and build on the authorized work already in flight:

1. **Qualify the active T1, 9A, and V1 tracks.** They are being implemented in separate tasks, but are not present in the audited commit. Keep their acceptance evidence and merge/release status separate; do not treat source from another task as delivered here.
2. **Separate execution from terminal attachment.** Continue the T1 path and complete T2 so a local or remote headless task can run, reconnect, and produce an exact result without tmux. Retain tmux for interactive heads and direct input. T3 is the distributed qualification slice.
3. **Add direct operator access only after identity is explicit.** A Herdr-style attach/client split is useful, but an attached screen is not an outcome and a provider’s “done” label is not verification. Reuse the shell mutation path and current task owner rather than creating a second scheduler or state authority.
4. **Measure before claiming parity or performance.** Use the proposed 1/10/50-worktree experiment with separate agent, terminal, observer, and transport attribution. Herdr’s public benchmark is a useful mechanism-led template, not evidence that Hydra or every Herdr workload has a particular CPU cost.

The audited tree does not include the separate-task changes for **T1, 9A, or V1**. Those three milestones are **in progress but unmerged** per the coordinator’s clarification; **T2, T3, and V2–V4 remain outstanding/unchecked** in the audited snapshot. None should be reported as delivered without its own merged and qualified evidence.

## 1. Method and evidence rules

This report combines:

- a source audit of the pinned Hydra tree and its canonical docs;
- current primary documentation for Herdr, Claude Code, Codex, Cursor, GitHub Copilot, and Conductor;
- Herdr’s own release notes and performance write-up;
- dated, firsthand Herdr issue/discussion reports for residual risk.

The web sources were checked on 9 September 2026. Product documentation and hosted-agent behavior are moving targets; a capability marked “documented” means the cited source describes it, not that it has been independently qualified here. A product comparison is a capability map, not a benchmark.

### Status vocabulary

| Label | Meaning in this report |
| --- | --- |
| **Delivered** | Present in the audited Hydra tree or explicitly released/documented by the cited product. |
| **Partial** | A real subset exists, but the requested boundary, host, provider, or qualification is missing. |
| **In progress / unmerged** | Explicitly authorized work reported active in another task but absent from the audited commit; not release or acceptance evidence. |
| **Planned** | An unchecked Hydra roadmap item or an explicitly preview/experimental product capability. |
| **Absent** | The cited product does not advertise that boundary, or it is outside its stated model. This is not proof that it is impossible. |
| **Unknown** | The sources do not establish the behavior strongly enough to classify it. |

### What “interactive parity” means here

The comparison uses eight dimensions that matter when several coding agents run at once:

1. **Work unit:** task, workspace, branch, and Git worktree identity.
2. **Terminal:** real PTY behavior, input, resize, scrollback, and what survives a UI close.
3. **Lifecycle:** process state, provider state, declared result, cancellation, restart, and unknown outcome.
4. **Attention:** blocked/approval/question signals, rollups, freshness, and next action.
5. **Review:** diff, test evidence, artifacts, approval, merge/PR, and cleanup.
6. **Remote:** SSH, cloud environments, multi-machine views, reconnect, and handoff.
7. **Automation:** CLI, socket/API, hooks, schedules, integrations, and follow-up control.
8. **Footprint:** installation prerequisites, platform limits, credentials, and security boundary.

## 2. Hydra baseline: what is real at `2c307c8`

### Product model

Hydra describes itself as mission control for coding agents, worktrees, and remote fleets. A normal head is a branch with its own worktree and tmux session; `spawn` normally attaches, while `HYDRA_NO_SWITCH=1` leaves the caller in place. The README explicitly says Hydra coordinates processes and records evidence; agent activity is not completion.[^1]

The current published tag is v2.3.0 (the remote annotated tag resolves to commit `d21a651a51873d0504c8c23c94598ca7e59f15f0`); the audited `main` commit `2c307c82e97b948ecd6965d8d33e7079a3701eca` is exactly 32 commits ahead and is not itself a release. The report therefore distinguishes source-present behavior from released behavior and does not infer release readiness from the branch name.

### Delivered capabilities

| Area | Evidence in the audited tree | Assessment |
| --- | --- | --- |
| Isolated work | Each head has a branch, worktree, and tmux session; Git remains the repository authority. | **Delivered** |
| Interactive terminal | Native C TUI and shell fallback; identity-checked attach to existing tmux heads; input, paste, Ctrl-C, resize, scrollback, and independent attached clients are covered by focused PTY evidence. | **Delivered** |
| Agent profiles | Declarative headless contracts for Antigravity, Cursor Agent, OpenCode, Claude Code, Codex, and Pi; exact-session resume and bounded observation fields are defined. | **Delivered, with qualification gaps** |
| Local workflows | Finite schema-1 DAG; dependency order, bounded parallelism, idempotency-aware retries/backoff, durable attempts/events, approval waits, cancellation, and resumable recovery. | **Delivered** |
| Evidence-bound integration | Dry-run/execute gates, immutable candidate bindings, worktree assembly, explicit reviewer approval, promotion, and cleanup/recovery. Hydra never pushes as part of integration. | **Delivered** |
| Remote task substrate | Package preparation, exact source/input hashes, durable submission keys, detached receiver owner, status/cancel/log/result protocol, bounded logs/artifacts, and `outcome_unknown` reconciliation. | **Delivered** |
| Remote interactive fleet | SSH host registration/bootstrap, remote heads/workflows, host-qualified state, fleet TUI, and explicit attach. | **Delivered, but terminal-backed** |
| Lifecycle separation | Declared outcome, observed provider status, and tmux/process liveness are independent; quiet output and missing sessions cannot satisfy completion. | **Delivered** |
| Safety boundaries | Explicit repository trust, host-local credentials, no silent replay after lost acknowledgments, no automatic recovery of uncertain external work, and bounded retention/terminal transcripts. | **Delivered** |

The key contracts are visible in [`LIFECYCLE.md`](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/LIFECYCLE.md#L3-L48), [`WORKFLOWS.md`](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/WORKFLOWS.md#L6-L125), [`PROFILES.md`](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/PROFILES.md#L8-L108), and [`REMOTE_TASKS.md`](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/REMOTE_TASKS.md#L140-L220).[^2][^3][^4][^5]

### Current interactive surface and its limits

The native workspace already displays a project/head/run tree, selected-head details, activity/recovery panes, overview cards, workflow graphs, statistics, remote hosts, changed-file bars, and sanitized terminal output. `a` opens an identity-checked client for the selected head, and the existing tmux session retains the agent process and unsent input.[^6]

The limitations are important:

- The selected terminal is an attachment client to an existing tmux head, not a terminal-independent execution object.
- Fleet currently lists remote observations and failure codes but does not collect CPU, memory, or load; remote workflow graphs are not present.[^6]
- A detached client is correctly treated as neither success nor failure, but the audited operator view still needs the explicit waiting reasons, observation freshness, pending requests, and next action targeted by the active V1 work.
- Interactive launch profiles do not install provider hooks or automatically claim authoritative agent observation. Cursor live qualification requires sign-in and Claude remote qualification remains deferred.[^7]
- The core dependency remains Git, tmux 3.0+, Make, and a C99 compiler. Remote task acceptance currently requires working Git/tmux/Hydra executables and advertises `tmux` as a capability.[^1][^5]

### Explicitly not delivered

The roadmap is unusually clear about what remains open:

| Roadmap item | Intended result | Snapshot status |
| --- | --- | --- |
| **T1** | Separate workspace/trust/execution identity from terminal launch; run a verified local headless command and adapter without invoking tmux. | **In progress; unmerged from audited baseline** |
| **T2** | Route remote exec and headless workflow steps through the detached task owner on hosts without tmux; preserve exact outputs, deduplication, cancellation, and unknown-outcome rules. | **Planned; unchecked** |
| **T3** | Qualify the two-host distributed fan-out/validation/join/composition path with tmux absent on execution hosts while retaining interactive regressions with tmux installed. | **Planned; unchecked** |
| **9A** | Represent satisfiable outcome obligations, evidence, evaluation paths, and compiler diagnostics; reject orphan/impossible checks. | **In progress; unmerged from audited baseline** |
| **V1** | Provide versioned run/host snapshots with owner, state, observation timestamps, waiting reasons, obligations, and stale connection semantics. | **In progress; unmerged from audited baseline** |
| **V2–V4** | Resumable event/log cursors, acknowledged controls, and end-to-end recovery qualification. | **Planned; unchecked** |

The pinned roadmap describes T1–T3 and V1–V4 as planned boundaries, while the coordinator reports active separate-task implementation for T1, 9A, and V1. This report audits only `2c307c8`; it does not inspect, merge, or qualify those active-task changes. Preserve that distinction in status and release communication.[^8][^9]

## 3. Herdr: the closest interactive reference

### Runtime shape

Herdr’s core distinction is runtime versus client. A background server owns real terminal panes, shells, agents, and process state; one or more clients render and control them. A pane is a real terminal that accepts input, resizes, preserves scrollback, and remains after client detach. Workspaces contain tabs and panes; agent state rolls up to the sidebar.[^10][^11]

Its semantic agent states are `blocked`, `working`, `done`, `idle`, and `unknown`. The docs explain that `done` remains until seen, `idle` means finished or waiting and already seen, and each client tracks its own displayed completions. This is useful attention UX, but it is not an evidence-grade domain outcome.[^10]

### Lifecycle and restore behavior

Herdr’s strongest persistence path is detach/reattach: the original processes never stop. A full server restart restores layout and optionally recent screen history, but ordinary shells, servers, tests, and arbitrary processes are gone. Native agent session restore can restart supported providers from integration-reported session IDs; live server handoff is experimental and can interrupt in-flight API requests and streams.[^12]

That is a valuable model for Hydra’s terminal/client split, with one caveat: Hydra must retain its stricter identity and outcome semantics. A pane that is alive, a provider that says “done,” a result file, and a verified objective are four different facts.

### Remote and multi-client operation

Herdr supports ordinary SSH, `herdr --remote`, direct agent/terminal attach, named sessions, and saved SSH machines. The remote server owns panes and sends terminal state; a local client draws the UI. Multiple saved machines remain in one window, each machine has its own server and processes, and a lost connection leaves cached state visibly dimmed and disables input until a fresh connection arrives. Background connections do not answer authentication or installation prompts.[^13][^14]

This is closest to the intended V1 operator experience: connection health and last-known execution state are separate, stale state is explicit, and one unavailable machine need not freeze the others. Herdr still centers a running PTY; it is not a headless artifact/evidence scheduler.

### Attention, automation, and integrations

Herdr can control panes and recognized agents through CLI/socket primitives: create/split layout, start a provider in an existing pane, prompt, send keys, read output, wait on exact activity, attach, and collect state. Agent status can come from lifecycle hooks/plugins or strict screen manifests. Unknown prompts fall back to idle rather than causing automatic input or destructive action; the docs call the blocked detector deliberately strict.[^15][^16]

The current docs list 21 detected agents, including Claude Code, Codex, Cursor Agent CLI, GitHub Copilot CLI, Devin, OpenCode, Pi, and others. Integrations can provide native session IDs for restore, while screen detection remains the lifecycle authority when hooks do not cover the whole lifecycle.[^17]

### Version and release evidence

Herdr v0.9.0 was released 7 September 2026. The release notes describe saved SSH machines in one client, independent multi-client views, client-side TUI rendering, idle scrollback compression, safer worktree-group close, improved prompt submission, and many agent-detection fixes. A 8 September preview build added an idle-SSH-CPU fix without dropping final output.[^18]

This matters for parity research: Herdr’s current feature surface is moving quickly, and the release notes show that prompt delivery, blocked detection, stale SSH behavior, client rendering, and worktree cleanup are all active correctness areas—not solved once by adding a sidebar.

### Fit against Hydra

Herdr is a strong reference for:

- a server-owned PTY and detachable client;
- direct attach to one agent without changing workspace identity;
- multi-client and multi-machine state with explicit stale views;
- semantic attention rollups and a socket API for agents controlling agents;
- client-side rendering that avoids broadcasting unchanged frames.

Herdr is not a reason to copy its state model wholesale. Hydra should borrow the operator affordances while keeping durable task/attempt IDs, exact provider session binding, sealed artifacts, verification gates, and `outcome_unknown` reconciliation.

An independent workflow report makes the distinction concrete: Christopher Penkin describes Git worktrees for isolation, Herdr’s sidebar/toasts for blocked/working/done triage, Herdr CLI/socket calls for agents spawning panes and waiting on logs, SSH/mosh disconnect-and-reconnect, and SSH port forwarding for local browser checks. That is valuable evidence of a real user loop, but it is one person’s setup rather than a product benchmark or a guarantee of Herdr behavior.[^36]

## 4. Comparable products

The products below represent distinct design points. Claude Code, Codex, Cursor, and GitHub Copilot are agent providers with increasingly rich background/cloud surfaces. Conductor is a manager that wraps several providers around worktrees and review. None of these rows is a claim that the product is superior overall; each is a reference for a particular slice.

### Capability matrix

Legend: **✓** documented/delivered; **△** partial, provider- or surface-dependent, or experimental; **—** not the product’s stated model; **?** not established by the cited source.

| Capability | Hydra (`2c307c8`) | Herdr | Claude Code | Codex | Cursor | GitHub Copilot | Conductor |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Git worktree/branch as task unit | ✓ | △ pairs with managers | ✓ native `--worktree`/desktop | ✓ app worktrees; CLI is separate | ✓ UI/CLI worktrees | ✓ branch/PR; app worktrees | ✓ core workspace model |
| Real PTY survives UI close | ✓ tmux head | ✓ server-owned | △ session resume; not a provider-neutral PTY runtime | △ local CLI/app surface; cloud is environment-based | △ local terminal; cloud VM | — cloud sandbox, not a user PTY | △ local process; cloud survives app close |
| Semantic attention state | △ outcome/observation split; V1 active elsewhere | ✓ blocked/working/done/idle/unknown | △ tasks/permission prompts | △ thread/task status | △ agent status/artifacts | ✓ session status/logs | △ session status/API |
| Direct attach to one agent terminal | ✓ identity-checked tmux attach | ✓ agent/terminal attach | ✓ terminal/Remote Control surfaces | ✓ terminal CLI | △ remote desktop takeover/cloud; local CLI | — | △ terminal panel |
| Client/server or remote-machine split | △ tmux + fleet; V1 freshness active elsewhere | ✓ server + clients + SSH machines | △ local/cloud/Remote Control | ✓ Remote/mobile control and cloud | ✓ cloud agents; web/mobile | ✓ GitHub-hosted cloud sessions | △ cloud workspace; no self-hosted SSH model documented |
| Headless execution without a PTY | △ T1 active elsewhere; T2 planned | — terminal-centered | ✓ `-p`/SDK/background surfaces | ✓ cloud/CLI automation | ✓ Cloud Agents/API | ✓ cloud agent | ✓ cloud workspace/API |
| Durable evidence/outcome contract | ✓ hashes, attempts, gates, verification, unknown | — | △ summaries/tests/diffs | △ summaries/diffs/logs | △ logs/screenshots/videos/PR | ✓ logs, signed commits, PR checks | △ checks/diff/PR |
| Review/worktree-to-PR flow | ✓ guarded local integration; no implicit push | △ pairs with manager | ✓ branch/worktree/PR integrations | ✓ summary/diff/follow-up/PR | ✓ PR and artifacts | ✓ first-class PR workflow | ✓ diff/checks/merge/archive |
| Agent/API automation | ✓ shell CLI, workflows, adapters | ✓ CLI/socket/agent primitives | ✓ hooks/MCP/SDK/teams | ✓ CLI/SDK/plugins/scheduled tasks | ✓ API, automations, MCP | ✓ automations, integrations, hooks/skills | ✓ HTTP API and scripts |
| Remote/install footprint | Git + tmux + Make + C99; SSH fleet | single binary; macOS/Linux/Windows clients, SSH hosts | provider CLI; local/cloud/remote interfaces | CLI/app/cloud; OpenAI account surfaces | editor/CLI/cloud VM; Git provider | GitHub account/Actions/cloud policy | macOS app; GitHub + provider credentials |

The matrix is intentionally conservative. For example, “semantic attention” in Herdr means provider-state detection and user-facing badges; Hydra’s lifecycle contract is stronger, and the active-but-unmerged V1 work is what targets actionable waiting reasons and freshness. Conversely, a cloud agent’s logs and screenshots are review evidence, not automatically a verified domain result.

### Claude Code

Claude Code exposes the same agentic loop through terminal, desktop, IDE extensions, web, Remote Control, Slack, and CI/CD. A conversation is tied to a directory; sessions are saved locally and can be resumed or forked. Its worktree mode creates a separate checkout under `.claude/worktrees/`, defaults to the repository’s default branch, supports a named or generated worktree, and can isolate subagents in worktrees. Agent view, subagents, agent teams, and worktrees are separate parallelization choices.[^19][^20][^21]

The useful Hydra lessons are explicit worktree base selection, session identity, hooks for lifecycle events, and a clear distinction between a session transcript and the files being changed. Claude’s docs do not describe a provider-neutral server that owns arbitrary PTYs across client loss; its remote/cloud surfaces should therefore not be treated as a replacement for Hydra’s terminal owner.

### Codex

The Codex CLI is a local terminal loop for inspecting, editing, running commands, review, skills/plugins, and repeatable `codex exec` automation. The Codex app’s worktree mode runs independent chats in Git worktrees on the computer or remote development environment, supports selecting a base branch, and provides Handoff between local and worktree execution. Scheduled tasks can use local projects or isolated worktrees, but the computer must remain on for local files.[^22][^23]

Codex cloud runs tasks in isolated cloud environments, supports parallel work, watches logs, and lets a user inspect a summary/diff, request follow-up, or open a PR. Its documented entry points include web, GitHub, GitLab, Linear, and Slack. This is a good reference for Hydra’s remote result review, but it is cloud task execution rather than a local PTY runtime.[^24]

### Cursor

Cursor’s Agents Window and CLI create isolated Git worktrees, with setup commands and cleanup limits. Its Cloud Agents run on dedicated VMs with repositories, dependencies, secrets, and network access; they continue when the laptop is closed, can open PRs, and attach videos, screenshots, and logs so a reviewer can validate the result without checking out the branch. Cloud Agents can be started in Cursor, web/mobile, Slack, GitHub, Linear, or automations; the CLI can hand a conversation to the cloud with `&` and can use `--worktree` locally.[^25][^26][^27]

Cursor demonstrates a polished split between local foreground editing, isolated worktrees, and remote background proof. Its cloud-agent docs describe full development environments with secrets and network access, so explicit environment and credential policy matters. Hydra should preserve explicit host trust, capability negotiation, and bounded ownership rather than silently adopting that autonomy model.

### GitHub Copilot cloud agent and app

Copilot cloud agent researches a repository, plans, changes a branch, runs in an ephemeral GitHub Actions environment, and optionally creates a PR. The GitHub Agents surface supports live session logs, follow-up steering, stop/archive, and links from signed Copilot commits back to session logs. Cloud-agent sessions are shared by default with repository collaborators, and automations can run from schedules or repository events.[^28][^29]

The current cloud-agent documentation states a 59-minute maximum execution time, one branch and one PR per task, and GitHub-only repository scope. That is a useful reminder that “background” does not mean unbounded or exactly-once. GitHub’s app adds parallel sessions with dedicated worktrees and optional cloud sandboxes, with interactive/plan/autopilot modes.[^30]

Copilot is strongest at issue-to-branch-to-PR traceability and hosted audit logs. It is not a real-terminal attach product, so its attention and recovery model should be compared with Hydra’s headless task path, not with Hydra’s tmux attachment path.

### Conductor

Conductor is a macOS manager for Claude Code, Codex, Cursor, and OpenCode. Each workspace gets its own Git worktree, branch, files, terminal, diff, checks, and review path; the app helps create a PR, merge, and archive. Its parallel-agent docs distinguish independent workspaces from multiple agents deliberately sharing one branch. The API exposes workspace/session lifecycle, transcripts, status, cancellation, and polling cursors.[^31][^32][^33]

The local/cloud distinction is material: local sessions use the user’s environment and end when the app or Mac shuts down; cloud workspaces run in Vercel sandboxes and continue when the app closes. Conductor is macOS-only for the desktop app and requires GitHub plus at least one provider credential. This is a useful review/worktree composition pattern, but not an SSH fleet runtime or a provider-neutral evidence engine.[^34]

## 5. Herdr performance evidence and residual uncertainty

### Vendor benchmark

Herdr’s 3 August 2026 write-up compares official 0.7.5 binaries with hashed master builds (the article labels the optimized examples 0.8.0) on separate Linux and macOS machines. Each OS had 54 isolated observations, fresh named sessions, real attached clients in fixed 86×47 terminals, a five-second warmup, and 21-second samples. CPU is total Herdr server plus every attached client. The article pairs CPU with mechanism counters such as scheduled frames, hidden-source skips, and delivered mouse packets.[^35]

Selected reported values:

| Workload | 0.7.5 | 0.8.0/master | Reported change/interpretation |
| --- | ---: | ---: | --- |
| One silent working pane, Linux | 1.467% | 0.133% | 91% lower; removes spinner-driven frames |
| One silent working pane, macOS | 3.280% | 0.265% | Same shape |
| Ten silent working panes, three clients, Linux | 4.316% | 0.400% | 91% lower |
| Ten panes, nine hidden writers, three clients, Linux | 13.014% | 0.617% | 95% lower |
| Same hidden-output workload, macOS | 22.958% | 1.673% | 93% lower |
| Passive mouse motion at 60 Hz, Linux | 9.450% | 0.667% | 93% lower; packets still delivered |
| Forty-nine hidden writers at 60 Hz, Linux | 5.800% | 4.433% | Only 24% lower; PTY read/parse remains dominant |
| Forty-nine hidden writers at 60 Hz, macOS | 14.857% | 13.498% | Only 9% lower |

The write-up’s headline says total CPU fell 89–95% in rendering-heavy cases, but it also says the comparison is release versus master, environments and intervening changes are part of the result, and not every CPU point can be assigned to one commit. It explicitly says output-heavy cases remain dominated by PTY reads and terminal parsing. This is a mechanism-led vendor report, not an independent benchmark or a product-wide guarantee.

### Firsthand reports and subsequent fixes

The following reports are useful stress cases, not controlled measurements. Values are reproduced with the version, date, and workload stated by each reporter; issue bodies can be edited or closed later.

| Report | Version/date and workload | Observation | Later product evidence |
| --- | --- | --- | --- |
| [Discussion #399](https://github.com/herdrdev/herdr/discussions/399) | 0.7.0, 1 Jun 2026, Manjaro Linux, about 30+ idle zsh panes on an M3 Max | Reporter saw roughly 15–25% CPU; maintainer discussed Kitty graphics and hoped a later release would halve the cost. | Discussion is firsthand and not a benchmark. |
| [Issue #726](https://github.com/herdrdev/herdr/issues/726) | 0.7.0, 21 Jun 2026, Manjaro Linux 6.18, 30+ panes; agent detection every 300/500 ms, writer polling 5 ms, full render about 800 ms | Detailed hypothesis about timers, `/proc` scans, and rendering; no product-wide conclusion. | Later v0.8.2/v0.9.0 notes mention idle detection, scrollback, hidden redraw, and client-rendering work. |
| [Issue #936](https://github.com/herdrdev/herdr/issues/936) | 0.7.1, 2 Jul 2026, Ubuntu, more than 1,000 processes | Keystrokes took hundreds of milliseconds and CPU exceeded 60%. | Minimal reproduction details; resolution is not a general qualification. |
| [Issue #1107](https://github.com/herdrdev/herdr/issues/1107) | 0.7.1, 7 Jul 2026, macOS 26.5.2, WezTerm, 12 workspaces/21 panes, 1,461 processes | Server near 100%; sample pointed to synchronous pane-list and foreground-CWD process scans; restart dropped to about 4.4%. | v0.9.0 notes include process/CWD and redraw fixes, but do not prove this exact workload is fixed. |
| [Issue #1862](https://github.com/herdrdev/herdr/issues/1862) | 0.7.5, 25 Jul 2026, macOS 27 M5 Max, three clients and two working Codex panes | Ten one-second samples ranged 16.2–23.0%, average about 20.1%, with no interaction. | Shows that a stable release can still be expensive in a different workload. |
| [Issue #2178](https://github.com/herdrdev/herdr/issues/2178) | 0.7.5, 1 Aug 2026, headless systemd Linux; about 38 processes and 16–24 panes | Cgroup shared memory reached 9.92 GB after 80 minutes and the process was OOM-killed; a second WSL signal was explicitly caveated. | Closed/not planned; not a validated product-wide memory limit. |
| [Issue #2592](https://github.com/herdrdev/herdr/issues/2592) | 0.7.5, 10 Aug 2026, CentOS Stream, 316 cores, 185 panes, 501 Tokio workers | Reporter measured 7,431% CPU and 95.75% cycles in futex lock; described a hard-to-reproduce long-run live-lock. | Still requires maintainer judgment; treat as a residual risk case. |
| [Issue #2642](https://github.com/herdrdev/herdr/issues/2642) | Current master `42789c8`, 10 Aug 2026, Windows, three 15-second samples; quiet one-pane, 15-Codex, and 15-Pi-hook cases | 10.30%, 16.03%, and 16.76% CPU respectively. | Closed; sample is too small to generalize. |

The v0.9.0 release notes subsequently document client-side rendering, reduced redraw, compressed idle scrollback, delayed prompt reliability, stale SSH detach behavior, active-worktree cleanup, and many provider-detection fixes. A 8 September preview adds an idle-SSH-CPU fix. These are credible follow-up changes, but none is evidence that every report above is resolved on every OS, terminal, provider, or workload.[^18]

### Interpretation for Hydra

Herdr’s strongest gains target avoidable rendering and fan-out to clients. The residual cost is terminal emulation and PTY parsing, which any real-terminal runtime must pay. Hydra’s native TUI already retains frames and performs bounded asynchronous observations, but it has not published a comparable 1/10/50 active-worktree benchmark at this snapshot. The correct next step is a controlled Hydra measurement, not a percentage copied from Herdr.

## 6. Recommended Hydra parity plan

### P0 — Qualify the active T1/9A/V1 work and make existing state useful while disconnected

Continue the separately authorized T1, 9A, and V1 implementation tracks, then qualify each against the pinned acceptance boundaries before calling it delivered. On the audited baseline, the follow-on V1 surface should cover these snapshot fields and stale semantics over the current shell-owned records:

- `run_id`, `task_id`, `step_id`, `attempt_id`, host, workspace, profile, owner, and current instance;
- desired state, last observed state, observation source, observation timestamp, connection health, and age;
- concrete wait reason: admission, dependency, authentication, approval, or reconciliation;
- pending approval/auth request, unsatisfied obligation, required evidence, and next useful action;
- separate process exit, result collection, verification verdict, and human approval.

This gives the operator the best part of Herdr’s sidebar without weakening Hydra’s state authority. It also enables a useful parity demo even before tmux is removed from headless execution.

### P1 — Finish T2/T3 around the active T1 identity boundary

The active T1 work should create workspace/trust/provenance/execution identity before deciding whether a terminal is needed. T2 should then route headless remote work through the existing detached owner and capability negotiation. T3 should qualify fan-out, join, reconnect, cancellation, owner death, bounded logs, and exact result collection across two hosts without tmux on execution hosts. Keep interactive spawn/attach/messaging/transcript regressions on tmux-backed heads.

The acceptance sentence to preserve is: **terminal absence is not worker death; lost acknowledgments are not permission to replay; successful process exit is not a verified objective.**

### P1 — Add direct attach and multi-machine UX as clients

Once V1 identifies the owner and terminal capability, expose a direct attach target and a saved-machine view. A remote connection should show cached state dimmed, disable unsafe input, and reconnect with bounded backoff. The client should never answer a remote approval prompt or install/replace a server in the background. This follows Herdr’s documented behavior while leaving the shell CLI and receiver as mutation authorities.

### P2 — Use existing review primitives instead of a second manager

Hydra already has diff, provenance, gates, explicit approval, integration worktrees, and sealed output contracts. Add compact “ready to review” navigation, artifact inventory, and provider transcript links to the TUI/CLI. Do not make a manager app’s branch/PR state the source of truth, and do not add auto-merge or automatic push.

### P2 — Treat provider adapters as contracts, not labels

Keep the current declarative profiles and exact-session resume. Close the live Cursor/Claude host qualifications when the required credentials are available, recording prompt delivery, session recall, independent result checks, and observed-process cancellation. A screen badge can be an attention hint; only an adapter event, process observation, artifact, or verification gate may satisfy the corresponding contract.

### P3 — Publish mechanism-led performance evidence

Instrument the observer and TUI with the same discipline as the Herdr article: count scans, subprocess launches, PTY bytes read/parsed, frames emitted, frames sent to clients, stale-refresh attempts, and dropped/coalesced observations. Report CPU and memory for Hydra coordinator, receiver/owner, each client, and the provider separately. Keep idle, output-heavy, multi-client, and remote-transport cases distinct.

## 7. Reproducible 1/10/50-agent experiment

This is a proposed qualification plan, not a result.

### Matrix

Run the same repository and task recipe at **1, 10, and 50 real Git worktrees**. At each scale, measure:

- idle shells;
- active agents with normal output;
- one selected pane with 9/49 unselected writers;
- one, three, and (where supported) more attached clients;
- 1 Hz, 10 Hz, and 60 Hz synthetic output bursts;
- local interactive heads, local headless tasks, and remote headless tasks;
- blocked approval/question prompts and done-but-unseen results;
- one client disconnect/reconnect, coordinator restart, owner death, lost cancellation response, and stale provider session.

Use a fixed terminal geometry (86×47 is a useful comparison point), pinned repository commit, pinned tool/provider versions, fixed OS/architecture, clean environment, bounded warmup, and at least ten repetitions per cell. Report median, p95, and run-to-run spread; retain raw logs and machine-readable counters.

### Metrics

1. Agent/provider CPU and wall time.
2. Hydra owner/coordinator CPU, client CPU, RSS, shared memory, file descriptors, and child-process count.
3. PTY bytes, parse time, observation scans, subprocess launches, frames generated, frames transmitted, and client render time.
4. Input-to-echo latency, output-to-visible latency, stale-state age, reconnect time, and terminal resize time.
5. Queue delay, time to verified result, artifact transfer bytes, cancellation acknowledgment latency, and unknown-outcome rate.
6. Attention precision/recall against an annotated set of blocked, working, done, idle, and unknown screens; false blocked/false done are safety failures, not cosmetic defects.
7. Worktree locality: cost when exactly one worktree changes while the others remain quiet.

### Acceptance boundaries

- No client disconnect changes execution outcome or releases a reservation.
- A lost start/cancel response reconciles the original task identity; it never creates a duplicate side effect.
- A missing terminal is reported as “not attached” or “headless,” not as “dead.”
- A provider “done” signal without the required artifact or verification remains unaccepted.
- A stale host is visibly stale and cannot receive unsafe input until fresh identity and screen state are established.
- Interactive tmux behavior remains unchanged while headless paths run without tmux.

## 8. Bottom line

Hydra does not need to become Herdr, Conductor, Cursor Cloud, or GitHub Copilot. Its differentiator is the evidence and recovery boundary around agent execution. The best parity target is a composable two-layer model:

```text
Hydra execution/evidence layer
  task -> owner -> attempt -> artifact -> verification -> approval
  exact identity, bounded retries, unknown-outcome reconciliation

Interactive client layer
  terminal attach -> semantic attention -> stale/fresh remote view
  direct input only when a live terminal capability exists
```

Herdr supplies the clearest interaction vocabulary and performance instrumentation ideas. Claude Code and Codex show explicit session/worktree and local-to-cloud handoff patterns. Cursor and GitHub demonstrate background execution with reviewable artifacts and PR traceability. Conductor shows how a manager can compose providers around isolated workspaces. Hydra should borrow those affordances while keeping T1/9A/V1 explicitly in progress until their own merged acceptance evidence exists, and keeping T2/T3/V2–V4 visibly open.

## Sources

### Hydra source and documentation

[^1]: [Hydra README at the audited commit](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/README.md) — product model, prerequisites, heads, TUI, remote fleet, and install surface.
[^2]: [Hydra Lifecycle v1](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/LIFECYCLE.md) — independent outcome, observation, and liveness channels.
[^3]: [Hydra static workflows](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/WORKFLOWS.md) — DAG, retries, attempts, approval, integration, and headless steps.
[^4]: [Hydra supported agents and profiles](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/PROFILES.md) — adapter declarations and qualification boundaries.
[^5]: [Hydra remote tasks](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/REMOTE_TASKS.md) — receiver owner, capabilities, durable keys, unknown outcomes, and result snapshots.
[^6]: [Hydra native TUI](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/NATIVE_TUI.md) and [attached terminals](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/ATTACHED_TERMINALS.md) — current workspace/host/terminal surfaces and PTY boundary.
[^7]: [Hydra supported-agent qualification notes](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/PROFILES.md#L25-L31) — live sign-in and remote-qualification limits.
[^8]: [Hydra roadmap: optional tmux for headless and remote execution](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/ROADMAP.md#L117-L168) — T1, T2, and T3 are unchecked planned milestones.
[^9]: [Hydra roadmap: remote visibility](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/ROADMAP.md#L487-L555) — V1–V4 are planned operator visibility and qualification work.

### Herdr primary sources

[^10]: [Herdr concepts](https://herdr.dev/docs/concepts/) — workspace/tab/pane model, semantic states, sessions, and client/server split.
[^11]: [Herdr agent automation](https://herdr.dev/docs/agent-automation/) — pane/agent primitives, waits, socket/CLI automation, and exact IDs.
[^12]: [Herdr session state and restore](https://herdr.dev/docs/session-state/) — detach persistence, restart limits, native provider restore, and experimental handoff.
[^13]: [How to work with Herdr](https://herdr.dev/docs/how-to-work/) — local, SSH-shell, phone, and `herdr --remote` workflows.
[^14]: [Herdr persistence and remote access](https://herdr.dev/docs/persistence-remote/) and [connecting machines](https://herdr.dev/docs/connecting-machines/) — saved machines, stale views, reconnect, direct attach, and auth/install boundaries.
[^15]: [Herdr agents](https://herdr.dev/docs/agents/) — status authority, strict blocked detection, rollups, integrations, and direct attach.
[^16]: [Herdr compare](https://herdr.dev/compare/) — runtime/client distinction and comparison with multiplexers and manager apps. This is vendor positioning, not independent evaluation.
[^17]: [Herdr integration and detection documentation](https://herdr.dev/docs/agents/#direct-integrations) — current provider integration list and lifecycle/session roles.
[^18]: [Herdr GitHub releases](https://github.com/herdrdev/herdr/releases) — v0.9.0 (7 Sep 2026), preview 8 Sep 2026, client rendering, detection, SSH, prompt, and worktree fixes.
[^36]: [Christopher Penkin, “Herding Claudes: My Agentic Workflow, Five Months On”](https://www.penkin.me/ai/development/tools/productivity/2026/07/21/herding-claudes-my-agentic-workflow.html) — independent, firsthand worktree/Herdr/SSH/review workflow report.

### Comparable-product primary sources

[^19]: [Claude Code: how Claude Code works](https://code.claude.com/docs/en/how-claude-code-works) and [manage sessions](https://code.claude.com/docs/en/sessions) — interfaces, directory-bound sessions, resume, and fork behavior.
[^20]: [Claude Code: run parallel sessions with worktrees](https://code.claude.com/docs/en/worktrees) — native worktree creation, base refs, cleanup, and subagent isolation.
[^21]: [Claude Code: run agents in parallel](https://code.claude.com/docs/en/agents) and [desktop quickstart](https://code.claude.com/docs/en/desktop-quickstart) — subagents, agent view/teams, background tasks, and desktop worktrees.
[^22]: [Codex CLI](https://developers.openai.com/codex/cli) — local terminal loop, review, permissions, skills/plugins, and `codex exec` automation.
[^23]: [Codex worktrees](https://developers.openai.com/codex/app/worktrees) and [scheduled tasks](https://developers.openai.com/codex/app/automations) — local/remote worktree chats, Handoff, and scheduled-task limits.
[^24]: [Codex cloud](https://developers.openai.com/codex/cloud) — isolated cloud environments, parallel tasks, logs, review, PRs, and GitHub/GitLab/Linear/Slack entry points.
[^25]: [Cursor worktrees](https://cursor.com/docs/configuration/worktrees) and [Cursor CLI](https://prod.cursor.com/docs/cli/using) — UI/CLI worktrees, setup, cleanup, and local-to-cloud handoff.
[^26]: [Cursor background agents](https://prod.cursor.com/help/ai-features/background-agents) — long-running VMs, PR artifacts, logs/screenshots/videos, and remote takeover.
[^27]: [Cursor cloud agents](https://cursor.com/docs/cloud-agent) — provider connections, environments, multi-repo support, and cloud execution model.
[^28]: [GitHub Copilot cloud agent](https://docs.github.com/en/copilot/concepts/agents/cloud-agent/about-cloud-agent) — repository research/planning, branch/PR work, Actions environment, integrations, and limits.
[^29]: [GitHub Copilot agent sessions](https://docs.github.com/en/copilot/how-tos/copilot-on-github/use-copilot-agents/manage-and-track-agents) — live logs, steering, stop/archive, signed commit links, and sharing.
[^30]: [GitHub Copilot app](https://docs.github.com/en/copilot/concepts/agents/github-copilot-app) and [cloud/local sandboxes](https://docs.github.com/en/copilot/concepts/about-cloud-and-local-sandboxes) — parallel worktrees, modes, and optional cloud sessions.
[^31]: [Conductor introduction](https://www.conductor.build/docs) and [Git worktrees](https://www.conductor.build/docs/concepts/git-worktrees) — provider composition, workspace/branch/terminal/review model.
[^32]: [Conductor parallel agents](https://www.conductor.build/docs/concepts/parallel-agents) — independent versus shared-workspace decisions.
[^33]: [Conductor API](https://www.conductor.build/docs/api) — workspace/session lifecycle, status, cancel, transcripts, and polling.
[^34]: [Conductor installation](https://www.conductor.build/docs/installation) and [cloud FAQ/security](https://www.conductor.build/docs/cloud/faq) — macOS-only local app, credentials, local shutdown behavior, and cloud persistence.

### Herdr benchmark and firsthand reports

[^35]: [Herdr: “Ten agents, three clients, 95% less CPU”](https://herdr.dev/blog/ten-agents-three-clients-95-percent-less-cpu/) — vendor benchmark methodology, mechanism counters, workload numbers, and limitations.
The issue/discussion links in the performance table are the primary URLs for the reporters’ versions, dates, workloads, and observed numbers: [#399](https://github.com/herdrdev/herdr/discussions/399), [#726](https://github.com/herdrdev/herdr/issues/726), [#936](https://github.com/herdrdev/herdr/issues/936), [#1107](https://github.com/herdrdev/herdr/issues/1107), [#1862](https://github.com/herdrdev/herdr/issues/1862), [#2178](https://github.com/herdrdev/herdr/issues/2178), [#2592](https://github.com/herdrdev/herdr/issues/2592), and [#2642](https://github.com/herdrdev/herdr/issues/2642). They are firsthand observations, not independent controlled benchmarks or product-wide guarantees.
