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
