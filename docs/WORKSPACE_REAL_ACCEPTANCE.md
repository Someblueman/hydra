# Real native workspace acceptance, 8 September 2026

The local A/B/C/D workspace completed a real two-Codex production-report task:
conversation, agent-authored plan, meaningful revision, exact approval, a failed
independent check, an explicit recovery decision, repair and verified delivery.
This qualifies the tested local workflow and terminal subset. It is not a release,
a remote-agent qualification or a claim of universal terminal compatibility.

This is the historical local exercise before distributed schema-2 plans and the
richer lifecycle statistics were integrated. Schema and measurement limitations
below describe that exercise; current statistics are defined in [STATISTICS.md](STATISTICS.md).

## Actors and execution boundary

Two existing Hydra heads, `planner` and `reviewer`, ran Codex CLI 0.153.3
(`gpt-6-astra`, low reasoning) in separate tmux sessions on a fixture-only socket.
The planner owned the draft/preflight and later the requested reporter repair;
the reviewer independently authored the checker without reading the producer.
Both worked in the disposable fixture source directory via explicit `-C`.
The operator interacted through real PTY input to the compiled native workspace.
No `tmux send-keys` or synthesized model answers were used for the journey.

The fixture was `/tmp/hydra-real-workspace-1ww4v6pp`. Its native Codex login had
been refreshed by the user. A qualification-owned app server loaded that login;
clients used `--remote unix://<fixture>/codex.sock --sandbox workspace-write
--ask-for-approval on-request --no-alt-screen`. No bypass flag, new API credential,
shared-server restart or global configuration change was used.

The task used three explicitly authorized stages. Compiled plans support spawn
and exec, so the recovery input request used the existing schema-1 approval-wait
workflow as a separate stage. This preserved the engine rather than adding a
new request kind or implying one compiled run can resume after final failure.

| Stage | Recorded run | Outcome |
| --- | --- | --- |
| Revised, seeded-negative plan | `run_0e6123d7d1ce8fff341f` | Failed final verification; all four execution steps exited successfully, but the independent check rejected staging contributions. |
| Explicit recovery decision | `run_d14b90449897630dad1e` | Waited for approval. Y recorded approval; it remained waiting with no confirmation file. Explicit R resumed it, producing exactly one confirmation. |
| Repaired, newly approved plan | `run_ff335619a40de2f45f46` | Succeeded and returned freshly verified sealed artifacts. Each step had one attempt. |

The initial three-node plan digest was
`79b901b7220307278477f721728daa27e477f6a64607639dc72f6d72a1fffb62`.
The requested revision inserted preflight before compose and produced
`9711ca625cb41f9064c0a27a7fc69381c522fc4460426c0c13b75465f4ffaaf5`.
Entering the earlier digest was refused with zero run directories. Entering the
revised digest admitted the negative run. The repaired revision was separately
validated and approved as
`4fae7926672b2410c7f61e8db8e37e9e154c589ada33cdf497350263839e4109`.

## Observed acceptance

| Requirement | Actual observation |
| --- | --- |
| Conversation and revision | Planner asked for output ordering; operator answered alphabetical. Planner authored the external draft, then added the requested preflight dependency. B showed the changed graph and complete validation/approval preview. |
| Preserved context | Selected compose node, preview scroll, unsent planner input and both agent sessions survived A/B/C/D, 40x10/80x24/140x40 resize, client close/reopen and return to planning. Original planner/reviewer tmux pane PIDs remained 28986/30041. |
| Two agents | Separate planner and reviewer panes remained interactive and independently scrollable. The reviewer inspected the final public result and recomputed the sealed CSV hash. |
| Actual permission request | Codex's native request was displayed at three sizes. Its expanded command pager was scrolled at 40 columns. A single approval created the bounded fixture marker, which had been absent beforehand. |
| Interruption | Escape stopped the model turn; it did not establish tool termination. Explicit `/stop` then stopped a 120-second background probe. Its start marker existed and its end marker remained absent beyond the deadline. |
| Failure and recovery | C showed the failed check and dependent/status evidence; R refused terminal replay. A separate recorded request required exact request-ID approval and an explicit resume. The planner repaired only the reporter's production filter after authorization, preserving the independent checks. |
| Detach during execution | The recovery stage continued while an attachment client was closed/reopened. Agent PIDs and unsent recovery input survived; exactly one confirmation was written. |
| Verified result | Public result retrieval, sealed bytes, independent aggregation and the subject hash all agreed. A success label alone was not used as verification. |
| No duplicate execution | Exactly the three authorized stage runs existed, with one attempt per step and one confirmation. The engine's duplicate owner claim is covered by the public CLI/PTy test; the observed redundant E dialog was fixed and now has a frontend regression assertion. |

The final report is [retained as CSV](acceptance/workspace-20260908/report.csv):

```csv
service,requests,errors,error_rate_pct
auth,800,8,1.00
billing,1500,4,0.27
TOTAL,2300,12,0.52
```

Its 94 bytes hash to
`3198ee2db843848077545dfda13cac114fe4e04ad58f789c51c6e98672c55c70`.
The [independent check](acceptance/workspace-20260908/check.json) binds that hash.
The input contained production billing 1000/3 and 500/1, production auth 800/8,
and staging 9999/999; the initial seeded reporter incorrectly included staging.

## Terminal evidence and visual inspection

[Retained cell captures](acceptance/workspace-20260908/) come from the independent
Python observer of actual PTY bytes; retained text trims trailing blank cells only.
They are terminal evidence, not mockups or
frames rendered by the C parser. Planning, running, waiting and failed states
were captured and interacted with at 40x10, 80x24 and 140x40. Exact approval,
permission paging and final verification were also inspected. The full local
interactive log and HTML reconstructions remain under `build/real-workspace-evidence`.

Browser inspection covered wide two-agent conversation, narrow exact approval,
failed-check output, and the final colored two-agent and verified-result captures.
Focus, input destination, graph/evidence boundaries and status remained legible.
Narrow layouts intentionally expose one focused pane; long content is scrollable,
and long navigation labels can be inspected by selecting their evidence.
Monochrome captures were taken with inherited NO_COLOR; final dark-theme captures
were separately taken without it. Automated three-size control captures verify
the final compact waiting header, in addition to the earlier real-agent journey.

Observed friction fixed before final qualification: stale action notices after
changing runs; clipped waiting-state evidence headers at 40 columns; redundant
approval dialogs for a submitted digest; and launch-owner polling that could
replace known run evidence with an unknown-outcome notice after completion. A late workflow snapshot still selects the admitted run once
after the finished receipt, without further owner polling or overriding a later
user selection; a focused UBSan probe checked that ordering.
The original redundant-dialog capture is labeled `pre-fix-duplicate-opened-dialog`
in local evidence; it is not evidence of frontend refusal.

## Reproduction and qualification

Use a disposable initialized Git repository, isolated HYDRA_HOME and a tmux socket
with `-f /dev/null`. Create two no-agent Hydra heads, attach with `a`, and launch
real Codex clients through their panes. Supply a small CSV with the production and
staging rows above, a reporter with the seeded missing production filter, and the
public plan schema/policy. Ask one actor to author the draft and another to write
an independent hash-bound check. Follow the sequence above with P/V/B/E, then C
and the existing separate approval-wait workflow. Preserve the negative run and
use a newly compiled/approved plan for the repair; never edit the sealed artifacts.
Use the repository's existing PTY observer to record each size and assert effects.
Model responses and fresh run IDs are nondeterministic; the CSV/check invariant is
stable. Repeating the real model exercise requires explicit execution authorization.

Final-code qualification passed on 8 September 2026:

| Check | Result and local log |
| --- | --- |
| Full repository acceptance | `make -j4 test-all`, exit 0; `build/workspace-final-acceptance.log`. Includes lint, shell, fleet, native C, UI/PTY, statistics, parity, install and onboarding. |
| Final planning and intervention UBSan | `make sanitize-plan-workspace`, exit 0; `build/workspace-final-plan-ubsan.log`. |
| Attached clients and standalone UBSan | `make sanitize-attached sanitize-workspace`, passed in `build/workspace-complete-ubsan.log`; their source was unchanged by the final launch-observation correction. |
| Linux standalone | Ordinary and ASan/UBSan component plus PTY suites, exit 0; `build/termviz-linux-final.log`. |
| Export with enclosing build override | `make BUILD_DIR=build/final-code test-termviz-export`, exit 0; `build/workspace-export-override.log`. |
| Evidence integrity | All 29 retained evidence-file hashes and document links checked; exact report bytes and subject hash independently reconciled. |

The existing non-PTY shell test can skip saving terminal state when it has no
controlling terminal; the dedicated real PTY tests independently checked exact
termios restoration. Size-threshold advisories are not a warning-free code claim.

The current deterministic public boundary tests are the corresponding native
C drivers under `tests/termviz`; see their [coverage mapping](../tests/termviz/README.md).
The full repository acceptance command is `make test-all`; targeted local UBSan
commands are `make sanitize-plan-workspace sanitize-attached sanitize-workspace`.

Standalone Linux extraction passed ordinary and ASan/UBSan component plus real
shell/PTY suites in Debian Bookworm aarch64, GCC 12.2.0, GNU Make 4.3, Python
3.12.14, LinuxKit 6.12.67. The image was official `python:3.12-slim-bookworm`,
digest `sha256:782412e85d0f0984994c290652577d4018aff08145c85b262bb63dc0c7522254`.
Install gcc, libc6-dev, make and procps, export with `scripts/package-termviz.sh`,
then run `make all test test-pty`; repeat after `make clean` with
`CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'`.
The Linux probe found and verified the fix for an ordinary background job left
alive by process-group-only cleanup. Linux cleanup now signals the owned session
through /proc while retaining its leader PID until cleanup finishes.

macOS qualifies integrated Hydra and standalone behavior; Linux qualifies the
standalone extraction only. Other platforms, additional agent CLIs, full grapheme
shaping/reflow and unrestricted terminal protocols remain unqualified. Statistics
retain unavailable queue delay, total execution time, time-to-verification,
recovery history, CPU/memory and provider-cost measurements when their required
records are absent. Remote cohorts remain conditional on the fleet protocol.
No telemetry, remote execution scope, dependency or publication was added.

A supplementary custom-BUILD_DIR acceptance invocation exposed an export-test
path leak: the extracted Makefile built in build/ while its observer inherited
Hydra's custom directory. The export test now explicitly supplies its own build/
path; the same custom-directory export check subsequently passed. This failure
was in qualification setup, separate from the Linux job-cleanup defect.

The captured results above retain their original observer and tool versions.
Current regression coverage uses native C PTY drivers; the
[port mapping](../tests/termviz/README.md) lists the preserved scenarios.
