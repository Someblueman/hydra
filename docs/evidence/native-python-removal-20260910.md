# Native Python removal: local acceptance record

This record covers the native test and example migration on `codex/release-next`
on 10 September 2026, with parent commit
`6d899fc1143352bcd5b367448a7f2385f7122021`. The frozen implementation tree is
`187208cfa474e79d7ae2fd4c99bd9abfebc6ccc5`, including the verified installation
fixture correction. This tree is a locally verified review candidate: all
`test-all` recipe commands and targets pass across the recorded runs and repairs.
The final commit may add only this record and `docs/RELEASE_NEXT_SCOPE.md`
to this qualified tree; its identity will be reported separately. This is not a
released revision. Historical Python-cleanup workflow evidence remains tied to its
original source revisions and is not replaced by this record.

## Changed boundary

All 39 tracked Python sources have been removed from the candidate. Native C
planning producers/checkers, CLI fixtures, export checks and independent PTY
observers replace them; shell entry points and CI invoke those native programs.
The active build, tests and examples have no Python runtime dependency. The
invocation blocker log, `build/no-python-invocations.log`, remains zero bytes
after integrated acceptance. The final tracked-file audit finds no Python
sources, and the active Make/scripts/CI/tests/examples executable scan finds no
Python invocation or `.py` reference.

The [native test mapping](../../tests/native/README.md) and
[PTY mapping](../../tests/termviz/README.md) identify the preserved cases. The
[dependency boundary](../PYTHON_USAGE.md) distinguishes active code from historical
evidence. The [native review rationale](../quality/native-python-removal.md)
records the reviewed complexity ceilings, subprocess bounds and independent
checker boundaries; it does not substitute for execution evidence.

## Completed checks

Log paths below are local evidence under the repository's `build/` directory;
they are not publication artifacts.

| Check | Observed result | Local evidence |
| --- | --- | --- |
| Shell lint | ShellCheck and dash checks pass; the later installation-fixture delta also passes | `build/no-python-lint-final.log`, `build/native-install-fixture-lint.log` |
| Final C quality, including the NUL regression case and existing feature test | Full and incremental gates pass against the same 248 source files: 286 advisory functions, no increases against the reviewed baseline; gate self-tests pass | `build/no-python-quality-reconciled.log`, `build/no-python-quality-incremental-reconciled.log` |
| Final headless fixture repair | 105/105 assertions pass, including all 14 mutation-byte guards. A deliberately unchanged mutation fails before validation. Targeted C quality retains `fx_plan` complexity 16; ShellCheck and dash pass | `build/native-headless-repair-final.log`, `build/native-headless-guard.log`, `build/native-headless-repair-quality-final-summary.log`, `build/native-headless-repair-lint-final.log` |
| Reuse acceptance after fixture correction | Pass: changed contracts/bindings/proof/dependencies rejected; accepted baseline and selective repair reconcile durable/native metrics | `build/no-python-reuse-final.log` |
| Native sanitizer coverage | Discovery passes in the initial sanitizer run. The continued run passes enrollment, receiver, strict loopback SSH, retention, workflow metrics/contracts and the remaining requested outcome targets | `build/native-port-sanitize/acceptance.log`, `build/native-port-sanitize/acceptance-continued.log` |
| Accepted reuse and retention sanitizer coverage | Accepted workflow reuse/invalidation and retention controls pass under ASan/UBSan; all six bindings reuse their submissions, with zero new receiver acceptances and workspaces preserved | `build/native-port-sanitize/reuse-accepted.log`, `build/native-port-sanitize/retention-accepted.log`, `build/native-port-sanitize/retention-accepted.json` |
| NUL regression sanitizer coverage | Performance outcome groups, CSV, integer-range and NUL-output rejection controls pass under ASan/UBSan | `build/native-port-sanitize/performance-nul-final.log` |
| Independent PTY sanitizer coverage | All eight drivers pass under ASan/UBSan: workspace/shell lifecycle, statistics, fleet controls, plan workspace, attached execution, plan launch, workflow controls and fleet recovery | `build/review-ecaee6b/pty-sanitizers-final.log` and its per-driver logs |
| PTY subprocess robustness | Standalone workspace, shell and lifecycle cases pass after descriptor bounds and interrupted-wait fixes | `build/review-ecaee6b/pty-robustness.log` |
| Research outcomes | Valid finite-trace negative recommendation passes; nine independent claim mutations, four malformed subjects and malformed CSV are rejected. Blank-line CSV framing also passes | `build/review-ecaee6b/research-native-final.log`, `build/review-ecaee6b/research-blank-lines.log`; earlier sanitizer controls in `research-sanitize.log` in the same directory |
| Performance rejection boundaries | Original five groups plus malformed CSV, integer-range and embedded-NUL output regressions pass | `build/native-tests/performance-nul-final.log` |
| Public feature workflow | Native public compile/run/result passes for `run_4cf78877e1cc5d642b99` | `build/native-feature-public.log` |
| Public staged workflows | Normal, maximum and empty cases pass, including changed-local-finding and stale-source negative controls | `build/native-tests/staged-public-normal.json`, `build/native-tests/staged-public-maximum.json`, `build/native-staged-empty-final.json` and `.log` |

The staged records retain their individual source hashes, plan hashes, run IDs
and fixture paths. The latest empty-case fixture is
`build/qualification/staged-empty-native-un4ZjI`; its stages are
`run_40ae344d02692f0b338e` and `run_b7bbc936e8793b87d5ee`.
These are evidence for the recorded executions, not proof that earlier binaries
are identical to the final candidate.

## Integrated run and retained failures

The first `make test-all` run stopped at `test-plan-reuse`: the native fixture
was invoked from the wrong working directory and returned 127. The failure is
retained in `build/no-python-test-all.log`. The shell caller was corrected and
the affected target passed in `build/no-python-reuse-final.log`. The initial run
must not be reported as a complete pass.

The continuation in `build/no-python-test-all-continued.log` runs the remaining
`test-all` targets, using Make's `-o` only for the six initial passing targets,
the separately rerun reuse target and lint. Shell CLI, PTY, export and
visualization checks have passed. The continuation then stopped at the first
headless `test_workflow_plan` run: 73 of 91 assertions passed. Compact JSON from
the native fixture made whitespace-sensitive mutation expressions no-ops,
causing nine negative-control pairs to fail. The fixture serialization was
corrected and 14 guards now assert that negative mutations change bytes. The
repaired headless run passes 105/105 assertions. The normal plan run in the
remaining fleet sequence also passes 105/105. The final fixture delta passes
targeted C quality and shell lint after the full/incremental quality runs above.
This continuation log is retained as a failed run, not a complete pass.

The remaining fleet commands were extracted from `make -n test-fleet`, beginning
after the failed headless command. The full command list is
`build/no-python-fleet-command-list.txt`; the exact remaining sequence is
`build/no-python-fleet-remaining.sh`, run with `sh -e`, the same Python invocation
blockers and `TMUX_TMPDIR=build/native-final-tmux`. Its log is
`build/no-python-fleet-remaining.log`. That sequence exits zero, including final
collection, integration, capability and task-acceptance checks. Subsequent
`test-all` targets pass across the original and repaired logs below. No single
uninterrupted `make test-all` pass is claimed.

The post-fleet target chain in `build/no-python-test-all-final-targets.log`
passes `test-c`, `test-tui`, `test-tui-pty`, `test-parity` and `test-install`,
then stops at `test-native-install` with 29/33 assertions passing. Its source
archive fixture copied the Makefile but omitted the newly required
`scripts/native-tests.mk`, preventing native binaries/provenance from being
built in that fixture. The narrow fixture-copy correction passes affected
ShellCheck/dash checks; the production installer is unchanged. The repaired
remainder in `build/no-python-test-all-final-targets-repaired.log` passes
`test-native-install` 33/33 and `smoke-onboarding` 39/39. All post-fleet targets
are therefore covered across the original and repaired logs.

The initial sanitizer run also stopped during enrollment. Its continuation
retains the successful enrollment rerun and remaining requested targets. The
initial sanitizer log is evidence for its completed discovery checks, not a
standalone all-target pass.

## Local acceptance and claim limits

The original passing targets, corrected reuse and headless reruns, successful
remaining fleet sequence and post-fleet original/repaired runs collectively cover
every `test-all` recipe command and target. The implementation tree named above
remained frozen at the final audit; the staged whitespace check also passes.
Final commit verification must show that its tree differs from that implementation
tree only in this evidence record and `docs/RELEASE_NEXT_SCOPE.md`.

No hosted CI, push, main-branch merge, tag, release or benchmark benefit is
claimed. Controlled local/loopback SSH fixtures do not qualify operational hosts
or provider accounts. Synthetic performance controls and the fixed 12-job
research trace preserve rejection behavior; they do not establish production
performance or general planning quality. The selected bounded 9E implementation
and real cleanup workflow remain in review scope; broader comparative 9E
research remains deferred and is not an acceptance prerequisite.
