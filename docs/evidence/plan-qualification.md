# Local objective-planning qualification — 7 September 2026

This records source qualification of `workflow plan` slices 1–3 on
`feature/workflow-inputs-agent-adapters`, based on `0b25c59`. It is not a release,
push, deployment, or distributed execution qualification.

## Public path and independent delivery checks

An already authenticated OpenCode 1.18.27 provider authored both plans, read the
published schema and planner recipe, invoked `hydra workflow plan validate`, and
revised proposals from its diagnostics and observed failures. No model selection,
authentication storage, or provider permission was changed. The planner sessions
were `ses_f86d304bbffeIjALx95pan0i9A` (feature) and
`ses_f86cab75fffe5uY9VKchDRhNvW` (research). Plans and policies retained under
[`examples/planning`](../../examples/planning/README.md) are the actual authored
proposals, not hand-written replacements for the live qualification.

The operator-side agent reviewed the requirements, graph, scripts and policy,
compiled each accepted proposal through the public CLI, saved its readable
preview and accepted its exact digest under the existing task authorization.
Execution used the ordinary local workflow scheduler, separate disposable Git
repositories and fresh worker/check heads. The compiler made no model calls.
The source roots and declared inputs were bound; tool installations and external
provider behavior were not frozen. No implementation worktree was created.

A workflow's recorded success was not treated as delivery acceptance. The final
feature archive was extracted and built again outside its run heads; its source
and the independent test were reviewed. The research report was read in full,
and a separate Python simulation recomputed schedules and metrics from the CSV,
independently of the repository's awk measurement script and provider assessment.

## Iterations that did not qualify

| Scenario/run | Observed result | Consequence |
| --- | --- | --- |
| Feature `run_d9ec03c31434198da233` | Composition rejected a missing terminal newline under strict C compilation. | Composition now emits a complete final source line. |
| Feature `run_a5a384112ce84512b7cd` | Worker timed out while debugging in scratch directories. | Worker scope was narrowed to returning source; independent execution remains in the verifier. |
| Feature `run_657b500f317d914f31fd` | Runtime and original tests passed, but parent source review found null input failed to clear a nonempty output buffer. | **Not qualified.** A sentinel assertion reproduced the defect with exit 134; the test was strengthened without changing its criterion. |
| Feature `run_53b7b24b251c94186627` | Worker omitted the required header include; strict composition failed. | Composition now supplies the fixed public header include; it does not repair worker function logic. |
| Research `run_111118033daa96b0d804` | Report composition succeeded, but assessor searched outside its workspace and returned progress text after a permission denial. | Typed report sealing failed. Prompt now embeds all report bytes and explicitly limits local reads. No permissions were relaxed. |
| Research `run_715fba691cc81a520cc8` | Independent assessor reproduced all results, but emitted prose before its JSON verdict. | Typed sealing failed. Prompt now requires one text emission containing only the verdict object. |
| Research `run_f956b542bbe351a2089b` | Analyst reached its 240-second limit after reading the permitted files. | Fresh plan allocates 420 seconds to analysis and 300 to assessment within the same 900-second envelope. |

Each revision was a fresh static plan with fresh heads and a new accepted digest.
There were no automatic retries, graph expansion, criteria reductions, or silent
reuse of a failed run's output as the accepted deliverable.

## Compiler and runtime boundaries

The CLI regression exercises deterministic compilation, schema discovery,
coverage diagnostics, unknown fields, missing criteria, duplicate IDs/members,
unsupported operations/hosts, unauthorized tools, budgets, unsafe artifact paths,
stale source/input rejection, exact digest acceptance, completed artifact
retrieval, corrupted sealed bytes, retained-head admission, negative verdicts
after every child step succeeds, and live graph/resource-limit projection mutations before
verification dispatch. Native checks exercise cycles, ordered/unordered write
conflicts, policy scope, minimum free-space floors, canonical member ordering,
malformed/truncated JSON, escaped duplicate keys, embedded NULs and size bounds.

The write/effect/tool declarations are reviewable scope, not OS isolation. A
check report ties a verdict and requirement IDs to exact final bytes; it does not
prove that the rubric is adequate or that an agent's account is true. The missed
null-input case above is direct evidence of that distinction. The implementation
preserves workflow schema 1 and shell-only execution. Remote placement, load
balancing, dynamic replanning, automatic repair and failover remain unimplemented.

## Qualified feature

Run `run_7cc9dd6ff5f5b6fbbe1f` used source commit
`051a7785fdc0f293dd73555629f45a4c4434a183` and accepted plan digest
`52fd010bf60601d861aaa5078b8c1cb8204b960845a34cce5cab94681f947b2b`.
Its [sealed source archive](planning/catalog.tar) is 14,848 bytes, SHA-256
`8e29995f69387cf9959b23a5356d7b47b93100af103aa7aba0f6fd872609923a`.
It contains `main.c`, `slug.c`, `slug.h` and `Makefile`; the repeated guarded
header include in the generated source is harmless and retained verbatim.

The separate workflow verifier and parent recheck both rebuilt the archive with
strict C99 warnings and exercised normalization, buffer bounds, null/zero
arguments, guard bytes and a 10,000-byte input. The parent check additionally
compiled the fixed test with UBSan. CLI assertions checked two-input output,
exact size limit, oversized output, invalid limit and missing arguments. Parent
source review confirmed clearing a valid output before the null-input error and
bounds checks before every output write. No performance claim is made.

[Public result](planning/feature-result.json),
[compiled artifact](planning/feature-compiled.json),
[preview](planning/feature-preview.txt),
[parent build/CLI log](planning/feature-parent-check.log) and
[source/provider bindings](planning/feature-evidence.json) retain the evidence.
The compiled artifact is a historical binding to its disposable source root;
use the example instructions to compile against a new root for another run.

## Qualified research

Run `run_48f6845e2b1cbbc6c165` used source commit
`1ec9bfc1dac62af8d197aefd35f73e679683de52` and accepted plan digest
`462a1cd8c8fd816eb36f79284dabdaca6bbdb1746297ff443a6d93a1a7941f64`.
The completed [report](planning/research-report.md) is 8,243 bytes, SHA-256
`028f679ac12eaf8193c66e6386624c21e8fceb2ec306135e58ea96482416a6c1`.
Its independent assessor emitted a valid positive object report with that exact
subject hash and all six required IDs.

| Policy | Mean wait | Mean turnaround | Nearest-rank p95 turnaround | Maximum wait | Worst-wait job |
| --- | ---: | ---: | ---: | ---: | --- |
| FCFS | 11.583333 | 14.166667 | 19 | 18 | L |
| SJF | 4.000000 | 6.583333 | 31 | 22 | A |

Neither satisfies both p95 turnaround ≤ 22 and maximum wait ≤ 16. This negative
policy-selection finding is a successful answer to the research objective;
the report explicitly preserves both limits, explains the mean/tail disagreement,
distinguishes observed deferral from starvation, retains the complete schedule,
and states the limits of the synthetic, exact-duration, single-server study.
Parent review covered the complete report, not just its summary or provider claim.

[Public result and assessment](planning/research-result.json),
[compiled artifact](planning/research-compiled.json),
[preview](planning/research-preview.txt),
[independent recomputation](planning/research-recomputed.json),
[parent assessment](planning/research-parent-check.txt) and
[source/provider bindings](planning/research-evidence.json) retain the evidence.
Repeated compilation of both final live plans produced byte-identical artifacts.

## Regression and final state

`make test-all` exited 0 across shell, native fleet/core, TUI, PTY, parity,
installation and onboarding acceptance. A final review then added comparison of
the runtime's copied resource limits against the compiled projection. The focused
planning CLI suite passed **56/56** assertions on that change, and
`make sanitize-fleet` subsequently exited 0 on the final runtime code, including
those same assertions and the full fleet acceptance suite. On this macOS host the
repository sanitizer target uses **UBSan**, not ASan. No ASan result is claimed.

[Check records](planning/checks.json) include command outcomes and local log hashes.
The generated public schema parsed as JSON; a separate Python JSON-Schema library
was unavailable, so no external schema-validator result is claimed. Native
validation exercised both actual plans. Bash planning completions and generated
zsh syntax were checked. The generated JSON-schema string table is longer than
500 lines because it embeds the published schema; the handwritten C modules are
all below that review threshold.

Temporary qualification heads were cleaned after retaining their source
candidates, provider state and sealed run evidence in the disposable qualification
area. The final deliverables and selected evidence are also retained here.
The implementation checkout's `.gmcs/` and `output/` remain untouched; no push,
release or deployment was performed.
