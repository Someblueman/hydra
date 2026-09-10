# Release-next review scope

This is the local review candidate on `codex/release-next`, inspected at
`ecaee6b` on 10 September 2026. It assigns no version and does not establish
release acceptance. Later implementation commits need their own recorded checks;
historical evidence below applies to the revisions named in each record.

## Included implementation and local qualification

| Requirement | Reviewable behavior | Qualification and boundary |
| --- | --- | --- |
| T1, T2 implementation | Terminal-independent identity/workspaces, local and remote headless execution and compiled plans; interactive heads retain tmux | [Integration](evidence/next-wave-integration.md), [T2](T2_ACCEPTANCE.md): no-tmux local checks and controlled SSH acceptance. T2 live-host acceptance stays open. |
| 9A–9C | Structural outcome obligations, bounded handoff contracts, subject-bound structured evidence and validator controls | [9A contract](PLAN_COMPILATION.md#outcome-obligations-9a), [9B](evidence/handoff-contracts-9b.md), [9C integration](evidence/observation-enrollment-integration.md). Structural validity is not semantic adequacy; no LLM judge calibration is claimed. |
| 9D | Public feature, performance and research outcomes with independent negative controls | [Workstream acceptance](evidence/workstream-integration-20260910.md) links each supplied pilot. The performance workload is synthetic and research data finite. |
| 9E, partial | Structural explanations, deterministic comparisons, finite patterns and a real maintenance workflow | [Inspection](PLAN_INSPECTION.md), [patterns](PLAN_PATTERNS.md), [Python cleanup](evidence/python-cleanup-20260910/README.md). Successful real planning, execution and independent verification qualification. Serial/parallel timings (106.33/106.69 seconds) are descriptive; this cleanup was not expected to benefit from parallelism, and speedup is not an acceptance criterion. |
| 9F, bounded | Versioned sealed-artifact reuse, fresh affected checks, precompiled maps and separately admitted staged plans | [Reuse](PLAN_REUSE.md), [maps](PLAN_MANIFEST.md), [stages](PLAN_STAGED.md). Local artifact-only reuse requires complete dependency assumptions; at most eight map members, frozen joins and no runtime expansion. |
| V1–V4 | Freshness-aware observations, resumable events/logs, exact-target controls and original-attempt recovery | [Observation integration](evidence/observation-enrollment-integration.md), [recovery](evidence/recovery-visibility-v4.md). Two local receiver homes and controlled SSH loss qualify these paths, not an external-host soak. |
| Visibility and retention | Recorded metrics, accessible saved-event/comparison views and protected local task/run evidence expiry | [Remaining-work acceptance](evidence/remaining-work-20260910.md). Transport-process bytes are not wire traffic; off-CLI intervention and provider cost remain unknown. Quotas exclude workspaces and concurrent admission; external claims need pins. |
| H1–H3 | Selected discovery, explicit enrollment intent and bounded source-snapshot batches | [Discovery](HOST_DISCOVERY.md), [H2 integration](evidence/observation-enrollment-integration.md), [H3](evidence/h3-source-batches.md). Controlled probes/enrollment and supplied vendor snapshots do not qualify live hosts or vendor clients. |
| Python cleanup | Shared native example precompiler and retired redundant evaluation plumbing with retained independent coverage | [Cleanup evidence](evidence/python-cleanup-20260910/README.md). Its source revisions, inventories and workflow outcomes remain historical; subsequent cleanup must be checked separately. |

## Outstanding review and release decisions

- **9E review scope is settled.** On 10 September 2026 the user explicitly
  selected the implemented 9E tools and successful real Python cleanup workflow
  for review, leaving broader comparative planning-quality research open. That
  research is not a review-readiness blocker and no new experiment is required.
  Neither the frozen toy pilot nor one maintenance workload closes stochastic
  variability, held-out outcome errors, calibration or benefit exceeding overhead.
- **T2 live qualification, T3 and provider checks remain open.** T2 needs an
  authorized tmux-free host and authenticated adapter. T3 needs the full distributed
  scenario in that environment. The [roadmap provider list](ROADMAP.md#2-adapter-conformance-and-headless-execution-remaining-live-qualification)
  retains Claude remote, Cursor local/remote and Antigravity remote checks with
  their individual authentication/deferment boundaries. Local fixtures do not
  close them. These are not silently added to the selected V/9/H workstreams.
- **H live-host limits remain visible.** Actual SSH/jump-host/key and operational
  enrollment qualification needs separately authorized targets; supplied inventories
  and local receiver fixtures are the current claim boundary.
- **Final candidate acceptance is separate from historical slice checks.** The
  leader must bind the integrated candidate to relevant local acceptance and review
  results. The [release definition of done](ROADMAP.md#release-definition-of-done)
  still requires applicable compatibility, installation, documentation and exact
  end-to-end evidence. Hosted CI, publication artifacts and release identity are
  later gates, not established by this documentation reconciliation.

Dynamic expansion under 9F remains conditional on an actual workload that cannot
use staged finite plans. Load balancing, performance baseline work, dynamic pools,
remote statistics cohorts and new provider/resource telemetry retain their existing
backlog selection boundaries. Nothing here selects another feature or campaign.

## Optional future 9E research

No further experiment is part of this review scope. If the user later selects
this research, first select one representative task class and an
explicit comparison question. A suitable first question is whether contract-aware
planning improves independently verified maintenance outcomes enough to pay for
its planning and correction time, with scheduling and placement fixed.

Missing inputs are the approved task packet and independent held-out checker,
current-versus-contract-aware planner definitions, available provider/model and
cost records, a run/time/spend cap, a meaningful benefit threshold, and an operator
who can record human corrections. Freeze these, the selection rule, incorrect
artifacts, stopping rule and estimate fields before generation. Retain failed
attempts and all exclusions. Use matched independent repetitions with randomized
order within the approved budget; report uncertainty and insufficient evidence
when that budget cannot distinguish the threshold. Compare scheduling separately
only after the planning comparison warrants it.

This future proposal is deferred, not a launched or required experiment. A single successful pair can prove
the measurement path; it cannot close broad task-class quality or calibration.
No operational host calls, provider runs, push, merge, tag or release are authorized
by this record.
