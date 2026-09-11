# Recovery visibility qualification

The integrated V4 fixture uses two real receiver homes, Git repositories, task
owners and the native fleet TUI. A local SSH shim routes the public transport to
the actual receivers and can make one receiver unavailable. This qualifies the
local two-receiver protocol path; it is not an operational-host or network soak test.

The fixture submits work to both receivers, loses observation of host A, kills
exactly A's recorded execution owner, and confirms `outcome_unknown` without
replacement execution. Host B completes independently. It then starts a gated
workflow on B, selects that original task in a real TUI, closes the observing
client while execution continues, and restarts the TUI while A is unavailable.
The selected B task visibly progresses from running to succeeded.

The test recovers the same task, run and attempt, traverses all twelve events with
paired cursor/byte-offset/stream identity, recovers the 351-byte owner log from a
saved 64-byte prefix with a 287-byte resumed chunk,
checks both work-output messages and verifies the exact `workflow-result` artifact.
Acceptance-record counts remain one on A and two on B. It also sends every saved
workflow event page through the public ASCII announcer, including events from
steps other than the current step.

| Build | Workflow run | Retained fixture |
| --- | --- | --- |
| Default | `run_1a402130fea27e2d5224` | `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/hydra-v4-real-d_3rqa_2` |
| UBSan, fleet/core/TUI | `run_829556b041b9a7965e9d` | `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/hydra-v4-real-wj11hu6x` |

Each fixture retains `commands.jsonl`, `summary.json`, `observer-after.txt`, the
result bundle, saved observation pages and ASCII announcements. Both executions
passed and fixture cleanup confirmed that their owned processes were quiescent.
The result bundle remains available for independent `fleet task inspect-result`.

Final test source: `1488df3`. It waits for the receiver result to finish sealing
before downloading; process success alone is not result availability.

Reproduce with `make test-fleet-controls test-fleet-recovery`. Set
`HYDRA_TEST_KEEP_FIXTURE=1` to preserve the V4 fixture and its raw evidence. The control fixture
separately covers exact approval/rejection/resume bindings, stale modal targets,
lost cancellation acknowledgement, receiver cancellation stages and preservation
of active/dirty work. The full public task suite covers receiver idempotence,
cancellation and result validation; stale dependent-result checks remain a
separate workflow boundary rather than an inference from the TUI screen.
