# Hydra 1.9.0 release checklist

This checklist separates the implemented candidate and local qualification from Git
publication. Nothing in the workflow or integration feature pushes, merges a hosted
PR, tags, or publishes a release.

## Implemented and locally evidenced

- Strict trusted schema v1, normalized preview, all workflow commands, atomic run
  creation, correlated events, bounded retries, parallel fan-out/join, cancellation,
  and interrupted-run recovery.
- Verified integration preview and isolated assembly, candidate/target bindings,
  observed conflict recovery, gate evidence, explicit approval, local promotion, and
  cleanup.
- Guarded merge train with immutable order, per-candidate gates, exact failure
  report, cancellation/resume, stale-binding rejection, and all-or-nothing promotion.
- Bash, Zsh, and Fish completion; version 1.9.0 shell/core/TUI/install handshakes;
  contract documentation; repository example; and local end-to-end fixture.

## Local qualification required for the exact candidate

- [x] `make test-all`
- [x] `make sanitize`
- [x] `git diff --check`
- [x] Review the complete diff.
- [ ] Record the exact candidate commit.

## Publication requires separate authorization

- [ ] Push the reviewed feature branch.
- [ ] Open the PR and run the exact commit through hosted Linux and macOS checks.
- [ ] Resolve formal reviews and verify hosted checks against the candidate commit.
- [ ] Merge, tag `v1.9.0`, and publish release artifacts only when authorized.
- [ ] Verify local branch, remote branch, merge commit, tag, and release-object parity.
