# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `make visual-review` installs this checkout into a private prefix, drives the
  installed native TUI over a pseudo-terminal through the planning, running,
  finished and failed, waiting-for-input, details, recovery, workflows,
  statistics, attention, review, identity, Fleet and 2.9.0 Hosts screens at
  80x24 and 140x40 in the dark, light and no-colour themes, keeps every state's
  raw bytes, renders each through the product's own terminal interpreter
  (`build/termviz-cells`, from `src/termviz/tools/cells.c`) with exact colours
  to HTML and SVG, and writes one contact sheet, `build/visual-review/index.html`,
  with before/after columns when `VISUAL_BASELINE=dir` names an earlier output.
  `make test-visual-lint` fails on text clipped at the edge or cut at the end of
  its area, overlapping panels, a missing focus indicator, colours outside the
  theme's declared palette or a status word in a contradicting tone, raw
  identifiers in headings or more than three outside detail and diagnostic
  screens, repeated keyboard hints and contrast below 4.5 (3.0 for muted text
  and borders); findings in `scripts/visual_review/lint-known.tsv` are reported
  as known. See `docs/VISUAL-REVIEW.md`.
- `make test-u10-loopback` (gated in CI) onboards a fresh throwaway account
  behind a private loopback sshd through the control centre's Hosts tab over a
  PTY: typed-`yes` host-key trust, preflight, provisioning from this checkout's
  helper, the deterministic agent fixture installed as `codex` by the real
  recipe machinery from a private `agent-recipes.json`, a sign-in killed
  mid-way and resumed without being repeated, a sign-in whose verification
  fails and is retried, a real fixture-agent task launched on the host and found
  again after reconnecting, a second setup of the same account reusing the pin,
  a host whose sshd hides `git`, unrelated files byte-identical and no local
  credential copied. The case prints SKIP without root (or passwordless sudo)
  and sshd. Evidence lands in `build/u10-loopback-evidence/`.
- The control centre passes `--binary FILE` to `hydra remote setup` when
  `HYDRA_REMOTE_SETUP_BINARY` names a helper, so a development build can
  provision from the Hosts tab with the unpinned helper the CLI's
  `asset_unavailable` recovery asks for; the plan shows it as not a release
  build, and only the provisioning command naming that same file is rerun.
- The control centre's Hosts tab opens an onboarding operator view with `O`:
  discovered and qualified candidates with filters by state (new, qualified,
  failed, conflicting, revoked, reviewed, unknown, enrolled), source, platform,
  label and text; a candidate detail with provenance, observation age,
  qualification result, presented and recorded fingerprints on a key change and
  revocation; the source changes of `discover --diff`; and the reviewed intent
  with per-host apply status, phase and operation ID. Every action is a
  `hydra fleet` child: `Q` runs `qualify --progress` and `r` runs `enroll review`
  into the private `$HYDRA_HOME/fleet/onboarding/`, and `a` shows the intent
  digest, project, package digest, prefix and the exact `fleet enroll apply
  --input INTENT --confirm DIGEST` command, which runs once only after the
  operator types at least 12 leading digest characters. Revoked, failed and
  conflicting rows are inspect-only; nothing is retried automatically, an
  interrupted apply is followed by a read-only status read, and the view never
  bootstraps a host or writes state. Qualified with recorded fixtures only
  (`tests/test_onboarding_view.sh`, `test-tui-onboarding`, `pty-onboarding`);
  no live multi-host onboarding was observed.
- `hydra fleet enroll status --input INTENT [--json]` reads an intent's reviewed
  hosts with the progress `enroll apply` recorded for its digest
  (`fleet-enrollment-status`, `enrollment_status_schema` 1): never-attempted hosts
  are `not_started`, recorded rows keep their status (including
  `outcome_unknown`) and phase, and bindings revoked since review read `revoked`.
  It connects nowhere and writes nothing. `fleet enroll` now accepts `--json`.
- `hydra fleet discover|qualify --source KIND[:LOCATOR]` acquires one explicitly
  selected live inventory and feeds the existing candidate importer: `ssh-config`
  scans the literal `Host` names of `~/.ssh/config` (or a file) and its literal
  includes, `ansible-inventory:FILE` reads an INI hosts file (groups become labels,
  secret-looking variables are counted and dropped, `vars`/`children` sections are
  ignored), and `dns-sd[:DOMAIN]` runs one terminating `avahi-browse` for
  `_ssh._tcp` and reports `unavailable` without the client. Each source runs once
  per invocation within `--timeout`, 1 MiB and 100 records, records its account or
  network scope and client, and is diffed against its previous private snapshot
  (`$HYDRA_HOME/fleet/discovery/sources/`): cache age and added, removed, changed
  and conflicting records are reported before selection (`--diff` lists them).
  Stale caches, missing files or clients, timeouts, partial output, identity
  conflicts and revoked identities are reported with provenance and never widen
  the selection; `qualify` over a source requires `--select`.
- `hydra fleet revoke candidate|source|transport|package|project ID --reason TEXT`
  records a final local revocation (private, with who/when/reason and the
  superseded binding) and `hydra fleet revoke list [--scope SCOPE]` shows them.
  Revoked identities show as `revoked` in discovery, re-import and resumed
  qualification, are refused by `fleet enroll review`, invalidate pending `fleet
  enroll apply` rows before any connection, and block every operation, task
  submission, alias reuse, setup and enrollment publication for a revoked alias;
  a revoked package or project is refused by bootstrap, review and `--project`
  operations. A changed host key now reports the presented and the recorded
  fingerprints with where each was observed (`qualify`, `enroll apply`, `remote
  trust-key`); acceptance stays with the operator's `ssh-keygen -R` and a fresh
  review. Revocation never cancels a task, uninstalls a package, deletes remote
  state or edits `known_hosts`. Help and bash, zsh and fish completions include
  the new commands and options.
- Automatic placement of new remote tasks across eligible hosts: `hydra fleet
  task submit --placement auto --input PACKAGE --key KEY [--separate-from
  TASK_ID]` observes every registered alias (handshake and admission status),
  excludes drained, offline, incompatible, unmapped, wrong-platform,
  label-unsupported, stale, queue-full or disk-starved hosts and hosts that must
  stay separate from a named task, ranks the rest by capacity-normalized load
  with comparable-class, queue-depth and alias-order tie-breaks, and dispatches
  to the first host; a definitive receiver refusal moves on to the next host and
  an ambiguous dispatch stays assigned for reconciliation. `--placement HOST`
  records an explicit pin; submissions without `--placement` are unchanged.
- `hydra fleet placement explain TASK_ID` prints the digest-sealed
  `fleet-placement` schema 1 record (observations with ages, candidates with
  reasons, scores and ranks, policy version, every dispatch attempt and its
  reservation outcome); `replay` re-derives the decision from the recorded
  observations and must match; `policy` shows the effective knobs
  (`max_observation_age_seconds`, `max_queued`, `validator_reserve_slots`, all
  with conservative defaults in `fleet/placement-policy.json`); `drain|undrain
  HOST` keeps new automatic work off a host.
- Rebalancing of accepted, not-started work: `hydra fleet placement withdraw
  TASK_ID` asks the current receiver for a durable withdrawal (new `task-withdraw`
  capability and `withdrawn` task state) and a later `--placement auto`
  submission opens the next assignment generation elsewhere; a lost withdrawal
  response blocks transfer until confirmed, and running or claimed work is never
  moved. Receivers record `assignment_generation` at acceptance and reject stale
  starts, resumes and re-submissions (`stale_generation`); result collection
  admits only the current generation and host.
- Spec capabilities `role.producer`/`role.validator` (placement class) and
  `platform.OS-ARCH` (checked by the receiver against its own platform).
  Help and bash, zsh and fish completions cover the new commands and options.
- `hydra workflow plan evaluate <left> <right> [--estimates FILE]` compares two
  compiled alternatives in three separate concerns: decomposition quality
  (required and unconnected nodes, validation effort, declared transfers, declared
  rework budgets), fixed-graph scheduling (depth, width, critical path, time and
  cost to a verified result, hard budgets copied from the envelope) and host
  placement (declared only, never assessed). Estimate-derived figures are ranges
  or `null`; an estimate above a hard budget marks a conflict and never relaxes
  the budget; no preference claims semantic adequacy or global optimality.
- `hydra workflow plan observed <run-id>` prints an estimates document from a
  run's recorded step durations (one-second resolution ranges, no cost rows).
- `hydra workflow plan decision record|list|show` keeps, under the head's private
  planning state, which compiled alternative was selected and which were
  rejected, each reason, the compiler, analysis and policy versions and the exact
  analysis inputs; `show --json` replays every comparison from the recorded
  inputs and reports whether it matches byte for byte. Selecting a plan whose
  estimate exceeds its own hard budget is refused.
- `make test-plan-eval` (`scripts/plan-eval.py`) revives the archived finite
  planner pilot from Git history without editing it, adds eight held-out cases
  with sixteen incorrect artifacts, and reports planning, validation and
  scheduling effects separately: candidate acceptance per arm and interface,
  false acceptances and rejections, executed verified outcomes, elapsed times,
  repeated work, unknown cost and human corrections, a one-run scheduling
  calibration and the determinism check. Every declared corruption must be
  rejected for the target to pass. Help text and completions cover the new
  planning commands.

- Reproducible T2/T3 qualification with tmux absent on the execution hosts:
  `make test-t2-headless` (remote command and headless-adapter tasks, reconnect,
  exact collection and dependent consumption, the local compiled plan, duplicate
  submission, lost start, owner death, cancellation, bounded logs and retained
  reservations) and `make test-t3-distributed` (the two-host fan-out, cross-host
  validation, join, composition and final check over two tmux-less loopback
  receivers, with lost acknowledgment, coordinator restart and stale results over
  the controlled transport and the same scenario over two private loopback OpenSSH
  daemons; `make test-t3-distributed-ssh` prints SKIP without sshd). The receiver
  side runs with a private PATH without tmux and a tmux stand-in that refuses
  terminal operations loudly (`headless_receiver_path` in `tests/headless_path.sh`);
  the coordinator keeps its own PATH. Both controlled cases run in `make test-fleet`,
  and CI requires the loopback SSH pass line next to `test-remote-setup-ssh`.
  `docs/T2_T3_QUALIFICATION.md` records the commands, evidence, the tmux
  dependency audit and the remaining operator step on a real host.
- `hydra remote add NAME DEST --ssh-config /absolute/file` binds an alias to one
  OpenSSH client configuration, as a finished guided setup records.

- `make test-usability` runs goal-driven screen-only trials beside the scripted
  journeys: a deterministic rule-based evaluator (`scripts/usability/policy.py`,
  no model) is given only a goal in plain words and each screen, reads the
  controls the screen offers (footer and inline key hints, "Press n to ..."
  sentences, prompts, the tab bar once the screen says how tabs are reached) and
  presses the one whose label matches the goal's vocabulary, within a step bound.
  Six goals (`start-task`, `why-stopped`, `find-attention`, `leave-return`,
  `remove-stopped`, `approve-plan`) are verified independently against Git, tmux
  and Hydra state, and each gets a discoverability score (steps, dead ends, help
  opened, controls read, fit, completed). A control whose label matched the goal
  but did nothing fails its check; nothing is special-cased.
- Comprehension trials (`--trial comprehension`) reach each concept's entry point
  or empty state (head, shell TUI head, workflow, plan versus approved workflow,
  workflow run, gate and check, coordination, Fleet and host, statistics, live
  session versus completed work, interruption, removal) and assert that the
  visible text states purpose, when it helps, a concrete example and the next
  action in plain language within 80x24 and 140x40, that consequences precede
  approval, interruption and removal, and that mentioned commands exist in
  `hydra help`. `--trial NAME` selects one trial; `USABILITY_ARGS` passes options
  through make. The runner is now the package `scripts/usability/`; the report
  lists the live-provider, visual, human first-use and remote-onboarding gates
  as explicitly unexecuted.

- `hydra pool create|add|seal|claim|complete|cancel|status|list|run`: file-backed
  task pools under `$HYDRA_HOME/state/v2/projects/<project>/pools/<pool>` with
  private modes. `create` seals the bounds (tasks, outstanding claims,
  expansions, definition bytes, repair rounds, lease, default retry) in a
  digest-bound manifest; `add` appends workflow or exec tasks to the open
  expansion and `seal` freezes its membership before any task can be claimed.
  Claims are unique under the pool lock, carry the lock protocol's owner record
  and a token, and never exceed the outstanding bound. A stale claim (expired
  lease or dead owner) is reconciled from recorded evidence only: a terminal
  workflow run decides, an idempotent task with budget is re-queued, and any
  other unknown outcome becomes `recovery-required` for an explicit
  `complete --by` decision (`retry` consumes a repair round). `run` is a worker
  loop around the public `hydra workflow run` and `hydra exec` commands, so
  admission is unchanged and no second scheduler exists; `cancel` stops queued
  tasks and asks claimed work to stop through those commands.
- `hydra schedule add|list|remove|run-now|status`: host-timer schedules that
  print the exact crontab line, systemd user timer/service units and launchd
  plist for you to install; Hydra never installs a daemon. Every firing is
  `hydra schedule run-now <name>`, whose dedup token is the current slot, so a
  double cron fire, a timer plus a manual run or competing firings submit once.
  Submissions are a workflow run in the firing process, one sealed pool task,
  or the pool worker loop. Missed slots are skipped by default or caught up
  within `--catch-up-limit`; `--reboot-recovery` opts into `@reboot`,
  `Persistent=true` and `RunAtLoad` entries that start a new firing after a
  restart without replaying in-flight work. Help, bash/zsh/fish completions,
  `--json` envelopes and `docs/CONTRACTS.md` schemas cover both commands.

- One workspace across local and remote execution (U11). `hydra tui` now
  observes registered remote hosts beside the local project: remote heads
  (`host/branch`) and receiver-owned remote tasks (`task ID`) are rows of the
  same Work and Overview lists as local heads, with a `WHERE` column from 95
  columns, the host in every summary and detail, and the host's connection and
  observation age (`fresh, 3s old`, `stale, 300s old`, `unknown`) wherever a
  remote row is described. `L` cycles the location filter (all, local, remote,
  then each host) and `hydra tui --location local|remote|HOST` opens with one;
  Esc shows all work again. Details, attention (`I`) and review (`r`) use the
  same navigation and keys for remote work; a remote task's Details shows its
  receiver state, waiting reason, next action, result and cancellation, and
  `Y`/`N`/`R`/`X` on a selected task row dispatch its decisions through
  `hydra fleet task` after a typed confirmation, only from a fresh observation.
  `a` on a remote head attaches through `hydra fleet attach` in the workspace
  with the same leave (`Ctrl-B Tab`), close (`Ctrl-B x`), reconnect (`Ctrl-B r`)
  and resize behaviour as a local pane; `Ctrl-B [` forwards the host tmux's
  default prefix and `[` so the remote pane scrolls the host's own history
  (`q` or `Ctrl-B ]` leaves it; a host tmux with another prefix receives the
  keys instead, which Hydra cannot verify). `x` on a remote head interrupts
  its agent through `hydra fleet cancel` after a confirmation that names the
  host and branch; `:` offers `interrupt remote head`. The Hosts tab lists the
  observed hosts with their connection, head count and observation age before
  the unfinished setups; Enter follows a host's work (the Work list filtered to
  it) and `r` asks for a fresh observation of it. The Overview gains a REMOTE
  card (remote heads and tasks, hosts, disconnected hosts) and, when filtered
  to remote work, a hosts panel; the header counts local and remote work
  separately.
- Remote observations never authorize an action or make work look finished:
  a host that stops answering is shown as `disconnected`, keeps its heads and
  tasks from the last observation as retained rows (status `disconnected`,
  "retained from an observation Ns old; host disconnected") and refuses attach,
  interrupt and task controls until it answers again; a remote observation that
  fails altogether keeps every remote row and shows `Remote: STALE, last good
  observation Ns ago` with the reason, every remote status becomes `stale`, and
  `r` retries explicitly. A host with no observation is `unknown`, never live.
  Remote liveness stays unobserved: a remote head shows its desired state only
  while its observation is fresh and is never counted as running.
- `hydra tui --remote-data` (internal) answers with the fleet visual stream,
  with an empty `HYDRA_FLEET_TUI 3` stream when no remote host is registered,
  and with a `fleet-unavailable` recovery finding when hosts are registered but
  the `hydra-fleet` helper is not built; `hydra tui --attention-data` merges
  `workflow attention-data` with `fleet attention-data` into one
  `HYDRA_ATTENTION 3` stream (a failed producer marks it partial).
  `hydra-tui --headless-fixture FILE --remote-fixture FILE` composes a local
  and a remote fixture the way the live adapters do.

- `make test-acceptance` runs the installed-build acceptance for the roadmap
  items whose source has shipped but whose acceptance had to be observed by hand
  (U1, U2, U3, U6, U7, U8, U9, U12, U13, U15, U17). The stdlib package
  `scripts/acceptance/` installs a fresh prefix and reuses the usability runner's
  journey and PTY observer helpers; each item's Acceptance sentences are checks,
  and the HTML and JSON report is an acceptance matrix (item, bullet, check,
  result, evidence) with retained snapshots, styled screens re-derived from the
  raw bytes, `/proc` and Git/tmux inspections and PTY recordings. It covers
  byte-identical repositories across open/configure/reopen, bootstrap-free
  transcripts, scrollback and shell history at 80 and 140 columns with the agent's
  `/proc/<pid>/environ` checked, conversations from an uninitialized repository
  from the UI and from an interactive `hydra spawn`, measured flicker (raw PTY
  bytes idle for 30 s, streaming, polling, pane switches and resizes replayed
  through a bounded VT emulator: full clears, identical-cell repaints, blinks,
  cursor moves), Details and Recovery wording for running, stopped and
  terminal-gone heads, hinted keys delivered as real key events, empty,
  single-agent and blocked multi-agent overview and coordination, in-app removal
  including a partial bulk action, Statistics round trips and the
  conversation-to-plan flow with exact revision and digest. `docs/ACCEPTANCE.md`
  documents the cases, thresholds and the gates that still need a human; the
  roadmap's delivery boundary table cites each item's automated acceptance.
- The native PTY observer (`test-tui-pty`) writes a timestamped read index
  (`terminal.index`) beside the flushed raw stream so phases of a recording can
  be replayed and styled snapshots re-derived.
- `make perf-matrix` runs the item 10 measurement matrix over the existing I1
  fixture (`scripts/perf-matrix.py`, standard-library Python): 1/10/50 real
  worktrees with deterministic stand-in agents across quiescent, one-changing,
  all-changing and sustained-output cases, one or three attached native clients,
  local interactive and local headless workers, and preview closed or open.
  Cells record idle cost per role (CPU, wakeups, memory, descriptors), helper
  launches counted and attributed by PATH shims, scheduled interaction latency
  (PTY frame completion, never input-to-pixel), selected-head update latency,
  per-worktree change locality, output pressure, startup and result retention,
  with unsupported cells and unknown counters kept explicit. The evidence bundle
  (`hydra-perf-matrix/1`) stores revisions, hashes, seeds, tool versions, clock
  calibration, observer overhead and raw samples; `--evidence-dir` publishes a
  compact gzipped copy (`bundle.json.gz`), a Markdown summary, provisional
  budgets and gzipped raw samples within a byte budget. `--fixture-env KEY=VALUE`
  runs a fixture under a declared non-default configuration.
- `make perf-attention` (`scripts/perf_matrix_attention.py`, also
  `scripts/bench-attention.py --local-corpus`) scores `hydra workflow attention`
  against thirteen annotated local cases (blocked/approval, question, working,
  done-but-unseen, idle, unknown, failed step), reporting false positives and
  missed signals separately.
- `make test-perf-matrix` runs the unit checks, the attention corpus, the
  smallest interactive and headless cells and the bundle schema validation.
- `scripts/perf_matrix_merge.py` joins several runner invocations (a retried
  fixture, the 50-head cells run on their own) into one validated bundle and
  summary; fixtures keep their errors, cells carry the run label, and cells
  measured under a non-default configuration are flagged and left out of the
  proposed budgets.
- First Linux x86_64 container baseline under
  `docs/evidence/performance/2026-10-17-linux-x86_64/` (one trial, shared 4-core
  container, load recorded per fixture): 45 measured cells covering 1 and 10
  worktrees in all four cases with one and three clients, interactive and
  headless, preview closed and open, and 50 interactive worktrees quiescent and
  one-changing; Linux-container provisional (not frozen) idle, tail-latency,
  memory, recovery and locality budgets. The 10-head headless sustained-output
  cells and the 50-head headless cells could not be measured and are recorded
  as findings (concurrent supervised runs ending `observation_unavailable`; the
  native client never receiving a first snapshot at 50 headless heads); remote
  headless and macOS cells are recorded as unmeasured.

### Changed

- Recovery rows say what a finding offers (`files kept`, `safe to clear`,
  `cleanup preview`, `review first`) instead of its raw kind, and the finding's
  kind, source path and confidence sit under a DIAGNOSTICS heading after the
  plain-language account.
- Details explain a stopped head in plain words: a head whose agent reported
  its exit (or failure) and whose terminal is closed says so and that the
  worktree's files are kept with `a` restarting the terminal, as the
  terminal-gone account already did. Both accounts are two rows and the NEXT
  group is two rows, so the account and every action fit 80x24 (the
  terminal-gone Details previously pushed `x remove` below the panel).
- The narrow (under 100 columns) Workspace plan footers name `Tab pane`, the
  hinted way back to the agent pane after Ctrl-B Tab leaves its input.
- tmux is documented as required only for interactive terminal operations; headless
  heads, exec, workflows, remote tasks, bootstrap, install and doctor run without it.
- A workflow task step whose first submission a receiver refuses before acceptance
  (`capability_unavailable`, `unmapped_project`, `submission_conflict`,
  `missing_dependency`, `placement_mismatch`) now records that code in
  `remote-response.json` and fails, instead of reporting `waiting-remote` for a
  dispatch that can never be reconciled; transport failures still wait.

- Explanations where users meet each feature: Work's empty state gives an example
  of two heads; Workflows wraps its explanation at 80 columns and names the plan
  review (E) as the usual way a run starts; Overview's run panel and the
  statistics empty state wrap instead of clipping; Attention's empty state says
  what the view lists (approval requests, failed checks, results to review,
  stopped terminals) and what Enter and r do; the plan pane says a proposal runs
  nothing until validated and its exact revision approved; Coordination says when
  it matters with an example and no longer clips its last line; Hosts says when a
  remote machine helps; Details says `Session` is the live terminal and `Reported`
  the outcome the agent declares; the attached status row says leaving or closing
  the view keeps the agent running. The shell TUI's empty list explains a head the
  same way.

- `hydra fleet tui` remains as a compatibility entry: it opens the same control
  centre filtered to remote work (`L` or Esc widens it) and reads the fleet
  helper's own `tui-visual-data` and `attention-data` streams. It no longer has
  its own tab order, its own Work list (`REMOTE HEADS / DESIRED`), a separate
  `REMOTE TASKS` panel or a separate keymap (`c` interrupt): remote heads and
  tasks use the one Work list, `x` interrupts, `c` is coordination everywhere.
  Host statistics (`D`) are unchanged.
- The I1 stand-in worker accepts an `interval_ns` override per phase and waits a
  bounded 120 s for a slow supervised sink before retaining its final result,
  recording the wait; `phase_proof` validates a named phase; the native PTY
  observer honours `HYDRA_ITEM10_PATH_PREFIX`; Linux process samples add
  reaped-children CPU, thread and context-switch counters.

### Fixed

- `check_tmux_version` accepted a tmux on PATH whose `tmux -V` failed or printed no
  parsable version, because the numeric comparison error was taken as "new enough";
  such a tmux is now reported as unusable.
- Workflow task dispatch ignored an alias's recorded `ssh_config` and `principal`
  (honoured by `hydra fleet task` and `fleet list`), so a setup-created alias could
  not reach its host through its own client configuration; recorded destinations
  now carry both when bound, and existing records without them stay valid.

- The bounded native-helper runner (`hydra snapshot --native` and the protocol
  and version probes before it) no longer lets part of a timed-out helper's
  subtree outlive the call. It signalled the helper's descendants from one
  process-table walk, so a descendant that had already been reparented, or was
  forked after the walk, was never signalled, and the parent stopped the
  watchdog as soon as the helper itself exited, before its KILL pass. The helper
  now runs in its own session (`setsid` where available) so a timeout signals
  the whole subtree as one process group, with the process-table walk kept as the
  fallback; after a timeout the call waits for the TERM, then KILL pass to finish
  and until nothing of the subtree still runs. Exit codes, the `124` timeout
  status and the named fallback reasons are unchanged.
- `make test-parity` and `make test-tui-pty` no longer fail on a host whose init
  reaps orphans on a schedule instead of at once (such as a container's PID 1):
  the cases that check a terminated owner's descendants are gone treated an
  already-dead descendant that still awaited init as alive. They now allow a
  bounded reaping grace, and the parity case also covers a helper whose
  descendant was orphaned before the timeout.

## [2.9.0] - 2026-09-28

### Added

- `hydra remote setup NAME [USER@]HOST` guides a fresh SSH account to a verified
  fleet alias and resumes from private state when rerun; `hydra remote setup status
  NAME` shows progress, and each step has its own command (`trust-key`, `preflight`,
  `provision`, `agents`, `install-agent`, `sign-in`). Every change shows a plan and
  needs approval: interactive `y` (typed `yes` for a host key), or with `--json`, CI
  or no terminal an `approval_required` result (exit 3) naming the exact approving
  command. `trust-key` never accepts a changed or ambiguous host key and only appends
  the approved line to known_hosts; `preflight` reports platform, tools and existing
  installs read-only and without sudo. macOS remotes pass preflight with a
  warning when an unpinned helper will be installed (`--binary FILE`, or this
  host's own helper for exactly the same platform); other platforms fail with
  `platform_unsupported`. Help and bash, zsh and fish completions include the new
  commands.
- The control centre adds remote hosts: the Hosts tab (`H`, now also in local
  work) lists unfinished setups (from `hydra remote setup list --json`) and `A`
  starts one from an SSH destination. Hydra runs the same `hydra remote setup`
  commands, shows each step, requirement (with its suggested fix) and plan, needs
  a typed `yes` for a new host key and `y` for other plans, pauses on a missing or
  pre-3.0 tmux (heads cannot run there; Enter continues anyway) while other
  preflight warnings only show in the step list, and never offers to
  accept a changed key. After provisioning it shows the agent inventory so you can
  pick agents to install (after reviewing the installer plan) or sign in to; both
  run on your terminal, and a failure is explained from the error the CLI recorded.
  While a step runs it shows what it is doing and for how long (`Checking the
  host key… 12s`); Esc or `c` cancels it (Hydra stops the step's processes and
  shows the recorded setup status, so continuing later is safe) and `b` keeps it
  running in the background. The form's SSH config file field accepts `~/...`
  (expanded to your home directory; nothing else is expanded). On Hosts, `e`
  edits an unfinished setup that has not changed the remote (the form opens
  prefilled; the same name replaces the old record after you approve its
  removal) and `x` removes a setup's record after showing what stays on the
  remote. A name that already has an unfinished setup is caught in the form,
  which offers to continue, edit or remove that setup instead.
  Below 100 columns the tab bar uses short labels (`Coord`, `Flows`, `Stats`) so
  every tab, Hosts included, stays visible at 80 columns.
- `hydra remote setup list [--json]` lists every setup with its destination,
  status and next step, read-only. `setup status NAME --json` reports a failed
  installer or sign-in's recorded `steps[].error{code,message}`, and preflight
  requirements gain an additive `suggestion` (an example install command on Linux)
  beside `detail`. `hydra remote setup remove NAME [--approve PLAN_SHA256]`
  deletes a setup's local record and lock so a setup bound to the wrong
  destination or SSH config can start again (`setup_binding_changed` now names
  it). It never connects to the remote: its plan, approved like any other, lists
  what earlier steps left there (Hydra install, agents, sign-ins) and in
  known_hosts, none of which is undone; it fails with `setup_busy` while a step
  runs and with `setup_complete` for a finished setup whose alias is published.
  `setup list` rows gain `ssh_config` and `remote_changed`. `status`, `list` and
  `remove` are reserved setup names.
- `hydra agent locate [--json] [--record EXECUTABLE PATH] [--forget EXECUTABLE]`
  finds agents outside PATH (`~/.local/bin`, `~/.opencode/bin`, nvm, mise, ...)
  as `agent-inventory` schema 1 and records a private per-executable location.
  Resolution is PATH first, then a valid record, in both the shell and native
  resolvers, so `spawn`, `agent list`, headless `exec --profile` and `agent
  probe` find installer-placed agents on hosts whose non-interactive PATH omits
  them. Records must be absolute, named after the executable, owned by you or
  root and not group- or world-writable. Help and completions include it.
- `hydra remote agents NAME [--record EXECUTABLE=/abs/path]` shows each agent's
  status on the remote (receiver inventory, else the preflight scan), records
  off-PATH locations and selects agents to install or sign in (interactively, or
  by running the listed `install-agent`/`sign-in` commands). `hydra remote
  install-agent NAME --agent A` runs the provider's own official installer
  (built-in recipes for claude, cursor, opencode, codex, agy and pi, each checked
  against the provider's docs on 2026-09-28) over `ssh -t` as your user, only after
  approval of a plan binding the exact command, which runs the installer under
  `umask 022` so a group-writable remote umask cannot leave an agent executable
  that Hydra then refuses; no sudo, and Hydra adds no symlinks or PATH changes. It checks prerequisites first, verifies the result by `--version`
  and the help probe, offers to record an off-PATH location, and reconciles an
  interrupted install by inventory instead of re-running it. A private
  `$HYDRA_HOME/fleet/agent-recipes.json` can replace recipes (plans say
  `source: local`); recipes never come from the remote. `hydra remote sign-in NAME
  --agent A` runs the provider's sign-in with the resolved absolute executable and
  verifies it by the provider's documented status command or credential file, else
  asks you to confirm; failures are recorded and can be retried, and credentials
  are never copied.
- Fleet receivers advertise `agent-inventory` (read-only; also a headless probe
  of one candidate) and `agent-locate-record`, and the handshake additively
  reports `platform: {os, arch}`.
- `hydra remote provision NAME` installs Hydra into a private pinned prefix
  (`~/.local/share/hydra/fleet/DIGEST`) on the remote, never changing PATH or
  other installs. The plan names the platform, exact version, helper digest and
  whether it is pinned (release asset) or unpinned (`--binary FILE`, or this
  host's helper for an identical platform). Release assets are downloaded with
  `curl -fL`, checked against the digests shipped in `fleet-assets.tsv` and
  cached privately; a mismatching download is discarded. A same-version install
  or an identical pin is reused without changes, and an interrupted install is
  reconciled on rerun instead of repeated, rebuilding a deleted local package
  when this Hydra reproduces its recorded digest.
- Releases carry static Linux `hydra-fleet` helpers for x86_64 and aarch64,
  reproduced by CI (`make build-fleet-static`) and listed in `SHA256SUMS`.
- `hydra fleet package --platform OS-ARCH` binds a package to a platform and
  checks the helper's executable header; packages can also be built from an
  installed prefix. Bootstrap refuses a remote with a different `uname` before
  running the package helper (`platform_mismatch`).
- `install.sh` installs `share/licenses/hydra/` and
  `share/hydra/fleet-assets.tsv`; `uninstall.sh` removes them.
- `hydra doctor` reports group- or other-writable Hydra state in `$HYDRA_HOME`
  and the repository's `.git/hydra`, and `hydra doctor --fix-permissions` repairs
  it: it removes group and other write from your own regular files and
  directories, never follows symbolic links, and only reports entries owned by
  someone else or that are not files or directories. Help and bash, zsh and fish
  completions include it.
- `make test-umask` runs the fast checks, a umask state acceptance test and the
  DAG fleet cases under umask `002`; CI runs it with the shell suite on Ubuntu.

### Changed

- The fleet installer requires the packaged `bin/hydra` and its handshake to
  report exactly the helper's own version instead of any 2.x release.
- Test suite (contributors): `make test-one T=<name>` runs one `tests/test_<name>.sh`
  the way `make test` does. Shell cases, including Fleet shell cases, take native
  binaries only from `BUILD_DIR` and fail with a clear message instead of using a
  stale `./build`. Each case gets private `TMPDIR`/`TMUX_TMPDIR`, runs detached
  from any caller's tmux (so checks no longer skip inside tmux), and leaves no
  temporary files or tmux sockets behind. Shell cases start longest-first, the
  dashboard case runs in parallel, fixed sleeps that raced asynchronous work now
  poll, and the loopback-SSH enrollment and accepted-run retention tests run in CI.

### Fixed

- Hydra creates its own state, locks, workflow runs, event archives, exec and
  gate evidence, caches and the repository's `.git/hydra` records without group
  or other write permission, whatever your umask. Under a group-writable umask
  such as Ubuntu's default `002`, workflow `steps/` directories, run records and
  other entries were group-writable, so Hydra's own readers refused them: run
  statistics and remote transport metrics were reported as unknown, and a
  pre-created home was refused by admission. The ownership checks are unchanged.
  Worktrees, agents, hooks, tmux sessions and the commands run by exec, gate and
  workflow steps keep following your umask; outputs a step writes into its run
  record are made private when the step ends.
- Planning launch and workspace workflow controls no longer pass their private
  umask to the heads, agents and step commands of the runs they start.
- `hydra fleet auth login` returns after the provider's sign-in instead of
  replacing Hydra with `ssh`, reports its exit status, and runs the remote
  agent by its resolved absolute path.
- Headless runs execute the exact executable the profile probe resolved.
- An `exec` worker's timeout watchdog no longer outlives a worker that was
  killed: it stopped only with the worker, so after a `SIGKILL` it kept running
  (with a `sleep` for the whole timeout) and at the deadline signalled a command
  PID the lost worker could no longer vouch for. It now leaves as soon as its
  worker is gone and signals only processes that are still that worker's
  unreaped children.
- An attached agent pane no longer types stray text such as `1;0c` into the
  agent. Hydra answered tmux's terminal queries through the agent's input; when
  Hydra was held up (for example by a terminal that stopped reading its output)
  and an answer reached tmux after its 5-second query window, tmux passed it on
  as keys. Hydra no longer answers them; tmux learned nothing from the answers.
- Fleet SSH no longer hangs when `~/.ssh/config` enables `ControlMaster auto`
  with `ControlPersist` (a common `Host *` setup). The first connection started a
  background master that kept Hydra's captured output open, so `hydra remote
  setup` (the host-key check), preflight and fleet requests waited until the
  master exited, up to its whole `ControlPersist` time, even past `--timeout`.
  Hydra now never starts or reuses your own SSH masters: every Hydra SSH
  connection passes `ControlMaster=no` and `ControlPath=none` unless the alias
  uses Hydra's own `--multiplex`, so host-key checks always verify the host
  itself. A command's own exit now also ends its output capture shortly after,
  even when a background process still holds the output open.
- The control centre footer no longer shows `Current snapshot · age Ns`, whose
  age restarted with every two-second refresh. It shows `Live` while refreshes
  succeed and `Updated Ns ago`, counting up, once the data is older than two
  refresh intervals (plus one remote request in fleet mode); a failed refresh
  shows `STALE: last good snapshot, updated Ns ago`.
- A failed SSH connection during remote setup says why: the error message ends
  with OpenSSH's own reason (also in `data.ssh_error`), such as `ssh: Could not
  resolve hostname ...`, `Permission denied (publickey)` or `Connection timed
  out`, and the recovery names the likely fix (for an unresolvable name: check
  the destination or pass the SSH config file that defines it). The control
  centre shows it on the error screen.

## [2.8.0] - 2026-09-27

### Added

- `hydra kill --dry-run` previews a branch, `--all` or `-g/--group` selection:
  each active head's session, worktree, tracked and untracked changes, and that
  its branch is kept. It never prompts or changes state; `--force` stays a
  preview. Help and bash, zsh and fish completions include it.
- Policies accept the write scope `@spawned:*`, authorizing writes only to heads
  created by the plan's own spawn steps; plans still name concrete heads.
- Plans carry their own inputs, so planning never requires committing files to
  your checkout. Agent steps accept an inline `prompt` (up to 32 KiB) instead of
  `prompt_input`; `workflow plan propose --asset NAME=FILE` publishes up to 16
  custom files (such as a verifier) that `data.inputs` reference as
  `{"asset": NAME, ...}`, and `validate`/`compile` accept `--assets-dir`. Prompts
  and assets are embedded in the compiled plan and bound by its digest, so any
  change requires fresh approval. `show` and `explain` preview inline prompts.
- Exec `argv` in a compiled plan can pass a step's materialized input as
  `@input/<name>`.
- `hydra gc --policy orphaned --path <worktree>` limits a run to one leftover
  worktree. Doctor, `hydra du` and Recovery show leftover worktree sizes, measured
  with a time bound and reported as unknown rather than zero.
- The plan view's `F` requests changes: `hydra workflow plan proposal <head> --return
  <feedback>` records that the exact draft was sent back, so it cannot be reviewed,
  validated or launched until the agent republishes, and the feedback is typed into
  the agent's pane for the user to send with Enter.
- Plans launched from an agent proposal are bound to that head instance. The TUI
  submits the run ID into the planning conversation, then approval waits, failed
  steps and the outcome; the run owner also records deduplicated inbox notices for
  that exact instance, including when the TUI is closed.
- Overview centres on the run: a run panel lists each step with its kind and role,
  state, attempts, duration and the head it ran on, and says what happens next or
  what the run needs from you ("Worker branch … holds the result: review · land ·
  dismiss"). With no run it explains how one starts; the queue chart appears only
  when work has been queued.
- Headless heads describe their steps: Details and the workspace show
  `headless (no terminal)` and the agent of the current or last step
  (`codex · step implement · running`) with its executable version, model and
  reasoning effort, tokens (in, cached, out), cost and duration. A model or effort
  the provider reports is shown as reported; one read from the provider's
  configuration (such as `~/.codex/config.toml`) is labelled "configured default …
  (not observed)"; anything else stays unknown, never zero.
- `p` on a headless head shows a bounded, read-only view of its step: the running
  agent's events (assistant messages, commands, file changes, usage) with a raw
  fallback, a command's output, or a finished step's declared result. Provider
  output is copied privately only while the step runs and removed when it ends.
- When a plan run succeeds, fails or is cancelled, Hydra retires the heads it
  created for verification steps with `hydra kill --protect-untracked` once their
  evidence is sealed in the run record; their branches are kept. A verifier head
  with changes is kept for review, every outcome is recorded as a run event, and
  a failed retirement never changes the run result. Worker heads stay until you
  land or dismiss their result.
- Reviewing a result of a compiled plan run shows what the run produced and how it
  was checked: the verdict, the deliverable's text, each requirement with its check,
  the verify step's exact command with a filtered summary of its output (full log
  with `L`), duration, agent, executable version and token use per step, the worker
  branch's commits, files and bounded diff as currently observed, and the `git` and
  `hydra` commands that would land and clean up. Hydra never runs them.
- Attention items can be marked seen with `s` from the list or detail view. The
  marker is kept per user in `$HYDRA_HOME/attention/seen.tsv` (512 entries, keyed by
  identity and revision) through `hydra workflow attention-seen`, survives restarts,
  moves the item to a collapsed Seen group and out of the count; a new revision
  shows it again. A seen approval stays listed until someone decides.
- Attention lists failures that need your decision: a failed check reads "Check
  failed · ux-greeting · repo-check — greeting-committed; review the log and send
  the plan back or retry", and a failed agent step, a step or run needing recovery
  and a run whose delivery was rejected get their own item (kind `failure`). They
  lead the list, count as needing you ("1 failed check or step needs a decision"),
  and stay listed when marked seen until the step succeeds or a later run of the
  same plan from the same planning head succeeds. `r` opens the run's result
  review with the failed requirement and the log summary (readiness `failed`).
  Fleet attention reports a remote task that failed as the same kind; cancelled
  and unknown outcomes are never claimed as failures.
- `hydra resume --terminal <branch>` restarts only the terminal of an interactive
  head whose tmux session is gone. It requires the existing worktree and never
  creates, resets or cleans it, starts what the head's profile resumes (Codex
  `resume --last`, a shell for `none`), and refuses clearly when the worktree is
  missing.
- The Statistics view shows agent tokens (in, cached, out), cost only when reported,
  agent, executable version, model and effort per step and per run, with totals for
  the selected scope. Workflow statistics data is schema 4 with one `U` row per agent
  step from its exec receipt; schemas 2 and 3 remain readable.
- `Ctrl-B [` in a local attached pane opens the agent's own tmux history (copy
  mode on that exact session): PgUp/PgDn or arrows scroll and `q`, Esc or
  `Ctrl-B ]` return to live output. The attached client draws on tmux's alternate
  screen, which keeps no scrollback of its own.
- When another, smaller client of the same tmux session typed last, tmux sizes the
  window to it and fills the rest of Hydra's view with dots; the attached pane now
  says it is "sized by another client" and that typing there takes the size back.
- `make test-usability` adds three installed journeys at 80 and 140 columns.
  `result-review` drives an in-app plan through a headless worker's two-file commit,
  a repository check on the worker head and a verifier head, then checks Overview's
  run panel, the grouped worker with land/dismiss, the retired verifier, the task
  diff and the result review (verdict, requirements, command, files and diff).
  `recovery` fails the repository check, checks the run panel, Attention and the
  review, then recovers through the planning agent's corrected revision, validated
  and executed with `y`. `recovery-terminal` removes an interactive head's terminal
  and checks that Hydra neither infers failure nor loss and that the offered action
  restores access. The provider fixture is installed as `codex` on a private PATH
  and reached only through the built-in profile's launch and headless contracts.
  Reports list every failed desired-behaviour check of a journey.

### Changed

- Execution approval is an in-app confirmation (`y`; `n`/Esc cancels) showing the
  revision, a 12-hex digest, the policy and what the run does, instead of typing the
  64-hex digest. The TUI still passes the full digest, and a changed draft or policy
  still refuses launch.
- The planning workspace gives the agent conversation full height and most of the
  width beside the plan review. At 80 columns navigation stays and the focused side
  gets the rest; `B` (Ctrl-B B from the agent) shows the review.
- Attached panes read "codex · session running" instead of "AGENT UNKNOWN": the
  agent and observable session state, never a claim that the agent awaits input.
- Heads a workflow run creates are grouped under their run, and a run launched
  from a planning conversation under that planning head, in Work, Overview and the
  workspace navigation (`add-kill` ▸ `plan run kill-dry-run · succeeded` ▸ `worker
  …`, `verifier …`). Runs start collapsed; Enter or `l`/`h` expands and collapses
  them. Header and Overview counts describe the heads you started, with run heads
  counted inside their run ("1 head (+2 in runs)").
- A headless head never needs attention merely for having no terminal; attention
  comes from failed steps, approval waits, runs needing recovery and approval
  requests. A planning head's details show its plan approval ("plan … approved …
  · run … · succeeded") instead of `Approvals 0 of 0`; gate counts are labelled
  "Approval requests".
- Technical details are grouped into identity, lifecycle, adapter and sources.
- Attention rows read as sentences with the workflow and step, for example "Result
  ready for review · kill-dry-run · verify", followed by why it needs you; long rows
  wrap instead of being cut, and a partial snapshot is explained in plain words only
  when it applies. `workflow attention-data` and `fleet attention-data` are wire
  version 3: version 2 appended the workflow name as a presentation label and
  version 3 appends a presentation detail (the requirements a failed check
  decides). The native TUI reads versions 1 to 3.
- Review navigation styles headings, check results and diff lines, jumps between
  sections with `n`/`N` and pages with space/`b`.
- Captured agent output reads as a faithful transcript. The Details terminal
  excerpt captures the pane's colors and joined lines (`capture-pane -e -J`, 40
  lines of history) and keeps the newest output instead of failing when large;
  headless step output keeps color. Each line is interpreted by the same termviz
  terminal model as the attached pane, so Unicode, emoji, colors, tabs and
  carriage-return progress lines match it, while screen erasure, cursor movement
  and OSC/DCS payloads are dropped. Long lines wrap at the view width with a `↪`
  continuation marker instead of being clipped, action results and review logs
  wrap the same way, and unified diffs without their own colors get theme tones
  (added, removed, hunk and file header); with `NO_COLOR` or `--no-color` the
  `+`/`-` markers remain. The excerpt is labelled a read-only transcript that
  PgUp/PgDn scroll (result overlays page the same way), and key
  hints printed by the provider ("esc to interrupt", "← for agents") are named as
  belonging to live input.
- termviz lays out emoji like tmux 3.5: ZWJ sequences, skin-tone modifiers and flag
  pairs occupy one two-column cell, VS16 widens a narrow symbol, and invisible
  format characters (zero-width space, bidirectional controls) are dropped instead
  of shown as `?`, so attached panes stay aligned with tmux. ASCII mode (a non-UTF-8
  locale or `--ascii`) approximates box drawing, arrows, bullets, quotes and Latin-1
  letters in the same columns instead of turning them into runs of `?`, and says so
  in the Details transcript.
- Attached panes always name how to leave agent input and close the view, fitted
  to the width down to 40 columns (`Ctrl-B Tab leave  Ctrl-B x close`); Esc and Tab
  still go to the agent. Leaving input and closing the view say that the agent keeps
  running and how to reopen it.

- The guided local policy now allows `sh`, `git`, `make` and the head's own agent
  profile, writes inside plan-spawned heads, 3600 seconds and a 1 GiB free-space
  floor, so agent-implemented plans can validate. Its approval flow is unchanged.
- The planning handoff explains how to express a spawned worker, an agent step
  with its instructions inline, and verification with the repository's own
  checks on the worker head under that policy. It tells the planning agent never
  to commit while planning or change your checkout, and to use `--asset` only for
  genuinely custom files. The `P` policy prompt notes that the plan carries its
  own prompts and assets.

### Fixed

- Opening a head whose terminal is gone from Work (`a`, as Details and Recovery
  offer) restarts its terminal in the same worktree through `hydra resume
  --terminal` and then attaches, instead of showing an empty pane or "CLIENT
  DISCONNECTED". Committed and uncommitted work are untouched; when the worktree
  itself is missing the restart is refused and nothing is recreated. Details,
  Work and the workspace offer "a restart its terminal" for such a head.
- A disconnected attach client no longer captures keys: Tab, Esc and `a` work in
  Hydra again (previously every key reported "Input not delivered" until `Ctrl-B`
  was used), and `a` or `Ctrl-B r` reattaches.
- At 80 columns and in the compact layout the attached footer no longer cuts off
  `Ctrl-B x close`; the notice after closing a view no longer says input is still
  attached.
- Headless step output no longer shows fragments such as `[31m` where escape
  characters were deleted, and a byte-bounded tail never starts inside a character
  or escape sequence.
- Review text wraps by display width instead of bytes, so multi-byte characters
  are no longer split into `?` at the wrap point.
- New tasks whose names contain spaces or other invalid characters start on a
  derived branch (`add kill dry run` becomes `add-kill-dry-run`) instead of failing.
- `invalid_source` names the checkout and the failed condition, lists tracked
  changes with a commit-or-stash recovery, and the TUI shows readable diagnostics.
- Doctor, cleanup, `doctor --fix` and TUI Recovery now use the `hydra gc --policy
  orphaned` detector: only `head_<id>` worktrees under the recorded worktree root
  whose head record is gone. User-created sibling worktrees such as
  `hydra-<branch>` are no longer flagged or offered for removal, and current
  leftover head worktrees are no longer missed.
- Cleanup removes leftover worktrees through gc, keeps worktrees with uncommitted
  changes unless `--include-dirty` is passed, and keeps branches.
- The Recovery finding runs `hydra gc --policy orphaned --dry-run` instead of an
  invalid command, reads "Leftover worktree from a removed task", and `x` removes
  a clean one after confirmation. Failed checks are reported as failed.
- `hydra gc` explains missing option values; orphan GC no longer treats other
  worktrees under the root, such as integration worktrees, as orphans.
- Spawning shell tests run only inside throwaway repositories, so standalone runs
  no longer leave head worktrees beside the source checkout or in `$TMPDIR`.
- Pass `--trust` in the built-in Cursor headless run and resume recipes so Cursor
  Agent accepts fresh head worktrees instead of refusing with a plain-text prompt.
  The help probe now requires the flag; `--force`/`--yolo` are never passed.
- Keep a bounded, character-safe excerpt (4096 bytes per stream) of provider stderr
  and undecoded stdout in failed headless receipts as optional `diagnostic`;
  completed receipts are unchanged.
- Details no longer shows `Full diff : ...` with a stray colon and no value;
  headless heads are no longer listed as `Agent shell`, `Session unknown`, or offered
  a terminal preview that could only report `preview unavailable`.
- Succeeded spawn and plain exec steps no longer appear in attention as unknown
  `result_binding_unknown` items that also marked the snapshot partial: a step that
  declares no outputs has nothing to review. Declared outputs that do not match
  their receipt remain explicit unknowns.
- Reviews no longer report a step without deliverables as revoked with malformed
  declarations; it is not applicable. Plan checks of a run that has not finished are
  pending instead of failed, and a sealed result waiting for them is in progress.
- Statistics word unmeasured CPU and memory in full instead of truncating
  "not measured", and no longer claim tokens are unmeasured when receipts record them.
- A result review marks the requirements of a check whose step failed as FAIL
  instead of PENDING after the run failed, and says "1 commit, 1 file" rather than
  "1 commits".
- Text-field prompts, decision panels and result reviews wrap between words instead
  of splitting them at the row end ("No e / xecution yet"); a long prompt may use up
  to half the terminal height instead of being cut after five rows. Plain review
  lines, such as a check's summary output, wrap between words with a hanging indent
  at 80 columns instead of splitting a word behind a `>` marker.
- Native PTY drivers no longer leave private tmux servers running: a guardian
  process kills the fixture's server and removes its directory on every exit path,
  including failed checks and signals, and a successful driver checks that no
  server remains. `pty-runs-heads` previously removed its fixture directory, and
  with it the socket, before stopping the server.
- `n` in a result review stops at "CHANGES ON <branch>" instead of skipping from the
  checks to the diff.
- The launch owner of a plan run that fails now records that it finished and its
  exit code; under `set -e` it exited early, so `--workspace-status` and the TUI
  reported the launch as still starting.
- The spawn bootstrap and workspace controls tests wait for the pane state, footer
  and run-list state they check, within deadlines, instead of reading after fixed
  delays; both failed under heavy load.

## [2.7.0] - 2026-09-24

### Added

- Start a conversation inside Hydra with task name, available agent profile and
  optional objective; project registration and agent input stay in the workspace.
- Publish head-associated agent drafts with `workflow plan propose`. Review with
  `B` then `P`, choose a bounded local policy, validate with `V`, and approve an exact
  revision with `E`. Draft replacement invalidates validation; `I` imports files.
- `make test-usability` installs a fresh prefix and runs five bounded journeys at
  two terminal widths, retaining HTML, actions, raw output and independent checks.

### Changed

- Interactive spawn and resume open the task's input inside Hydra when the native
  TUI is available; shell-only installs stay in the terminal with the context
  block. Explicit `--attach`, non-TTY, headless and `HYDRA_NO_SWITCH` behavior is
  preserved.
- Label Details output as a read-only, clipped plain-text excerpt and preserve its
  blank lines; open the attached pane for live terminal styling and input.
- Run independent shell tests with bounded parallelism and private temporary
  files/tmux sockets, preserving per-case logs and serializing shared fixtures.
  Nested shell and Fleet suites share Make's jobserver to avoid oversubscription.

### Fixed

- Let simultaneous agent outcome declarations wait through brief project-state
  lock contention, retaining a bounded wait and explicit retry overrides.
- Refuse untracked worktree files during UI removal; expose the same opt-in CLI
  behavior with `kill --protect-untracked`.
- Keep plan/monitor focus in Hydra and preserve the tmux server when opening an
  attached client from a nested control centre.
- Render DEC special graphics in attached terminal output instead of repeated
  letters, including split sequences, shifts and saved character sets.
- Preserve real untracked configuration during init's generated-stub migration.
- Bound captured TUI actions across pipe EOF, continuous output and termination;
  report interrupted actions as unknown instead of success.
- Report failed broadcast sends and skipped current-head removal accurately.
- Resolve moved worktrees and project identity consistently from subdirectories;
  explicit worktree repair normalizes legacy relative move records.
- Exclude retired heads from bulk execution and label idle headless workspaces
  accurately in the human-readable session list.
- Keep the exec deadline active through output completion. Inherited streams that
  cannot be drained return timeout and retain unknown admission rather than
  claiming confirmed descendant termination.
- Refresh benchmark readiness screens and semantic predicates for the control
  centre, including actual search cancellation rather than an already-visible tab.
- Complete `workflow plan propose`/`proposal` and their flags in bash, zsh and
  fish.

## [2.6.0] - 2026-09-14

### Added

- A bare `hydra` inside a repository opens the control centre; elsewhere it
  prints a short usage. `hydra <command> --help` works for every command.
- `hydra spawn --attach` attaches to the new session directly, and
  `hydra spawn --resume` resumes a stopped head of the same branch.
- `hydra init --write-shared-config` writes the optional shared repository
  configuration explicitly; `hydra list --verbose` restores the detailed fields.

### Changed

- Redraw the native TUI through one frame canvas and presenter so every view
  shares the same title, tab bar, panel and footer, and only changed cells are
  repainted; idle refreshes and view switches no longer clear the screen.
- Use one interaction model across views: `Tab`/`Shift-Tab` move between tabs
  or workspace panes, arrows select, `Enter` opens and `Esc` steps back one
  level; `n` starts a task and `1`-`9` jump to tabs.
- Restrain the palette to cyan chrome, amber attention, green running and a
  muted tone for hints; the focused workspace pane is shown by its border.
- Describe sessions, details, coordination, overview and recovery findings in
  plain language, with raw identifiers and finding kinds kept under technical
  details; empty states explain what a head or workflow is and offer the next
  action.
- Confirm head removal inside the UI, run `hydra kill` with captured output,
  report the result in place and show the full output only when a head was
  kept; recovery checks run the same way. Statistics and Workspace keep the
  shared tab bar.
- `hydra init` no longer writes `.hydra/config.yml`, `.hydra/local.yml` or
  `.git/info/exclude`; registration is host-local and a clean repository stays
  clean. Generated leftovers from earlier releases are migrated once and real
  or tracked configuration is never removed. Init prints a two-line `Ready:`
  summary; `init --json` is unchanged.
- Interactive `hydra spawn` stays in the current terminal and prints where the
  head lives with `hydra tui` / `hydra switch` as next actions. `spawn` and
  `spawn --dry-run` refuse an existing durable head with the same message.
  `HYDRA_NO_SWITCH`, `--headless`, non-interactive behavior, `--json` and exit
  codes are unchanged.
- Spawn and resume deliver the head environment through the tmux session
  environment and a per-instance launcher: bootstrap exports and the agent
  launch are no longer typed into the shell or its history, the pane opens
  with a short `Hydra head ... / agent ... / repo ...` banner, and the agent runs
  by the absolute executable path that `hydra agent list` and `hydra provenance`
  report (tmux 3.2+ uses `new-session -e`; 3.0 and 3.1 fall back to
  `set-environment`). While an agent runs in the head pane, Ctrl-Z is ignored;
  a failed session creation leaves a retired head record.
- `hydra kill` says the branch is kept, and `hydra list` prints an aligned
  BRANCH / AGENT / SESSION / REPORTED / AGE table (`--json` is unchanged).

### Fixed

- `hydra doctor` detects installed agents correctly and names the no-agent path.
- `hydra status` reports the tmux version even when no heads exist.

## [2.5.0] - 2026-09-12

### Added

- Native attention views for bound workflow approvals, task results and Fleet
  observations, with explicit freshness and coverage information.
- Exact workflow and Fleet review routes, with native evidence navigation and
  identity checks that keep review separate from approval and execution.
- Remote head attachment in the native workspace, using the existing Fleet
  transport and preserving terminal input during background observations.
- Bounded interaction benchmark tools and semantic terminal observers. These
  tools do not establish the outstanding performance budgets or live-host gates.

### Fixed

- Keep startup responsive during snapshot loading and resolve head directories
  once per snapshot.
- Initialize the workspace before attaching from a fresh Fleet view.
- Cancel read-only captures across nested process groups and wait for actual
  output closure, including TERM-resistant helpers and orphaned stdout.
- Validate attention snapshots, preserve attachment client state, and handle
  Linux path bounds and portability correctly.
- Include the correct README in standalone termviz source exports.

### Changed

- Run a fixed fast PR lane with attachment and capture-cleanup regressions;
  retain the full release gate for release qualification.
- Consolidate documentation and retain explicit provider, remote-host,
  performance and visual-polish follow-ups in the roadmap.

## [2.4.0] - 2026-09-11

### Added

- Add terminal-independent headless workspaces, structured outcome obligations
  and handoff contracts, subject-bound evidence and independent validator controls.
- Inspect and compare compiled plans, use finite patterns and maps, reuse sealed
  artifacts with fresh affected checks, and admit staged plans separately.
- Observe task freshness, resume event/log collection and recover the original
  attempt; use exact-target controls, saved-event views and protected evidence expiry.
- Discover selected hosts, review explicit enrollment intent and apply bounded
  source-snapshot batches. Current qualification limits are recorded in the release notes.

- Native statistics now include queue delay, total run duration, recorded independent
  verification time and owner recovery counts, with coverage, percentiles, trends
  and contributing evidence. Minimal lifecycle scalars preserve unknown historical
  values; press `M` in statistics to cycle metrics.

- Optional two-agent splits in A/B/C with distinct existing tmux clients,
  independent scrollback, input focus and preserved drafts across layout changes.

- Local project/head/run navigation from recorded branch references, with
  preserved expansion, unmatched runs at project level and direct evidence access.

- Native draft/policy loading, asynchronous validation and complete compiled plan
  previews with revision invalidation, reversible A/B/C layouts, and text dialogs
  that preserve attached terminal I/O. Exact-digest approval launches through the
  existing workflow engine with durable run receipts, duplicate launch refusal and
  execution that survives UI closure.

- C monitoring now follows selected-run step output, input requests and freshly
  verified artifact evidence. Native approve/reject/resume/cancel controls retain
  the existing engine's binding checks and explicit decision/resume boundary;
  owners and attached shell drafts survive UI closure.

- Focus zoom for native workspace panes and compact single-pane rendering at
  narrow sizes, preserving terminal drafts, saved splits and statistics navigation.

- Local standalone termviz source export with a portable static-library build,
  optional POSIX examples, independent component/PTY tests and documented
  ownership, compatibility and license boundaries. No repository or publication
  is created by the exporter.

- Deterministic local objective planning with validation, bound compilation,
  comprehensive preview, exact-digest execution approval and artifact-bound final
  verification. The native workspace branch reuses the existing planning engine;
  see the workflow planning guide.

- Native workspace attachment clients for up to four existing local tmux heads,
  with identity checks, direct terminal input, explicit focus routing, preserved
  drafts, client-only detach and reconnect. Periodic observations now run without
  blocking terminal I/O.

- Dedicated native statistics view (`D`) with workflow/time/search filters,
  recorded outcomes, retries, latest-attempt timing, run creation trends and
  run/step/graph drill-down. Fleet mode exposes host response coverage and known
  head counts. Missing measurements and failed refreshes remain explicit.
  Qualification limits are recorded in the release notes.
- Workspace selected-work summaries and an `a` shortcut to the existing head
  session, preserving workspace context on return.

- Dependency-free termviz workspace with incremental rendering, nested draggable
  panes, independent scrolling, focus routing, navigation trees, and bounded
  UTF-8 wide/combining text. The native Hydra UI opens this workspace by default.
- Standalone workspace demo with one real shell through a separate POSIX PTY
  adapter, bounded screen/parser and scrollback, alternate-screen support, resize
  propagation and explicit child cleanup. Hydra heads retain tmux ownership.
  See the [supported terminal subset](src/termviz/TERMINAL.md).

- Antigravity (`agy`) and Cursor Agent headless profiles with bounded event
  translation, explicit recorded-session resume, and native fleet sign-in.
- Interactive Antigravity and OpenCode profiles; Cursor now starts `cursor-agent`.
  Agent discovery, completions, and documentation distinguish interactive launch,
  headless capabilities, and completed live qualifications.

- Named workflow file and structured inputs, sealed output manifests, and exact
  artifact references that block dependents on missing or changed output.
- Durable workflow approval waits with bound evidence, explicit decisions and
  resume, stale-decision rejection, and remote task suspension and recovery.
- Failure-class retry policies and durable backoff deadlines, preserving completed
  attempts and reporting uncertain non-idempotent work for recovery.
- Versioned declarative headless agent profiles, bounded event translation,
  capability evidence, exact recorded-session resume, safe-point prompt delivery,
  supervised cancellation, and opt-in bounded provider payload retention.

- Fleet host authentication through native provider sign-in or preview-bound
  copying of private credentials, with selected-provider merges for Pi/OpenCode.

### Fixed

- Avoid an empty-record `qsort` sanitizer diagnostic, preserve split UTF-8 and
  escape decoding across PTY resize, and harden event-rotation recovery and task
  cancellation races.

- Standalone export qualification uses its own build directory when Hydra is
  invoked with a custom build path.
- Closing a standalone PTY on Linux also stops ordinary background jobs in its
  owned session, while leaving external tmux sessions outside that boundary.
- Workspace execution approval now reports an already submitted revision before
  opening another dialog. Finished launch receipts stop polling; unavailable
  owner observations retain known run evidence. Run switching clears stale action
  notices, and narrow evidence headers retain the complete waiting state.

### Changed

- Remove Python from the active build, tests, planning examples, standalone export
  and CI. Native producers, independent checkers, fixtures and PTY observers retain
  the covered behavior; native builds and tests have no Python dependency.
- Run fleet CI in bounded parallel shards to reduce per-runner process contention.

- Fleet task cancellation allows 30 seconds per transport phase and accepts a
  bounded `--timeout` override so suspended approval result sealing can finish.
- Interactive Codex restore retains worktree-scoped `codex resume --last`.
  Headless resume requires the exact recorded provider session and matching head,
  instance, worktree and profile; see the public contracts guide.

Workflow-data acceptance is complete. Claude Code remote qualification is deferred
until native host sign-in is available and remains on the outstanding roadmap.
The local and remote provider matrix remains a qualification concern tracked by
the roadmap; dated evidence is intentionally not stored in the repository.
## [2.3.0] - 2026-09-08

### Added

- Run finite workflows across explicitly selected hosts with durable coordinator
  ownership, stable task receipts, and verified file and collected Git handoffs.
- Compile distributed schema-2 plans with artifact-bound validation joins and
  version-2 reports tied to accepted check definitions. Required checks gate
  composition, and the combined candidate requires its own validation.
- Repair rejected candidates within an accepted whole-graph budget and replay
  scheduling decisions from recorded observations. Unknown remote execution is
  reconciled under its original identity rather than retried or reassigned.
- Share shell-authoritative FIFO admission across local exec, gates, head startup
  and resume, queued spawns, and remote tasks and workflows. Receiver policy
  controls host/project concurrency, disk floors, required labels, and queue
  bounds. Isolated remote task clones count against their original mapped project.
- Retain reservations after uncertain execution or owner loss; confirmed teardown
  and managed-command cancellation release only the corresponding claims. Initial
  remote admission uses the original acceptance deadline and precedes workspace
  creation.
- Inspect capacity, queue reasons and age, reservation ownership, and observation
  timestamps with `hydra admission` and read-only `hydra fleet admission HOST`.
  Bounded capacity summaries are included in initialized-host fleet snapshots;
  cached observations never grant execution authority.

### Fixed

- Count uncertain remote work against workflow parallelism and resume result
  collection after transport loss using the original task identity. Corrupt
  results still require recovery.
- Reject incomplete admission policies instead of treating omitted limits as
  unlimited capacity.
- Reject plans containing a check with no assigned requirement before execution;
  such checks cannot produce a valid report. Multiple requirements per check
  remain supported in both local and distributed plans.

## [2.2.1] - 2026-09-08

### Security

- Fix repository trust hashing that omitted nested `local.yml` files and symbolic
  links, allowing unapproved workflow changes to retain a previous approval.
  Only root `.hydra/local.yml` remains exempt. Linked configuration, special files,
  and newline-bearing paths now fail closed; replace links with regular files,
  review `.hydra`, and rerun `hydra init --trust` as needed.

- Reject a linked `.hydra` root before initialization writes configuration.
- Keep oversized or interrupted terminal paste/CSI input inert until its
  terminator, and enable bracketed paste while the native TUI is active.

### Changed

- Native modules and command handlers have clearer ownership, shared dependency
  loading, and regression-gated C complexity ceilings.
- Native TUI palette actions now declare commands and selection requirements in
  one table, preserving search order, prompts, and literal argument handling.

## [2.2.0] - 2026-09-07

### Added

- Bounded JSON objective plans with public schema discovery, structured validation,
  requirement coverage, deterministic compilation and readable previews.
- Exact-digest accepted local plan execution bound to source, inputs, context,
  policy and adapter definitions through the existing workflow runtime. Final
  results require passing verification reports bound to sealed deliverable bytes.
- Agent-facing planner recipe and live-qualified feature/research examples, with
  independent executable checks and assessment of the completed outputs.
  Distributed DAG execution, automatic placement and dynamic expansion remain
  outside this implementation.

- Antigravity (`agy`) and Cursor Agent headless profiles with bounded event
  translation, explicit recorded-session resume, and native fleet sign-in.
- Interactive Antigravity and OpenCode profiles; Cursor now starts `cursor-agent`.
  Agent discovery, completions, and documentation distinguish interactive launch,
  headless capabilities, and completed live qualifications.

- Named workflow file and structured inputs, sealed output manifests, and exact
  artifact references that block dependents on missing or changed output.
- Durable workflow approval waits with bound evidence, explicit decisions and
  resume, stale-decision rejection, and remote task suspension and recovery.
- Failure-class retry policies and durable backoff deadlines, preserving completed
  attempts and reporting uncertain non-idempotent work for recovery.
- Versioned declarative headless agent profiles, bounded event translation,
  capability evidence, exact recorded-session resume, safe-point prompt delivery,
  supervised cancellation, and opt-in bounded provider payload retention.

- Fleet host authentication through native provider sign-in or preview-bound
  copying of private credentials, with selected-provider merges for Pi/OpenCode.

### Changed

- Contributor quality-hook pilot includes local shell checks and native C analysis.
- Fleet task cancellation allows 30 seconds per transport phase and accepts a
  bounded `--timeout` override so suspended approval result sealing can finish.

### Fixed

- Workflow cancellation refreshes residual worker evidence before publishing terminal
  state, avoiding stale residuals and false cancellation failures.
- Agent cancellation reaches the native supervisor before its children so receipts
  preserve the observed cancellation; deadline signals remain timeouts.
- Git fingerprints reject failed or truncated probes. Agent result decoding rejects
  malformed terminal records and embedded NUL data.
- Shell lint remains compatible with ShellCheck 0.9.
- Restore the Hydra logo in the README.

### Compatibility

- Interactive Codex heads preserve cwd-scoped `codex resume --last`, including
  heads without a recorded provider session ID. Headless `hydra exec --resume-run`
  uses only the exact session bound to the selected successful run receipt.
- State v2 and existing core, TUI, fleet, event, and JSON protocol versions remain
  unchanged. Upgrade the shell CLI and native helpers together.

### Qualification

Workflow-data and local planning acceptance are complete. Cursor local/remote,
Antigravity remote, and Claude Code remote live qualification remain outstanding;
Claude remote sign-in is explicitly deferred. These are qualification limits,
not claims that the implemented profiles have passed on those hosts.
The local and remote provider matrix remains a qualification concern tracked by
the roadmap; dated evidence is intentionally not stored in the repository.

## [2.1.0] - 2026-09-05

### Added

- Local remote-task package preparation and inspection, binding an exact Git
  commit/bundle, selected binary inputs, destination, work, outputs, and limits.
  Preview includes transfer size and checksums; unsafe paths, symlinked inputs,
  submodules, and LFS pointers fail preflight.
- Durable receiving-host task acceptance with pending launch intent, immutable
  receipts, project/dependency preflight, and submission-key deduplication across
  concurrent clients and lost acknowledgments. Task status preserves transport
  failures and corrupt-state uncertainty.
- Explicit digest-authorized detached task execution through the existing shell
  exec/workflow engine, preserving real run/provenance evidence and selected inputs.
  Durable launch claims prevent replay after owner loss; queue, startup, and
  execution deadlines remain distinct.
- Durable remote-task cancellation with requested/delivered/confirmed/unknown
  outcomes and a separate grace deadline. Bounded paginated logs expose real owner
  and exec/workflow output, preserve exact bytes, and retain work after cancellation.
- Immutable receiver-completion task result snapshots with checksummed declared
  artifacts, real commit/attempt/gate evidence, and independent client verification.
  Downloads preserve binary bytes and refuse unsafe paths or mismatched digests;
  collection installs isolated refs/artifacts without changing dirty checkout work.
  Successful committed results feed the existing local integration, gate, approval,
  and promotion flow, with stale-ref and symbolic-ref protection.
- Optional C fleet coordinator with OpenSSH host aliases, pinned bootstrap,
  capability negotiation, bounded parallel list/doctor observations, and explicit
  reconcile/watch after connection loss. Partial results retain reachable hosts;
  reconnect does not replay mutations.
- Instance-checked remote interrupts, spawn, workflow operations, and interactive
  attach through the public shell CLI. Configuration and terminal workflow-history
  bundles transfer only explicitly selected records; imported configuration remains
  untrusted and imported history stays outside live runtime state.
- Native fleet view with host-qualified heads, recorded desired state, recovery
  findings, attach, and confirmed interrupt actions. Desired state does not imply
  observed activity or task completion.
- Prebuilt fleet-helper installation with `HYDRA_INSTALL_FLEET=auto|required|never`,
  native fleet CI coverage, and real-host acceptance documentation. JSON-C is linked
  statically into the optional helper; core shell operation has no added dependency.
- Native terminal, dark, and light palettes with live `t` switching and
  `HYDRA_TUI_THEME` startup selection. Direct `hydra-tui` invocation also accepts
  `--theme`; explicit dark/light palettes use fixed 256-color entries, while
  `NO_COLOR` and `--no-color` remain authoritative.
- Native mouse navigation: click visible head/recovery rows and wide-screen view
  tabs, or move selection with the wheel. Filtered and scrolled lists retain correct
  row targets; keyboard controls remain complete and clicks never mutate heads.

### Changed

- Simplified everyday CLI examples around profiles, supervised `exec`, inbox
  steering, and verification gates. Terminal broadcast and inactivity tools remain
  available as expert utilities; inactivity is no longer described as completion.
- Aligned contributor guidance with the C99 helpers, state-v2 contracts, focused
  native checks, and combined installation/onboarding acceptance.
- Simplified native mission control with branch-first lists, focused details,
  on-demand diagnostics, bordered panels, fixed table columns, a title bar,
  full-row selection highlighting, a selected-head summary, and a fixed action footer.
  Head and recovery lists follow selection in short terminals; Escape returns to
  heads and clears search.
- Refreshed the real-agent and fleet demo with theme switching, neutral host labels,
  and a private SSH recording alias. Simplified the README and moved detailed
  command recipes into `docs/USAGE.md`.
- Documented rolling delivery, release-time SemVer, draft-first publication, and
  public deprecation windows. Versions remain assigned at release time.

### Fixed

- Render native TUI action feedback before idle model refreshes can clear it,
  and retain paired theme markers in terminal interaction checks.

- Clarified the task-test cleanup guard for CI ShellCheck compatibility and added
  fleet installation acceptance for modes, helper identity, rejection safety, and
  uninstall.
- New Hydra homes are private even with a group-writable login umask; remote task
  storage still refuses existing shared directories without changing permissions.
- Result-head ref formatting checks truncation explicitly, including strict GCC
  builds on Linux.
- Mouse reporting is disabled for prompts, delegated commands, normal/signal exit,
  and basic-TUI fallback after a native crash. Malformed, overlong, legacy, release,
  drag, and modified mouse reports cannot select rows; resize invalidates old targets.
- Explicit dark/light selection colors remain readable when a terminal remaps its
  base ANSI palette, including pastel palettes and bold-black rendering.

### Security

- Fleet enforces strict SSH host-key checks, negotiated capabilities, bounded
  transport, and instance-bound interrupts. Remote arguments travel as JSON argv
  rather than shell-interpolated strings; bootstrap validates hashes and allowed
  paths before publishing an immutable installation. Host-local trust, live state,
  and locks are not copied between hosts.

### Removed

- Dated per-release checklists and the superseded regenerate demo. Release guidance
  now points to the roadmap's release definition of done.

## [2.0.0] - 2026-09-04

### Added

- Native TUI keyboard help, a dashboard palette action, and structured spawn prompts
  for branch, profile, template, and layout, all delegated to the shell CLI
- Native TUI branch-stable multi-selection, select-all, and bulk group assignment
  through explicit shell CLI arguments
- Native TUI bulk kill through the public confirming shell command, with a bounded
  tmux lookup that excludes the current session

### Changed

- `hydra tui` now launches native mission control by default when available and falls
  back visibly to `hydra tui --basic`
- Source installation auto-builds the native TUI when a C99 toolchain is available;
  `HYDRA_INSTALL_TUI=never` preserves a compiler-free shell-only install
- Hydra, native core, and native TUI version handshakes now report 2.0.0; protocol
  versions remain unchanged
- State-v2 per-head scalar records are now the only runtime authority for list,
  status, group, dependency, messaging, limits, maintenance, dashboard, completion,
  basic TUI, and native TUI readers
- The stable 2.0 contracts now define JSON/error negotiation, lifecycle and workflow
  evidence, native/basic parity, supported platforms, upgrades, deprecation windows,
  and the consolidated local trust model

### Removed

- Basic TUI tag mutation and filtering, including its separate non-authoritative tag
  store
- The basic TUI's one-key kill-all shortcut; select-all plus confirmed bulk kill and
  the public `hydra kill --all` command remain available
- The redundant preview-follow toggle; preview content continues to update on the
  normal bounded refresh cycle
- The redundant `hydra tui --native` mode now that native mission control is the
  default
- Runtime dual-writing and reading of the seven-field project `compat-map`, the
  global map fallback, and the in-process state-map cache

### Migration

- `hydra state migrate --dry-run` verifies state v2 and every retained 1.9
  compatibility projection without mutation
- `hydra state migrate` backs up the full state tree, removes only verified 1.9
  projections and the inactive global map, then verifies state again
- `hydra state rollback` fails closed around active state/event writers and restores
  1.9 projections only for an intentional downgrade

## [1.9.0] - 2026-09-02

### Added

- Strict static workflow schema v1 with trusted lookup, validation, dry-run, durable
  run manifests, bounded fan-out/join execution, cancellation, and crash recovery
- Explicit idempotency and retry policy, per-run parallelism, stable scheduling,
  resource bounds, disk safeguards, correlated events, and authoritative attempts
- Isolated verified integration worktrees with candidate/base bindings, conflict
  preview, verification reports, explicit approval, local promotion, and cleanup
- Guarded local merge trains with per-candidate gates, exact failure reports,
  cancellation/resume, and all-or-nothing target promotion
- Bash, Zsh, and Fish completion plus an offline local workflow example and
  workflow-to-integration acceptance coverage

### Changed

- Hydra, native core, and native TUI release handshakes now report 1.9.0; protocol
  versions remain unchanged
- Completed candidate groups and workflow runs can feed the same verified integration
  path; no integration or workflow operation pushes a branch

### Security

- Repository workflow execution is bound to the existing `.hydra` trust hash
- Shell-string workflow commands require explicit `allow_shell: true`; argv remains
  the default
- Promotion revalidates the immutable manifest, candidates, verified result,
  approval, clean worktree, and target ref under a project lock

## [1.8.0] - 2026-08-30

### Added

- Optional C99 `hydra-tui` mission control with list, detail, coordination, recovery,
  search, sanitized pane preview, resize handling, and explicit source/confidence
  rendering
- Distinct declared, observed, liveness, stale, and unavailable states plus event,
  signal, message, gate, claim, scope, queue, resource, diff, and approval summaries
- Searchable fixed-action palette whose spawn, switch, kill, and regenerate actions
  delegate to the shell CLI with argv-safe process execution
- Recovery board for malformed state, stale locks, dead sessions, orphan worktrees,
  interrupted transitions, and teardown failures
- `hydra tui --native`, `--basic`, capability diagnostics, and a deterministic
  headless fixture renderer with explicit frame and terminal-size bounds
- Checksummed platform-qualified native TUI packaging, offline/source installation,
  sanitizer coverage, and Linux/macOS hosted build/test configuration
- Native/basic behavior, fallback, keymap, accessibility, terminal safety, and
  recovery documentation

### Changed

- The basic shell TUI remains the default for 1.8.0; native dispatch is opt-in
- Native observation uses measured bounded polling because the control-mode prototype
  did not yet qualify reconnect and flow-control reliability
- Hydra/core/TUI release handshakes now report 1.8.0 while protocol v1 remains stable

### Security

- Pane and fixture bytes are treated as untrusted and cannot emit terminal controls
  or feed the action parser
- Bracketed paste, unknown escape sequences, terminal dimensions, record counts, and
  preview capture are bounded
- Native code performs no state mutation or notification delivery

## [1.7.0] - 2026-08-30

### Added

- Expiring path claims, injected read/write scopes, four-category collision analysis,
  locked per-head resource allocation, and evidence-bearing verification gates with
  separate human approval
- Explicit typed context packs and guarded `sync`/`land` integration with merge
  simulation, pre-operation archives, dry runs, exact gate binding, teardown, and
  clean recovery from conflicts
- Per-head `du`, policy-driven `gc`, and worktree doctor lock, unlock, move, repair,
  and prune operations that preserve dirty/untracked work by default
- Optional read-only C99 `hydra-core` protocol v1 for capabilities, state/event
  validation, canonical JSON strings, and snapshot aggregation
- Strict native build, unit, parity, sanitizer, benchmark, packaging, and macOS/Linux
  CI lanes, including absent, skewed, crashing, timing-out, malformed, and
  non-executable helper fixtures
- Offline/source native installation with SHA-256, platform, dependency, exact
  version/protocol verification, atomic replacement, and rollback preservation
- Reproducible tmux control-mode prototype and published parallel-safety, native
  architecture/distribution, and versioning guidance

### Changed

- Shell remains the only mutation authority; native snapshot dispatch is explicit
  with `hydra snapshot --native` and deterministically falls back to canonical shell
- Completions and CLI help cover all 1.7 coordination, integration, disk, worktree,
  and snapshot commands

### Security

- Context packs include only selected typed inputs, hash file manifests instead of
  copying file contents, and reject likely secret-bearing paths
- Gate approval is invalidated by any commit or worktree-status change
- Resource, claim, archive, and teardown records remain project-scoped and locked

## [1.6.0] - 2026-08-29

### Added

- Project-, head-, and instance-scoped state v2 with reversible migration, backups,
  verification, opaque IDs, cross-project isolation, and versioned JSONL events
- Independent declared outcome, observed status, and liveness channels; explicit
  completion policies; durable waits; stale-instance rejection; resume history
- Agent profiles, capability reporting, trusted project initialization, task-aware
  spawn/dry-run/no-agent paths, and safe task-file injection
- Provider-neutral adapter ingest with bounded canonical JSON v1 validation and a
  dated capability matrix that makes absent adapters explicit
- Typed inbox/safe-point messages and receipts, rate-limited local lifecycle
  notifications, bounded transcripts, and trusted teardown hooks
- `hydra exec` with bounded parallelism, timeouts, private captured results, argv-safe
  execution, and separately authorized trusted shell-string mode
- Recorded-base `hydra diff`, `hydra review`, `hydra list --git`, and per-head
  `hydra provenance` views
- Versioned JSON success/error envelopes across automation-relevant `--json` commands
- State, lifecycle, adapter, automation, security, operations, provenance, and release
  evidence documentation

### Changed

- Dependencies require named durable evidence instead of treating a missing tmux
  session as success
- Repository configuration and hooks execute only while their exact content hash is
  trusted; a change invalidates trust
- Regenerate and resume create a new instance and preserve prior instance history
- State and message mutations use evidence-bearing shared locks and atomic replacement

### Fixed

- Timeout watchdogs now terminate the command process tree without retaining output
  pipes for the full timeout
- Shell integration fixtures isolate `HYDRA_HOME`, tmux state, branches, and worktrees
  and clean only resources they own
- State cache keys, sender identity, worktree paths with spaces, optional PR input,
  and tmux `:0.0` pane targeting are handled literally and reliably

## [1.5.2] - 2026-08-26

### Added
- Non-root `PREFIX` install: `$PREFIX/bin/hydra` and `$PREFIX/lib/hydra` (`install.sh` and `make install`)
- Matching `uninstall.sh` / `make uninstall` for the same prefix; optional `DESTDIR` staging
- Install verification runs `hydra version` and prints exact binary and library paths
- `hydra doctor` reports install layout, git/tmux versions, writable `HYDRA_HOME`, repo/worktree readiness, detected agents, and a `Next:` recovery for every failure
- Fresh-prefix `make test-install` and throwaway-repo `make smoke-onboarding` tests
- README five-minute Quick Start (run-from-source or prefix install, `HYDRA_SKIP_AI=1`, completions)

### Changed
- Library discovery: `HYDRA_ROOT`, source `../lib`, PREFIX `../lib/hydra`, then legacy `/usr/local/lib/hydra`
- First-run errors name the failed precondition and the next command or doc action
- Spawn fails if the selected agent is not on `PATH` unless `HYDRA_SKIP_AI=1`

### Fixed
- Installer and Make no longer require root or hard-code `/usr/local` as the only layout

## [1.5.1] - 2026-08-25

### Added
- Public contract snapshot in `docs/CONTRACTS.md` (map fields, JSON, locks, TUI rows, send-keys targets)
- Supported-platform statement (Linux and macOS, POSIX `sh`, `tmux >= 3.0`)
- `make bench` / `scripts/bench.sh` for list, status, doctor, and TUI refresh timings at 5 and 20 heads
- `tests/bin/tmux` stub for deterministic send-keys and pane-target tests
- Broadcast `--pane` and `--force` for explicit pane targeting

### Changed
- State map writers share the `state_map` mkdir lock and fail closed if it cannot be acquired
- Replacement files are created next to their destination before `mv`
- Queue filenames sort by priority (high first) then request time, with a monotonic seq for FIFO
- `json_escape` emits `\n` / `\r` / `\b` / `\f` and `\u00XX` for remaining C0 controls, and passes UTF-8 bytes through unchanged
- Startup and agent launch always send keys to `session:0.0`
- TUI list readers consume the eight-field tab row contract
- TUI activity prefers `window_activity` from a batched pane snapshot; capture-pane hashing is fallback
- `hydra list` / `status` use a tmux session snapshot instead of per-head `has-session` where loaded
- Shell completion covers every command dispatched by `bin/hydra`

### Fixed
- `hydra status` no longer treats deps/PR as part of the duration timestamp (`Illegal number`)
- Kill preflights dirty/untracked worktrees before tearing down tmux or mappings
- `hydra spawn` rolls back the session and worktree if the state map cannot be written
- `hydra kill` acquires the state-map lock before destroying tmux, and does not delete the worktree if mapping removal cannot be committed
- Send-keys and spawn live-probe tmux instead of a stale TUI/list snapshot
- `hydra broadcast` only auto-selects recognized shell panes; non-shell panes require `--pane` or `--force`
- `hydra broadcast` skips a live agent on `:0.0` (even when the map stores `-`) and may use `:0.0` again after that process exits
- Session-qualified `broadcast --pane session:target` applies only to that session
- `make bench` runs spawn/kill from a throwaway repo and does not sweep unrelated `bench-*` tmux sessions
- Message inboxes are removed from kill and dead-mapping cleanup
- Doctor/cleanup stale-lock detection matches empty `*.lock` directories
- `hydra regenerate` preserves group, deps, PR, and timestamp
- Malformed `.gitignore` entry; runtime `.hydra/` is no longer tracked

## [1.5.0] - 2026-08-24

### Added
- **TUI redesign**: Two-panel detail sidebar on wide terminals, context footer, spawn wizard
- **TUI key aliases**: `Enter` switches (same as `s`), `A` select-all, `D` dashboard, `f` preview follow
- **TUI config**: `HYDRA_TUI_REFRESH_MS`, `HYDRA_TUI_ACTIVITY_INTERVAL`, wired `HYDRA_TUI_PREVIEW_LINES`
- **Refactor**: Split god files into modules (`lib/cmd_*.sh`, `lib/tui_*.sh`, `lib/state_cache.sh`, `lib/maintenance.sh`)
- **Worktree helpers**: `list_hydra_worktrees`, `branch_from_hydra_worktree_path` in `lib/paths.sh`

### Changed
- **TUI**: Expanded search filters session name, group, and AI tool
- **TUI**: Activity detection uses cached pane hashes to reduce tmux load
- **TUI**: Terminal resize handling; honors `HYDRA_NONINTERACTIVE` for kill confirmations
- **Architecture**: `bin/hydra` slimmed to dispatcher (~470 lines); commands in `lib/cmd_*.sh`

### Fixed
- **Doctor/cleanup**: Orphan worktree detection uses `hydra-*` prefix (not repo basename)
- **Regenerate**: Slash-containing branch names (e.g. `feature/test-1`) discovered correctly
- **Spawn**: Rollback session/worktree/mapping when AI tool validation fails
- **Queue**: Mixed `--agents` spawn queue entries use correct branch names and AI tools
- **Queue**: Failed queue processing retains entry for retry
- **Limits**: Active session count excludes dead mappings
- **Limits**: Queue JSON output uses `json_escape`
- **Messages**: Lock retry instead of unsafe fallback write on contention
- **Switch**: fzf uses tab-separated fields; empty selection handled safely
- **Dashboard**: Configurable pane join retries (`HYDRA_DASHBOARD_JOIN_RETRIES`)
- **TUI**: Bulk group assignment calls `set_group`

## [1.4.2] - 2025-12-30

### Added
- **Doctor**: Auto-fix mode with `hydra doctor --fix`
  - Automatically runs `regenerate` for dead sessions
  - Automatically runs `cleanup` for stale locks and dead mappings
  - Orphaned worktrees require manual confirmation for safety
- **Tests**: Added comprehensive test coverage for template.sh (41 tests)
  - Covers all 11 template functions: init, list, get, exists, show, validate, create, delete, get_field, expand_vars, apply
- **Tests**: Added comprehensive test coverage for hooks.sh (16 tests)
  - Covers locate_config_dir, run_hook, apply_custom_layout_or_default, run_startup_commands

### Changed
- **Code Quality**: Refactored nested function in deps.sh to module scope
  - `_check_circular_recursive()` moved to `_check_circular_helper()` at module level
  - Uses module-level `_DEPS_VISITED` variable instead of closure semantics
  - Improves POSIX shell clarity and maintainability

## [1.4.1] - 2025-12-29

### Added
- **TUI**: Spawn with inline options support
  - Enter `my-branch --ai codex --template dev` when spawning
  - Supports `--ai`, `--template`/`-t`, `--layout`/`-l` flags
  - Shows parsed options before spawning

### Fixed
- **TUI**: Fixed incorrect "(current)" session detection when running outside tmux
  - `tmux display-message` could return stale session names from server state
  - Now checks `$TMUX` env var to confirm actually inside tmux
- **TUI**: Fixed spawn from TUI not attaching when outside tmux
  - After spawning a session via TUI, now automatically attaches to it
- **TUI**: Fixed switch from TUI not working when outside tmux
  - `switch-client` only works inside tmux; now uses `attach-session` when outside
- **Dashboard**: Fixed pane collection failing with non-zero tmux base-index
  - Removed hardcoded `:0` window references that assumed base-index=0

## [1.4.0] - 2025-12-28

### Added

#### Multi-Agent Workflows
- Session Dependencies for staged workflows
  - `hydra spawn feature-tests --after feature-impl` waits for dependencies
  - `hydra list --deps` shows dependency tree visualization
  - Circular dependency detection prevents infinite waits
  - Configurable timeout and polling intervals
- Resource Limits to prevent system overload
  - `HYDRA_MAX_SESSIONS` environment variable sets global session limit
  - Priority queue for pending spawns when limit is reached
  - `hydra queue` command to view/manage pending spawns
  - Queue automatically processed when sessions are killed
- Environment Setup Automation
  - `setup:` section in `.hydra/config.yml` runs commands before session creation
  - Commands execute blocking in worktree directory
  - `HYDRA_SKIP_SETUP=1` bypasses setup; `HYDRA_SETUP_CONTINUE=1` continues on failure
- Cross-Session Messaging for loose agent coordination
  - `hydra send <branch> "<message>"` queues message to session's inbox
  - `hydra recv [--peek] [--json]` reads messages for current session
  - File-based message queue at `~/.hydra/messages/`

#### GitHub Integration
- PR Integration
  - `hydra spawn --pr <#>` creates a head from an existing GitHub PR
  - `hydra spawn --pr-new` creates a draft PR after spawning
  - `hydra pr [<branch>]` creates or shows PR for a session
  - PR numbers stored in state file and displayed in `list`
- PR Status Display
  - `hydra list` shows `[PR #42 OPEN]` with status (open/merged/closed)
  - Cached status with configurable TTL (`HYDRA_PR_CACHE_TTL`, default 5 min)
  - `--no-pr-status` skips lookup; `--refresh-pr-status` forces refresh
  - JSON output includes `pr_status` field

#### Session Management
- Session Templates for reusable configurations
  - `hydra template list|create|show|delete|edit` commands
  - `hydra spawn <branch> --template <name>` applies template on spawn
  - Templates stored in `~/.hydra/templates/`
  - Variable expansion: `${BRANCH}`, `${SESSION}`, `${WORKTREE}`, `${REPO_ROOT}`
  - Merges with session-level `.hydra/config.yml` overrides
- Enhanced Group Workflows
  - `hydra group create <name> <branch> [branch...]` bulk creates groups
  - `hydra group wait <name>` blocks until all sessions in group are killed
  - `hydra group status <name> [--json]` shows group health

#### TUI Enhancements
- Session Preview panel
  - Press `p` to toggle preview panel showing session output
  - Displays last N lines (configurable via `HYDRA_TUI_PREVIEW_LINES`)
  - Auto-hides on small terminals

#### Testing
- New test suites: `test_kill.sh` (12 tests), `test_deps.sh` (15 tests)

### Fixed
- **Critical**: Undefined `require` function broke `--pr`, `--issue`, and `--after` spawn options
- **Bug**: Missing numeric validation in limits.sh could cause silent failures
- **Test**: `test_layout.sh` now properly tests with `HYDRA_DISABLE_HOTKEYS`
- **Test**: `test_json_output.sh` improved JSON structure validation

### Changed
- State file format extended to 7 fields (backward compatible):
  `branch session ai group timestamp deps pr`
- Kill command now loads limits library for queue processing
- Performance: Added tmux session caching to reduce subprocess calls

## [1.3.3] - 2025-12-27

### Added
- TUI Multi-Select for bulk operations
  - `SPACE` - Toggle selection on current session
  - `x` - Bulk kill all selected sessions
  - `G` - Bulk assign group to selected sessions
  - `Esc` - Clear selection (first press) or filters (second press)
  - Visual `[x]` markers for selected items
  - Selection count displayed in header
- New test suites
  - `test_switch.sh` - Input validation tests for cmd_switch
  - `test_json_output.sh` - JSON escaping and output validity tests

### Fixed
- **Security**: Unvalidated input in `cmd_switch` now validates numeric input and range
- **Bug**: `cmd_broadcast` count always showed 0 due to subshell variable loss (now uses temp file)
- **Bug**: `spawn_bulk` off-by-one error caused switch to wrong session (fixed leading space in concatenation)
- **Bug**: `json_escape` didn't handle newlines (now converts to spaces)

### Changed
- Performance: Cache tmux sessions list in `cmd_list` instead of per-entry `tmux has-session`
- Performance: Cache timestamp at command start instead of repeated `date +%s` calls
- Performance: Replace AWK field counting with POSIX shell word splitting in state.sh
- Documentation: Updated README with cleanup, tail, broadcast, wait-idle, group commands
- Documentation: Added missing environment variables to README table
- Documentation: Added `--json` flag to list/status help text

## [1.3.2] - 2025-12-27

### Added
- Session duration tracking
  - Spawn timestamp stored in state file (5th field)
  - `list` and `status` display duration (e.g., "2h 15m")
  - Backward compatible with old state file format
- JSON output mode for scripting/automation
  - `hydra list --json` - machine-readable session list
  - `hydra status --json` - system and session info
  - `hydra list --groups --json` - group summary
- Consistency checks in `hydra doctor`
  - Detects dead sessions (mapping exists, tmux session gone)
  - Detects orphaned worktrees (worktree exists, no mapping)
  - Detects stale locks (older than 60 seconds)
- New `hydra cleanup` command
  - Removes stale locks
  - Cleans dead session mappings
  - Offers to remove orphaned worktrees (interactive)
- State file auto-repair
  - Validates state file on load
  - Backs up and repairs malformed entries automatically

### Fixed
- Fixed `kill --all` not killing tmux sessions due to extra state file fields
- Fixed spacing in list output when AI tool present but no duration

## [1.3.1] - 2025-12-26

### Added
- TUI activity status indicators for sessions

### Changed
- Performance optimizations
  - Lazy library loading reduces startup time
  - In-memory state caching provides O(1) lookups
  - Git worktree list caching with 5-second TTL

## [1.3.0] - 2025-12-23

### Added
- TUI Enhancements
  - Help overlay (`?`): shows all keyboard shortcuts in a centered modal
  - Session tags (`t`): cycle through wip/review/priority tags for selected session
  - Tag filter (`T`): filter sessions by tag
  - Search/filter (`/`): real-time session search by branch name
  - Escape key clears all active filters
  - Progress indicators: visual feedback during spawn/kill/regenerate operations
  - Tags persisted in `~/.hydra/tags` file
- Shell completion dispatcher (`hydra completion <shell>`)
  - New `generate_completion()` function routes to bash/zsh/fish generators
  - Fixes: `hydra completion bash` now works correctly

### Fixed
- Bug #34: Missing `generate_completion` dispatcher caused completion command to fail
- Bug #39: Worktree cleanup now works when invoked outside the main repository
  - Added fallback to `git worktree list --porcelain` for locating worktrees
  - `kill` and `kill --all` now work from any directory

### Changed
- TUI header updated to show new keybindings
- Improved empty state messages in TUI to guide users based on active filters

## [1.2.0] - 2025-08-30

### Added
- Dashboard: configurable multi-pane collection
  - New env `HYDRA_DASHBOARD_PANES_PER_SESSION` controls panes per session:
    - `1` (default): collect first pane only (previous behavior)
    - `N`: collect up to N panes per session
    - `all`: collect all panes from each session (leaves one pane behind)
  - New CLI flag for `dashboard`: `-p, --panes-per-session <N|all>`
  - Pane titles set to branch names for clarity
- Per-head AI persistence in mapping file
  - `spawn` now stores the selected AI tool per head in `~/.hydra/map` (third column)
  - `list` and `status` annotate entries with `[ai: <tool>]`
  - `regenerate` auto-launches the stored AI tool for each restored session
- Concurrency mitigation for session naming
  - Reserve session names using best-effort lock directories under `~/.hydra/locks` during creation
  - Added stale lock cleanup to remove `.lock` dirs older than 24 hours
- Uninstall improvements
  - `uninstall.sh` now detects both default `~/.hydra` and custom `HYDRA_HOME` user data locations
  - New `--purge` flag removes user data non-interactively
 - Safer layout hotkeys
   - `setup_layout_hotkeys` binds `cycle-layout` via absolute hydra path or direct library invocation, reducing PATH injection risk
 - New environment flags for demos/automation
   - `HYDRA_SKIP_AI`: skip AI command launch on spawn
   - `HYDRA_DASHBOARD_NO_ATTACH`: create dashboard without attaching
   - `HYDRA_NO_SWITCH`: create sessions without auto-attaching on spawn

### Changed
- Documentation updated to describe per-head AI persistence and regenerate behavior
- Hardened validation in Git helpers (lib/git.sh): stricter branch and worktree path checks to prevent traversal and injection
 - Layout behavior: new panes inherit the current working directory using tmux `-c`
 - Fallback layouts: splits are anchored to the worktree path (`-c "$wt"`)
 - Dashboard workflow: support non-attaching mode via `HYDRA_DASHBOARD_NO_ATTACH`

### Fixed
- Inconsistent pane directories causing mismatched git branches across panes

### Notes
- Backward compatible with existing two-column map files; missing AI column is handled gracefully
- No changes required for users relying solely on `HYDRA_AI_COMMAND` or `--agents`
- Optional: Set `HYDRA_ALLOW_ADVANCED_REFS=1` to relax conservative charset checks for Git refs while retaining core safety guards

## [1.1.0] - 2025-07-03

### Added
- GitHub Issue Integration (`hydra spawn --issue <#>`)
  - Create heads directly from GitHub issues
  - Automatically generates branch names from issue titles
  - Validates issue numbers and fetches issue details via GitHub CLI
- Bulk spawn capability for multi-agent workflows
  - `hydra spawn <branch> -n <count>` creates multiple numbered sessions
  - `hydra spawn <branch> --agents "claude:2,aider:1"` for mixed AI agents
  - Automatic rollback on failure with confirmation prompts
- Kill all sessions command (`hydra kill --all`)
  - Optional `--force` flag to skip confirmation
  - Safely removes all sessions with proper cleanup
- Support for Google Gemini CLI as an AI tool option
  - Users can now spawn sessions with `--ai gemini`
  - Gemini provides free access with generous limits (60 req/min, 1000 req/day)
  - Added gemini to mixed agents support (e.g., `--agents "claude:2,gemini:1"`)
  - Requires Node.js 18+ and Google account authentication

### Changed
- Enhanced library path resolution for better reliability
  - Support for running hydra from inside hydra-managed sessions
  - Multiple fallback paths with HYDRA_ROOT environment variable
- Updated AI tool validation to include gemini
- Enhanced shell completions for all supported shells (bash, zsh, fish)
- Updated documentation with gemini requirements and examples
- Version bumped to 1.1.0-dev during development cycle

### Fixed
- Library path resolution when running hydra inside sessions
- Non-interactive mode handling in delete_worktree
- Test cleanup to prevent HYDRA_NONINTERACTIVE state leak
- ShellCheck compliance improvements (removed grep|wc -l patterns)

## [0.2.0] - 2025-06-18

### Added
- Multi-AI tool support with `HYDRA_AI_COMMAND` environment variable
  - Support for claude, codex, cursor, copilot, aider, and custom commands
  - Whitelist-based command validation for security
- Security hardening for branch names and paths
  - Protection against command injection
  - Path traversal prevention
  - Option injection protection in git commands
- Install and uninstall scripts with root permission checks
- MIT License file

### Changed
- Updated documentation to reflect multi-AI support
- Improved test robustness for non-terminal environments
- Enhanced error handling for missing command arguments

### Fixed
- Dashboard test failures with branch names containing slashes
- Git branch existence check using incorrect rev-parse syntax
- Test assertion checking for shell-specific error messages
- Race condition awareness documented for session naming

## [0.1.0] - 2025-06-18

### Added
- Initial release of Hydra
- Core POSIX-compliant shell CLI wrapping tmux ≥ 3.0 and git worktree
- Main commands:
  - `spawn` - Create new branch with dedicated tmux session and worktree
  - `list` - Show all active Hydra heads with their status
  - `switch` - Interactive session switching with fzf or numeric selection
  - `kill` - Remove session and worktree for a branch
  - `regenerate` - Recreate missing sessions from saved mappings
  - `status` - Quick status overview
  - `doctor` - System health diagnostics and performance testing
  - `cycle-layout` - Cycle through tmux pane layouts (Ctrl-L hotkey)
- Dashboard command for unified session monitoring
- Three built-in layouts: default, dev, and full
- Session state persistence in `~/.hydra/map`
- Layout persistence and restoration
- Shell completion support for bash, zsh, and fish
- Comprehensive test suite with 107 tests
- Full documentation including README and CLAUDE.md

### Fixed
- Library path resolution for installed executable
- POSIX compliance issues in initial implementation
- Critical unbound variable errors

[0.2.0]: https://github.com/yourusername/hydra/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/yourusername/hydra/releases/tag/v0.1.0
[1.2.0]: https://github.com/yourusername/hydra/compare/release/v1.1.0...release/v1.2.0

[Unreleased]: https://github.com/Someblueman/hydra/compare/v2.9.0...HEAD
[2.9.0]: https://github.com/Someblueman/hydra/compare/v2.8.0...v2.9.0
[2.8.0]: https://github.com/Someblueman/hydra/compare/v2.7.0...v2.8.0
[2.7.0]: https://github.com/Someblueman/hydra/compare/v2.6.0...v2.7.0
[2.6.0]: https://github.com/Someblueman/hydra/compare/v2.5.0...v2.6.0
[2.5.0]: https://github.com/Someblueman/hydra/compare/v2.4.0...v2.5.0
[2.4.0]: https://github.com/Someblueman/hydra/compare/v2.3.0...v2.4.0
[2.3.0]: https://github.com/Someblueman/hydra/compare/v2.2.1...v2.3.0
[2.2.1]: https://github.com/Someblueman/hydra/compare/v2.2.0...v2.2.1
[2.2.0]: https://github.com/Someblueman/hydra/compare/v2.1.0...v2.2.0
