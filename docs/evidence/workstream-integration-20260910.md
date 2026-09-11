# Local workstream integration — 10 September 2026

The local `codex/release-next` branch integrates the selected V, planning and
onboarding workstreams. The full fleet suite passed on the runtime built at
`1488df3`. Later commits `12ffd9b` (ASCII output formats) and `7c15c77`
(successful-result requirement for reuse) passed focused default and UBSan
acceptance on dedicated builds. Local and remote main were independently checked
at `2c307c82`. No push, main merge, release, operational-host enrollment or live
provider campaign is part of this qualification.

The [machine-readable evidence index](workstream-integration-20260910.json) records
source commit `ef8f853`, source-tree and runtime hashes, retained run/result
identities and completed log hashes. It distinguishes the earlier full-fleet
runtime from later focused changes and does not claim one final `make test-all`
invocation. Its final manifest entries include all five cases on both builds;
the negative case binds the deliberately incorrect composer source separately.

| Scope | Locally qualified behavior | Evidence and limits |
| --- | --- | --- |
| V3/V4 | Exact task-bound approval/resume/cancel controls, visible cancellation stages, client restart and original-attempt recovery across two receiver homes | [Recovery evidence](recovery-visibility-v4.md). A local SSH shim controls transport loss; this is not an external network soak. |
| 9D | Public feature, performance and research outcomes with independent checks and declared negative controls | [Feature](9d-feature-outcome.md), [performance](9d-performance-outcome.md), [research](9d-research-outcome.md). Results apply to those supplied pilots. |
| 9E, partial | Complete structural node explanations, matched deterministic plan comparisons, finite serial/fork/join patterns and incorrect-artifact rejection | [Inspection](../PLAN_INSPECTION.md), [patterns](../PLAN_PATTERNS.md). No measured semantic planner improvement or calibrated estimates. |
| 9F, partial | Explicit versioned sealed-artifact reuse, preserved original attempts, fresh affected checks, interrupted repair reconciliation and precompiled finite manifest maps | [Reuse](../PLAN_REUSE.md), [manifest maps](../PLAN_MANIFEST.md). Six accepted tasks replace eight in the reuse fixture; this is avoided work, not a speedup claim. Manifest selection is frozen before admission. Local artifact-only effects and complete dependencies are operator assumptions. |
| H3 | Four supplied snapshot formats, 100 selected targets, 16-host processing and 50-host interrupted enrollment/reconciliation | [H3 evidence](h3-source-batches.md). No live vendor discovery client or provider credential path. |
| Observability | Native JSON metrics/comparison and ASCII saved-event announcements with explicit missing evidence | [Observability](../OBSERVABILITY.md). Receiver unknown totals, total manual interventions and network bytes remain unmeasured. |
| Retention, partial | Explicit bounded head-event archives, audit expiry summaries and monotonic stream identity | [Retention](../RETENTION.md). Global task/run archive quotas remain open; recovery records and accepted evidence are preserved. |

Verification on the integrated source:

- Full `make test`: passed, including the 14 public schema-3 evidence controls,
  durable runtime metric reconciliation, 49 event-retention assertions and 50
  event-foundation assertions. Log: `build/all-streams-shell-suite.log`.
- Full `make test-fleet`: passed. This includes workflow replay/lost-response
  reconciliation, malformed-result refusal, source tampering, all nine plan
  verdicts, repair and budget controls, approval, package/install and the complete
  public task acceptance suite. Log: `build/all-streams-fleet-suite.log`.
- `make test-c test-parity test-task-announce test-statistics-export
  test-plan-inspection test-plan-outcomes`: passed. This includes six public plan
  inspection tests and thirteen pattern/performance/research test methods with
  their corruption cases. Log: `build/all-streams-native-final.log`.
- Strengthened V4 default and UBSan core/fleet/TUI: passed with retained fixtures.
  Both recover twelve events and a 351-byte owner log as a 64-byte prefix plus a
  287-byte resumed suffix, then verify the exact result. Logs:
  `build/v4-retained-default.log`, `build/v4-retained-sanitize.log`.
- Selective repair default and UBSan: passed, including interrupted journal
  recovery, preserved attempts, fresh affected checks, sixteen corrupted-state
  cases, changed current PATH and nine unsupported-policy controls on each final
  fixture. Logs: `build/reuse-success-final-default.log`,
  `build/reuse-success-final-sanitize.log`.
- ASCII announcement and statistics-comparison formats: passed with default and
  UBSan builds, malformed-input refusal, unknown denominators, signed deltas and
  exact parity with all six saved workflow pages. Logs:
  `build/accessible-views-default.log`, `build/accessible-views-sanitize.log`.
- Native TUI and PTY tests passed with default and UBSan builds. The final normal
  build also passed plan revision/launch/control, attached-terminal, visualization,
  fleet-control and 33 native-install assertions. Workspace/component and
  standalone export checks passed separately. Logs:
  `build/all-streams-tui-default.log`, `build/all-streams-tui-sanitize.log`,
  `build/all-streams-final-integration.log`, `build/all-streams-controls-final.log`,
  and `build/all-streams-workspace-final.log`.
- Integrated source C analysis and ShellCheck/POSIX lint: passed. C analysis has
  184 advisory functions and no regression against the reviewed ceilings. Logs:
  `build/all-streams-quality-reuse-terminal.log`,
  `build/all-streams-lint-final-source.log`.
- Finite manifest maps: four positive public outcomes and one zero-exit incorrect
  composition rejected per build, including empty/all-skipped maps and eight
  enabled members. Current compiler/checker tests pass in default and UBSan builds;
  the combined outcome target passes nineteen test methods. The mixed public case
  also refuses a changed manifest after compilation without publishing a new run.
  Logs: `build/manifest-public-current-default.log`,
  `build/manifest-public-sanitize-final.log`,
  `build/manifest-outcomes-integrated.log`, `build/manifest-focused-sanitize.log`
  and `build/manifest-final-lint.log`.

A final semantic corruption probe exposed a reuse defect: a valid, collectable
receiver result with a failed runtime state could still qualify after its hashes
were recomputed. Reuse now separately requires the sealed receiver state to be
`succeeded` with exit status zero. Both result and repair hashes are recomputed in
the negative controls, so these checks exercise that semantic requirement. The
valid interrupted-repair runs still accept six tasks and preserve the original
attempts; no control adds a receiver acceptance record.

One V4 test defect surfaced during final qualification: it downloaded after
process success while `result_state` was still `sealing`. The test now waits for
`ready`. The separate process/result states are preserved. Earlier recovery tests
read the entire owner log before restart; the final test requires a genuinely
partial prefix and a nonempty suffix.

Remaining work is explicit in the [roadmap](../ROADMAP.md): matched task-class
planner evaluation and benefit measurement; runtime membership/conditional/expansion
semantics if selected workloads require them; global reference-aware task/run
archive and quota operations; and instrumentation for the unmeasured telemetry.
The completed local slices do not close those broader acceptance requirements.
