# Hydra interactive parity — decision brief

**Status:** provisional discussion brief  
**Snapshot:** 9 September 2026  
**Full report:** [Hydra interactive parity research](./interactive-parity-report.md)  
**Roadmap:** unchanged; this brief proposes no roadmap edits.

## The short decision

Hydra should borrow the best operator affordances from Herdr—server/client separation, direct attach, semantic attention, explicit stale remote views, and low-noise rendering—without importing Herdr’s state model as Hydra’s completion authority. Hydra’s differentiator is stronger: exact task/attempt identity, evidence-bound artifacts, verification gates, approval, bounded retries, and conservative `outcome_unknown` recovery.

The authorized implementation tracks for T1, 9A, and V1 are already running in separate tasks. This brief does not re-plan or qualify those changes; it sets the follow-on priorities around them:

1. Continue the active T1/9A/V1 work in parallel, with separate acceptance, merge, and release evidence for each.
2. Complete T2/T3 around the T1 identity boundary so headless local and remote work can run without tmux while interactive heads retain tmux.
3. Add direct attach and multi-machine interaction as clients over the explicit identity and snapshot contracts.
4. Measure 1/10/50-worktree behavior before making performance or parity claims.

In the audited `2c307c8` baseline, T1, 9A, and V1 are absent from the tree. Per the coordinator’s clarification they are **in progress but unmerged**; T2, T3, and V2–V4 remain **planned/unchecked** in that baseline. None is a delivered release claim.[^1][^2]

## Strongest findings

### 1. Hydra already owns the hard correctness boundary

The audited [`2c307c8`](https://github.com/Someblueman/hydra/tree/2c307c82e97b948ecd6965d8d33e7079a3701eca) tree separates declared outcome, provider observation, and tmux/process liveness. Its workflows persist attempts/events, enforce idempotency-aware retries, bind artifacts and approvals, and refuse to infer completion from quiet output. Remote tasks use durable submission keys, a receiver-owned detached process, bounded logs/results, and `outcome_unknown` reconciliation.[^3][^4][^5]

**Implication:** do not replace these contracts with a single “working/done” badge. A status badge can drive attention; it cannot satisfy a verification gate.

### 2. Herdr is the closest interaction reference

Herdr’s server owns real PTYs and processes while clients attach, detach, and render. It exposes `blocked`, `working`, `done`, `idle`, and `unknown`, direct agent/terminal attach, CLI/socket waits, and workspace/tab rollups.[^6][^7] Its saved-machine view keeps multiple SSH hosts in one client, leaves disconnected state visibly stale, and disables unsafe input until a fresh connection is established.[^8]

**Implication:** this is a strong model for Hydra’s operator surface and V1 semantics, but Herdr is terminal-centered rather than an evidence-bound objective engine.

### 3. Other products optimize different layers

- [Claude Code](https://code.claude.com/docs/en/worktrees) provides native worktrees, session resume/fork, subagents, agent view/teams, hooks, and several local/cloud interfaces.[^9]
- [Codex](https://developers.openai.com/codex/app/worktrees) provides app worktrees/Handoff, scheduled local work, and [cloud tasks](https://developers.openai.com/codex/cloud) with logs, diffs, follow-ups, and PRs.[^10]
- [Cursor](https://cursor.com/docs/cloud-agent) provides UI/CLI worktrees and Cloud Agents in dedicated VMs with PR artifacts, screenshots, videos, logs, and web/mobile/Slack/GitHub/Linear entry points.[^11]
- [GitHub Copilot cloud agent](https://docs.github.com/en/copilot/concepts/agents/cloud-agent/about-cloud-agent) provides issue/branch/PR execution in ephemeral GitHub Actions environments, live session logs, steering, signed commits, and repository automations.[^12]
- [Conductor](https://www.conductor.build/docs) composes Claude, Codex, Cursor, and OpenCode around isolated workspaces, terminals, diffs, checks, PRs, and archive flow; its desktop app is macOS-only and its local/cloud persistence differs.[^13]

**Implication:** Hydra should not become a provider-specific manager or cloud PR service. It can integrate with those surfaces later while keeping its shell-owned local/trusted-remote execution authority.

### 4. Herdr’s performance claim is useful but not portable

Herdr reports 89–95% lower total server+client CPU in rendering-heavy comparisons, with fixed 86×47 terminals, 54 observations per OS, 5-second warmup, 21-second samples, and mechanism counters. The same article says output-heavy cases remain dominated by PTY reading/parsing and that the comparison is release versus master.[^14] Firsthand reports range from roughly 15–25% CPU on 30+ panes to >60%, ~100%, memory OOM, and a hard-to-reproduce 7,431% CPU lock case across different versions and workloads; v0.8.2/v0.9.0 later list several rendering, SSH, scrollback, prompt, and detection fixes.[^15]

**Implication:** borrow the measurement method and counters, not the headline percentage. Hydra needs its own baseline and attribution.

## Existing implementation milestones (status, not proposals)

| Existing item | Intended boundary | Current classification |
| --- | --- | --- |
| T1 | Workspace/trust/provenance/execution identity independent of terminal; verified local headless execution without tmux. | In progress, unmerged from audited baseline |
| T2 | Remote exec/headless workflow through the detached owner on hosts without tmux; exact outputs, cancellation, and unknown-outcome rules retained. | Planned, unchecked |
| T3 | Two-host distributed qualification with tmux absent on execution hosts and interactive regressions retained with tmux. | Planned, unchecked |
| 9A | Satisfiable outcome obligations, evidence paths, compiler diagnostics, and rejection of orphan/impossible checks. | In progress, unmerged from audited baseline |
| V1 | Fresh run/host snapshots with owner, state, observation freshness, waiting reasons, obligations, and stale-connection semantics. | In progress, unmerged from audited baseline |
| V2–V4 | Resumable event/log cursors, acknowledged controls, and end-to-end recovery qualification. | Planned, unchecked |

Git verification at this snapshot shows the remote `v2.3.0` annotated tag resolving to commit `d21a651a51873d0504c8c23c94598ca7e59f15f0`; audited `main` is `2c307c82e97b948ecd6965d8d33e7079a3701eca`, exactly 32 commits ahead. Source-present native-TUI/statistics behavior must therefore not be described as a new release without separate publication evidence.

## New proposals for discussion

| Option | Shape | Benefit | Tradeoff |
| --- | --- | --- | --- |
| **A. Evidence-first parity (recommended follow-on)** | Qualify the active T1/9A/V1 tracks in parallel, then add actionable snapshots/attach clients and complete T2/T3/V2–V4 around those contracts. | Smallest coherent extension; preserves Hydra’s safety model; improves disconnected operation without reopening the terminal/headless contract. | Less immediately flashy than a new pane manager; requires careful cross-task acceptance evidence. |
| **B. Runtime-first** | Build a Herdr-like server-owned PTY/client layer before headless separation. | Fastest route to direct attach, multi-client views, and terminal persistence. | Duplicates or displaces tmux lifecycle; risks a second state authority; does not solve evidence/outcome semantics. |
| **C. Manager/cloud-first** | Prioritize worktree/PR/artifact integrations with Codex, Cursor, Copilot, or Conductor. | High review/throughput leverage and provider-native artifacts. | Provider/cloud coupling; weaker fit for Hydra’s local/trusted-SSH and no-cloud-account guardrails; still leaves terminal/headless boundary unresolved. |

## Uncertainties and contradictory evidence

- **Benchmark variance:** Herdr’s controlled vendor report and issue reports measure different versions, operating systems, terminal renderers, pane counts, output rates, and observation periods. No single number is a product-wide baseline.
- **Documentation versus qualification:** Product docs establish intended behavior; they do not independently prove crash recovery, cancellation exactness, false-attention rates, or resource scaling.
- **“Headless” is not tmux-free in the audited baseline:** Hydra has headless provider contracts, but current remote execution still requires tmux; active T1/T2 work is the explicit boundary for changing that.
- **Provider state versus completion:** Herdr and other managers expose useful semantic states, but a provider “done” or a quiet terminal cannot substitute for Hydra’s required artifact/evidence/verification path.
- **Current branch versus release:** `2c307c8` is the audited source snapshot; v2.3.0 is the published tag. Keep those claims separate.

## Decisions needed from the user

1. **Remote posture:** Should multi-machine visibility target trusted SSH hosts only, or should cloud environments/providers be first-class Hydra targets?
2. **Attention authority:** Which provider signals may be displayed as hints, and which (if any) may satisfy a formal workflow obligation?
3. **Performance gate:** What CPU, memory, latency, stale-age, and false-attention thresholds should qualify 1/10/50-worktree parity?
4. **Integration boundary:** Should Hydra expose provider-native logs/artifacts/PR links as read-only evidence references, while leaving provider-specific review and merge actions outside Hydra?
5. **Provider breadth:** Should parity focus first on the currently declared adapters and trusted remote hosts, or include cloud-only providers and manager integrations in the initial acceptance matrix?

The safe interim position is **Option A**: continue the already-authorized T1/9A/V1 work in parallel, then use its merged contracts for the follow-on parity slices. No roadmap update is proposed here.

## Sources

[^1]: [Hydra roadmap: optional tmux for headless and remote execution](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/ROADMAP.md#L117-L168).
[^2]: [Hydra roadmap: remote visibility](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/ROADMAP.md#L487-L555).
[^3]: [Hydra Lifecycle v1](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/LIFECYCLE.md).
[^4]: [Hydra static workflows](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/WORKFLOWS.md).
[^5]: [Hydra remote tasks](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/REMOTE_TASKS.md).
[^6]: [Herdr concepts](https://herdr.dev/docs/concepts/).
[^7]: [Herdr agent automation](https://herdr.dev/docs/agent-automation/) and [agents](https://herdr.dev/docs/agents/).
[^8]: [Herdr connecting machines](https://herdr.dev/docs/connecting-machines/) and [persistence/remote access](https://herdr.dev/docs/persistence-remote/).
[^9]: [Claude Code worktrees](https://code.claude.com/docs/en/worktrees), [sessions](https://code.claude.com/docs/en/sessions), and [parallel agents](https://code.claude.com/docs/en/agents).
[^10]: [Codex worktrees](https://developers.openai.com/codex/app/worktrees), [scheduled tasks](https://developers.openai.com/codex/app/automations), and [Codex cloud](https://developers.openai.com/codex/cloud).
[^11]: [Cursor worktrees](https://cursor.com/docs/configuration/worktrees), [CLI](https://prod.cursor.com/docs/cli/using), and [Cloud Agents](https://cursor.com/docs/cloud-agent).
[^12]: [GitHub Copilot cloud agent](https://docs.github.com/en/copilot/concepts/agents/cloud-agent/about-cloud-agent) and [agent sessions](https://docs.github.com/en/copilot/how-tos/copilot-on-github/use-copilot-agents/manage-and-track-agents).
[^13]: [Conductor introduction](https://www.conductor.build/docs), [worktrees](https://www.conductor.build/docs/concepts/git-worktrees), and [installation/cloud FAQ](https://www.conductor.build/docs/installation).
[^14]: [Herdr performance benchmark](https://herdr.dev/blog/ten-agents-three-clients-95-percent-less-cpu/).
[^15]: [Herdr releases](https://github.com/herdrdev/herdr/releases) and the firsthand reports linked in the [full report](./interactive-parity-report.md#firsthand-reports-and-subsequent-fixes).
