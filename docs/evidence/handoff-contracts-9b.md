# 9B local acceptance

Implementation baseline: `0190bf938d82fa7d136369f96ed9061d6c0e2df6`.
Contract and limits: [Bounded handoff contracts](../HANDOFF_CONTRACTS.md).

The accepted scope is a finite opt-in data-2 contract, implemented through the
existing plan compiler, artifact snapshots, receipts, materialization and task
source-collection APIs. No provider campaign or host enrollment was performed.

## Reproducible checks

| Check | Observed result |
| --- | --- |
| `make test-workflow-contracts` | 22 tests pass, including six real supervised handoffs |
| `HYDRA_FLEET_BIN="$PWD/build/hydra-fleet" sh tests/test_workflow_data.sh` | 11/11 pass |
| `HYDRA_FLEET_BIN="$PWD/build/hydra-fleet" sh tests/test_workflow_plan.sh` | 91/91 pass after host PTY capacity was restored; native statistics comparison enabled |
| `build/test-plan` and `build/test-workflow-data` | Pass |
| `make test-quality-c quality-c` | Pinned clang-tidy 22.1.8; full analysis and unchanged complexity ceilings pass |
| `make lint` | Pass |
| Native helper and plan/data unit tests built with `-fsanitize=undefined` | Pass; 21 non-interactive contract tests pass, supervised test deliberately omitted from this sanitizer run and covered by the normal target |

The six supervised cases are compatible success, missing semantic fields, wrong
units, stale identity, lossy conversion and a shared composition-invariant
failure. Each negative first passes structural workflow validation. Assertions
then require an actual `invalid_data` preparation/sealing record, not merely a
nonzero process exit. The consumer has neither a command PID nor stdout launch
record, and never writes its execution marker. For the composition case, the
producer succeeded and both components independently satisfied their contracts.
The consumer's cross-input check stopped submission.

Other cases verify exact schema/version matching, required evidence IDs, array
cardinality, changed input snapshots, post-composition checks, generated source
provenance, configuration/artifact hash binding, complete manifest coverage,
unsupported relation enforcement, malformed constraints and duplicate JSON
members. Out-of-range signed integer tokens are rejected before JSON-C can
saturate them; embedded NUL cannot alias member names or metadata references.

## Legacy acceptance binding

The baseline native helper was built from `git archive` of the baseline commit
inside ignored `build/9b-legacy-source`, without a worktree or checkout change.
Both helpers compiled the same unmodified data-1 plan/source fixture through the
public plan CLI. Their compiled files were byte-identical, and the new helper
admitted the old compiler's original acceptance digest through the existing
binding check. `PLAN_COMPILER`, the compiled schema version, and legacy output
receipt shape remain unchanged.

## Qualification limits

A preliminary legacy-plan run reached 89/91 because `tmux` failed to create the
graph-test head (`fork failed: Device not configured`). Its two downstream graph
assertions could not exercise the intended barrier. The final 91/91 run followed
authorized host cleanup and supersedes that environmental result.

AddressSanitizer was attempted but stalled at native process startup on this
macOS host; the attempt was stopped. The repository's supported macOS sanitizer
mode is UBSan, whose checks passed. AddressSanitizer qualification is not claimed.

The generated collected-step provenance path reuses verified collection,
result-envelope and parent-receipt checks and requires one clean collected head.
The new local negative case proves that a succeeded flag and sealed artifact
alone cannot supply that provenance. These local fixtures do not establish a new
external-host or live-provider qualification. Resource mutex enforcement,
arbitrary schema implication, ambient dependency discovery, OS isolation,
determinism, safe repetition of external effects and semantic correctness remain
outside the supported contract. This milestone does not implement 9C or 9D.
