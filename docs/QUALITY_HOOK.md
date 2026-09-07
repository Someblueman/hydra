# Local quality hook pilot

`quality.json` runs the existing `make lint` target after shell-source changes
and at turn completion through the agent-toolkit Codex adapter. It checks
ShellCheck 0.11.0 availability and GNU Make 3.81, and `make lint` runs ShellCheck
with Hydra's POSIX/style flags plus dash syntax checks. These tools must be on
PATH. Checks do not install tools. Run agent-toolkit's `tools/quality/bin/quality
--root /path/to/hydra doctor` to verify setup.

Shell coverage is sources in bin/hydra, lib, scripts, tests, install/uninstall,
and assets/demos. C sources, headers, and included fragments in src and tests/c are included in the Stop-stage C pilot. The 500-line threshold is
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
or `uv venv build/quality-tools` followed by
`uv pip install --python build/quality-tools/bin/python clang-tidy==22.1.8`.
Check-time installation is disabled. C runs only at Stop; shell remains fast-stage.

C cognitive complexity is now an incremental regression gate. The reviewed
`docs/quality/cognitive-complexity.tsv` records ceilings by relative source path
and function name. A function absent from the table has a ceiling of 15. Higher
scores fail `make quality-c`; line-number changes do not. Existing analyzer
warnings remain visible and require review, while compiler/tool failures block.
The checker version, enabled checks, and threshold remain unchanged. The original
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
