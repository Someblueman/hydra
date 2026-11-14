# T2/T3 qualification: execution without tmux

This is the reproducible, automated qualification for roadmap items **T2**
(remote execution and planning without tmux) and **T3** (distributed
qualification and operator access). It runs in a container and in CI with
tmux hidden from PATH on the execution side while the coordinator keeps its
own PATH (tmux may be installed there). It is local qualification over
loopback transports; qualification on a real, separately authenticated remote
host without tmux remains an operator step (see the last section).

## Harness

`tests/headless_path.sh` provides three sourced helpers:

| Helper | Effect |
| --- | --- |
| `headless_path_build DIR` | Links every executable on PATH except `tmux` into `DIR`; the caller's PATH is unchanged |
| `headless_path DIR` | Existing behaviour: the current process becomes tmux-less (`PATH=DIR`) |
| `headless_receiver_path DIR LOG` | `DIR` as above plus a `tmux` stand-in: `tmux -V` fails quietly (recorded in `LOG.probes`, as a dependency probe on a host without tmux would), any other invocation is refused loudly (exit 99) and appended to `LOG` |

Every receiver-side command in the T2/T3 cases runs with `PATH=DIR`: the
receiving host's `hydra init`, agent import, `hydra fleet serve`, the detached
task owner, workspaces, headless heads, exec and workflow steps. The cases
fail if `LOG` is non-empty at the end and require `LOG.probes` to show that
dependency checks ran and reported absence.

Two transports stand in for the SSH boundary:

- **Controlled** (default): the same fault-injecting loopback `ssh` the DAG
  fleet cases use. It runs the remote command with the receiver PATH and can
  drop acknowledgments (`lose-ack`), go offline, record/replay a sealed result
  (`record-result`, `stale-result`) or corrupt a result digest (`bad-result`).
- **Loopback OpenSSH** (`HYDRA_TEST_T3_TRANSPORT=ssh`): two private `sshd`
  daemons on distinct loopback ports with ephemeral ed25519 keys, explicit
  client configuration (`UserKnownHostsFile`, `IdentitiesOnly`, batch mode,
  strict host-key checking) and a `ForceCommand` that runs every request with
  the receiver PATH. Nothing touches `~/.ssh`. Without `/usr/sbin/sshd`,
  `ssh` and `ssh-keygen` the case prints `SKIP T3 distributed SSH: ...` and
  exits 0, exactly like `test-remote-setup-ssh`.

## Commands

```sh
export LANG=C.UTF-8 LC_ALL=C.UTF-8
make test-t2-headless            # tests/test_t2_headless.sh (controlled transport)
make test-t3-distributed         # tests/test_t3_distributed.sh, controlled then loopback SSH
make test-t3-distributed-ssh     # loopback SSH variant only; SKIP without sshd
make test-fleet                  # includes fleet-case-t2-headless and fleet-case-t3-distributed
make test-one T=t2_headless      # one case with the full log printed
HYDRA_TEST_T3_TRANSPORT=ssh make test-one T=t3_distributed
HYDRA_TEST_KEEP_FIXTURE=1 ...    # keep the fixture directory as evidence
```

Each case prints its final line only when every assertion held:

- `T2 headless: command and adapter tasks, reconnect, exact consumption, local compiled plan, duplicate submission, lost start, owner death, cancellation and bounded logs passed with tmux absent on the execution host`
- `T3 distributed: two tmux-less hosts, fan-out, cross-host validation, join, composition, final check, lost acknowledgment, coordinator restart and stale results passed`
- `T3 distributed over loopback SSH passed: two tmux-less hosts, fan-out, cross-host validation, join, composition, final check, coordinator restart`

CI (`.github/workflows/ci.yml`, job `fleet`): the controlled cases run in
every fleet acceptance shard on Ubuntu and macOS through `make test-fleet`;
the Ubuntu odd shard additionally installs `openssh-server` and runs
`make test-t2-headless test-t3-distributed`, requiring the loopback SSH pass
line, in the same step layout as `test-remote-setup-ssh`.

## T2 acceptance (`tests/test_t2_headless.sh`)

The coordinator keeps its PATH; the receiving host (`build`, alias over the
controlled transport, its own `HYDRA_HOME` and mapped project) has no tmux.

| Acceptance bullet | Case | Evidence |
| --- | --- | --- |
| Admission, bootstrap, doctor, installation and capability negotiation require tmux only for terminal operations | driver | Receiver handshake advertises `execution-headless` and no `tmux`; `hydra fleet list --json` reaches the host; receiver `doctor` reports "headless execution does not require tmux"; receiver `hydra spawn` (interactive) fails before any terminal operation; `tests/test_fleet_install.sh` keeps covering bootstrap and install without tmux |
| Unsupported terminal capabilities fail before launch with a typed error | driver, flow cases | `fleet task submit` of a recipe requiring `tmux` returns `capability_unavailable` with no acceptance record; a `hydra workflow run` task step with that recipe records `capability_unavailable` in `attempt-1/remote-response.json`, the step and run are `failed`, no receipt or dependent attempt exists; a compiled plan rejects the recipe at compilation with `invalid_task_policy` and writes no artifact |
| Submit a command task; disconnect, reconnect; collect exact outputs | `t2_headless_task_cases.sh` | Six concurrent submissions with one key converge on one acceptance with identical receipts; the start acknowledgment is lost and `fleet task status` reconciles to `succeeded` with exactly one execution; the execution head's durable `terminal-mode` is `headless` with no session; the sealed result is collected on the coordinator (`fleet task collect`) and compared byte for byte; a file edited after sealing does not change the snapshot |
| Submit an available headless adapter; disconnect, reconnect; consume in a dependent step | `t2_headless_adapter_cases.sh` | The deterministic session fixture (`tests/fixtures/agents/echo-worker.sh`, imported on the receiver with tmux absent) runs inside a workflow task; `offline` is returned while disconnected; after reconnecting the run is `succeeded`, the recalled artifact equals the first answer, the dependent gate consumed it, step/attempt logs are readable without attaching, and the collected artifact is exact. This is the committed fixture, not a live provider |
| Consume exact outputs in a dependent step through the public workflow | `t2_headless_flow_cases.sh` | `hydra workflow run` producer → verified collection → sealed input → consumer on the tmux-less host; `workflow status --json` shows both steps succeeded with one attempt; `fleet task logs --source work` reads the producer's bounded worker stream |
| Run the corresponding local compiled plan through public interfaces | `t2_headless_flow_cases.sh` | `workflow plan compile`, `show`, `run --accept`, `result` and `status --json` run entirely under the tmux-less PATH with `host: local`; four accepted tasks, all heads headless, composition bytes exact |
| Duplicate submission, lost start response, owner death, cancellation, bounded logs | task and control cases | Owner SIGKILL leaves `outcome_unknown` / `owner_unavailable`; `start` and `resume` are refused with `outcome_unknown`; logs are bounded by byte range, `log_bytes` caps the worker stream (`limit_reached`), out-of-range requests are rejected; a lost cancel acknowledgment is `outcome_unknown` until the receiver confirms `confirmed_stopped`; a never-launched task cancels with `cancellation_scope: not_launched` |
| Uncertain work is never replayed; retained reservations are not released by terminal absence | control cases | After owner death the admission claim stays `reserved` (`fleet admission HOST -- inspect`), an unconfirmed cancel leaves it `reserved`/`unknown`, and the tmux refusal log is empty: no terminal operation ran and terminal absence released nothing |

## T3 acceptance (`tests/test_t3_distributed.sh`)

Two receiving hosts (`host-a`, `host-b`), each with its own `HYDRA_HOME`,
mapped project clone and transport endpoint (distinct loopback aliases or
distinct sshd ports), both without tmux. The checked task plan from
`tests/fixtures/plan-task` is placed across them: `produce` and `compose` on
host-a, each validated (`inspect`, `verify`) on host-b, with the required
evidence join before composition.

| Acceptance bullet | Evidence |
| --- | --- |
| Complete distributed scenario with tmux absent on execution hosts | Both hosts advertise `execution-headless` and no `tmux`; `workflow plan run --accept` succeeds; `status --json` shows four steps succeeded with one attempt each; composition bytes are exact; `workflow plan result` is ok; two acceptances per host, four distinct task IDs, each present in the placed host's task store |
| Coordinator restart preserves task identity | While the second host holds a receipt, a concurrent coordinator is refused; the owner is SIGKILLed; `workflow resume` finishes the run with byte-identical `dispatch.json` and `receipt.json` for produce and inspect |
| Lost acknowledgments preserve task identity (controlled transport) | The first submission's acknowledgment is lost after host-a accepted it: the run returns 3 (`waiting-remote`), host-a holds one acceptance, and resume binds the receipt to the task ID the receiver had already published |
| Stale results cannot advance dependents (controlled transport) | A second run receives a replay of the first producer's sealed result: the run is `recovery-required`, no validator attempt exists and host-b accepted nothing new. A third run receives a result whose digest does not match its bytes: `recovery-required`, exit code 76, no dependent attempt |
| Run/step/attempt visibility through the CLI without attach | `workflow status --json`, `fleet task status`, `fleet task observe` and `fleet task logs --source work` for every task on its host; `workflow replay` twice is byte-identical. Viewing logs reads recorded streams; nothing attaches to a process's stdin |
| Operator access over real SSH | `HYDRA_TEST_T3_TRANSPORT=ssh`: aliases created with `hydra remote add ... --ssh-config FILE`, real OpenSSH client and two daemons; the same scenario and coordinator restart pass |

## tmux dependency audit

| Check | Scope after this work |
| --- | --- |
| `check_tmux_version` (`lib/tmux.sh`) | Interactive spawn, interactive workflow steps, dashboard, TUI only. A tmux on PATH whose `-V` fails or is unparsable is now reported as unusable instead of passing the version gate |
| `hydra doctor` (`lib/cmd_maintenance.sh`) | Missing tmux is an error only when interactive heads exist; otherwise informational |
| Receiver handshake (`src/fleet/transport/server.c`) | `tmux` is advertised only when `tmux -V` reports 3.x; `execution-headless` is always advertised |
| Task acceptance (`src/fleet/task/task_accept.c`) | A `tmux` capability is refused with `capability_unavailable` before acceptance; Git and the shell CLI are the only hard dependencies |
| Task launch and execution (`task_launch.c`, `task_execute.c`) | Exec tasks spawn `hydra spawn task --headless --no-agent`; no terminal is created |
| Plan compilation (`src/fleet/plan/plan_tasks.c`) | Task recipes admit only `exec` plus `execution-headless` |
| Workflow task dispatch (`src/fleet/workflow/workflow_task_execute.c`) | A first submission refused with a final receiver code (`capability_unavailable`, `unmapped_project`, `submission_conflict`, `missing_dependency`, `placement_mismatch`) fails the step with that code instead of waiting for reconciliation |
| Remote preflight (`src/fleet/setup/preflight.c`) | Missing tmux is a warning (heads cannot run there), never blocking |
| Bootstrap and install (`tests/test_fleet_install.sh`) | Pinned bootstrap and doctor pass without tmux |

## Interactive regressions with tmux installed

The interactive surface keeps its tmux requirement. These existing suites
were re-run with tmux 3.4 installed after the change to `check_tmux_version`,
all passing: `make test-one T=tmux` (66), `T=lifecycle` (43), `T=spawn_ux`
(56), `T=spawn_bootstrap` (58), `T=switch`, `T=group_messaging`, `T=kill`
(133), `T=kill_all`, `T=terminal_restore`, `T=headless_execution` (19/19),
`T=fleet_install` (29), `T=workflow_run_heads` (39), `T=deps`; `make test-tui`
(185, including transcript); and the fast lane's shell, TUI and native checks.
`make test-tui-pty` (real pseudo-terminal attach, capture and review) must be
run on a quiet machine: its process-reaping checks compare recorded PIDs
against the live process table and are disturbed by concurrent process churn
from other test workloads on the same host; the suite, its fixtures and the
TUI sources are unchanged by this work.

## Remaining operator step

Nothing here authenticates to a provider or to a separately administered host.
To qualify a real remote host without tmux, an operator runs the same public
interfaces against it:

1. `hydra remote setup NAME USER@HOST` (or `hydra remote add NAME USER@HOST
   --ssh-config FILE`), verifying the host key through your trusted process.
   `hydra remote preflight NAME --json` should show tmux as a warning only.
2. `hydra fleet list --json`: the host answers with `execution-headless` and
   without `tmux` in `capabilities`.
3. Place the recipes of `tests/fixtures/plan-task` (or your own) on that host
   with `"capabilities":["exec","execution-headless"]`, compile and run the plan
   with `hydra workflow plan compile` / `plan run --accept`, disconnect and
   reconnect, then inspect `workflow status --json`, `fleet task status`,
   `fleet task logs` and `fleet task result`.
4. For an authenticated headless adapter, sign in on the host (`hydra fleet
   auth login HOST --agent NAME`) and submit a workflow task whose steps use
   that profile; the committed fixture does not stand in for that.

Keep the receiver's `HYDRA_HOME/fleet/tasks` records and the run directory as
evidence. Hydra never claims live-host or provider qualification it did not
observe.
