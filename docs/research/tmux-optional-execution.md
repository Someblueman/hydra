# Optional tmux for headless execution

Analysis initially updated 8 September 2026 against main commit `0319959`
(v2.2.1), then reconciled with the finite distributed DAG implementation.
Status: recommendation and implementation milestones; no runtime change or new
remote qualification is claimed by this document.

## Recommendation

Make tmux optional for headless local and remote work, and retain it for interactive
heads. Extend the existing detached task owner instead of replacing supervision or
adding another scheduler. The benefit is removing an unnecessary terminal lifecycle
from command/agent execution and making worker ownership easier to explain.
Removing tmux entirely would also require replacing useful attach, input, layout,
and transcript behavior; there is no demonstrated need for that larger change.

## What changed since the initial investigation

Main now includes local objective planning and the v2.2.1 correctness/trust fixes.
Fleet sources were reorganized into task, transport, support, agent, and plan
modules. The earlier flat source paths are obsolete. Local planning already lowers
into the existing workflow engine; it must be included in this migration rather
than treated as future work. Finite distributed planning/execution, resource
admission, validation joins, bounded repair, and scheduling replay are now
implemented on this branch; see the [qualification record](../evidence/distributed/qualification.md).
That controlled two-host qualification used tmux and executable fixtures. Execution
without tmux and additional live-provider qualification remain outstanding.
See [plan compilation](../PLAN_COMPILATION.md) and
[the roadmap](../ROADMAP.md#candidate-features).

The underlying tmux dependency remains:

| Responsibility | Current source and behavior | Required change |
| --- | --- | --- |
| Disconnected task ownership | [task_launch.c](../../src/fleet/task/task_launch.c) syncs launch intent, retains an owner lock, double-forks, calls `setsid`, and redirects stdio | Reuse; do not substitute terminal liveness for ownership |
| Command supervision | [process.c](../../src/fleet/support/process.c) manages process groups, deadlines, bounded capture, and cancellation | Reuse and qualify the changed execution path |
| Remote exec workspace | [task_execute.c](../../src/fleet/task/task_execute.c) calls `spawn task --no-agent` before `exec` | Create workspace/identity without a terminal |
| Receiver requirements | [task_accept.c](../../src/fleet/task/task_accept.c) and [bootstrap.c](../../src/fleet/transport/bootstrap.c) require tmux | Check capabilities needed by the actual operation |
| Local plan contract | [plan_schema.c](../../src/fleet/plan/plan_schema.c) supports local spawn/exec; [plan_graph.c](../../src/fleet/plan/plan_graph.c) requires exec to depend on its head's spawn | Explicit compatible extension and lowering for terminal-free workspaces |
| Durable head lifecycle | [state_v2.sh](../../lib/state_v2.sh), [lifecycle.sh](../../lib/lifecycle.sh), and [kill.sh](../../lib/kill.sh) assume terminal session fields or use tmux observation/teardown | Define terminal-independent state and update all affected readers/writers |

The existing remote owner already survives an ordinary SSH disconnect independently
of tmux. That does not guarantee survival of every host logout policy or a reboot.
Headless provider invocation currently still uses a head whose workspace was
created through terminal-backed spawn. Deleting dependency checks alone is therefore
insufficient. Cleanup and status must also stop treating missing terminals as dead
executions; see [maintenance.sh](../../lib/maintenance.sh).

Existing [remote acceptance](../REMOTE_TASK_ACCEPTANCE.md) records lost responses,
cancellation, and refusal to replay after owner loss. Those records qualify their
original paths, not execution on a machine without tmux.

## Intended boundaries

1. The DAG coordinator owns dependencies, assignment, result admission, and recovery
   decisions. Persist identity and exact inputs before dispatch.
2. The receiving-host task owner owns one execution attempt, command supervision,
   cancellation, logs, and the durable outcome. Keep the existing shell mutation
   authority and native execution implementation.
3. A workspace supplies source, inputs, trust, and provenance without requiring a
   terminal. Preserve repository-based execution initially.
4. A terminal is an optional interactive resource. Existing interactive spawn,
   attach, messaging, layouts, and transcript capture continue to use tmux.

A headless attempt should expose logs and structured observations through its
run/step/attempt identity. Log viewing cannot promise interactive stdin attachment.
Provider conversation IDs, process identity, terminal identity, and task identity
remain distinct. Do not build a PTY server merely to recreate tmux for headless work.

Cancellation and workspace destruction are separate operations. A stopped process
may leave useful or dirty outputs. Conversely, a surviving terminal or provider
conversation is not proof that an execution is active or successful.

## Compatibility and recovery

[Public contracts](../CONTRACTS.md) currently require tmux for core shell operation,
and durable state v2 requires session fields. Before T1 implementation, decide the
explicit representation of terminal-free execution and its schema/version impact.
Do not put fake session names in existing records or silently reinterpret an old
compiled plan's spawn operation. Update validation, lifecycle, result collection,
provenance, maintenance, and frontends with the writer change. Old interactive heads
retain their behavior. Changed durable formats need the migration/rollback required
by [release policy](../VERSIONING.md); incompatible removals require its deprecation
window and major-release treatment. Optional new capability need not remove a
published interface.

Keep one execution authority. Process-group signalling is not OS isolation and
cannot certify arbitrary descendants that escape the group. Owner death, uncertain
cancellation, or a lost acknowledgment must remain unresolved until reconciled;
none authorizes redispatch on another host. Admission must retain unresolved claims.
Tmux removal does not add safe external-effect retries, reboot recovery, automatic
coordinator failover, or provider authentication.

For an explicit requirement to run through logout/reboot, evaluate host-managed
supervision around the same task owner: systemd on Linux or launchd on macOS. Define
owner identity across boots/PID reuse and foreground service operation before adding
restart policy. Reconcile durable state before any launch; restarting the service
must not blindly restart the task. Stronger process containment and platform-specific
logout/reboot trials are separate acceptance work, not prerequisites for basic
terminal-free execution.

## Delivery and acceptance

[Roadmap milestones T1–T3](../ROADMAP.md#t-optional-tmux-for-headless-and-remote-execution)
sequence workspace/state separation, remote and compiled-plan execution, then
cross-host DAG qualification without tmux. Resource admission and finite distributed
execution already exist; extend their current paths and repeat the distributed
scenario without tmux rather than introducing a second execution authority.

The decisive first proof is a command and headless adapter run on a host without
tmux installed: submit, disconnect, reconnect, collect exact artifacts, and consume
an output in a dependent step. Keep provider sign-in requirements visible; fixture
success does not qualify a live provider. Follow with duplicate/lost-response,
owner-loss, cancellation, log-limit, and interactive regression checks. Two-host
fan-out and verified composition complete the distributed milestone. Performance
benefits remain unmeasured; dependency removal and clearer ownership are the current
reasons for the change.
