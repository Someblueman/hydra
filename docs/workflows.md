# Static workflows

Named artifacts and durable operator decisions use the sealed outputs, approval
waits, and exact digest rules described below.

Hydra workflow schema `1` describes a finite DAG with one local coordinator. Repository
definitions live at `.hydra/workflows/<id>.yml` (or `.yaml`).
They are covered by `hydra init --trust`; changing any file below `.hydra` makes
repository workflows untrusted until reviewed and trusted again. An explicit file
path may also be passed to `show`, `validate`, or `dry-run`.

```yaml
version: 1
id: review-change
description: Review a change in parallel
parallelism: 2
resources:
  disk_mb: 20480
  max_heads: 4
steps:
  - id: create
    kind: spawn
    needs: []
    retry: 0
    idempotent: false
    args:
      branch: review-change
      profile: default
  - id: tests
    kind: exec
    needs: [create]
    retry: 2
    idempotent: true
    args:
      head: review-change
      argv: [make, test]
```

IDs are lowercase, start with a letter, contain at most 64 letters, digits,
single `-` or `_` separators. `parallelism` is 1-16, `disk_mb` 1-1048576,
`max_heads` 1-64, and `retry` 0-10. A retry is only valid when `idempotent` is
true. Supported kinds and required arguments are: `spawn` (`branch`), `wait`
(`head`), `exec` (`head` and `command` or `argv`), `message` (`head`, `message`), `gate`
(`head`, `name`, and `command` or `argv`), `approve` (`head`, `name`, `by`), `approval-wait` (`head`, `name`), and
`kill` (`head`), and `task` (`task_input`). Task steps use the native fleet runtime
described by the native fleet runtime. Optional delegation arguments are `group`, `profile`, `reason`,
`completion_policy`, `timeout`, `force`, and `allow_shell`. `command` and `argv`
are mutually exclusive.

This is deliberately a restricted YAML subset: two-space indentation, mappings,
step sequences, scalar values, and inline scalar lists only. Tags, anchors,
aliases, merge keys, block scalars, flow mappings, multiline sequences, tabs,
and escape sequences are rejected. Unknown keys are errors.

Use `hydra workflow list`, `show`, `validate`, and `dry-run` to inspect a definition.
Normalized output and previews use a stable dependency-first topological order
(source order breaks ties). Inspection does not create heads, worktrees, or run
state. `hydra workflow run <id|path>` creates the durable run manifest first, then
executes the accepted `spawn`, `wait`, `exec`, message, gate, approval, and kill
commands. Repository definitions are never executed unless the current `.hydra`
tree matches the trust recorded by `hydra init --trust`.

Each run is stored beneath the current project's state directory with its resolved
definition, immutable bindings, `manifest.tsv`, `graph.tsv`, per-step attempts and
stdout/stderr, and an ordered `events.jsonl`. `hydra workflow status <run-id>` reads
that recorded state; `--json` emits schema version 1. The runner honors
`parallelism`, serializes spawn steps around Git's worktree mutation boundary,
refuses execution below `resources.disk_mb`, and never exceeds the declared retry
count. Every step must declare `idempotent`. Only idempotent failures may retry;
an interrupted non-idempotent step with no authoritative result becomes
`recovery-required` rather than being replayed.

Optional step fields `retry_on: [failure, timeout, interrupted, configuration]`
and `retry_backoff: 2` select retry classes and an initial delay in seconds
(0-86400). Delays double for each subsequent failed attempt, capped at 86400
seconds. An empty `retry_on: []` disables retries. Omitting these fields retains
immediate retries for all failure classes, subject to the retry count and
idempotency declaration. Exit 124 is `timeout`, 126/127 are `configuration`,
statuses above 128 and missing completion evidence are `interrupted`, and other
nonzero exits are `failure`. Exec steps request the underlying command status
using `hydra exec --exit-code`; this option requires exactly one selected head.
An interrupted attempt that cannot retry is `recovery-required`, since its side
effects remain unresolved. Non-idempotent attempts are never automatically retried.

Each attempt records `failure-class` and, when retryable, an absolute `retry-at`
deadline. Backoff does not occupy a worker slot. Restart preserves that deadline,
completed attempts, and the remaining retry budget; cancellation cancels pending
retries. Resume also verifies the runtime graph against the resolved definition.

`hydra workflow cancel <run-id>` records intent before signalling running command
trees and reports any residual child. `hydra workflow resume <run-id>` accepts a
stale or recovery-required run only when its project, base commit, and resolved
definition still match. Completed attempts are authoritative and are not repeated.

## Verified integration and merge trains

`hydra integrate <run-or-group> --base <ref> --target <branch> --dry-run` reports
immutable candidate commits, recorded and computed merge bases, claims, path
overlaps, predicted conflicts, gates, and the planned disposable worktree without
changing a ref. Replace `--dry-run` with `--execute` and repeat `--gate <command>`
to assemble and verify in an isolated worktree. The target remains unchanged until
both of these explicit steps succeed:

```sh
hydra integrate approve run_ID --by reviewer
hydra integrate promote run_ID
```

`hydra integrate train ...` uses the same report, worktree, locking, approval, and
promotion path but runs the configured gates after each ordered candidate. A train
stops at the first stale binding, observed conflict, gate failure, cancellation, or
disk refusal. `hydra integrate status run_ID` names the failure class, exact
candidate or gate, expected and observed target commits, preserved worktree/report,
and recovery command. Interrupted, cancelled, gate-failed, and resource-refused
runs can use `hydra integrate resume run_ID`; completed merges are not replayed.
Other failures require `hydra integrate cleanup run_ID --apply` and a new preview.
Promotion revalidates the manifest, every candidate, the verified worktree, approval,
and target ref before a local fast-forward. Hydra never pushes as part of integration.

Spawn steps accept `args.terminal_mode: headless` for a terminal-free workspace,
or `interactive` for a tmux terminal. Omitting the field preserves interactive
spawn. A headless spawn has no profile; use a following profile exec step to run
an adapter. Unknown modes and modes on non-spawn steps fail validation. A workflow
that requests a terminal checks tmux before dispatching its first step, without
falling back to another execution mode.

Headless agent steps use `args.profile`, `prompt_file` or `prompt_input`, optional
`requires`, and an optional declared `result_file`. They run through the same
supervised exec path. Profiles are literal bounded declarations, never shell
templates. Resume requires the exact recorded session, head, instance, worktree,
and profile. Sealed outputs bind declared bytes and digests to downstream inputs;
approval waits record an explicit decision and stale decisions cannot launch a
changed plan. Provider completion is an observation and does not satisfy a
verification gate.

## Supported workflow agents

Profile-based `exec` steps support Antigravity (`agy`), Cursor Agent (`cursor`),
OpenCode (`opencode`), Claude Code (`claude`), Codex (`codex`), and Pi (`pi`).
Imported profiles can add a plain script or another declared worker. Select the
profile with `args.profile`, deliver a verified `prompt_input` or `prompt_file`,
and require only capabilities the profile declares. Cursor does not declare usage
reporting; its recipe passes `--trust` so each fresh head worktree is accepted
without Cursor's interactive workspace prompt. A failed step's agent receipt keeps
a bounded `diagnostic` excerpt of provider output that was not an event. Use an
independent compare or gate step to verify the result.
Inspect built-in declarations with `hydra agent contract NAME`; live provider
authentication and qualification remain separate from help probes.

## Objective planning

Use [`hydra workflow plan`](../README.md#plan-an-objective) to author a bounded JSON plan,
compile and review its source/input/policy bindings, then run the exact accepted
artifact through this same scheduler. Final success requires artifact-bound
verification reports for the declared deliverables. This optional native path
does not change workflow schema 1 or shell-only execution.

A policy's `writes` may contain the exact scope `@spawned:*`. It authorizes a
plan's declared `<head>:<path>` or `<head>:*` scopes only for heads created by
that plan's own spawn steps. Plans must still name those heads; admission refuses
spawn branches that already exist, so the scope never covers the source checkout
or an existing head.

The guided local policy that Hydra writes when you review an agent proposal with
`P` allows tools `sh`, `git`, `make` and the head's own `profile:<name>` (omitted
when the head has no profile), writes `@spawned:*`, one worker, four heads, 3600
seconds summed over exec timeouts, 1 MiB of artifacts, a plan `disk_mb` of at
least 1024, and no retries or repairs. A typical implementation plan spawns a
headless worker head, runs an agent exec step there with `profile`, an inline
`prompt`, `result_file` and `timeout`, then verifies on that worker head with
the repository's own checks and a check step that writes an object report.
`result_file` equals the name and path of a declared output of that step.

### Heads a plan run creates

Each head a plan's spawn steps create belongs to that run. Hydra classifies it by
the steps that run on it: a **worker** head runs work or compose steps and holds
the result; a **verifier** head runs only verify steps. The native TUI lists them
inside their run, and a run launched from a planning conversation inside that
planning head, so Work shows the task you started rather than every head
(`add-kill` ▸ `plan run kill-dry-run · succeeded` ▸ `worker …`, `verifier …`).
Runs start collapsed; Enter, `l` and `h` expand and collapse them, and the header
counts run heads inside their run.

A headless head has no terminal. Details shows `headless (no terminal)`, the agent
of its current or last step (`codex · step implement · running`), the executable
version, the model and reasoning effort, tokens and duration. A model the provider
reports is shown as reported; one read from its configuration is labelled
"configured default … (not observed)"; otherwise it is unknown. `p` shows the
running step's live events (or a command's output, or a finished step's declared
result) in place of a terminal preview. Overview's run panel lists every step with
its kind and role, state, attempts, duration and head, and what comes next.

When the run succeeds, fails or is cancelled, verifier heads are retired with
`hydra kill --protect-untracked` after their evidence is sealed in the run record;
their branches are kept, and a verifier head with changes is kept for you to
review. Worker heads stay until you land (`hydra land <worker>`) or dismiss (`x`,
which removes the head and keeps its branch) the result. Each retirement is a run
event (`head.retired`, `head.retire_skipped`, `head.retire_failed`); a failed
retirement is reported and never changes the run result.

### Plans carry their own inputs

Planning never requires committing files to the source. The draft and anything
it needs travel with the proposal and are embedded in the compiled artifact, so
the acceptance digest binds their exact bytes:

- An agent exec step gives the worker's instructions inline as `prompt` (UTF-8,
  1 byte to 32 KiB, no NUL) instead of `prompt_input`; exactly one of the two is
  required. The compiler lowers it to a generated bundle input named
  `prompt-<step>` (so step IDs with inline prompts are at most 57 characters and
  that input name is reserved). `workflow plan show` prints a bounded, single-line
  excerpt with the prompt's size and digest; `show --json` holds the full text and
  `explain` adds a `prompt` summary to the step.
- A schema 1 `data.inputs` entry may name a proposal asset instead of a
  repository path: `{"asset": "<name>", "type": "file", "max_bytes": N}`. Publish
  assets with `hydra workflow plan propose <draft.json> --asset <name>=<file>`
  (repeatable; at most 16, each a regular UTF-8 text file of at most 64 KiB
  without NUL bytes; symlinks are refused). Hydra keeps private copies in
  `assets/` beside the head's `planning/draft.json`, replaces the whole set on
  each publication, and refuses a draft whose asset references and published
  files differ. Expert CLI use passes `--assets-dir <dir>` to `validate` and
  `compile`; the native workspace passes the draft's sibling `assets/` directory.
  The compiled artifact carries the text in its `assets` member (absent when a
  plan uses none, so earlier artifacts keep their bytes); the whole artifact must
  still fit 256 KiB.
- An exec `argv` element `@input/<name>` is replaced at run time by the path of
  that step's materialized input `<name>` (`$HYDRA_WORKFLOW_INPUTS_DIR/<name>`), so
  a check can run `argv: [sh, "@input/verify"]` after mapping
  `"verify": {"input": "verify"}` in `data.steps.<step>.inputs`. Validation
  rejects a reference to an undeclared input with `invalid_input_reference`.
  Scripts may also read `$HYDRA_WORKFLOW_INPUTS_DIR` and write
  `$HYDRA_WORKFLOW_OUTPUTS_DIR` directly.

Changing a prompt or any asset byte changes the compiled digest: a previously
approved digest no longer admits the changed plan, and the workspace treats a
republished asset set like a changed draft that must be validated again.
Admission and every step's binding check recompile from the artifact's own
embedded inputs, so republishing a proposal never alters a run in progress.
The runtime materializes these inputs as workflow data declarations with
`"source": "bundle"`, read from `bundle/` beside the admitted definition.

Exec steps run in their head's worktree. Only `argv[0]` is checked against the
policy's `tools` at validation; later arguments are not resolved against the
source. A verify step that runs on a plan-spawned head after the step that
changes it therefore sees the worker's files, including commands and tests the
worker added (for example `argv: [make, check]` or `[sh, tests/test_kill.sh]`).
Such a step should depend on that implementation step. Keep `--asset` for genuinely
custom files such as a report-writing verifier.

Validation binds the checkout Hydra runs in at its current commit. It must be a
Git repository with no tracked changes, and repository inputs (`path`) and
`context` files must exist there; inline prompts and assets need not. An
`invalid_source` diagnostic names that checkout and the failed
condition; for tracked changes it lists up to ten paths with a count and the
recovery `commit or stash these changes in <dir>, then validate again`.

In the planning view, `V` validates the reviewed proposal and `F` requests
changes: Hydra records through `hydra workflow plan proposal <head> --return
<feedback>` that this exact draft was sent back, so it cannot be validated or
executed until the agent republishes, and types the feedback (with any validation
diagnostics) into the agent's pane for you to send with Enter. `E` opens an
in-app confirmation showing the revision, a 12-hex digest, the policy and what the
run does; `y` executes that revision, with the full digest passed to the CLI. A
draft or policy change after validation refuses the launch. Once the run receipt
arrives, Hydra submits the run ID to the planning conversation, followed by
approval waits, failed steps and the outcome; the run owner also queues the same
notices in that head's inbox, so they are recorded even when the TUI is closed.

### Reviewing a finished plan

Attention (`I` in the TUI) lists what needs you as sentences such as "Result
ready for review · kill-dry-run · verify". Steps that declare no outputs, such as
spawn steps, are not results and never appear. `r` opens the review of the
selected item. For a compiled plan run it starts with the result: the verdict,
the deliverable's text, each requirement and the check that covered it, the
verify step's exact command with a summary of its output (`L` shows the full
log), duration, agent, executable version and tokens per step, and the worker
branch's commits, files and diff as they are now. It ends with the commands that
would land the worker branch (`git merge --no-ff <branch>` in your checkout) and
remove the plan's heads; Hydra never runs them. `n`/`N` move between sections.
Technical identity and retained-contract checks follow the result.

`s` marks an item seen. The marker is kept per user under `$HYDRA_HOME`, moves the
item to a collapsed Seen group and out of the count, and lasts until the item
changes. An approval that is marked seen stays listed until someone decides.
The Statistics view (`D`) shows the same token counts per step and run, with
totals for the selected range; CPU and memory are not measured.

### Inspecting, evaluating and deciding between compiled plans

`hydra workflow plan explain <compiled.json>` reports every node with its
dependencies, inputs, writes and the deliverables and checks it reaches; an edge
that creates a head or supplies an artifact says so, other edges are "declared
order; purpose not formalized", and a node that reaches no outcome is marked for
review. `hydra workflow plan compare <left.json> <right.json>` reports structural
counts, changed sections and changed check bindings between two compiled
artifacts, and whether their scope matches (same objective, requirements, checks,
deliverables, obligations, envelope, data, policy, source and context).

`hydra workflow plan evaluate <left.json> <right.json> [--estimates <file>]`
separates three concerns of the same comparison and reports each with its own
`modeled_preference` (`left`, `right`, `unresolved` or `not_assessed`) and reason:

- **decomposition**: nodes, edges, required and unconnected nodes, validation
  effort (checks, obligations, verify steps and, with estimates, their time and
  cost ranges), transfers (artifact inputs, cross-head inputs, the declared
  `max_bytes` bound; observed bytes stay `null`) and rework risk (the declared
  retry and repair budgets and repair scope; `probability` is always `null`).
  Under matched scope a preference needs fewer unconnected nodes, cross-head
  transfers or verify steps with nothing worse.
- **scheduling** of the fixed graph: depth in nodes, the widest level, the hard
  budgets copied from the envelope, critical path, total work, cost,
  `time_to_verified_result_milliseconds` (the longest estimated path ending at
  a check step over required nodes only) and `cost_to_verified_result_microunits`
  (summed over required nodes). A preference needs non-overlapping critical-path
  ranges with no cost disadvantage.
- **placement**: declared hosts and heads created; always `not_assessed`, since
  only local execution is implemented.

Every estimate-derived figure is a range or `null`; a missing step estimate keeps
the aggregates that need it unknown. Hard budgets are never changed by estimates:
an estimate whose lower bound exceeds a timeout sets `estimate_budget_conflict`
and leaves the preference unresolved. No preference claims semantic adequacy or
global optimality.

Estimates use the closed schema `{"schema_version": 1, "plans": {"<sha256>":
{"source", "cost_unit", "steps": {"<id>": {"milliseconds": [lo, hi],
"cost_microunits": [lo, hi]}}}}}` bound to the exact compiled digests;
`evaluate` accepts a file that names only one of the two plans and keeps the
other unknown, `compare` and `explain` need every inspected digest.
`hydra workflow plan observed <run-id> > estimates.json` writes that document
from a run's recorded step durations: one observation per step, as the range
`[d-1, d+1]` seconds in milliseconds because timestamps have one-second
resolution, `cost_unit` `unrecorded` and no cost rows (provider cost is never
recorded), with steps that have no completed attempt omitted. The `source` text
names the run and how many steps had repeated attempts.

Each selected or rejected alternative is recorded with
`hydra workflow plan decision record <head> --select <compiled.json> <reason>
[--reject <compiled.json> <reason>]... [--estimates <file>] [--json]`. The record
lives under the head's private `planning/decisions/<id>/` with the exact canonical
inputs (`inputs/<sha256>.json`, `inputs/estimates.json`), the reasons, the
compiler and analysis versions (`hydra-plan-1`, `hydra-plan-evaluate-1`), the
policy digest, the selected plan's hard budgets and the full `evaluate` output
for every rejected alternative. Selecting a plan whose estimate exceeds its own
hard budget is refused with `estimate_budget_conflict`. `decision list <head>
[--json]` lists the records, and `decision show <head> <id> [--json]` recomputes
every comparison from the recorded inputs and reports `replay.status`
`reproduced` when the canonical output matches byte for byte, `diverged` when an
input changed; a record whose bytes changed is refused as `invalid_decision`.
Recording a decision validates, approves and executes nothing; the reasons are
the generator's statements, not verified claims.

### Expansions

When a finished run's staged plan turns out to be insufficient, an expansion
adds work to it without a second compiler or a dynamic scheduler. A proposal
in the closed schema `hydra-plan-expansion-1` names new nodes, their
dependencies on sealed deliverables of earlier versions, new checks and the
joins the new branches feed:

```sh
hydra workflow plan expand <head> --run <run-id> --proposal expansion.json [--json]
hydra workflow plan expand <head> --run <run-id> [--json]           # the chain so far
hydra workflow plan run <round>/compiled.json --accept <sha256>     # one round, once
```

`<run-id>` is the base run (a schema 1 plan run) and `<head>` the head whose
private planning state keeps the chain, `planning/expansions/<run-id>/round-N/`.
The base run's policy must declare `expansion` bounds; without them nothing
authorizes an expansion (`expansion_not_authorized`). The proposal has
`schema_version` 1, `expansion` `"hydra-plan-expansion-1"`, `base_sha256` (the
digest of the chain's latest version; a stale one is `stale_base`), `claims`,
`dependencies`, `joins`, the new `steps`, `deliverables`, `requirements`,
`checks`, optional `obligations`, `data.steps` and an optional `envelope`.

A one-branch round that consumes the base run's sealed `report` deliverable
as bundle input `base` and feeds the join `compose1`:

```json
{"schema_version": 1, "expansion": "hydra-plan-expansion-1",
 "base_sha256": "<latest compiled digest from expand --json>",
 "claims": [{"id": "second", "kind": "new"}],
 "dependencies": [{"input": "base", "deliverable": "report"}],
 "joins": ["compose1"],
 "steps": [
  {"id": "spawn1", "role": "work", "kind": "spawn", "needs": [], "writes": [], "args": {"branch": "plan-expansion-1"}},
  {"id": "compose1", "role": "compose", "kind": "exec", "needs": ["spawn1"], "writes": [],
   "args": {"head": "plan-expansion-1", "argv": ["sh", "expansion-compose.sh"], "timeout": 30}},
  {"id": "verify1", "role": "verify", "kind": "exec", "needs": ["compose1"], "writes": [],
   "args": {"head": "plan-expansion-1", "argv": ["sh", "expansion-check.sh", "second"], "timeout": 30}}],
 "deliverables": [{"id": "report2", "description": "Second report", "step": "compose1", "output": "report", "destination": "run-artifact"}],
 "requirements": [{"id": "second", "criterion": "Second report equals the base report", "deliverable": "report2", "check": "check2"}],
 "checks": [{"id": "check2", "method": "executable", "definition": "Compare with base report", "step": "verify1",
   "input": "subject", "report": "check", "deliverable": "report2"}],
 "data": {"steps": {
  "compose1": {"inputs": {"base": {"input": "base"}}, "outputs": {"report": {"type": "file", "path": "report.txt", "max_bytes": 1024}}},
  "verify1": {"inputs": {"subject": {"step": "compose1", "output": "report"}, "expected": {"input": "base"}},
              "outputs": {"check": {"type": "object", "path": "check.json", "max_bytes": 2048}}}}}}
```

```sh
hydra workflow plan expand plan-smoke --run "$RUN" --proposal round1.json --json   # data.compiled, data.compiled_sha256
hydra workflow plan show "$COMPILED" --json                                         # review the round as any plan
hydra workflow plan run "$COMPILED" --accept "$SHA256"                              # prints the round's run ID
hydra workflow plan result "$ROUND_RUN"                                             # the ordinary result gate
```

Admission lowers the proposal into an ordinary schema 1 plan: the base
objective and assumptions, `id` `<base-id>-rN`, the proposal's records, and
`data.inputs` with one bundle input per dependency whose text is the exact
sealed bytes of that deliverable. It compiles that plan with the one compiler
under the base run's own policy, so the 9A obligation checks, the 9B handoff
checks, the tool, effect and write-scope checks and the source binding apply
unchanged; the checkout must be at the base run's bound commit
(`source_changed`). The round is recorded as version N+1 with its own exact
digest, and execution is the ordinary `workflow plan run` with that digest: a
changed round no longer admits it (`acceptance_mismatch`, `invalid_chain`), a
round whose consumed bytes changed does not execute (`dependency_changed`), and
a round executes once (`round_executed`); an interrupted run is resumed with
the ordinary workflow commands, never re-admitted. Authority never enlarges:
the policy is inherited verbatim and the expansion envelope (the base plan
envelope by default) may only shrink; a write scope may additionally name a
head the expansion itself spawns, under the policy's `@spawned:*`
(`authority_enlarged`).

The policy's `expansion` object declares `max_nodes`, `max_work_seconds` (sum of
exec timeouts), `max_artifact_bytes` (sum of envelope `artifact_bytes`),
`max_rounds` and `max_repairs_per_claim`, each a total over the base run and
every admitted round. Exceeding any bound is one typed refusal,
`expansion_bound_exceeded` with `data.bound`, `used` and `max`; nothing partial
is admitted. `joins` lists the steps where the new branches converge; each
needs members, every other new node must feed or follow one, and the recorded
membership is frozen in the round's record and digest, so nothing joins or
leaves at run time. A failed member leaves its join and everything behind it
cancelled: the round fails, no delivery exists and the chain reports the claim
as `fail`.

Each requirement of a proposal is a claim: `{"id", "kind": "new"}` asks a
question no earlier version asked, `{"id", "kind": "retry", "of": <earlier
requirement>}` restates an earlier claim exactly (same criterion, same check
method and definition) and consumes that claim's repair budget. A new claim
whose criterion an earlier requirement already states is refused
(`claim_restated`), a retry that changes the criterion or the acceptance rule is
refused (`claim_changed`), a retry of a verified claim duplicates verified work
(`duplicate_work`), a retry of a claim without a recorded terminal verdict is
refused (`unknown_outcome`), and a retry names the original claim, never an
earlier retry (`retry_of_retry`). Deliverable, requirement and check IDs stay
unique over the whole chain (`duplicate_id`).

Dependencies follow the declared dependency model through the sealed receipts
of earlier runs: a deliverable is eligible when its producer succeeded, its
bytes still match the sealed receipt and every check on it passed in a run with
a recorded terminal state. A deliverable no version declares is
`unknown_dependency`, a failed or invalid verification is `dependency_failed`,
bytes that cannot travel as a bundle input (not UTF-8 text, NUL bytes, over
64 KiB) are `unsupported_artifact`, and a run without a recorded terminal
outcome (running, stale, recovery-required, or an admitted round that has not
run) blocks dependents with `unknown_outcome` rather than guessing. The record
notes the producer closure's effects (`artifact_only`, `declared_writes`,
`agent`) for review.

`expand <head> --run <run-id>` prints the chain: bounds used over every version,
each round's digest, run and recorded terminal state, every claim with its
recomputed status (`pass`, `fail`, `invalid`, `unknown`, `not_executed`), every
dependency as `intact`, `changed` or its current status, and the frozen joins.
Statuses are recomputed from the run records on every call, so unchanged
state prints identical bytes and tampered files show as `expansion_changed`
with `integrity` `invalid`. The supported form is local schema 1 execution;
runtime membership changes and late conditional evaluation remain unsupported.

### Planning evaluation

`make test-plan-eval` (or `python3 scripts/plan-eval.py --execute 1`) measures
planning quality on a held-out case set. It exports the archived finite pilot from
Git history at commit `b0fb5b1d` and refuses to continue if any byte differs from
`examples/planning/evaluation/archive-manifest.json`; the archived record is never
edited. The nine archived cases are joined by the eight held-out additions in
`examples/planning/evaluation/heldout-v3-additions.json`, each with two declared
incorrect artifacts, and `examples/planning/evaluation/oracle.py` computes every
expected artifact from the case inputs and the frozen visible contracts alone.
Three effects are reported separately in `build/plan-eval/report.json`:

- **planning**: each frozen `current` and `contract-aware` candidate against all
  seventeen cases under the declared interface and under the archived post hoc
  interface adaptation, by task class and by source, with the number of repeated
  invocations and the number of distinct outcomes (a determinism check; frozen
  deterministic candidates exercise no stochastic variability);
- **validation**: false rejections of correct artifacts and false acceptances of
  the thirty-four declared corruptions, by task class; every declared corruption
  must be rejected;
- **scheduling**: the serial and fork/join patterns compiled by the current
  compiler and compared with `evaluate`; with `--execute`, both run through
  `hydra workflow plan run`, their recorded durations become estimates through
  `observed`, and the serial run's step durations are transplanted onto the
  fork/join graph as a prediction that is compared with the fork/join run's own
  elapsed time (calibration, n = 1).

With `--execute N`, the first N candidates of each arm also run through the
archived evaluation plan at the current source; the report records the verified
outcome, elapsed times, repeated work (steps with more than one attempt),
provider cost (`unknown`) and human corrections (`unknown (not observed)`).
Estimate calibration by task class stays `unknown`: the frozen candidates
supplied no estimates. The runner exits non-zero when a corruption is accepted, a
correct artifact is rejected, a candidate varies across repeats, the archived
nine-case outcomes are not reproduced or an executed outcome differs from the
archived expectation. See the [evaluation record](../examples/planning/evaluation/README.md)
for the measured results and their limits.

## Pools and schedules

Pools and schedules add persistent queues and host-timer starts without a
second scheduler. Every pool task is executed by `hydra workflow run` or
`hydra exec`, so admission, supervision, retries and recovery stay where they
are; every schedule firing is the public `hydra schedule run-now` command.
Hydra installs no daemon. A pool's one coordinator is its lock, and the
execution owner of a firing is the process the host timer starts.

### Task pools

`hydra pool create <pool>` seals the pool's bounds in `manifest.json`:
`--max-tasks` (1-1024, default 64), `--max-outstanding` claimed tasks at once
(1-16, default 1), `--max-expansions` (1-64, default 8), `--max-artifact-bytes`
of task definitions (1-1048576, default 65536), `--max-repair-rounds` operator
retries (0-10, default 0), the claim `--lease` in seconds (default 300) and the
default per-task `--retry` (0-10). Bounds never change; create a new pool for
more. Pool and task names follow the workflow ID rules.

`hydra pool add <pool> --workflow <id|path>` or `--exec <head> -- <argv...>`
appends a task to the open expansion, opening one when needed. `--idempotent`
with `--retry N` allows automatic retries; a non-idempotent task never retries.
Growth beyond a bound is refused (`growth_exhausted` for tasks or expansions,
`artifact_bytes_exhausted` for definition bytes). `hydra pool seal <pool>` (or
`add --seal`) writes the expansion's manifest, freezes its membership and makes
its tasks `ready`; tasks of an unsealed expansion are never claimed.

`hydra pool claim <pool> --worker <label> [--owner-pid <pid>] [--lease <s>]`
takes the first ready task under the pool lock, so competing workers never
share a task and the outstanding bound is never exceeded. The claim record
holds the lock protocol's owner fields (`pid`, `host`, `created_at`,
`operation`), the worker label, a random token and the lease expiry.
`hydra pool complete <pool> <task> --token <token> --outcome succeeded|failed
[--run <run-id>]` reports with that token; a token that no longer matches the
live claim is rejected as `stale_owner`, so a worker whose claim was taken away
cannot overwrite the record.

A claim is stale once its lease expires or its recorded owner pid is dead on
this host. The next `claim` (or `run`) reconciles stale claims from recorded
evidence only: a workflow run bound to the attempt that reached `succeeded`,
`failed` or `cancelled` is authoritative; anything else is an unknown outcome.
An idempotent task with retry budget then returns to `ready` (the attempt is
counted); every other task becomes `recovery-required` with no new attempt and
nothing replayed. The operator resolves it with `hydra pool complete <pool>
<task> --by <label> --outcome succeeded|failed|retry`; `retry` consumes one of
the pool's sealed repair rounds. `hydra pool status` shows a stale claim
(`"stale": true`) without changing it.

`hydra pool run <pool> [--worker <label>] [--max N] [--lease <s>]` is a worker
loop, not a scheduler: claim, run the task's public command in the repository
with the attempt's `stdout`/`stderr` as evidence, renew the lease while it runs,
and complete with the exit status. A workflow task's run ID is captured from
the command's first output line and bound to the attempt. The loop stops when
nothing is claimable (`none`, `unsealed`, `outstanding_limit`,
`pool_cancelled`) or after `--max` tasks. `hydra pool cancel <pool> [<task>]`
cancels pending and ready tasks immediately and asks claimed work to stop: the
worker loop sends TERM to its `hydra workflow run` or `hydra exec` process,
and a recorded running workflow run is cancelled through `hydra workflow
cancel`. A claimed task ends `cancelled` when its owner reports or is
reconciled; cancellation never invents an outcome for work that may have
finished.

### Schedules

`hydra schedule add <name> --every <N>m|<N>h|<N>d` records a schedule and
prints the exact host timer entry to install: a crontab line (every platform),
systemd user timer and service units (Linux) or a launchd property list
(macOS); `--format crontab|systemd|launchd|all` selects the text, and `status`
prints it again. The cadence must divide the hour (`1m`-`30m`), the day
(`1h`-`12h`) or be 1-28 days, so each timer expresses it exactly. Hydra
never installs the entry. Three submissions exist: `--workflow <id|path>`
runs the workflow in the firing process; `--pool <pool> --workflow <id|path>`
adds one sealed task to the pool; `--pool <pool>` alone runs the pool worker
loop. `hydra schedule list`, `status <name>` and `remove <name>` manage the
record; removal reminds you to remove the timer entry yourself.

Every entry runs `hydra schedule run-now <name>` from the repository with
`HYDRA_HOME` set. A firing's dedup token is its slot, `floor(now / interval)`
in UTC seconds, recorded as `firings/<name>-<slot>` under the schedule lock
before anything is submitted. A double cron fire, a timer firing plus a manual
`run-now`, or two competing firings of the same slot therefore submit exactly
once; the rest report `deduplicated`. `run-now --token <token>` names an
explicit token for integrations that already have one.

Missed runs are detected from the last fired slot. The default `--missed
skip` fires only the current slot and counts the skipped slots
(`missed_total`, a `schedule.missed` event). `--missed catch-up` additionally
fires the newest missed slots in order, at most `--catch-up-limit` (1-16,
default 4) of them, each with its own slot token; older slots are skipped.
Catch-up never fires more than that bound, whatever the outage length.

Unattended scheduling needs an always-on execution owner, which Hydra does
not provide: the host timer service starts the firing process, and that
process owns a `--workflow` run or a `--pool` worker loop until it exits. A
`--pool P --workflow W` schedule only enqueues; its owner is whoever runs
`hydra pool run P`, typically a second schedule or a supervised session. If the
owner dies, the run becomes `stale` and pool claims become stale exactly as
described above; nothing restarts them by itself.

Reboot recovery is an explicit opt-in: `--reboot-recovery` adds `@reboot` to
the crontab text, `Persistent=true` to the systemd timer and `RunAtLoad` to the
launchd plist, so the first firing after a restart happens without waiting for
the next slot. Its boundaries: the restart firing is an ordinary firing of the
current slot (deduplicated if that slot already fired), missed slots follow the
missed-run policy, and work that was in flight when the host went down is never
replayed by the service restart. A workflow run interrupted that way is resumed
only by an explicit `hydra workflow resume`, which keeps completed attempts and
turns interrupted non-idempotent steps into `recovery-required`; a pool task
whose owner died is reconciled by the next claim as above. The platform
qualification is local: the generated text is exact and stable under test, but
no live cron, systemd or launchd installation was observed here.
