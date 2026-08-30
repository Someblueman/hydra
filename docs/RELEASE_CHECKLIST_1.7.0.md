# Hydra 1.7.0 release qualification

This checklist records local qualification on 2026-08-30 from
`feature/1.7.0-release-closeout`. It does not claim a tag, hosted release, or hosted
Linux/macOS CI run before those publication steps occur.

## Local acceptance

```sh
git diff --check
make lint
make test
make build-core test-c test-parity
make sanitize
make test-install
make test-native-install
make smoke-onboarding
make bench-core
sh scripts/bench-tmux-control.sh
otool -L build/hydra-core
```

Observed results:

- ShellCheck style and `dash -n` passed for every shell file.
- The complete shell suite passed, including 26 parallel-safety, 25 guarded
  integration, and 27 worktree-operation assertions.
- Native C unit and 33 protocol/parity/fallback assertions passed; UBSan passed on
  local macOS. The Linux CI lane adds ASan to UBSan.
- Fresh shell-only install, successful in-place upgrade with state preservation,
  and uninstall passed 40 assertions; offline/source native
  install, checksum, platform, dependency, source identity, handshake, preservation,
  discovery, source-archive provenance, and uninstall passed 19 assertions.
- Clean-home no-agent onboarding passed 39 assertions and left no source-repository
  branch or worktree debris.
- The macOS arm64 20-head snapshot benchmark reported shell cold/p50/p95 of
  5459/5277/5459 ms and native cold/p50/p95 of 84/73/84 ms for that run.
- The tmux 3.5a control prototype returned all 30 markers in both paths and measured
  97 ms for bounded polling versus 9 ms for one control-mode attachment.
- `otool -L` reported only `/usr/lib/libSystem.B.dylib` for the local arm64 helper.

The GitHub workflow defines the same strict native build, parity, sanitizer,
shell-only/native installation, and dependency inspection on current Ubuntu and
macOS runners. Those hosted results remain a publication prerequisite.

The native-TUI parity, state-migration, and new adapter-matrix items in the global
definition of done are not applicable: 1.7 adds no native TUI, state schema change,
or adapter contract. The existing lifecycle and onboarding suites still pass.

## End-to-end guarded integration scenario

The replayable fixture is:

```sh
sh tests/test_integration_safety.sh
```

Inside a throwaway repository it runs this user-level sequence (the artifact path is
under the fixture's generated temporary root):

```sh
hydra init --no-agent --trust
hydra spawn integration-head --no-agent
hydra context create integration-head --diff --file tracked.txt --note 'review note' --history 1 --artifact /tmp/fixture/result.log
hydra gate run integration-head --name ready -- true
hydra gate approve integration-head --name ready --by human-reviewer
hydra sync integration-head --from main --gate ready --dry-run
hydra sync integration-head --from main --gate ready
hydra land integration-head --into main --gate ready --dry-run  # rejected: approval is stale
hydra gate run integration-head --name ready -- true
hydra gate approve integration-head --name ready --by human-reviewer
hydra land integration-head --into main --gate ready --dry-run
hydra land integration-head --into main --gate ready
```

The 25 passing assertions verify typed input selection and secret refusal, captured
and approved evidence, merge simulation, exact-gate invalidation after commit change,
pre-operation bundles, base advancement, successful landing and teardown, plus a
second conflicting sync that aborts cleanly and records recovered failure evidence.

## Publication boundary

Before tagging 1.7.0, the maintainer must review the feature-branch diff, commit and
push the exact candidate, obtain green hosted Linux/macOS CI for that commit, and
then create the tag and hosted release from the unchanged commit. None of those Git
or hosted publication actions is performed by this implementation branch work.
