# Native workspace integration qualification

On 9 September 2026 the native workspace branch was merged with main's Hydra
2.3.0 admission and distributed DAG implementation (`d21a651`). The tested merge
is `8a6e07f5e5966d56e365b1b58f0136a255ceb8df`, tree
`3d98feabd4bb2c97fe213436f7f6bf87ba4256c7`. The subsequent main merge
(`bfd6823`, PR #79) changes only the roadmap; this record is also documentation.
The code, tests and CI configuration remain those of the tested merge.

## Results

All commands below completed with exit status zero on local macOS arm64:

- `TMUX_TMPDIR="$PWD/build/integration-tmux-ordinary" make -j4 test-all`:
  all 16 acceptance dependencies, including shell/C/fleet, native workspace and
  attached PTYs, exact plan approval, statistics, standalone export, install,
  parity and onboarding. Log: `build/main-integration-acceptance-isolated.log`.
- `make -j3 sanitize-workspace sanitize-attached sanitize-plan-workspace sanitize-statistics`:
  `build/main-integration-sanitize-ui.log`.
- `make -j3 sanitize-core sanitize-tui`:
  `build/main-integration-sanitize-core.log`.
- `make sanitize-fleet`: `build/main-integration-sanitize-fleet.log`.
  Together these cover every dependency of `make sanitize`, using UBSan.
- `make test-quality-c quality-c`: `build/main-integration-quality.log`.
  Analysis completed with 193 advisory functions and no regression against the
  reviewed complexity baseline; it is not a warning-free result.

The logs are local build evidence, not tracked artifacts. Linux/macOS hosted CI
adds the workspace-specific acceptance suites; hosted results must be checked
separately before merge.

## Runner findings

The first full-suite attempt overlapped the fleet sanitizer on the default tmux
server and failed while creating a timestamp-named task session already present
there. The same exact-main source-handoff test passed in a separate tmux namespace.
The full acceptance rerun used a dedicated namespace and every target completed.
GNU Make 3.81 then remained blocked on its jobserver pipe because the test-owned
idle tmux server had inherited the pipe descriptors. After auditing all 16 target
completion records and confirming make had no children, closing only that server
released the pipe and the original make process returned zero. No exit status was
substituted and no runtime cleanup behavior was changed. CI uses separate tmux
namespaces for native, workspace, sanitizer and installation steps.

## Scope

The merge preserves schema-2 task/repair previews and first-attempt queue timing
through repair. Queue timing ends at coordinator dispatch; downstream admission
wait is included in execution elapsed time. Missing historical or remote start
boundaries remain unknown. Recovery assertions account for the explicit resume in
the injected repair write-fault case.

The real-provider exercise in [WORKSPACE_REAL_ACCEPTANCE](../WORKSPACE_REAL_ACCEPTANCE.md)
is historical evidence for its stated earlier revision. This integration does not
establish a new provider exercise, remote statistics cohort, provider cost/resource
telemetry, release, or hosted CI result.
