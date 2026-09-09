# Native workspace integration qualification

On 9 September 2026 the native workspace branch was merged with main's Hydra
2.3.0 admission and distributed DAG implementation (`d21a651`). The tested merge
is `8a6e07f5e5966d56e365b1b58f0136a255ceb8df`, tree
`3d98feabd4bb2c97fe213436f7f6bf87ba4256c7`. The subsequent main merge
(`bfd6823`, PR #79) changes only the roadmap; this record is also documentation.
That roadmap merge preserved the tested code, tests and CI configuration.

PR #80's hosted ShellCheck 0.9.0 subsequently flagged SC2015 in the recovery
counter bound. An explicit `if` replaces the equivalent `A && B || C` condition.
Focused checks under dash and macOS sh cover zero, leading zeros, the maximum,
overflow, invalid/missing history and approval continuation. Repository lint is
rechecked with both ShellCheck 0.9.0 and 0.11.0 for this syntax-only follow-up.

## PR review follow-up

Optional statistics writes no longer block run startup/recovery or turn a verified
delivery into failure. The required execution-state and independent-verification
gates remain intact. Actual CLI rename faults cover run start, recovery count,
verification time and its plan binding, including seeded stale timing. The plan
suite passed 78 checks and runtime suite passed 47; negative verification, altered
plan guards, cancellation and ordinary recovery remain covered.

The private workspace projection escapes tabs, carriage returns and newlines in
its display-only root. The complete plan/UI acceptance passed in a tab-containing
checkout. After moving that existing checkout to a newline-containing path, the
read-only CLI projection and native parser retained the same project identity and
association rows. This does not expand the existing durable initialization rule,
which rejects newlines in repository scalar paths.

The affected statistics PTY, workspace controls, real visualization and ShellCheck
0.9.0 repository lint passed. Local logs use `build/pr80-*-review.log`,
`build/pr80-plan-timing.log`, `build/pr80-runtime-timing.log`,
`build/pr80-path-plan-launch.log`, `build/pr80-workflow-controls.log` and
`build/pr80-review-fixes-lint.log`.

The shell-only test target now excludes the native visualization script, retaining
its prerequisite-backed target in full/native acceptance. A clean source archive
attempt first hit an unchanged admission failure, then passed admission and stopped
at path tests because the archive lacked Git metadata. The same path tests passed
12/12 after initializing that disposable fixture as a Git repository. No further
full shell rerun was used to claim clean-source acceptance; hosted shell CI remains
the complete check for this routing correction.

Python test bytecode is ignored by the repository itself. The earlier macOS local
checks inherited that rule from the user's global Git excludes, while hosted Linux
did not; `tests/termviz/__pycache__/pty_support.*.pyc` consequently made the hosted
source appear dirty. The package dirty-source guard is unchanged.
An isolated Git snapshot of staged tree `d4afe87988be0d408d1092bd802dd4cdaa0f1cec`,
with global excludes disabled, rebuilt core/TUI and passed the statistics tests.
The resulting bytecode left its Git status clean; both native packages matched
their built bytes, SHA-256 metadata and snapshot commit. A deliberate README edit
was still refused by packaging. Only this evidence paragraph was added afterward.
Logs: `build/pr80-package-probe.log` and `build/pr80-package-dirty-refusal.log`.

### Launch receipt synchronization

PR CI run `34290951594` failed intermittently at the duplicate-approval notice on
the same `a308fab` source that passed the push run. A controlled observation delay
reproduced the boundary: the receipt file existed before its UI projection; the
duplicate refusal appeared, then the pending receipt update replaced the footer.
The PTY test now waits for the matching rendered run receipt before pressing E
again. Duplicate refusal, absence of a second confirmation, detached continuation,
public duplicate rejection and the single-run assertion remain unchanged.

No production code changed. The controlled case passed with synchronization, as
did the full plan-launch PTY test on macOS arm64 and a fresh Ubuntu 24.04 arm64
container, plus repository lint with ShellCheck 0.9.0. Local logs:
`build/pr80-receipt-race-before.log`, `build/pr80-receipt-race-after.log`,
`build/pr80-receipt-synchronized.log`, `build/pr80-linux-plan-launch.log` and
`build/pr80-receipt-lint.log`.

### Queued form input under terminal backpressure

The older push CI run `34290948221` failed 22 macOS PTY assertions around search,
palette input and action return, while terminal restoration checks passed. Local
ordinary acceptance passed. The form repainted the full view for each queued byte:
one 13-character search burst generated 30,889 bytes of output locally. A bounded
slow-consumer case reproduced a late palette action and subsequent input mismatch
with the original one-second deadlines.

Forms now drain at most 64 already-queued bytes before repainting. A drained queue
returns to painting and the ordinary 40 ms wait before flushing incomplete escape
input. The same observed search burst generated 1,834 output bytes afterward;
this is a local output-volume observation, not a general latency benchmark.

The existing palette suite applies 64-byte reads with 2 ms pacing to its longest
form burst and checks a fragmented Home key with cursor insertion. It retains all
action, literal-argv, raw-mode and deadline assertions. Against the archived
pre-fix binary this final fixture failed two checks; the corrected binary passed
138/138 on macOS arm64 and fresh Ubuntu 24.04 arm64. UBSan PTY checks and the
planning, launch and workflow-control suites passed, as did 79 native checks and
repository lint. Targeted Clang-Tidy 22.1.8 analysis passed without increased
complexity ceilings (`prompt_text`: 42 against the existing ceiling of 45).

Logs: `build/pr80-form-burst-regression-before.log`,
`build/pr80-forms-pty-final.log`, `build/pr80-forms-linux-final.log`,
`build/pr80-forms-pty-sanitize.log`, `build/pr80-forms-plan-sanitize.log`,
`build/pr80-forms-native-tests.log`, `build/pr80-forms-quality.log` and
`build/pr80-forms-lint.log`.

## Fleet detail exit synchronization

PR CI run `34294265107` on macOS passed the C PTY, plan launch/control and
local statistics checks, then missed the fleet `Find host:` prompt. The fleet
PTY test sent Escape followed by `/` after a fixed 50 ms delay. A delayed UI
could read both bytes as one escape sequence, leaving host detail open.
The test now waits for the selected host list row to reappear before sending
search input; search, filtering and drill-down assertions and deadlines remain
unchanged. Production code is unchanged.

A controlled probe paused the real UI for 150 ms at detail Escape. The old test
reproduced the same missing-prompt failure; the corrected test passed under the
same pause. `make test-statistics` and full lint with CI ShellCheck 0.9.0 also
passed on macOS. Local logs: `build/pr80-fleet-escape-before.log`,
`build/pr80-fleet-escape-after.log`, `build/pr80-fleet-statistics-final.log` and
`build/pr80-fleet-escape-lint.log`. Hosted acceptance for the follow-up remains
separate.

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
