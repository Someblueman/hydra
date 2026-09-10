# Native CLI and evidence tests

These C99 programs replace the former interpreter-based CLI test drivers. They
exercise the real Hydra binaries and public commands, using JSON-C (already a
fleet build dependency) for test data. `scripts/native-tests.mk` builds them under
`$(BUILD_DIR)/native-tests`; existing Make targets invoke them. Run Make from the
repository root. Assertions must remain enabled.

`support.c` owns subprocess capture, disposable Git fixtures, JSON assertions and
bounded child waits. `outcome_support.c` copies the actual `plan-example` binary
and `payload.sh` into each example source before its first Git commit. The tests
specify expected outcomes independently; they do not call the implementation's
analyzer or checker to derive expected answers.

## Coverage migration

Each row identifies a retired driver and its replacement. Helper source files
keep related fixture setup separate from the cases, without a general test
framework.

| Retired driver | Native entry point | Preserved coverage |
| --- | --- | --- |
| `statistics_evidence.py` | `statistics_evidence.c` | Independently derive queue, terminal, verified and recovery metrics from durable records; reconcile with native aggregate export when built. |
| `test_statistics_export.py` | `test_statistics_export.c` | JSON/TSV/text parity, v2/v3 and mixed records, partial and malformed input, oversize rejection, public wrapper. |
| `test_task_announce.py` | `test_task_announce.c` | Options, cursor and retained history, historical steps, foreign runs, ordering, reset, malformed/oversize events and bounded batches. |
| `test_workflow_task_metrics.py` | `test_workflow_task_metrics.c` | Seven durable-history groups, exact zero versus unknown, partial/overflow/duplicate observations and JSON/TSV parity. |
| `test_plan_inspection.py` | `test_plan_inspection.c` | Six public inspection groups: explanations, comparison, scope, costs, missing/invalid estimates and tampering; no execution. |
| `test_plan_reuse_invalidation.py` | `test_plan_reuse_invalidation.c` | Accepted fixture baseline, changed PATH, nine policy controls and sixteen independently rehashed corruption controls. |
| `test_retention.py` | `test_retention.c` | Fourteen groups including 512/513 bounds, actual owner locks, transitive pins, lost acknowledgments, corruption and quotas. |
| `test_retention_accepted.py` | `test_retention_accepted.c` | Six acceptance bindings, duplicate submissions, pinned preservation, expired evidence, no fresh acceptance, workspace-byte preservation. |
| `workflow_contract_cases.py` | `workflow_contract_cases.c` | Twenty-one data/compiler/acceptance groups and six supervised runtime handoffs. `contracts_runtime.c` verifies rejected consumers were never submitted and inspects typed seal/preparation failures. |
| `discovery/ssh_fixture.py` | `discovery_ssh_fixture.c` | Controlled SSH configuration and probe responses, error logging and cancellation-child evidence; no operational hosts. |
| `discovery/test_discovery.py` | `test_discovery.c` | Eleven groups, real `ssh -G`, strict jump configuration, mixed typed results, progress corruption/locking/retry and 100-host batching/cancellation. |
| `test_enrollment_receiver.py` | `test_enrollment_receiver.c` | Six real receiver groups including duplicate/concurrent mutation, held child, owner death/unknown state and malformed records. |
| `test_enrollment.py` | `test_enrollment.c` | Eleven base groups, interrupted 50-host batches and five package groups. Verifies policy states, stable package identity, no repeated installation/init and no destructive prefix substitution. |
| `test_enrollment_ssh.py` | `test_enrollment_ssh.c` | Real loopback `sshd`, ephemeral keys, pinned install/init/duplicate; lost master refuses fallback and resume succeeds. |
| `test_plan_patterns.py` | `test_plan_patterns.c` | Matched public compilation/explanation/comparison, missing-output rejection and seven independently specified held-out artifacts. |
| `test_plan_manifest.py` | `test_plan_manifest.c` | Seven groups covering empty/conditional/collision/maximum maps, real sh/dash payloads, missing joins, closed schema/types/bounds, raw duplicates, preserved prior output and forged reports. |
| `test_plan_staged.py` | `test_plan_staged.c` | Six gate groups using a fixed public-result double: bounded maps, sh/dash joins, finding corruption, accepted hash, stale source and forged stage-two members. Includes null-subject evidence regression. This test makes no public acceptance claim. |
| `test_plan_staged_public.py` | `test_plan_staged_public.c` | Actual two-stage public acceptance; sealed finding hash, changed-local-finding and stale-source negatives, final member equality. Supports normal, empty, all-skipped, collision and maximum cases. |
| `test_research_outcome.py` | `test_research_outcome.c` | Five groups covering schema-3 evidence, unsupported recommendation, question/provenance/location/explanation binding, malformed subject and typed-invalid jobs. |
| `test_performance_outcome.py` | `test_performance_outcome.c` | Five synthetic control groups: positive/no-improvement, exact measurement domains, source/protocol/warmup binding, report tampering and coordinated-window gate. Includes malformed/quoted/reordered CSV, int64 overflow bounds and a controlled NUL-output run that preserves all malformed bytes while rejecting the measurement. |

## Running and evidence

Typical focused commands are `make test-plan-outcomes test-plan-staged`,
`make test-plan-inspection test-task-announce test-workflow-metrics`,
`make test-discovery test-enrollment`, and `make test-enrollment-ssh`.
`make test-workflow-contracts` runs the supervised contract cases. These tests use
throwaway repositories; operational project heads and hosts are not test targets.

The public staged runner retains its fixture, exact command log, stdout/stderr,
accepted run IDs, plan hashes and copied executable hashes:

```sh
make build/native-tests/test-plan-staged-public build-plan-example build-fleet build-plan-precompile
build/native-tests/test-plan-staged-public --case maximum --output build/staged-maximum.json
```

Other staged cases use the same `--case` option. Public runtime tests should be
coordinated with other Hydra qualification runs. Performance tests use synthetic
timings and a deliberately invalid-output measurement run; they do not establish
a speedup.

Accepted-fixture reuse and retention drivers require a real retained accepted
fixture. They copy it before mutation; their results distinguish accepted,
preserved, expired and rejected evidence. Discovery/enrollment test doubles are
native executables. The loopback SSH suite requires an available `sshd`,
`ssh-keygen` and permission to bind a local socket. Missing executables are
reported as a skip, not a successful real-SSH qualification; socket or server
setup failures fail the test.
