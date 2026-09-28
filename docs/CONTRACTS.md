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
`collision`, `resource`, `gate`, `du`, and `snapshot` where their help documents
`--json`.

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
qualified `hydra-tui`. Core shell operation requires POSIX `sh`, Git, and tmux 3.0 or
newer on supported macOS and Linux systems. The root README lists supported tools.

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
`$HYDRA_HOME/fleet/setup/NAME.json`; `status` and `list` are reserved names.
Every `remote-setup*`/`remote-*` step envelope adds `data.setup_schema` 1,
`data.steps[]` (`{id, status, detail}`, plus `error{code,message}` while an
installer or sign-in that ran on a terminal is `failed` or `outcome_unknown`) and
`data.next` (`{step, argv, approval_sha256}` or null). `remote-setup-list` returns
`data.setups[]` of `{name, destination, status, complete, next}` (`status` is the
first unfinished step's status, `done`, or `unreadable` with `error`), read
without locks. `remote-preflight` requirements are `{name, status, blocking,
detail}` with an optional `suggestion` (an example command; Hydra never runs it).
Plans are `remote-setup-plan` schema 1 approved by their SHA-256; exit statuses
are 0, 1, 3 (approval required), 4 (outcome unknown) and 128+n.

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
Collection installs verified snapshots beneath the common Git directory and uses
compare-and-set direct refs under `refs/hydra/tasks/collection_ID/`. Repeated
collection preserves checkout/index/ordinary refs; changed private bindings fail.
`integrate task:collection_ID` consumes successful clean commits through the existing
local gate, approval, and promotion flow without restoring remote live state.
Fleet stdin rejects embedded NUL bytes
and input beyond its 8 MiB bound instead of accepting a truncated JSON prefix.

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
