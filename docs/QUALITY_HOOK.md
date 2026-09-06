# Local quality hook pilot

`quality.json` runs the existing `make lint` target after shell-source changes
and at turn completion through the agent-toolkit Codex adapter. It checks
ShellCheck 0.11.0 availability and GNU Make 3.81, and `make lint` runs ShellCheck
with Hydra's POSIX/style flags plus dash syntax checks. These tools must be on
PATH. Checks do not install tools. Run agent-toolkit's `tools/quality/bin/quality
--root /path/to/hydra doctor` to verify setup.

Coverage is shell sources in bin/hydra, lib, scripts, tests, install/uninstall,
and assets/demos. Native C is outside this pilot. The 500-line threshold is
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
