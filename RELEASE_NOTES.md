# Hydra v1.9.0 Release Notes

Release date: 2026-09-02

Hydra 1.9.0 closes the local coordination loop: a trusted finite workflow can create
and operate parallel heads, preserve authoritative results across interruption, and
feed completed candidates into an isolated verified integration or guarded merge
train. Promotion remains an explicit local action and never pushes.

## Highlights

- Workflow schema v1 rejects unknown keys and unsupported YAML constructs, requires
  explicit idempotency, and supports `spawn`, `wait`, `exec`, `message`, `gate`,
  `approve`, and `kill` steps.
- The runner persists its resolved definition and manifest before execution, honors
  bounded parallelism/retries/resources, propagates cancellation, and resumes stale
  runs without replaying completed effects.
- Verified integration previews candidate order, bases, claims, overlaps, and
  predicted conflicts before creating a disposable worktree.
- Integration reports retain immutable candidates and target, merge/gate evidence,
  approval, result tree, observed failures, and a concrete recovery command.
- `hydra integrate train` verifies after every candidate and promotes the complete
  result only after current approval and binding revalidation.
- Bash, Zsh, and Fish completions, an offline workflow example, and a fully local
  workflow-to-integration acceptance fixture ship with the release candidate.

## Quick start

```sh
hydra init --no-agent --trust
hydra workflow validate examples/workflows/local-review.yml
hydra workflow dry-run examples/workflows/local-review.yml
hydra workflow run examples/workflows/local-review.yml

hydra integrate train release-group --base main --target main --dry-run --gate 'make test'
hydra integrate train release-group --base main --target main --execute --gate 'make test'
hydra integrate approve run_ID --by reviewer
hydra integrate promote run_ID
```

`hydra workflow status`, `cancel`, and `resume` manage workflow runs. Integration
failures use the recovery command printed by `hydra integrate status run_ID`.

## Compatibility and safety

The shell CLI remains the mutation authority. Native core protocol v1 and native TUI
protocol v2 are unchanged; their release handshakes now report 1.9.0. Existing 1.8
state remains valid. Repository workflows share the `.hydra` trust boundary, argv is
the default execution form, and no workflow/integration command pushes or publishes.

See [docs/workflows.md](docs/workflows.md) and
[docs/RELEASE_CHECKLIST_1.9.0.md](docs/RELEASE_CHECKLIST_1.9.0.md) for contracts and
the exact-candidate qualification/publication boundary.
