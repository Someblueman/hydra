# Local quality hook pilot

`quality.json` uses the agent-toolkit adapter to check changed shell files with
`scripts/lint-shell.sh`: ShellCheck 0.11.0 with Hydra's POSIX/style flags plus
dash syntax checks. `make lint` still checks the complete shell inventory when
invoked explicitly. Tools must be on PATH; checks never install them. Run
agent-toolkit's `tools/quality/bin/quality --root /path/to/hydra doctor` to verify
setup, including GNU Make 3.81 and the pinned C checker.

Shell coverage is sources in bin/hydra, lib, scripts, tests, install/uninstall,
and assets/demos. C sources, headers, and included fragments in src, tests and examples/planning are included in the Stop-stage C pilot. The 500-line threshold is
advisory. Existing dirty work is not automatically repaired; the hook limits
repair continuation to one block and reports unresolved findings.

Install using `tools/quality/bin/quality --root /path/to/hydra install-codex`
from agent-toolkit, then review/trust its three hooks in Codex. Registration is
local because it points to the local agent-toolkit checkout.

Outcome records are local JSONL under `~/.cache/agent-toolkit/quality/`, named
with the SHA-256 of the absolute repository path. They record timing, event,
checked/skipped, result, hashed session, and Stop blocking, without source or
command output. Logs remain until removed. Compare real checked events and
repair outcomes separately from setup/trial events after a week of use.

## C analysis

Run `make quality-c` for clang-tidy 22.1.8 analysis of all native C sources and
the C test translation units. The target uses CORE_CFLAGS, JSON-C pkg-config includes,
and the macOS SDK sysroot when applicable. Headers and .inc fragments are
analyzed through their including translation units, not as standalone programs.
Native .c files are discovered recursively. Fleet/TUI builds and C analysis use
the same source inventory, including the domain subdirectories.

Provision with the toolkit quality `setup` command using the existing quality.json,
or install a native LLVM 22 distribution providing clang-tidy 22.1.8
(`brew install llvm@22` on macOS). `scripts/clang-tidy.sh` locates the native
binary; `HYDRA_CLANG_TIDY` can select its exact path.
Check-time installation is disabled. At Stop, `scripts/quality-c-incremental.sh`
checks cognitive complexity for changed C translation units using the same native
flags and reviewed baseline. Header, included-fragment, Makefile, checker-script
or baseline changes select all C translation units. The full static analyzer is
manual-stage in quality.json: run `make quality-c` or explicit `quality check`.
Shell remains fast-stage. Successful fast checks are reused at Stop.

C cognitive complexity is now an incremental regression gate. The reviewed
`docs/quality/cognitive-complexity.tsv` records ceilings by relative source path
and function name. A function absent from the table has a ceiling of 15. Higher
scores fail `make quality-c`; line-number changes do not. Existing analyzer
warnings remain visible and require review, while compiler/tool failures block.
The full analysis target retains its checker version, enabled checks and threshold. The original
118-function audit baseline and subsequent dispositions are documented in
`CODEBASE_SIMPLIFICATION.md`.

When simplifying an existing hotspot, review the complete function and its
extracted helpers together. Update the baseline only with that reviewed change,
ratchet reduced scores down, and remove obsolete entries. New helpers above 15
need an explicit reason in the change description; moving code or adding an
allowance alone does not establish simplification. Do not suppress diagnostics,
raise the threshold, or edit allowances merely to pass the check.

`make test-quality-c` exercises the actual pinned checker: new and increased
scores fail, threshold-15 and unchanged scores pass, line shifts are ignored,
and empty baselines, missing sources, and compiler failures cannot hide a
regression. CI runs these checks on macOS with the same pinned checker and keeps
the full report. Native tests and sanitizers still run on Linux and macOS.
`test-all` remains usable with the ordinary build toolchain; C analysis is its
separate required CI job. Shell cognitive complexity remains unavailable.

The complete report is written to `build/quality-c.log` and normalized metrics
to `build/quality-c.tsv`. Set `QUALITY_C_LOG` to choose another report path.
Do not redirect make's stdout to the same file that the checker owns.
The incremental wrapper uses private temporary reports and removes them after
returning diagnostics, preventing report collisions between worktrees.

## Incremental hooks and worktrees

The turn baseline and successful-check cache use content hashes. Untracked sources
are included; deletions and configuration changes invalidate the affected check.
A successful incremental hook is not a full-project acceptance result. The adapter
serializes checks within one checkout without waiting; busy and concurrent-edit
results explicitly require a later retry. Linked worktrees use separate locks,
baselines and caches keyed by their resolved paths.

In each worktree, provision ignored `build/quality-tools` dependencies with
`quality --root /path/to/worktree setup`, then register hooks with `install-codex`.
Tracked quality.json follows the branch; ignored tools and local hook registration
must be established separately. Tools are never borrowed from another checkout.

On 9 September, selected-file trials through the complete hook process took
0.60 seconds for `lib/notify.sh` and 1.24 seconds for
`src/fleet/task/task_control.c`. These trials simulated the changed-file baseline
without editing those sources and ran the actual native checkers. Header/config
changes still require broader work; the trial timings are not a project-wide bound.

## Shell pilot evaluation — 7 September 2026

Snapshot after excluding the explicit deployment trial: 901 events, 835 skipped,
56 passed checks, 9 failed checks, and 1 setup error. One Stop event blocked;
its subsequent unresolved failure was reported without another block. These are
invocation counts, not unique defects or independent tasks. Median checked-event
duration was 11.77 seconds, maximum 14.61 seconds, with 711 seconds summed across
checks (not measured user waiting time). Concurrent sessions can check the same
failure more than once.

A session Stop message confirmed ShellCheck SC2319 in test_workflow_plan.sh:
`$?` referred to a condition rather than the command under test. This is a useful
correctness finding. The remaining failures have not all been individually
attributed, and the setup error remains unclassified. Evidence supports extending
the pilot, but does not establish a defect-prevention rate or net time savings.

Historical pilot validation: analysis completed on the native build flags with the macOS SDK;
a temporary 16-branch probe emitted the configured complexity warning without
blocking, and malformed C returned a failure. Initial analysis before enabling
header diagnostics emitted 139 warnings; that is an untriaged baseline, not a
count of confirmed defects. Full shell-plus-C quality check passed with warnings.
Makefile changes are fingerprinted along with C sources to invalidate cached checks.
