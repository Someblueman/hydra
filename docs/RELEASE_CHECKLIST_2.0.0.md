# Hydra 2.0.0 release checklist

This checklist qualifies one immutable candidate. Record the candidate commit before
running commands; tag, artifacts, checksums, and the hosted release must resolve to
that same commit.

## Candidate

- [ ] Record `git rev-parse HEAD` and confirm `git status --short` is empty.
- [ ] Confirm the changelog, contracts, migration guide, support policy, security
      model, CLI help, completions, and roadmap describe the same 2.0 surface.

## Local qualification

Run from the candidate commit:

```sh
make lint
make test-all
make sanitize
git diff --check HEAD^
```

`make test-all` is the canonical shell, lifecycle, workflow, integration, migration,
native, parity, install, package, onboarding, and PTY suite. The state migration
tests cover dry-run, backup, post-migration verification, writer interruption, failed
rollback preservation, and successful downgrade restoration.

## Hosted qualification

- [ ] The exact candidate passes current Ubuntu and macOS CI.
- [ ] Shell-only installation passes without a compiler.
- [ ] Source/native installation, native/basic parity, crash/timeout/version-skew
      fallback, unsuitable terminals, sanitizer checks, and packaged artifacts pass.
- [ ] Release archives and checksum manifests are built from the candidate commit.

## Publication

- [ ] Merge only the qualified candidate.
- [ ] Create `v2.0.0` at that exact commit.
- [ ] Publish archives and checksums generated from that commit.
- [ ] Verify the hosted release, tag, and artifacts resolve to the same commit.

Commit, push, merge, tag, and hosted release publication are distinct actions.
