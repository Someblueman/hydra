# T2 terminal-free remote execution

This record covers the T2 implementation on `codex/t2-remote-headless`, based on
`0190bf938d82fa7d136369f96ed9061d6c0e2df6`. It is local qualification, not a release
or a live-provider campaign. T2 remains unchecked in the roadmap until its live
host and authenticated adapter requirement is qualified.

## Contract

Remote command tasks use the existing detached owner and T1 headless workspace.
Task submission requires Git and Hydra, with tmux 3.0+ checked only when requested.
`execution-headless` is an explicit receiver capability; new requests requiring it fail
against receivers that do not implement it. Existing-key reconciliation precedes
optional capability checks, preserving identical retries after capability changes.
Workflow spawn and schema 1 plan spawn accept `terminal_mode: headless`; omitted mode preserves interactive behavior
and existing compiled bytes. Adapter execution remains a separate exec step.
Terminal requirements are checked before workflow dispatch. Installation,
bootstrap, and doctor work without tmux; doctor still diagnoses interactive heads
that lack a usable terminal.

No task IDs, durable record schemas, compiled artifact versions, reservation
rules, cancellation semantics, result digests, or log bounds were replaced.

## Qualification

The existing acceptance suites run the real CLI, detached owners, receiver,
workspaces, result sealing, and collection. `tests/headless_path.sh` constructs a
PATH containing the host's executable tools except tmux. Remote tests substitute
only SSH transport and use the existing deterministic adapter fixture. They do
not authenticate to a provider or enroll a host.

| Check | Result |
| --- | --- |
| `make build-fleet build/test-plan`; `build/test-plan` | Passed, including both explicit modes, invalid values and null, and omitted mode |
| `build/test-fleet`, `build/test-task-package`, `build/test-workflow-data` | Passed |
| `HYDRA_TEST_HEADLESS=1 sh tests/test_workflow_plan.sh` | 91/91 passed: compilation, exact delivery, independent verification and runtime binding tamper rejection |
| `sh tests/test_headless_plan_adapter.sh` | Passed: omitted mode rejects before launch; explicit headless bound profile produces exact bytes consumed by an independent verifier |
| `sh tests/test_workflow_plan_task.sh` | Passed: schema 2 compiled tasks with explicit `execution-headless`, intermediate checks and final evidence join |
| `HYDRA_TEST_DAG_LOST_ACK=1 sh tests/test_workflow_task.sh` | Passed: controlled SSH lost response, reconciliation, verified collection, sealed input and dependent consumer |
| `sh tests/test_task_acceptance.sh` | Passed: detached remote command/workflow, fixture adapter session recall and sealed-output consumer after reconnect, outages, duplicate submits, lost starts, owner death, cancellation, bounded logs, reservations, exact collection and integration |
| `sh tests/test_fleet_install.sh` | 19/19 passed, including controlled pinned bootstrap and doctor with no tmux |
| `sh tests/test_headless_execution.sh` | 18/18 passed; no tmux invocation |
| `make lint` | Passed |
| `make test-quality-c quality-c` | Passed with pinned clang-tidy 22.1.8; existing advisory warnings remain visible |
| macOS UBSan `test-plan`, `test-fleet`, `test-task-package` | Passed with `-O1 -g -fsanitize=undefined -fno-omit-frame-pointer` |

A separate ignored source archive of the exact baseline was built for compatibility
qualification. Compiling the same schema 1 plan and source with the baseline and
T2 yielded byte-identical artifacts when mode was omitted. The actual baseline
receiver rejected a new `execution-headless` requirement before acceptance, then
returned the identical receipt for a key accepted by T2: optional capability
changes do not defeat existing-key reconciliation. The reproducible local probe
is `build/t2-compiled-parity.sh`; no worktree, branch or external host was created
for that baseline build.

The final remote suite also covers a lost trusted-submit response followed by
removing the advertised terminal dependency. Identical retry retains the task ID
and executes once; changed content with the same key returns `submission_conflict`;
a new key returns `capability_unavailable` without another acceptance.

The reductions in `plan_schema.c:recipe` (26 to 24) and
`server.c:f_handshake` (23 to 22) were ratcheted down in the reviewed complexity
baseline. No ceiling was raised.

## Environment limits

The interactive `sh tests/test_workflow_plan.sh` run returned 86/91, exactly
matching the coordinator's baseline. A bounded initial tmux probe succeeded,
but the negative and two mutation-guard runs failed to allocate PTYs. Their
spawn stderr says `create window failed: fork failed: Device not configured`.
Evidence is retained under
`/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.fwg4dIlMzX` in runs
`run_f7993635e4fd2a59699b`, `run_9c969ca73fe7f1477532`, and
`run_2f30fca06252895dd98f` (each `steps/spawn/attempt-1/stderr`). The same cases
passed without tmux. No unrelated terminal sessions were removed. Full interactive
qualification therefore remains constrained by the host's PTY capacity.

An optional Apple AddressSanitizer attempt stalled in sanitizer/dyld allocator
initialization before `main`, as sampled in `build/t2-acceptance/asan-startup.txt`,
and was stopped. Hydra's documented macOS sanitizer selection is UBSan; those
checks passed. This is not an ASan qualification claim.

## Remaining live qualification

Use a separately authorized receiving host with tmux uninstalled and an available
authenticated headless adapter. Submit a command and adapter workflow, disconnect,
reconnect, collect exact outputs, and consume them in a dependent step. Preserve
the duplicate-submit, lost-start-response, owner-death, cancellation, bounded-log,
and retained-reservation evidence. Controlled SSH and provider fixtures do not
prove live authentication, host provisioning, transport behavior, or provider
availability. No live provider quota was spent for this implementation.
