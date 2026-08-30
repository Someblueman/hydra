# Hydra v1.7.0 Release Notes

**Release date:** 2026-08-30

## Highlights

Hydra 1.7.0 makes parallel heads safer to coordinate and proves a small optional
native acceleration without moving mutation or policy out of the shell CLI.

Shell-only installation remains fully supported. No compiler, native artifact, or
background daemon is required.

## Parallel safety

- `claim`, `scope`, and `collision` expose intent, accepted change boundaries, and
  claim/overlap/predicted/observed conflict evidence without presenting overlap as a
  guaranteed conflict or scope as a sandbox.
- Locked per-head ports, Compose names, and database names are unique and released
  during successful teardown.
- Verification command evidence and human approval are separate, and approval is
  valid only for the exact verified commit and worktree status.
- Typed context packs include only selected diffs, hashed file manifests, notes,
  bounded history, and artifact references; likely secret paths are rejected.

## Guarded integration and recovery

- `sync` and `land` require clean worktrees and current approved gates, simulate
  merges, archive the prior commit and a Git bundle, and abort conflicts back to the
  exact clean pre-operation commit.
- `du` separates worktree and state usage. Named `gc` policies and worktree doctor
  actions default to dry-run and preserve dirty/untracked work unless separately
  authorized.

## Optional read-only native core

- Protocol v1 accepts only capability reporting, state/event validation, canonical
  JSON encoding, and snapshot aggregation. Shell remains the mutation authority.
- `hydra snapshot --native` is explicit. Absence, protocol or release-version skew,
  crash, timeout, malformed output, and permissions all fall back to byte-equivalent
  shell output.
- Offline artifacts carry checksum, OS/architecture, and dependency metadata and are
  verified against exact Hydra/core version agreement before atomic replacement.
- The reproducible 20-head macOS arm64 benchmark measured native snapshot p95 at 84
  ms versus 5459 ms for shell in that run. This claim applies only to snapshot.

## Upgrade from 1.6.x

Install the new version using the same prefix as the existing installation:

```sh
git pull
PREFIX=$HOME/.local ./install.sh
hydra version                      # should show 1.7.0
hydra doctor
```

No state migration is required from 1.6. To build and install the optional helper:

```sh
HYDRA_INSTALL_CORE=required HYDRA_BUILD_CORE=1 \
  PREFIX=$HOME/.local ./install.sh
hydra snapshot --native
```

## Validation and documentation

Run the release gates from the repository root:

```sh
git diff --check
make lint
make test
make test-all
make test-install test-native-install smoke-onboarding
```

Parallel safety, native architecture/distribution, protocol, versioning, and the
tmux control-mode prototype are documented under [`docs/`](docs/).

## Deferred

Native writes, native workflow policy/YAML, a public C ABI, the native TUI, general
workflows, and fleet operations remain deferred.
