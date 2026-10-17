# Hydra public contracts

Hydra 2.9.0 retains state v2, core protocol 1, TUI protocol 2 and Fleet protocol 1.
It adds guided remote setup (`hydra remote setup`, `provision`, `agents`,
`install-agent`, `sign-in`) with private per-host setup state, `hydra agent locate`
with recorded off-PATH agent locations, platform-bound install packages and pinned
static release helpers, and `hydra doctor --fix-permissions`. Hydra's own state is
created privately under any umask. Receivers additively advertise `agent-inventory`
and `agent-locate-record`, and the handshake reports `platform`. Since 2.8.0, plans
carry their own prompts and assets, with the policy write scope `@spawned:*`,
proposal send-back, `hydra resume --terminal`, `hydra kill --dry-run`, attention
data version 3 and statistics export schema 4; since 2.7.0, head-associated
planning proposals and the opt-in `kill --protect-untracked` are available; since
2.6.0, `hydra init` stays out of the source tree and the head environment is
delivered through the tmux session and a per-instance launcher.
See the [2.9.0 changelog](../CHANGELOG.md#290---2026-09-28).
Interactive Codex restore retains cwd-scoped latest-session selection; headless
resume binds an exact recorded session.
Internal shell function names,
module layout, renderer details, caches, and on-disk temporary files are not public
contracts.

## Compatibility and negotiation

- `hydra version` and `--version` print `Hydra version <semver>`.
- CLI syntax documented by `hydra help` is public. An incompatible removal requires
  a major release and the deprecation policy in the changelog.
- Machine interfaces reject unsupported schema or protocol versions. They do not
  guess, silently downgrade, or accept a different format as a fallback.
- Readers ignore unknown JSON output fields within a supported schema version.
  Input formats with a closed schema, including planning documents, reject unknown
  fields rather than silently discarding requested behavior.
- The shell CLI is the only mutation authority. Native processes receive bounded,
  versioned input and invoke public shell commands with an argument vector.

## JSON envelope v1

Every documented `--json` command emits exactly one JSON object and exits nonzero on
failure. Success has:

```json
{"schema_version":1,"ok":true,"command":"list","data":{}}
```

Failure has:

```json
{"schema_version":1,"ok":false,"command":"list","error":{"code":"invalid_input","message":"...","recovery":"..."}}
```

`schema_version`, `ok`, `command`, and the matching `data` or `error` member are
required. Error `code`, `message`, and `recovery` are required strings. Envelope v1 is
used by `init`, `capabilities`, `list`, `status`, `group status`, `recv`, `receipts`,
`queue`, `lifecycle`, `wait`, `exec`, `diff`, `review`, `provenance`, `workflow`,
`collision`, `resource`, `gate`, `du`, `snapshot`, `pool`, and `schedule` where their
help documents `--json`.

`json_escape` accepts POSIX C strings (there is no NUL). It escapes JSON syntax and
C0 controls and preserves other UTF-8 bytes.

## Durable state v2

`$HYDRA_HOME/state/v2` with `schema-version` equal to `2` is the only runtime state
authority. Projects, heads, instances, workflow runs, integration reports, messages,
claims, resources, gates, and provenance use validated opaque IDs as path keys.
Human labels are scalar values, never path identity. See [Durable state v2](#durable-state-v2).
`$HYDRA_HOME/cache` holds disposable derived data, such as recent leftover-worktree
sizes for Recovery, and is never an authority.

A leftover (orphaned) worktree is a registered `head_<id>` worktree directly under
the project's recorded worktree root whose head record is gone. `hydra gc --policy
orphaned` is the only detector; doctor, cleanup, `du` and Recovery reuse it. Other
worktrees, including `hydra-<branch>` siblings, are never candidates.

The seven-field global map and project `compat-map` are not 2.0 runtime formats.
Before migration, finish or stop active mutations, preserve local work, then run
`hydra state verify` and `hydra state migrate --dry-run`. The dry run must report
authoritative state v2 as verified. Run `hydra state migrate`, then
`hydra state verify`; retain the generated backup path printed by migration.
For a 1.9 rollback, use that exact path:
`hydra state rollback "$HOME/.hydra/backups/state-YYYYMMDDTHHMMSS-PID"`, then
`hydra state verify`. Rollback restores state and obsolete projections only; it does
not replay commands or restore active owners.

Writers use project- or record-scoped directory locks and adjacent-file rename.
Failure to acquire a lock is a failed mutation; there is no unlocked write path.
Head history remains after teardown with `desired-state=stopped`.

## Events, lifecycle, and completion

- Events are append-only JSON Lines schema v1 with ordered sequence numbers,
  correlated project/head/instance IDs, and repair/retention under the event lock.
- Declared outcome, observed provider status, and tmux liveness are independent.
- Completion policies are `declared-done`, `observed-exit-zero`, and `either`.
  Session disappearance alone is not task completion.
- Resume retains the head ID, creates a new instance, and marks the old instance as
  superseded. Stale-instance adapter input is rejected.
- Teardown defaults to no transcript. `redacted` and `full` are explicit policies;
  retained transcripts are bounded and instance-scoped.

Lifecycle, event, and message records follow the schemas described in this guide.

## Profiles, tasks, adapters, and scopes

- Built-in profiles are `agy`, `cursor`, `opencode`, `claude`, `codex`, and `pi`.
  `--profile` selects one explicitly; `--no-agent` selects a shell, and multiple
  detected providers require an explicit choice. Custom profiles are private,
  schema-versioned literal declarations. Headless prompt transport, provider
  translations, exact recorded-session resume, and capability requirements are
  bounded by the rules in [workflows](workflows.md).
- A relative profile executable resolves from PATH first (executable regular
  files), then from a recorded location `$HYDRA_HOME/agents/locations/EXECUTABLE`
  (one absolute path; a private file owned by the user). A record is used only when
  its path ends in `/EXECUTABLE`, is an executable file whose entry and target belong
  to the user or root, and whose target is not group- or world-writable; shell and
  native resolvers apply the same rules and headless runs execute the resolved path.
  `hydra agent locate --json` reports `agent-inventory` schema 1:
  `{search_dirs, agents:[{profile, executable, on_path, candidates:[{path, source:
  path|record|search, version, recordable}], recorded, status: on_path|recorded|
  found_off_path|ambiguous|missing}]}`. Receivers expose it as the read-only
  `agent-inventory` action (args `[]`, or `["probe", PROFILE, PATH?]` for the
  profile's help probe of one valid candidate) and `agent-locate-record`
  (`[EXECUTABLE, PATH]`); the handshake additively reports `platform {os, arch}`.
- Remote installers come only from local recipes, `agent-recipes` schema 1:
  `{"schema":"agent-recipes","schema_version":1,"recipes":[{agent, executable,
  command, requires[], expected_dirs[], login_args[], auth_status_argv[]?, docs_url,
  verified_on}]}`. Built-in recipes are compiled in; an optional private (0600, owned,
  not a symlink) `$HYDRA_HOME/fleet/agent-recipes.json` with exactly these members
  replaces built-in recipes per agent, and an unsafe or invalid file disables
  installs (`recipe_unavailable`) rather than falling back silently.
- The built-in `cursor` recipes pass `--trust` after `--print` for new and resumed
  runs, and its help probe requires that flag. Cursor Agent otherwise refuses a
  directory it has not trusted, and every head is a fresh Hydra-created worktree.
  Selecting the head is the operator's trust decision, and Hydra's repository trust
  (`hydra init --trust`) still governs repository-controlled configuration. `--trust`
  only skips Cursor's workspace prompt; Hydra never passes `--force` or `--yolo`.
- A headless run receipt (`agent-run` data and `exec/RUN/HEAD/agent.json`, schema 1)
  whose `exit_status` is nonzero adds an optional `diagnostic` object,
  `{"stdout":EXCERPT|null,"stderr":EXCERPT|null}`. `stdout` is provider output the
  adapter did not decode, from the first rejected line or the unread tail (never
  for adapter `none`); `stderr` is provider stderr. `EXCERPT` is
  `{"text":string,"bytes":integer,"truncated":boolean}`: `text` is at most 4096
  bytes of the start of that output, cut at a character boundary, with invalid UTF-8
  and NUL bytes replaced by `?`; `bytes` counts the whole source and `truncated` is
  true when `text` is shorter. It is absent from completed runs and when both
  streams are empty. It is diagnostic text, never an answer, event or verification.
- The receipt additively records `configuration` when a provider's own
  configuration decides the model: `{"model":{"value","source"},
  "reasoning_effort":{"value","source"},"scope"}` with either member optional.
  For `codex` it is read at launch from literal `-m`/`--model` or `-c
  model[_reasoning_effort]=...` recipe arguments, else from
  `$CODEX_HOME/config.toml` (default `~/.codex/config.toml`, honouring its
  `profile`). It is configuration, never an observation. Optional
  `observed_model` and `observed_reasoning_effort` strings hold the first value a
  provider event names itself (Claude Code's `system/init` carries `model`).
  Readers keep either absent value unknown.
- While a provider runs, its stdout is copied (at most 1 MiB, mode 0600) beside
  the receipt as `.provider-stdout` for a read-only live view, and removed when
  the step ends; keeping provider output remains the explicit `--retain` choice.
- Task text is resolved before launch, stored privately, and delivered as one quoted
  argument. Events contain only its hash and byte count.
- Adapter input is bounded canonical JSON schema v1 and must name the current
  instance. Capability confidence never upgrades an observation into authority.
- Read/write scopes and expiring claims are advisory coordination constraints. Gate
  approval is a separate exact binding to verification evidence.

## Workflows and integration

Objective planning schema v1 and schema v2 are optional, closed JSON authoring
contracts exposed by `hydra workflow plan schema`. The additive 9A `obligations`
array keeps those plan versions and the compiled-artifact wrapper unchanged:
legacy plans remain byte-compatible, while explicit obligations are included in
the canonical plan and therefore in the compiled SHA-256/acceptance binding.
Each obligation names its intent reference, exact deliverable/step/output subject,
criterion, evaluation method/check, required report evidence, applicable
environment, supported completion rule, and limitations. Multiple obligations
may share one requirement. `workflow plan obligations --json` is a read-only
projection; legacy rows are marked `derived` with unknown intent/semantic
evidence. Compilation rejects orphan or circular evidence and unreachable
evaluation joins with field paths and counterexamples. Structural satisfiability,
runtime evidence, and semantic adequacy remain separate; coverage alone is not
semantic proof.
See [workflows](workflows.md) for planner limits and report format.
Policy envelopes additively accept the exact write scope `@spawned:*`. It
authorizes a plan write scope only when its head is the branch of one of that
plan's own spawn steps; plans still declare concrete `<head>:<path>` scopes, and
`@spawned:*` in a plan envelope is invalid. Admission already refuses spawn
branches that exist, so the form never reaches the source checkout or an
existing head. Earlier releases reject such a policy as `invalid_policy`.
Plan schema 1 additively accepts an inline agent-step `prompt` (1 byte to 32 KiB
of UTF-8 without NUL; exactly one of `prompt` or `prompt_input`) and asset input
declarations `{"asset", "type", "max_bytes"}` in `data.inputs`. Earlier releases
reject both as unknown fields. The compiler lowers them to data inputs with
`"source": "bundle"` (paths `prompts/<step>` and `assets/<name>`, generated input
`prompt-<step>`); workflow data manifests accept that source and read it from
`bundle/` beside the definition. The compiled wrapper stays version 1 and adds an
optional `assets` object (asset name to text) only when a plan uses assets, so
existing artifacts and digests are unchanged; prompt and asset bytes are part of
the acceptance digest. `validate` and `compile` accept an optional trailing
`--assets-dir <dir>`; diagnostics add `missing_asset`, `invalid_asset`,
`unsupported_asset` (schema 2), `prompt_conflict` and `invalid_input_reference`.
In a compiled plan run, an exec argv element `@input/<name>` becomes the path of
that step's materialized input; plain workflow definitions pass it unchanged.
`invalid_source` diagnostics name the checkout and add optional `source`,
`condition` (`missing_directory`, `not_repository`, `no_commit`,
`tracked_changes` or `unreadable_content`) and `recovery` fields; tracked changes
also carry `changed_paths` (at most ten) and `changed_count`. The envelope's
`error.recovery` repeats the first diagnostic's recovery when one is present.
Structured v3 reports require each obligation's `measurements` evidence to contain
at least one finite JSON integer or floating-point observation. Narrative strings,
nulls, objects, arrays, and non-finite numeric values do not satisfy that evidence
requirement. The human-readable delivery view separately prints v3 execution,
evidence, and domain statuses plus per-obligation counts; legacy report views retain
their existing fields.
Assessment checks use their accepted definition as rubric text rather than an
executable predicate. Each obligation contributes one observation keyed by its
obligation ID, with a `verdict` of `pass`, `fail`, or `inconclusive`; assessment
records also require nonempty rubric, source locators, disagreement, and authority
review fields. Assessment records bind the same definition-and-arguments recipe
digest, but do not claim machine verification of rubric correctness.

`workflow plan evaluate` (schema 1, `analysis` `hydra-plan-evaluate-1`) adds
to the `compare` fields three sections, `decomposition`, `scheduling` and
`placement`, each with `left`, `right`, `modeled_preference` (`left`, `right`,
`unresolved`, or `not_assessed` for placement) and `reason`. Decomposition rows
carry `nodes`, `edges`, `required_nodes`, `unconnected_nodes`,
`validation_effort` (`checks`, `obligations`, `verify_steps`,
`verify_milliseconds`, `verify_cost_microunits`), `transfers` (`artifact_inputs`,
`cross_head_inputs`, `declared_max_bytes`, `observed_bytes`) and `rework_risk`
(`retry_budget`, `repair_budget`, `repair_scope`, `probability`, `basis`).
Scheduling rows carry `depth_nodes`, `max_level_width`, `hard_budgets`
(`parallelism`, `timeout_seconds`, `max_heads`, copied from the envelope),
`critical_path_milliseconds`, `total_work_milliseconds`, `cost_microunits`,
`time_to_verified_result_milliseconds`, `cost_to_verified_result_microunits`,
`estimate_budget_conflict` and the supplied `estimates`. Placement rows carry
`hosts` and `heads_created`. Range fields are `[low, high]` integer arrays or
`null`; `observed_bytes` and `probability` are always `null`. `evaluate` accepts
an estimates file that names only one of the two digests. `workflow plan
observed <run-id>` prints an estimates document (schema 1) bound to the run's
compiled digest with one `milliseconds` range per step that has a completed
attempt and no cost rows.

A planning decision record (`planning/decisions/<id>/decision.json`, schema 1,
`record` `hydra-plan-decision-1`) is canonical JSON with `recorded_at`,
`compiler`, `analysis`, `policy` (`schema_version`, `sha256`), `hard_budgets`
(the selected plan's envelope), `estimates` (`sha256`, `input`, both null without
estimates), `selected` (`sha256`, `reason`, `input`, `estimate_budget_conflict`),
`rejected` (rows with `sha256`, `reason`, `input`, `evaluation_sha256` and the
full `evaluation`) and `limits`. `<id>` is the first sixteen hex characters of
the record file's SHA-256; `inputs/<sha256>.json` holds each alternative's
canonical compiled artifact and `inputs/estimates.json` the canonical estimates.
`decision show --json` returns `decision_id`, `record` and `replay` (`status`
`reproduced` or `diverged`, `comparisons` rows with `rejected_sha256`,
`recorded_sha256`, `replayed_sha256`, `reproduced`, and `method`); a record whose
bytes or ID changed fails with `invalid_decision`. `decision list --json` returns
`decisions` rows (`decision_id`, `recorded_at`, `selected_sha256`, `rejected`,
`replay`). Recording refuses `estimate_budget_conflict`, `duplicate_alternative`,
`invalid_alternative`, `invalid_reason` and `invalid_estimates`.

The planning evaluation report (`build/plan-eval/report.json`, schema 1,
`report` `hydra-plan-eval-1`) records `source_commit`, `archive` (`commit`,
`files` digests, `verified`), `matched_contract`, `case_set`, `planning` (per arm
and candidate: `interfaces.declared` and `interfaces.adapted` with `accepted`,
`total`, `by_class`, `by_source`, `cases`, `repeats`, `distinct_outcomes`),
`validation` (`false_rejections`, `false_acceptances`, `incorrect_rejected`,
`incorrect_total`, `by_class`, `controls`), `execution` (`compiled`, `runs` with
`verified_outcome`, `as_expected`, `wall_seconds`,
`time_to_verified_result_milliseconds`, `cost`, `human_corrections`,
`repeated_work`), `scheduling` (`structural`, `executed`, `calibration`),
`calibration`, `human_corrections`, `stochastic_variability`, `failures`,
`status` and `limits`. Unobserved quantities are `null` or `unknown` strings,
never estimates.

A policy may additively declare `expansion` bounds (`max_nodes` 1-256,
`max_work_seconds` 1-864000, `max_artifact_bytes` 1-33554432, `max_rounds` 1-16,
`max_repairs_per_claim` 0-10, all required when present); earlier releases
reject the member as `invalid_policy`, and the bounds are part of the compiled
artifact and its digest. An expansion proposal (`hydra-plan-expansion-1`) is a
closed object: `schema_version` 1, `expansion`, `base_sha256`, `claims` (rows
`{id, kind: new}` or `{id, kind: retry, of}`, one per requirement),
`dependencies` (at most 16 rows `{input, deliverable}`), `joins` (1 to 64 step
IDs), `steps`, `deliverables`, `requirements`, `checks`, optional `obligations`,
`data.steps` and optional `envelope`. The expansion record
(`round-N/expansion.json`, schema 1, `record` `hydra-plan-expansion-record-1`)
is canonical JSON with `round`, `recorded_at`, `compiler`, `base_run_id`,
`base_sha256`, `previous_sha256` (the version it extends),
`previous_record_sha256` (the previous record file, `null` for round 1),
`policy_sha256`, `compiled_sha256`, `proposal_sha256` (the proposal file),
`claims` (with `repair_attempt` on retries), `dependencies` (rows with `input`,
`deliverable`, `version`, `sha256`, `bytes`, `effects`), `joins` (step to
frozen member list), `bounds` (`used`/`max` per bound and per retried claim)
and `limits`. `expand --json` returns `base_run_id`, `base_sha256`,
`latest_sha256`, `rounds`, `integrity` (`intact` or `invalid`), `diagnostics`
(chain rows with `path` `round-N` and codes `invalid_run`, `unsupported_base`,
`expansion_not_authorized`, `expansion_changed`, `run_mismatch`), `policy`,
`bounds` and `versions` (per version `round`, `compiled_sha256`, `run_id`,
`run_state` among `not_executed`, `absent`, `unknown`, `succeeded`, `failed`,
`cancelled`, `claims` with `status`, `dependencies` with `status`, `joins`,
`compiled` and `recorded_at` for rounds). An admission returns `round`,
`compiled_sha256`, `proposal_sha256`, `compiled`, `record`, `resolved` and
`acceptance`. Refusals are typed `invalid_chain`, `invalid_expansion`,
`stale_base`, `duplicate_id`, `claim_restated`, `claim_changed`,
`duplicate_work`, `unknown_claim`, `retry_of_retry`, `unknown_outcome`,
`unknown_dependency`, `dependency_failed`, `unsupported_artifact`,
`authority_enlarged`, `invalid_joins`, `expansion_bound_exceeded` (with
`data.bound`, `used`, `max`), `invalid_plan` (compiler diagnostics) and
`source_changed`; execution of a round adds `acceptance_mismatch`,
`dependency_changed` and `round_executed`. A round's run records
`plan-expansion.json` (the admitting record) and the round records `run-id`.

Workflow definition schema v1 is the stable finite-DAG contract. Definitions use the
documented restricted YAML subset, every step declares idempotency, argv is the
default execution form, and shell strings require both `allow_shell: true` and a
current repository trust decision. Durable manifests bind resolved definitions,
inputs, attempts, outputs, events, cancellation, and recovery.

A compiled plan run records its step roles and heads (`plan-roles.tsv`). When it
reaches `succeeded`, `failed` or `cancelled`, each head its spawn steps created on
which only verify-role steps ran is retired through `hydra kill
--protect-untracked` once every succeeded step on it has sealed its outputs; the
branch is kept. A head with uncommitted or untracked changes is kept. Each outcome
is recorded as `retirement/<spawn-step>/{state,detail}` (`retired`, `kept` or
`failed`) and a run event `head.retired`, `head.retire_skipped` or
`head.retire_failed`, after the terminal run event; retirement never changes the
run result. Heads of work and compose steps are never retired automatically.

Integration manifests bind the target, initial target ref, ordered immutable
candidates, gates, merge output, verification result, approval, and recovery action.
Promotion revalidates those bindings under a project lock, updates only a local ref,
and never pushes. See [workflows.md](workflows.md).

## Pools and schedules

Task pools and schedules (since 3.0.0) are project-scoped state v2 records
beneath `$HYDRA_HOME/state/v2/projects/<project>/pools/<pool>/` and
`.../schedules/<schedule>/`, created with private modes and inspectable as
plain files. Pool and task names are validated workflow-style identifiers
used as path keys. A pool holds `schema-version` (`1`), the sealed bounds
manifest `manifest.json` (`{"schema_version":1,"pool","project_id",
"created_at","bounds":{"max_tasks","max_outstanding","max_expansions",
"max_artifact_bytes","max_repair_rounds","lease_seconds","retry"}}`) with its
Git blob digest in `manifest-digest`, scalar counters (`task-count`,
`expansion-count`, `expansion-open`, `artifact-bytes`, `repair-rounds`), the
task `order`, `expansions/<n>/{state,tasks,manifest.json,manifest-digest}`
(`manifest.json` is `{"schema_version":1,"pool","expansion","sealed_at",
"task_count","artifact_bytes","tasks":[...]}`, written once at seal),
`tasks/<id>/` and an append-only `events.jsonl` (schema 1 lines with
`sequence`, `occurred_at`, `pool`, `task_id`, `type`, `detail`). Bounds and
sealed manifests never change. A task directory holds `state` (`pending`,
`ready`, `claimed`, `succeeded`, `failed`, `cancelled`, `recovery-required`),
`kind` (`workflow` or `exec`), `definition` (an absolute workflow path or a
head branch), `argv` (one element per line), `idempotent`, `retry`,
`attempts`, `expansion`, `bytes`, `cancel-requested` when requested, the live
`claim/` record (`pid`, `host`, `created_at`, `operation`, `worker`, `token`,
`attempt`, `expires-at`) and `attempt-<n>/` evidence (`worker`, `claimed-at`,
`outcome`, `source` of `worker`, `workflow-record` or `operator`, `decision`,
`stale-at`, `run-id`, `exit-code`, `stdout`, `stderr`, `completed-at`).

Claims are issued only under the pool lock (`pool_<project>_<pool>`), one
task per claim, never above `max_outstanding`, and only from sealed
expansions. A claim is stale when its lease expired or its owner pid is dead
on the recording host; stale claims are resolved from recorded evidence
alone. A terminal workflow run bound to the attempt is authoritative; an
unknown outcome re-queues an idempotent task within its retry budget and
otherwise yields `recovery-required` with no new attempt. Completion requires
the live token (`stale_owner` otherwise) or, for `recovery-required` tasks, an
explicit `--by` decision; operator `retry` consumes a repair round. Tasks are
executed by the public `hydra workflow run` and `hydra exec` commands, so
admission applies unchanged; pools add no scheduler.

`pool create|status --json` return `data` with `pool`, `state`,
`created_at`, `manifest_digest`, `bounds`, `usage` (`tasks`, `expansions`,
`open_expansion`, `artifact_bytes`, `repair_rounds`, `outstanding`), `counts`
per state, `drained`, `cancel_requested`, `expansions[]` (`number`, `state`,
`task_count`, `manifest_digest`) and `tasks[]` (`task_id`, `state`, `kind`,
`definition`, `argv`, `idempotent`, `retry`, `attempts`, `expansion`,
`cancel_requested`, `claim` with `worker`, `host`, `pid`, `claimed_at`,
`expires_at`, `stale` or null, `run_id`, `last_outcome`, `outcome_source`).
`pool add` returns `pool`, `task_id`, `state`, `expansion`,
`sealed_expansion`; `pool seal` returns `expansion`, `task_count`,
`manifest_digest`; `pool claim` returns `claimed`, `reason` (`none`,
`unsealed`, `outstanding_limit`, `pool_cancelled`), `token`, `lease_seconds`,
`reconciled` and the claimed `task`; `pool complete` returns `task_id`,
`outcome`, `state`; `pool cancel` returns `result` (`cancelled`, `signalled`,
`terminal`) for one task or `cancelled` and `signalled` counts for the pool;
`pool list` returns `pools[]`; `pool run` returns `worker`, `executed`,
`stopped` and `results[]`. Error codes include `not_initialized`,
`not_found`, `already_exists`, `growth_exhausted`,
`artifact_bytes_exhausted`, `nothing_to_seal`, `not_claimed`,
`stale_owner`, `invalid_state`, `repair_exhausted`, `pool_cancelled`,
`head_not_found` and `busy`.

A schedule holds `schema-version` (`1`), `manifest.json`
(`{"schema_version":1,"schedule","project_id","created_at","every",
"interval_seconds","submission":{"kind","workflow","pool"},"missed_policy",
"catch_up_limit","reboot_recovery"}`) with `manifest-digest`, the same
values as scalars, `last-slot`, `missed-total`, `firings/<token>/` (`slot`,
`fired-at`, `trigger` of `run-now` or `catch-up`, `state` of `submitting`,
`finished` or `failed`, `owner-pid`, `exit-code`, `run-id` or `task-id`,
`stdout`, `stderr`, `completed-at`) and `events.jsonl`. Kinds are
`workflow`, `pool-add` and `pool-run`. The dedup token of a firing is
`<schedule>-<slot>` with `slot = now - now mod interval_seconds` (UTC), or an
explicit `--token`; the firing directory is created under the schedule lock
before submission, so a token submits at most once. Missed slots are the
slots strictly between `last-slot` and the current slot; `skip` fires only
the current slot, `catch-up` also fires the newest `catch_up_limit` missed
slots in order. Timer text is a pure function of the schedule record,
`HYDRA_HOME`, the repository root and the hydra binary path; paths with
quotes, `%`, `\`, `<`, `>`, `&`, `$`, backquotes or whitespace are refused
(`unsupported_path`). `schedule add|status --json` return `schedule`,
`state`, `created_at`, `manifest_digest`, `every`, `interval_seconds`,
`submission`, `execution_owner` (`timer-process` or `pool-worker`),
`missed_policy`, `catch_up_limit`, `reboot_recovery`, `last_slot`,
`next_slot`, `missed_total`, `firings[]` and `timer` (`crontab`, `systemd`,
`launchd` text). `schedule run-now --json` returns `schedule`, `slot`,
`fired[]`, `deduplicated[]` and `missed` (`policy`, `detected`,
`caught_up`, `skipped`), exiting nonzero when a submission failed.

Hydra never installs a timer or daemon. Unattended scheduling requires an
always-on execution owner outside Hydra: the host timer service starts the
firing process, which owns a `workflow` run or `pool-run` loop until it exits;
a `pool-add` firing only enqueues and its owner is whoever runs `hydra pool
run`. Reboot recovery is opt-in (`--reboot-recovery`: `@reboot`,
`Persistent=true`, `RunAtLoad`) and only starts a new firing of the current
slot; a service restart never replays in-flight work, an interrupted run is
resumed only by an explicit `hydra workflow resume`, and a restarted
coordinator reconciles unknown pool outcomes from evidence instead of
inventing completion. Live cron, systemd and launchd installations are not
qualified by Hydra's own tests; the generated text is.

## TUI protocols and parity

The basic TUI row is seven tab-separated fields:

```text
branch  session  profile  status  activity  group  pr
```

The internal native adapter begins with `HYDRA_TUI<TAB>2`, followed by bounded `H`
head and `R` recovery rows whose fields contain no tabs or newlines. The fleet
overview adapter uses `HYDRA_FLEET_TUI<TAB>3`: it retains version-2 `T` host and `F`
head rows, extends `T` with connection/freshness fields, and adds bounded `O` task
rows. Version-1 and version-2 fleet fixtures remain readable. Unsupported protocol
versions fail closed. This adapter is not a general automation API.

Version-2 `H` rows may add a trailing terminal mode (`interactive` or
`headless`); readers accept rows without it. The workflow projection (`workflow
tui-data`) is internal protocol `HYDRA_WORKFLOW_TUI<TAB>2`: version 1 `W` and `N`
fields keep their positions and gain trailing fields (run kind, planning branch,
created and completed times, accepted digest prefix; step role, head, profile,
started and completed times), with `E` agent receipt summaries and `R` rows for
heads a run's spawn steps created (worker or verifier, and retirement). Version-1
workflow fixtures remain readable. `hydra tui --head-output <branch>` is a private,
bounded, read-only text view of the step running (or last run) on a headless head.

Plain `hydra tui` is native-first with a visible `hydra tui --basic` fallback. Both
retain navigation, search, refresh, preview, switch, spawn, group assignment,
dashboard, regenerate, confirmed kill, and help behavior. Native mutations execute
the public shell CLI with explicit argv and never write Hydra state directly. Native
attention consumes `workflow attention-data` or `fleet attention-data`; its `I`
view and `r` exact review route are read-only client interaction.
Review selection carries the complete attention identity, requested revision, and
identity hash. `review-data` is a bounded framed projection of the public workflow
or Fleet review command; opening a review or supplied reference cannot change
durable state or confer approval. Stale or ambiguous selections remain unavailable
until a fresh exact identity is established. Native UI behavior is covered by the
public CLI and protocol rules in this guide.

Attention data is `HYDRA_ATTENTION<TAB>3`: each `ITEM` keeps the 19 version-1
fields and appends a presentation `label` (the workflow name, or `-`; version 2)
and a presentation `detail` (version 3: for a failed check, the IDs of the
requirements it decides, comma separated and bounded to 255 bytes, otherwise
`-`). Neither is part of the identity or revision hashes. Readers accept versions
1 to 3. A succeeded step whose data manifest declares no outputs, and whose
receipt names no files, is not a result and produces no item; declared outputs
that do not match their receipt stay explicit `unknown` items.

Kind `failure` (route `workflow-evidence`) is a recorded failure that needs the
user's decision. Reasons: `check_failed` (a failed step that the compiled plan
names as a check's step; the JSON item carries `requirements`), `step_failed`,
`step_recovery_required`, and, for a run in state `failed` or
`recovery-required` with no failed step (for example a rejected delivery),
`run_failed` or `run_recovery_required` with step and attempt `-`. The revision
covers the step and run states, the attempt's exit code, failure class and
completion time, and the checks and requirements. A failure is resolved, and no
longer produced, when its step succeeds or when a later run (by `created-at`) of
the same `workflow-id` from the same planning head succeeds; a run cancelled on
request is the user's own decision and is not a failure. Fleet attention reports
a remote task whose receiver recorded `execution_state` `failed` as kind
`failure`, reason `task_failed`, with its step and attempt when both are valid;
cancelled and `outcome_unknown` tasks are never claimed as failures. A failure's
review has readiness `failed` (`candidate_state` `failed_needs_decision`) and
the plan's result section; a run-level failure is reviewed from the run's own
records.

Seen markers are a per-user client preference, not workflow state.
`hydra workflow attention-seen list | mark IDENTITY REVISION | clear IDENTITY`
(64-hex digests) keeps them in `$HYDRA_HOME/attention/seen.tsv`, one
`identity<TAB>revision<TAB>seen-at` row per identity, at most 512 rows (oldest
dropped), replaced atomically under the `attention-seen` lock. Every call prints
`HYDRA_ATTENTION_SEEN<TAB>1`, `SEEN<TAB>identity<TAB>revision` rows and
`END<TAB>count`. A marker matches only its exact revision, so a changed item is
unseen again. Marking an approval seen never approves, rejects or dismisses it;
marking a failure seen never resolves it, and both stay listed and counted as
needing the user.

A review of an item belonging to a compiled plan run adds a read-only `result`
object: the plan name and objective, verdict (`pass` only from the verified
delivery; `pending`, `fail` or `not_verified` otherwise), deliverables with a
bounded text excerpt, requirements with their check state (`pass`, `fail`,
`pending`, `unverified` or `not_reported`), checks with the exact argv, head, exit
code, report evidence and a filtered output summary and log path, steps with
duration, attempts and exec-receipt agent evidence, the worker branch's commits,
files and bounded diff as observed at review time (not sealed with the run), and
the land and cleanup commands. Hydra never runs them. The text projection shows
this first and offers each check log as a local `REF log`. Plan checks of a run
that is not terminal are `pending`; a step without deliverables reports
`inventory_state` and `readiness` `not_applicable`.

`workflow statistics-data` is `HYDRA_STATISTICS<TAB>4`. Schema 4 adds
`U<TAB>run<TAB>step<TAB>profile<TAB>executable-version<TAB>model<TAB>effort<TAB>tokens-in<TAB>tokens-cached<TAB>tokens-out<TAB>cost-microusd`
after a run's `S` rows, one per listed step with an exec receipt; `-` is unknown,
never zero. Readers accept schemas 2 to 4. CPU and memory are not collected.

## Process, install, and platform contracts

Startup and agent launch target `session:0.0`. Broadcast automatically selects only
a recognized shell pane; `--force` or `--pane` is required to target anything else.
An interactive head's `HYDRA_PROJECT_ID`, `HYDRA_HEAD_ID`, `HYDRA_INSTANCE_ID`,
`HYDRA_BRANCH`, `HYDRA_WORKTREE`, `HYDRA_STATE_DIR`, and `HYDRA_TASK_FILE` are
delivered through the tmux session environment and a per-instance launcher that
`session:0.0` runs; the launcher starts the agent by the absolute executable path
recorded in provenance and then hands the pane to the interactive shell. Bootstrap
exports and the agent launch are never typed into the shell or its history; only
configured startup and YAML pane commands, and an agent launch that must follow
them, remain typed keys. `hydra provenance` exposes the exact identity, paths, and
launcher.

Source and prefix installs provide `bin/hydra`, `lib/hydra/*.sh`, and an optional
qualified `hydra-tui`. Core shell operation requires POSIX `sh` and Git on supported
macOS and Linux systems. tmux 3.0 or newer is required only for interactive
terminal operations (interactive spawn and resume, attach, switch, broadcast,
dashboard, TUI, and workflow steps that request a terminal); headless heads, exec,
workflows, remote tasks, bootstrap, install, and doctor run without it. A tmux on
PATH that cannot report a version is unusable, never "old enough". The root README
lists supported tools; [T2/T3 qualification](T2_T3_QUALIFICATION.md) records the
reproducible execution-without-tmux cases.

## Fleet coordination (since v2.1.0)

[Fleet protocol 1](FLEET.md) is an optional C/OpenSSH coordinator. Its one-request
stdin JSON boundary negotiates capabilities before mutations, transports argv
without shell interpolation, and delegates head/workflow mutations to the local
shell CLI. Fleet's alias, package, and inert bundle stores are distinct from live
state v2. Lost mutation responses never cause automatic replay.

Install package schema 1 carries `bin/hydra`, `lib/hydra/*.sh`, the fleet helper
and licenses, bound by the package SHA-256, and installs only into one exact prefix
(default `~/.local/share/hydra/fleet/DIGEST`); an existing prefix is verified, never
overwritten, and the staged shell and handshake must report exactly the helper's
version. Since 2.9.0 an optional `platform` (`{"os":"linux"|"darwin","arch":
"x86_64"|"aarch64"}`) makes bootstrap refuse a different remote `uname` before the
package helper runs, and the installer refuses it again; packages without it still
install. Release helpers `hydra-fleet-X.Y.Z-linux-{x86_64,aarch64}` are static,
reproducibly built, and pinned in `release/fleet-assets.tsv`, installed as
`share/hydra/fleet-assets.tsv`; that local table is the only trust anchor for
provisioning downloads. Helpers from `--binary` or the local build are labelled
unpinned in the reviewed plan.

Guided remote setup (since 2.9.0) keeps private per-NAME state in
`$HYDRA_HOME/fleet/setup/NAME.json`; `status`, `list` and `remove` are reserved names.
Every `remote-setup*`/`remote-*` step envelope adds `data.setup_schema` 1,
`data.steps[]` (`{id, status, detail}`, plus `error{code,message}` while an
installer or sign-in that ran on a terminal is `failed` or `outcome_unknown`) and
`data.next` (`{step, argv, approval_sha256}` or null). `remote-setup-list` returns
`data.setups[]` of `{name, destination, ssh_config, status, remote_changed,
complete, next}` (`status` is the first unfinished step's status, `done`, or
`unreadable` with `error`), read without locks. `remote-setup-remove` deletes the
local record after a `remove` plan is approved (`{removed, name, destination,
record, summary, remote_left[], local_left[]}`); it never contacts the remote and
fails with `setup_busy` or `setup_complete`. A step whose SSH connection failed
adds OpenSSH's own reason as `data.ssh_error`. `remote-preflight` requirements are `{name, status, blocking,
detail}` with an optional `suggestion` (an example command; Hydra never runs it).
Plans are `remote-setup-plan` schema 1 approved by their SHA-256; exit statuses
are 0, 1, 3 (approval required), 4 (outcome unknown) and 128+n.

Host discovery (`fleet discover|qualify`) returns candidate schema 1:
`data.{candidate_schema_version, observed_at, required_capability, partial_failure,
revoked_count, candidates[]}` where a candidate carries `candidate_id` (`cand_` plus
hex of the exact target), `target`, `deduplication`, `sources[]` (`{kind, locator,
name, observed_at, age_seconds, freshness, labels}`, plus `scope` and `client` for
snapshot and live sources), `resolution`, `qualification`, `qualified_at` and
`status` (`discovered`, `resolution_failed`, `not_qualified`, `compatible`,
`qualification_failed` or `revoked`, the last with `revocation`). Inventories are
schema 1 (`{schema_version, observed_at, hosts[{name, target, labels}]}`) or schema
2 with a closed `source_snapshot` (`kind` `mdns|vpn|cloud-tags|config-management`,
`locator`, `scope`, `observed_at`, `freshness`, up to 100 `records`). Since 3.0,
`--source KIND[:LOCATOR]` (`ssh-config`, `ansible-inventory`, `dns-sd`; at most four,
each acquired once per invocation within `--timeout`, 1 MiB and 100 records) adds
`data.sources[]`: `{kind, locator, scope, client, source_id, status
acquired|partial|unavailable|revoked, reason, observed_at, bytes, record_count,
credentials_omitted, skipped[{name, reason}], cache {observed_at, age_seconds,
record_count, status}|null, diff {diff_schema_version 1, first_observation,
added_count, removed_count, changed_count, conflicting_count, conflicting[{name,
target, reason duplicate_name|duplicate_target|target_reassigned}], and with --diff
added[], removed[], changed[{name, previous_target, target, previous_labels,
labels}]}, revoked[{name, target, revocation}]}` and `data.diff_requested`. An
unavailable or revoked source fails the command with `source_unavailable` or
`revoked` while retaining every report. Each source's previous acquisition is a
private snapshot `{schema_version 1, kind "fleet-discovery-source-snapshot", source,
status, observed_at, records}` under `$HYDRA_HOME/fleet/discovery/sources/`, keyed by
the SHA-256 of `KIND:LOCATOR`; qualification progress binds `sources[{kind, locator,
records_sha256}]`. A `host_key_changed` qualification adds `error.host_key`
(`presented_fingerprint`, `key_type`, `known_hosts`, `known_hosts_line`,
`observation`, `observed_at`, `recorded[{fingerprint, source}]`, `acceptance`);
an enrollment apply row with that status adds `host_key {reviewed_fingerprint,
reviewed_source, presented_fingerprint, observation, acceptance,
presented_key_revoked?}`. Enrollment intent schema 2 and progress schema 1 are
unchanged; a row may now have status `revoked` with `revocation`.

Revocation (`fleet revoke SCOPE ID --reason TEXT`, since 3.0) writes one private
record `{schema_version 1, kind "fleet-revocation", scope candidate|source|
transport|package|project, id, requested_id?, reason, revoked_at, revoked_by {user,
uid, host}, superseded, effect}` at `$HYDRA_HOME/fleet/revocations/SCOPE/<sha256 of
id>.json` (0600 inside 0700 directories, never replaced; a repeat fails with
`already_revoked`). Identities are canonical: a candidate target becomes its
`cand_` ID, a source is `KIND:LOCATOR`, a transport is the alias name, a package its
64-hex digest, a project its absolute path. `superseded` retains the alias record
(with `target`, `principal`, `project`, `accepted_host_key`), the candidate target,
the source's cached snapshot summary, or the aliases mapped to a project.
`fleet-revoke-list` returns `data.{revocation_schema_version 1, count,
revocations[]}`. Blocked operations fail with code `revoked` and `data` holding the
record's `{scope, id, reason, revoked_at, revoked_by}`; `fleet list` reports a
revoked alias as a row with that error. Revocation records are decisions, not
remote state: they cancel nothing, uninstall nothing and never modify
`known_hosts` or an alias record.

Task package schema 1 binds an
exact Git bundle, selected inputs, work, destination, completion policy, and limits
for local preparation and validation. Task protocol 1 advertises `task-accept`
and `task-status`: receiver-owned receipts and pending launch intent are published
durably, and keys deduplicate across projects in one receiving-host state directory.
`task-start` adds an explicitly authorized detached owner, permanent launch claim,
and existing shell exec/workflow attempt identity. Loss of the owner is unknown,
never permission to replay. `task-cancel` records cancellation independently from
transport acknowledgment; `task-logs` returns bounded byte ranges from owner or
recorded worker streams with stable run/step/attempt selectors. Cancellation
confirmation is scoped to managed commands; missing owner evidence stays unknown.
`task-result` returns an immutable receiver-completion snapshot with checksummed
artifacts/evidence and an exact source/result Git bundle. Both endpoints validate
the result envelope before use; downloads never recapture changed worktrees.
A receiver advertises `tmux` only when a working tmux 3.x answers `tmux -V` and
always advertises `execution-headless`; a task requiring `tmux` on a receiver
without it is refused before acceptance with `capability_unavailable`, and a
compiled plan admits only `exec` plus `execution-headless` recipes
(`invalid_task_policy`). A workflow task step whose first submission is refused
with a final receiver code (`capability_unavailable`, `unmapped_project`,
`submission_conflict`, `missing_dependency`, `placement_mismatch`) records that
code in its `remote-response.json` and fails instead of waiting for
reconciliation; transport failures still wait. Alias records may bind an OpenSSH
client configuration (`remote add --ssh-config`, or a finished guided setup), and
recorded workflow destinations carry `ssh_config` and `principal` when bound.
Collection installs verified snapshots beneath the common Git directory and uses
compare-and-set direct refs under `refs/hydra/tasks/collection_ID/`. Repeated
collection preserves checkout/index/ordinary refs; changed private bindings fail.
`integrate task:collection_ID` consumes successful clean commits through the existing
local gate, approval, and promotion flow without restoring remote live state.
Fleet stdin rejects embedded NUL bytes
and input beyond its 8 MiB bound instead of accepting a truncated JSON prefix.

Automatic placement (since 3.0, [FLEET.md](FLEET.md#automatic-placement)) adds
to task protocol 1, additively: receivers advertise `task-withdraw`; a `submit`,
`start` or `resume` request may carry `assignment_generation` (integer
1..1000000), which acceptance records as `assignment_generation` (1 when absent)
and which a later request must match (`stale_generation`); the `withdraw`
operation moves an accepted task without a launch claim to the durable runtime
state `withdrawn` (refusing anything else with `execution_owned` or
`invalid_state`), after which start and resume fail with `withdrawn`; spec
capabilities `role.NAME` are recorded placement classes and `platform.OS-ARCH`
must equal the receiver's handshake platform. `fleet task submit --placement
auto|HOST [--separate-from TASK_ID]` and `fleet placement explain|replay|
withdraw TASK_ID`, `drain|undrain HOST` and `policy` are the submitter surface.
`fleet-placement` schema 1 is the digest-sealed record at
`$HYDRA_HOME/fleet/placements/TASK_ID/placement.json`: `{schema_version, task_id,
submission_key, mode: auto|explicit, prepared_spec_sha256, prepared_host, inputs
{project, work_kind, task_class, required_labels[], required_labels_key,
adapter_capabilities[], platform, disk_bytes_required}, policy {policy_version,
max_observation_age_seconds, max_queued, validator_reserve_slots}, generation,
assigned_host, reservation: none|accepted|refused|unknown|withdrawn,
bound_spec_sha256, generations[] {generation, host, reservation,
bound_spec_sha256, withdrawal: null|{state: requested|confirmed|unknown|refused,
code?, requested_at, confirmed_at?}, rounds[] {decided_at, mode, excluded_hosts[],
separation_hosts[], observations[], candidates[] {host, eligible, reasons[],
observation_age_seconds, score {capacity, reserved, queued, class_reserved,
load_milli}|null, estimates {schema_version: 1, queue_delay_seconds,
measured_duration_seconds, transfer_cost_bytes, used_in_ranking: false}, rank},
decision {host|null, reason: ranked|explicit|no_eligible_host, policy_version},
attempts[] {host, outcome: in_flight|accepted|refused|unknown|error, code?,
receiver_record, requested_at, spec_sha256, task_state?}}}, sha256}`. `fleet
placement replay` re-derives every automatic round's candidates and decision from
its recorded observations, excluded and separation hosts, `decided_at` and the
recorded policy and reports `identical: true` or `replay_mismatch`. Successful
`--placement` submissions add `data.placement {task_id, generation,
assigned_host, reservation, bound_spec_sha256, mode, record_directory}`. Result
collection refuses a snapshot whose receipt generation or host differs from the
record (`stale_generation`). The fence covers Hydra's records only: external side
effects need their own idempotency contract, and no deadline or lease expiry
authorizes duplicate execution. The policy file is
`$HYDRA_HOME/fleet/placement-policy.json` (closed schema, integer knobs; an invalid
file fails every automatic submission with `policy_invalid`).

## Durable state, trust, and execution boundaries

State v2 is project-scoped beneath `$HYDRA_HOME/state/v2`. Records are replaced
atomically under locks; malformed, linked, oversized, or partially written records
fail closed. Identity, branch, worktree, instance, and profile bindings are checked
on resume and collection. A missing owner, stale observation, timeout, or lost
transport is an unknown outcome and never authorizes replay. Release or cancellation
of a reservation requires confirmed termination and is scoped to the owning task.

Hydra creates its own state, lock, run, cache, and evidence entries beneath
`$HYDRA_HOME` and the repository's `<git-common-dir>/hydra` without group or other
write permission, whatever the caller's umask (such as Ubuntu's default `002`).
Readers still refuse group- or other-writable and foreign-owned state; the checks
are never relaxed. Worktrees, agents, hooks, tmux sessions, and the commands run by
exec, gate, and workflow steps keep the caller's umask; outputs a step writes into
its run record are made private when the step ends. `hydra doctor` reports writable
state. `hydra doctor --fix-permissions` removes group and other write from the
current user's own regular files and directories only, never follows symbolic
links, and reports foreign-owned or special entries without changing them.

Repository-controlled configuration is inert until `hydra init --trust` records an
approval. Trust covers regular files and symlink-safe paths under `.hydra`; linked,
special, or changed configuration invalidates the approval. Submitted workflows use
the restricted YAML subset, explicit argv, declared inputs/outputs, and exact
digests. Shell strings require both `allow_shell: true` and a current trust decision.

Headless agent profiles are literal, schema-versioned declarations. Prompt and resume
slots are bounded and cannot contain shell templates; an exact recorded session,
head, instance, worktree, and profile are required for resume. Provider completion
is an observation, not verification or approval. Remote task packages bind an exact
source commit, selected regular-file inputs, output declarations, capabilities, and
deadlines; checksums detect changes but do not replace SSH authentication.

Admission is enforced by the receiving host's shell authority under one lock. FIFO
queue deadlines, host/project limits, labels, disk floors, and retained unknown
reservations apply to exec, gates, heads, resume, workflows, and remote tasks. A
capacity snapshot is observational and never grants a slot; a reservation is not an
execution deduplication lock.

## Compatibility and release policy

The public surface includes CLI syntax, machine-readable JSON, durable state and
event schemas, installation layout, and documented shell behavior. Patch releases
fix defects, minor releases add compatible capabilities, and major releases remove
or replace incompatible contracts. After 2.0, versions are selected at release time
from compatibility impact. Public interfaces remain functional for at least one
minor-release window before removal, except where a security or integrity fix
requires immediate removal with migration guidance. Releases are cut only from the
exact qualified commit; local checks do not publish or grant release-write access.

## Head-associated planning proposals

`hydra workflow plan propose <draft.json> [--branch <head>]` atomically publishes a
strict, bounded JSON draft under the selected head's `planning/draft.json`. Without
`--branch`, the current Git branch identifies the head. Head and instance identity
from an agent launcher must match the current recorded owner. The head lock
serializes replacement; malformed input preserves the previous draft. A proposal
is durable input, not a validation, approval, or execution receipt.
Repeatable `--asset NAME=FILE` publishes at most 16 private asset copies (regular
UTF-8 text files of at most 64 KiB, no symlinks) in `planning/assets/` beside the
draft; each publication replaces the whole set, and a draft whose asset
references differ from the published files is refused with `asset_mismatch`.

`hydra workflow plan proposal <head>` is read-only and fails if the draft or policy
is absent. Explicit `--local-policy` writes the guided local policy: host
`local`; tools `sh`, `git`, `make` and `profile:<name>` for the head's recorded
profile when it is a plan ID other than `none` (otherwise no agent tool, noted on
a terminal's stderr only); effects `execute` and `worktree`; writes `@spawned:*`;
parallelism 1; 3600 seconds; 1 MiB of artifacts; four heads; a 1024 MiB free-space
floor; no retries or repairs. Success emits
`HYDRA_PLAN_PROPOSAL<TAB>1`, followed by
`P<TAB>absolute-draft-path<TAB>absolute-policy-path`, each newline terminated.
Paths containing tabs/newlines are refused. Native validation snapshots both files
and compiles with the draft's sibling `assets/` directory; changed draft, policy
or asset bytes invalidate the compiled revision and its exact-digest approval.

`hydra workflow plan proposal <head> --return <feedback>` records, under the head
lock, `planning/returned` (the SHA-256 of the current draft) and `planning/feedback`
(at most 4096 bytes). While the draft still has that digest, `proposal` (with or
without `--local-policy`) refuses, and so does a launch associated with the head.
A successful `propose` removes both files. The TUI types the feedback into the
agent's attached pane without submitting it.

`hydra workflow plan decision record <head> ...` writes, under the head lock,
one private `planning/decisions/<id>/` directory per decision (see the decision
record schema above); `decision list` and `decision show` are read-only and
create no planning state. A decision is a recorded reason, not a validation,
approval or execution receipt.

`hydra workflow plan expand <head> --run <run-id> --proposal <file>` admits,
under the head lock, one private round directory
`planning/expansions/<run-id>/round-N/` holding `proposal.json`,
`compiled.json` and, written last, `expansion.json`; directories are created
`0700` and files `0600` independent of the caller's umask. The round is built in
a scratch `.round.XXXXXX` directory and renamed into place only after every
check passed, so an interrupted admission leaves only scratch that no later
admission counts and nothing is replayed. Without `--proposal` the command is
read-only and creates no planning state. Executing a round through `hydra
workflow plan run round-N/compiled.json --accept <sha256>` records the run ID in
`round-N/run-id` (created exclusively, so a round never gets two runs; a link to
a run that was never published is cleared) and the record in the run's
`plan-expansion.json`. Admission is neither approval nor execution.

The private launch owner `hydra workflow plan --workspace-owner <sha256>
[<head-id> <instance-id>]` reads the compiled snapshot on stdin. The optional pair
binds the launch to the planning head instance; it is refused unless that instance
is current and its draft is not returned. The run records `planning-head`,
`planning-instance` and `planning-branch`. Its `run.created`, `approval.requested`,
`step.failed`, `step.recovery-required` and terminal run events then queue at most
one inbox note each (keyed by event and step under `planning-notices/`, at most 64
per run) for that branch only while it still names the same head and instance. A
queued note is not evidence that the agent read it. While open, the TUI also
submits the run receipt and those state changes into the attached planning pane.

The private attachment helper accepts an optional absolute tmux socket after the
head and instance IDs. Nested native clients carry their observed server selection
explicitly across the PTY boundary. Session, head and instance checks run against
that same server before attachment; closing the client preserves the session.
