# Hydra v2.4.0 Release Notes

Release date: 2026-09-11

Hydra 2.4.0 adds a native workspace for planning, execution and evidence review,
plus bounded planning tools, task recovery visibility and explicit host enrollment.

## Highlights

- Work in resizable panes with attached local tmux heads, preserved terminal drafts,
  project/head/run navigation and recorded statistics. Validate and preview a plan,
  approve its exact digest, and inspect evidence through the existing workflow engine.
- Create terminal-independent headless workspaces while interactive heads retain
  tmux. Structured outcome obligations, handoff contracts and subject-bound reports
  distinguish execution, evidence and domain results.
- Explain and compare plans, use finite patterns and maps, reuse sealed artifacts
  with fresh affected checks, and admit staged plans separately. Public feature,
  performance and research examples include independent negative controls.
- Follow observation freshness and resumable events/logs, use exact-target controls,
  and recover the original attempt after controlled transport loss. Inspect saved
  events and comparisons and expire eligible local evidence with protection rules.
- Discover selected hosts, review explicit enrollment intent and apply bounded
  source-snapshot batches.
- Build and test without Python. Planning producers/checkers, test fixtures and
  independent PTY observers now use native code. Standalone termviz export includes
  a portable static-library build and native component/PTY tests.
- Fix empty-record sorting under sanitizers, event-rotation recovery, cancellation
  races and incremental terminal decoding across resize.

## Compatibility and upgrade

This is a minor release with compatible additions. Upgrade the shell CLI and
optional native helpers together to 2.4.0, including selected fleet hosts. State v2,
core protocol 1, TUI protocol 2 and fleet protocol 1 remain in use. Existing local
and distributed plans remain supported; new obligations are additive and become
part of the compiled acceptance digest when supplied.

Older interactive head records default to interactive terminal mode. New headless
records use an explicit mode and desired-state token: older readers ignore them
and older state verification refuses that token. Upgrade tools before using
headless records; do not use older tools to manage those new records. See
[the state contract](https://github.com/Someblueman/hydra/blob/v2.4.0/docs/STATE.md).
Interactive Codex restore retains worktree-scoped `resume --last`; headless resume
requires the exact recorded session and matching execution identity.

The shell CLI remains compiler-free. Build optional helpers with
`make build-core build-tui build-fleet`; fleet requires JSON-C development files
and pkg-config, with JSON-C linked statically. Native tests and planning examples
require the documented C toolchain. The source archives are installed with
`sh install.sh`; verify their bytes against `SHA256SUMS` before installing.

## Scope and qualification limits

The [release scope](https://github.com/Someblueman/hydra/blob/v2.4.0/docs/RELEASE_NEXT_SCOPE.md)
links implementation and historical acceptance records. Local fixtures and
controlled SSH failures do not qualify operational hosts, external-host soak or
live provider behavior. T2 live-host acceptance, T3 and the documented outstanding
provider checks remain open. Performance examples use synthetic work; research
examples use a finite supplied trace.

The selected 9E scope includes the implemented tools and a successfully executed,
independently checked real Python cleanup workflow. Its serial/parallel timings
are descriptive; speedup is not an acceptance criterion. Broader planning-quality,
calibration and benefit-overhead research remains deferred.

Reuse assumes complete dependency declarations. Maps are finite, joins are frozen
and staged plans need separate admission. Dynamic expansion, coordinator failover,
automatic reassignment and scheduled pools remain outside this release. Structural
validity and successful execution do not establish semantic adequacy or grant
review approval. Historical evidence remains bound to its recorded revisions.
The release tag and source archives identify the qualified merged main commit.
