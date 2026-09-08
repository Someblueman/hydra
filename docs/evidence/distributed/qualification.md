# Distributed DAG qualification — 8 September 2026

The implemented scope is roadmap item 5: finite, explicitly placed workflows on
one original coordinator, with verified handoff, independent checks, bounded
repair and replay. Required repository and two-host acceptance checks passed.

## Two-host acceptance

Run `run_af948c1898a4093e91fe` used macOS and the selected Linux VPS. It fanned out
one producer on each host, validated each on the opposite host, and deliberately
failed the VPS candidate's first check. One accepted repair round created new
task identities. After both fresh checks passed, macOS assembled from the VPS
producer's collected Git commit; the VPS checked the combined candidate.

All ten executions had distinct task IDs. Three final reports passed exact
expected-content comparisons, subject and validator digests, and requirement
coverage. Replaying the recorded observations twice returned identical choices.
The same collected composition then passed the existing integration gate:
promotion required approval, moving the target after approval blocked promotion,
and a new assembly and bound approval promoted the checked bytes.

[Task identities, reports, repair evidence and integration commits](two-host-complete.json)
include the accepted plan digest and native helper hashes. The
[resolved graph](two-host-graph.tsv) and [scheduling journal](two-host-schedule.jsonl)
retain the replay inputs. Full local command records remain in
`/tmp/hydra-dag-qualified-two-host`.

All ten completed execution heads were removed through the public cleanup
interface. Replay remained byte-identical afterward; collected objects and run
records were retained.

The final admission guard reserves output bytes for all accepted repair rounds.
It rejected an envelope that fitted one round but not two, then reproduced the
same accepted digest for this two-host plan. Final result retrieval and replay
were revalidated with that helper; execution behavior was unchanged by this
additional admission check.

## Failure and recovery evidence

| Boundary | Acceptance evidence |
| --- | --- |
| Lost submit acknowledgement | Public workflow resume preserves the original package, key and receiver task ID; one acceptance |
| Concurrent coordinator / coordinator and observer SIGKILL | Second owner refused; recovered run preserves dispatch and receipt |
| Changed dispatch, key, placement or attempt counter | No replacement acceptance; corrupt identity requires recovery, unavailable original destination waits |
| Changed producer dispatch before Git handoff | Even with a recomputed file checksum, consumer submission is blocked by the producer receipt/key comparison |
| Dirty producer Git tree | Consumer stops before submission |
| FAIL / INCONCLUSIVE / stale subject / stale validator / missing coverage / bad artifact / changed harness / validator crash | Required join blocks composition; process success cannot substitute for evidence |
| Repair success and combined-candidate repair | New candidate and validator attempts; latest evidence gates delivery |
| Exhausted repair / unchanged rejected candidate | No further composition; unchanged bytes fail even if the new report says PASS |
| Validator crash with repair budget | No semantic repair is created from a process error |
| Partial repair reset | Filesystem write failure leaves a pending record; public resume finishes that same reset before dispatch |
| Corrupt or truncated scheduling journal | Read-only replay rejects it |
| Confirmed and unavailable cancellation | Confirmed stop cancels the run; unconfirmed stop retains waiting-remote and blocks dependents |

These cases are retained in `test_workflow_task.sh`, `test_workflow_plan_task.sh`
and their focused case files, plus the native plan and scheduling tests. The
attempt-counter probe demonstrated three acceptances before the guard and one
afterward. The producer-key probe demonstrated the formerly accepted consumer
and its rejection after the contextual receipt check.

The confirmed-cancellation fixture waits for the receiver's published plain head
before cancellation. An earlier full-suite run cancelled during incomplete
startup and correctly retained `waiting-remote`; treating that as confirmed
termination would weaken the contract.

## Repository checks

- `make lint`: passed.
- `make quality-c`: passed; 121 advisory complexity functions, no baseline regressions.
- `make test`: passed.
- `make BUILD_DIR=build/dag-qualified test-fleet`: passed.
- UBSan native plan and scheduler tests plus a public repaired Git-handoff run
  with an injected partial reset: passed.
- Final repair artifact-budget refusal and two-host compiled-digest/result/replay
  revalidation: passed.

No source or artifact was pushed or released. This qualification does not claim
coordinator failover, automatic reassignment, load balancing, identical worker
outputs, same-user OS isolation, or live AI-provider conformance. The two-host
work used committed executable fixtures through the public task interfaces.
