# Command and configuration guide

See `hydra help` for complete command syntax. Fleet and remote tasks shipped in
v2.1.0; workflow data, approval waits, headless adapters and local objective
planning in v2.2.0. Version 2.6.0 changes the interactive `spawn` default, keeps
`init` out of the source tree and reworks the native TUI; 2.7.0 adds in-app task
launch and agent plan proposals. See the [changelog](../CHANGELOG.md) for upgrade
notes.

## Starting out

Open `hydra` in your repository and press `n`.
Each view explains its concept where you meet it, in its empty state: Work says
what a head is (one piece of work with its own branch, worktree, terminal and
agent, with an example), Workflows what a workflow is and when a single
conversation is enough, the plan pane that a proposal runs nothing until it is
validated and the exact revision approved, Overview's run panel how a run starts,
Attention what needs a decision (an approval request, a failed check, a result to
review, a stopped terminal), Hosts when a remote machine helps, and Details that
`Session` is the live terminal while `Reported` is the outcome the agent declares.
While typing to an agent the status row says that leaving or closing the view
keeps the agent running; removal and execution approval state their consequences
before `y`. The shell TUI (`hydra tui --basic`) carries the same explanation of a
head in its empty list.
Enter a task name, choose an available agent profile (`none` opens a shell), then
enter an objective or leave it blank to start a conversation. A task name that is
not already a valid branch, such as `add kill dry run`, starts on a derived branch
(`add-kill-dry-run`); the agent still sees the task name. Hydra registers the
project, creates the head and opens agent input in the same workspace. Opening
Hydra alone does not start work. Provider sign-in and permission prompts remain
visible in the agent pane; existing repository trust checks still apply.

For a plan, the agent started for a task is asked to plan with you first. It
writes a short plan (`hydra plan schema`) and publishes it from its head with
`hydra plan propose plan.json`; Hydra checks it against the project's plan
policy at once and prints anything the agent must fix. You then review and
approve it yourself:

1. `hydra plan show` prints the plan in words: each step, the worker's full
   instructions, the commands that run, "changes only branch X", the limits in
   minutes, what counts as done and the text of any script the plan carries.
2. `hydra plan approve` shows it again and asks `Run revision N of this plan?`.
   One `y` runs exactly that revision; a republished plan is a new revision that
   needs approving again. Automation passes `--accept` with the digest
   `hydra plan show --json` prints; `HYDRA_NONINTERACTIVE` never approves.
3. `hydra plan status` follows the run. The worker agent works on its own new
   branch, Hydra commits what it changed, and each check passes when its
   command exits 0 on that commit.
4. `hydra plan review` shows the verdict, the worker's commits and diff, and the
   checks with their logs.
5. `hydra land <worker>` merges exactly the verified commit into your checkout's
   current branch after you confirm. It refuses while your checkout has
   uncommitted changes to tracked files or when the worker branch has moved
   since its checks ran, never pushes, then removes the worker head and keeps
   its branch.

None of these needs a value copied: without a head or run ID they use the
current or only proposal and the most recent plan run. Approving, running a
plan and changing the policy are refused from inside an agent's head (where
`HYDRA_HEAD_ID` or `HYDRA_INSTANCE_ID` is set); this guards against an obedient
agent approving its own plan and is not a security boundary, since the agent
runs as you. `hydra plan policy` shows the project's plan policy (by default sh,
git, make and the planning head's own agent profile, writes only inside branches
the plan creates, one step at a time, at most 60 minutes and four heads, 1 MiB of
artifacts, a 1 GiB free-space floor, no retries or repairs) and
`--allow-tool <tool>`, `--remove-tool <tool>` and `--reset` change it. Approval
reads the checkout Hydra runs in at its current commit; commit or stash tracked
changes there first. Once a plan runs, changes to your checkout do not affect
it. The policy governs the compiled workflow, not the agent process's OS
permissions. The control centre shows the same plan and approval; expert plans
are described under [Advanced plans](workflows.md#advanced-plans).

Interactive `spawn` and `spawn --resume` now open agent input inside the native
control centre; without the optional native TUI they print the head's context and
stay in the terminal. `--attach` selects direct tmux attachment; `HYDRA_NO_SWITCH`, headless and
non-TTY invocations retain their existing behavior. `Ctrl-]` returns input to
Hydra, `Ctrl-\ x` closes only the attached client, and `Ctrl-\ q` exits the UI;
[TUI keys](#tui-keys) lists these chords and how to change them.

UI removal refuses ordinary untracked files as well as tracked changes. The CLI
exposes the same protection as `hydra kill <branch> --protect-untracked`; existing
noninteractive CLI cleanup behavior is unchanged when that flag is omitted.
Ignored files retain the existing Git removal semantics.

`hydra kill <branch> --dry-run`, `hydra kill --all --dry-run`, and `hydra kill
-g <name> --dry-run` preview active heads without confirmation or teardown.
Each preview shows the recorded session and worktree path, separate tracked
(staged or unstaged) and ordinary untracked change indicators, and that the
branch is kept. An unavailable worktree has unknown change indicators.
`--force` is accepted with `--dry-run` but does not change the preview.

- `hydra` with no arguments opens the control centre (`hydra tui`) when you run it
  from a terminal inside a Git repository. Outside a repository, or without a
  terminal, it prints a short usage with the most common commands.
- `hydra help` (or `hydra --help`) prints the full command reference.
- `hydra <command> --help` (or `-h`) prints that command's usage and exits 0, for
  example `hydra spawn --help` or `hydra kill --help`.
- `hydra doctor` reports the coding agents found on PATH. With none installed it
  suggests Claude Code or Codex, or a plain terminal task with
  `hydra spawn <branch> --no-agent`.
- `hydra doctor` also fails when Hydra's own state (`$HYDRA_HOME` and the
  repository's `.git/hydra`) is group- or other-writable, because Hydra refuses
  shared state. Hydra creates its state privately under any umask; entries left by
  earlier releases run under a group-writable umask such as Ubuntu's `002` are
  listed. `hydra doctor --fix-permissions` removes group and other write from your
  own files and directories there, never follows symbolic links, and only reports
  entries owned by someone else. Your worktrees keep following your umask.
- `hydra list` prints one aligned row per head: `BRANCH`, `AGENT`, `SESSION`
  (`running`, `terminal gone`, `stopped`, or `unknown`), `REPORTED` (the declared
  outcome or `-`), and `AGE`. The current head is marked with `*`. Add `--verbose`
  for the session name and the full `[declared]`/`[observed]`/`[live]` fields;
  `hydra list --json` is unchanged.

```sh
# Create a new head (tmux + worktree); opens agent input inside Hydra
hydra spawn feature-branch [-l default|dev|full]
hydra spawn feature-branch --dry-run --no-agent
hydra spawn feature-branch --profile claude --prompt "Implement the task"
hydra spawn feature-branch --profile codex --prompt-file task.md
hydra spawn feature-branch --attach          # expert: take over this terminal
hydra spawn feature-branch --resume          # start again after `hydra kill`

# From a GitHub issue
hydra spawn --issue 123

# Bulk and mixed agents
hydra spawn feature -n 3 --profile aider
hydra spawn exp --agents "claude:2,aider:1"

# Inspect & switch
hydra list              # table: BRANCH AGENT SESSION REPORTED AGE
hydra list --verbose    # session names plus full lifecycle fields
hydra list --json       # JSON output for scripting (unchanged by --verbose)
hydra list --git        # add recorded-base Git evidence
hydra list -g mygroup   # filter by group
hydra switch feature/ui # enter a named head directly
hydra switch            # interactive (fzf if available)

# Manage
hydra kill feature-branch
hydra kill feature-branch --dry-run       # preview only; never prompts or changes state
hydra kill --all --dry-run                # preview every active head
hydra kill -g backend --dry-run           # preview an active group
hydra kill --all [--force]
hydra cleanup           # stop dead heads; remove stale locks; offer to remove leftover worktrees (branches kept)

# Group operations
hydra group feature-x backend    # assign to group
hydra group feature-x            # show group
hydra group feature-x --clear    # remove from group

# Session output
hydra tail feature-x             # view last 50 lines of session output
hydra tail feature-x -f          # follow session output continuously

# Commands and inbox steering
hydra exec --branch feature-x --timeout 300 -- make test
hydra exec --group backend --timeout 300 -- make test
hydra send feature-x "Please review the failing test"
hydra recv --peek               # run inside the receiving head

# Durable lifecycle and review evidence
hydra lifecycle feature-x --json
hydra outcome feature-x done --actor agent
hydra wait feature-x --for outcome=done --timeout 300
hydra diff feature-x --stat
hydra review feature-x --json
hydra provenance feature-x --json

# Finite trusted workflows
hydra workflow validate examples/workflows/local-review.yml
hydra workflow dry-run examples/workflows/local-review.yml
hydra workflow run examples/workflows/local-review.yml
hydra workflow review PROJECT RUN STEP ATTEMPT REVISION [REFERENCES_JSON]
hydra workflow status run_ID --json
hydra workflow attention --json
hydra workflow attention-data
hydra workflow cancel run_ID
hydra workflow requests run_ID --json
hydra workflow decide run_ID step_REQUEST_ID approve --by reviewer
hydra workflow resume run_ID
hydra workflow statistics-json                      # recorded runs, remote cohorts and telemetry (schema 3)
hydra workflow statistics-compare left.tsv right.tsv --format text

# Plans an agent proposes (optional native helper)
hydra plan schema                     # the short plan format
hydra plan show [HEAD] [--technical | --json]
hydra plan approve [HEAD]             # one y on the revision shown
hydra plan status [RUN_ID]
hydra plan review [RUN_ID] [--json]
hydra land WORKER [--into BRANCH] [--dry-run] [--keep-head]
hydra plan policy [--allow-tool TOOL | --remove-tool TOOL | --reset]

# Advanced plans: the full schema, explicit policies and compiled artifacts
hydra workflow plan schema
hydra workflow plan validate ../plan.json ../policy.json [--assets-dir ../assets]
hydra workflow plan compile ../plan.json ../policy.json ../compiled.json [--assets-dir ../assets]
hydra workflow plan show ../compiled.json
hydra workflow plan run ../compiled.json --accept SHA256_FROM_PREVIEW
hydra workflow plan result run_ID
hydra workflow plan explain ../compiled.json [--estimates ../estimates.json]
hydra workflow plan evaluate ../left.json ../right.json [--estimates ../estimates.json]
hydra workflow plan observed run_ID > ../estimates.json
hydra workflow plan decision record HEAD --select ../left.json 'reason' --reject ../right.json 'reason'
hydra workflow plan decision show HEAD DECISION_ID --json
hydra workflow plan expand HEAD --run run_ID --proposal ../expansion.json   # version N+1 of a finished run
hydra workflow plan expand HEAD --run run_ID --json                         # the chain, bounds and claim states

# Parallel safety and guarded integration
hydra claim add feature-x --path 'lib/*' --access write --reason refactor --expires-at 1790000000
hydra scope check feature-x --json
hydra collision feature-x feature-y --json
hydra resource allocate feature-x --port http=3000-3999
hydra gate run feature-x --name acceptance -- make test
hydra context create feature-x --diff --history 5
hydra sync feature-x --from main --gate acceptance --dry-run
hydra land feature-x --into main --gate acceptance --dry-run
hydra integrate release-group --base main --target main --dry-run
hydra integrate release-group --base main --target main --execute --gate 'make test'
hydra integrate train release-group --base main --target main --execute --gate 'make test'
hydra integrate status run_ID
hydra integrate resume run_ID
hydra integrate approve run_ID --by reviewer
hydra integrate promote run_ID       # local promotion; never pushes
hydra integrate cleanup run_ID --apply
hydra du
hydra gc --policy orphaned --dry-run   # leftover head_<id> worktrees whose head record is gone
hydra gc --policy orphaned --apply --path /path/to/head_ID   # remove one; dirty needs --include-dirty
hydra worktree doctor status

# System
hydra init --profile claude --trust      # host-local registration; source tree unchanged
hydra init --write-shared-config         # opt-in committed .hydra/config.yml
hydra agent list
hydra capabilities --json
hydra state verify
hydra events tail
hydra regenerate   # restore sessions after restart
hydra status       # per-head health
hydra status --json # JSON output
hydra doctor       # install, dependencies, and first-run readiness
hydra doctor --fix-permissions # remove group/other write from your own Hydra state
hydra snapshot --native # explicitly try the optional read-only native helper

# Dashboard & TUI
hydra dashboard                                    # multi-session overview
hydra tui                                          # native mission control
hydra tui --location remote                        # the same, opened on remote work (L cycles the filter)
hydra tui --capabilities                           # native TUI availability diagnostics
```

The control centre shows local and remote work together: heads on registered
remote hosts (`host/branch`) and remote tasks are rows of the same Work list,
with the host and the age of the observation they come from. `L` filters by
location, `a` talks to a local or remote agent, `x` removes a local head or
interrupts a remote agent, and remote rows whose observation is stale,
retained from a host that stopped answering, or unknown refuse every action.
See [fleet](FLEET.md#remote-work-in-the-control-centre).

## Launching heads and attaching

An interactive `hydra spawn` creates the head and leaves you in your current
terminal. It prints a short context block (branch, agent, worktree location, tmux
session) with the two next actions: `hydra tui` to follow the head in the control
centre, or `hydra switch <branch>` to attach to its terminal. Pass `--attach` to
take over the terminal immediately instead; this is the expert path and is never
the default. `HYDRA_NO_SWITCH=1` still creates the head without attaching and
overrides `--attach`. Non-terminal invocations (pipes, automation) and `--headless`
heads keep their existing messages and never attach.

An interactive head whose tmux session is gone (for example after a reboot) keeps
its worktree and files. `hydra resume --terminal <branch>` restarts only its
terminal: it requires the recorded worktree, never creates, resets or cleans it,
and starts what the head's profile resumes (Codex `resume --last`, a shell for
`none`). It refuses a headless head, a head whose terminal is still live, and a
head whose worktree is missing. Opening such a head from Work in `hydra tui`
(`a`) runs exactly this command and then attaches.

Durable head state outlives `hydra kill`, so `hydra spawn` and `hydra spawn --dry-run`
both refuse a branch whose head still exists and name the way forward: `hydra resume
<branch>` (or `hydra spawn <branch> --resume`, which takes the same resume path and
ignores spawn-only options such as `--prompt`) or a new branch name. A live head is
never given a second execution owner; `--resume` on a branch without a head simply
spawns it.

Attention results are immutable run/step/attempt evidence. A sealed result remains
inspectable when its execution head is no longer live; missing recorded head or
instance identity is reported as `identity_provenance: not_recorded` and never
grants attachment or action authority.

`hydra workflow review PROJECT RUN STEP ATTEMPT REVISION` returns a read-only
JSON review of the selected attempt. `REVISION` is the SHA-256 of its attention
revision. For an exact native selection, both `review` (JSON) and `review-data`
(framed text) accept `KIND PROJECT HOST TASK RUN STEP ATTEMPT HEAD INSTANCE
REQUEST BINDING REVISION_SHA256 IDENTITY_SHA256`; copy those fields from
`workflow attention-data`, using `-` for absent fields. The requested identity
and revision remain separate from the observed revision and computed readiness.

Review checks retained recipe, Git blob data hashes, graph, resource scalars,
sealed artifacts, and any accepted compiled plan and verification reports.
Historical inspection does not require the original recipe path or current
checkout HEAD. A planless base commit is recorded provenance; review does not
independently verify its source contents. A current approval reports `pending`,
its request state and evidence path, and explicit approve/reject argv with the
project working directory. Stale or expired context revokes those decision
routes. Opening review never records a decision; `accepted` remains false.

An optional final `REFERENCES_JSON` argument contains up to 16 objects with
`kind` (`transcript`, `log`, or `pr`) and `locator` (an absolute local path or
HTTP(S) URL). Local regular files get a bounded 4096-byte preview; URLs remain
supplied and unopened. References do not establish result readiness.

## Agent profiles and inputs

`--profile` selects an agent; `--no-agent` selects a plain shell. `spawn --headless`
creates a terminal-free workspace head (use `hydra exec --branch <branch> -- ...`
to run a command). Interactive
profiles include `agy` (Antigravity), `cursor` (Cursor Agent), `opencode`, `claude`,
and `codex`, plus launch-only integrations. Headless execution also supports `pi`.
See the complete profile and execution contract in [Contracts](CONTRACTS.md). `exec` runs a
command through Hydra's supervision and records its result. `send` queues steering
in the head's inbox, and `recv` reads it from that head. Queuing a message does not
prove the agent consumed it. Provider safe-point delivery requires the capability
and receipt described in [Contracts](CONTRACTS.md).

| Profile | Headless executable and mode | Interactive launch |
| --- | --- | --- |
| `agy` | `agy`, print/stream JSON | `agy` prompt mode |
| `cursor` | `cursor-agent --print --trust`, stream JSON | `cursor-agent` positional prompt |
| `opencode` | `opencode run --format json` | `opencode --prompt` |
| `claude` | `claude --print`, stream JSON | `claude` positional prompt |
| `codex` | `codex exec --json`, stdin prompt | `codex` positional prompt; interactive `resume --last` |
| `pi` | `pi --print --mode json` | Headless only; use a custom launch profile for an interactive head |
| `copilot`, `aider`, `gemini` | Launch-only integrations | Matching executable, no declared delivery contract |

Use `hydra agent list`, `hydra agent contract NAME`, and `hydra agent probe NAME` to
inspect availability. Resolution is explicit CLI profile, host-local project default,
one detected built-in executable, or `none`; multiple detected agents require an
explicit choice. Import custom declarations with `hydra agent import NAME adapter.json`.
Imports cannot replace reserved built-in names. Headless `exec --resume-run RUN_ID`
requires the exact successful recorded session, head, instance, worktree, and profile;
it never selects the provider's latest session. Interactive Codex restore is the
separate cwd-scoped `resume --last` convenience.

### Agents outside PATH

A built-in profile's executable (`claude`, `codex`, `cursor-agent`, `agy`,
`opencode`, `pi`, ...) resolves from PATH first, then from a location you
recorded. Provider installers usually write to `~/.local/bin`, which a
non-interactive SSH session's PATH often omits, so a fleet receiver would
otherwise report the agent as missing.

```sh
hydra agent locate                  # status per agent: on_path, recorded, found_off_path, ambiguous, missing
hydra agent locate --json           # agent-inventory schema 1
hydra agent locate --record claude ~/.local/bin/claude
hydra agent locate --forget claude
```

`locate` searches PATH plus `~/.local/bin`, `~/bin`, `~/.claude/local`,
`~/.opencode/bin`, `~/.pi/agent/bin`, `~/.npm-global/bin`, `~/.bun/bin`,
`~/.volta/bin`, `~/.local/share/pnpm`, `~/.cargo/bin`, `~/.local/share/mise/shims`,
`~/.asdf/shims`, `~/.nvm/versions/node/*/bin` (several versions are `ambiguous`),
`/usr/local/bin`, `/opt/homebrew/bin`, `/home/linuxbrew/.linuxbrew/bin` and `/snap/bin`.
It never sources a login shell and runs only `--version` of candidates it could
record. A record is a private scalar file,
`$HYDRA_HOME/agents/locations/EXECUTABLE` (0600 in a 0700 directory whatever the
umask), keyed by executable rather than profile. Hydra accepts and uses a record only
when the path is absolute, its file name equals the executable, it is an executable
file (a symlink keeps its invoked name), the entry and its target belong to you or
root, and the target is not group- or world-writable. A record never shadows PATH
and cannot create a profile: custom profiles still cannot reuse built-in names.
`spawn`, `agent list/show/doctor`, headless `exec --profile` and `agent probe` share
this resolution, and headless runs execute the probed absolute path.
`hydra fleet auth login HOST --agent A` resolves the remote executable the same way
and waits for the provider's sign-in to finish.

Headless Cursor passes `--trust` so Cursor Agent accepts each fresh head worktree;
it only skips Cursor's workspace prompt and never auto-approves commands (`--force`).
A failed headless run keeps a 4096-byte, character-safe excerpt of provider stderr
and of stdout the adapter could not decode in its receipt's `diagnostic` field; see
[Contracts](CONTRACTS.md#profiles-tasks-adapters-and-scopes).
Provider status reported by a headless run is a heuristic: Hydra records only
changes, and when the project state is too busy to record one (for example many
heads finishing together) the run continues and its receipt's
`diagnostic.observation` says why and which status, if any, was never recorded.

A declared `done` outcome is separate from verification: use `exec` or a named
`gate` to record whether a command passed, then review before integration.

For plan/policy authoring, scope review, report formats and disposable examples,
see [workflows](workflows.md). An accepted digest binds execution
scope; it does not establish that a natural-language objective was interpreted
correctly. Inspect the final deliverable and its checks.

## Fleet discovery sources and revocation

`hydra fleet discover` and `hydra fleet qualify` take explicit selections only.
Besides `--ssh ALIAS` and `--inventory FILE --select NAME`, an opt-in live source
reads the operator's own inventory through its normal client or files:

```sh
hydra fleet discover --source ssh-config --diff --json            # ~/.ssh/config Host names
hydra fleet discover --source ansible-inventory:/etc/ansible/hosts --json
hydra fleet discover --source dns-sd --json                       # avahi-browse, or unavailable
hydra fleet qualify --source ssh-config --select build --require list
```

Each report names the scope the records belong to, the cache age, and the
added/removed/changed/conflicting records against the previous private snapshot.
A missing file or client, a timeout, partial output, a conflicting identity or a
revoked identity is reported and never widens the selection. Credentials found in
an inventory are counted and dropped.

```sh
hydra fleet enroll status --input enrollment.json --json   # read-only per-host progress
```

`enroll status` lists an intent's reviewed hosts with what `enroll apply` recorded
for each (`not_started`, `enrolled`, `outcome_unknown`, `revoked`, ...) without
connecting or writing anything. The control centre's onboarding view (`O` on Hosts)
shows the same records.

```sh
hydra fleet revoke transport build --reason "host key rotated"
hydra fleet revoke candidate cand_6275696c64 --reason "retired"
hydra fleet revoke list --json
```

A revocation is final for that identity, keeps who/when/reason and the superseded
binding, blocks new operations and pending enrollment intents, and never cancels a
task, uninstalls a package, deletes remote state or edits `known_hosts`. See
[fleet](FLEET.md#discover-qualify-and-enroll-hosts) and
[revocation](FLEET.md#revoke-decisions).

## Expert terminal utilities

These operate on tmux panes and terminal activity. `broadcast` does not provide
`exec`'s command-result contract, and terminal inactivity does not establish task
completion. Use them for interactive terminal work rather than workflow gates.

```sh
hydra broadcast -g backend "git status" # type text into the group's target panes
hydra wait-idle -g backend -s 10        # wait for ten seconds of terminal inactivity
```

## Pools and schedules

A pool is a file-backed queue of tasks that run through the ordinary
`hydra workflow run` and `hydra exec` commands. Bounds are sealed when the
pool is created; tasks become claimable once their expansion is sealed.

```sh
hydra pool create review --max-outstanding 2 --max-tasks 32   # seal the bounds
hydra pool add review --workflow review-change                 # queue a workflow
hydra pool add review --exec feature-x --idempotent --retry 1 -- make test
hydra pool seal review                                         # freeze membership
hydra pool run review --worker laptop                          # claim, run, report
hydra pool status review --json                                # inspect the record
hydra pool cancel review                                       # stop queued and claimed work
```

A worker that dies leaves a stale claim. The next `claim` or `run` reconciles
it from evidence: a recorded workflow run decides, an idempotent task with
budget is re-queued, and anything else waits as `recovery-required` for
`hydra pool complete review <task> --by you --outcome succeeded|failed|retry`.
Expert workers call `hydra pool claim review --worker w --owner-pid $$` and
`hydra pool complete review <task> --token <token> --outcome succeeded`
themselves; a token that no longer matches is refused as `stale_owner`.

A schedule starts work from a host timer that you install yourself; Hydra
prints the exact crontab, systemd or launchd entry and never runs a daemon.

```sh
hydra schedule add nightly --every 1d --workflow review-change   # prints the timer entry
hydra schedule add inbox --every 15m --pool review --workflow review-change
hydra schedule add drain --every 15m --pool review               # worker loop by timer
hydra schedule status nightly --format systemd                   # reprint the units
hydra schedule run-now nightly                                   # fire this slot by hand
hydra schedule remove nightly                                    # then remove the timer entry
```

Each firing is `hydra schedule run-now <name>` with the current slot as its
dedup token, so a double timer fire or a manual run in the same slot submits
once. Missed slots are skipped by default; `--missed catch-up --catch-up-limit
N` fires the newest N missed slots. `--reboot-recovery` adds the `@reboot`,
`Persistent=true` or `RunAtLoad` entry; it starts a new firing after a
restart and never replays work that was in flight. See
[workflows](workflows.md#pools-and-schedules) for the owner contract.

## Shell completions

```sh
# Bash
hydra completion bash >> ~/.bashrc

# Zsh: ensure this directory is on fpath before compinit
mkdir -p ~/.zsh/completions
hydra completion zsh > ~/.zsh/completions/_hydra

# Fish
mkdir -p ~/.config/fish/completions
hydra completion fish > ~/.config/fish/completions/hydra.fish
```

## Layouts

- `default`: Single full-screen pane
- `dev`: Two panes (editor ~70% left, terminal right)
- `full`: Three panes (editor top-left, terminal top-right, logs bottom)
- Cycle in-session with `Ctrl-L`.

## Configuration

### Environment Variables

| Variable | Description |
|----------|-------------|
| `HYDRA_NO_SWITCH` | Set to `1` to never attach on spawn, even with `--attach` (demos, automation) |
| `HYDRA_HOME` | Runtime dir (default `~/.hydra`) |
| `HYDRA_AI_COMMAND` | Default agent override; project profiles are preferred |
| `HYDRA_ROOT` | Force library discovery when running from source |
| `HYDRA_DASHBOARD_PANES_PER_SESSION` | `1`, `N`, or `all` |
| `HYDRA_SKIP_AI` | Non-interactive shell-only default; `spawn --no-agent` is explicit |
| `HYDRA_DASHBOARD_NO_ATTACH` | Create dashboard without attaching |
| `HYDRA_NONINTERACTIVE` | Skip all confirmation prompts (for CI/automation) |
| `HYDRA_REGENERATE_RUN_STARTUP` | Run startup commands on regenerate |
| `HYDRA_ALLOW_ADVANCED_REFS` | Relax branch charset validation (use with care) |
| `HYDRA_DISABLE_YAML` | Disable YAML config parsing |
| `HYDRA_CORE` | Explicit optional `hydra-core` executable for native qualification |
| `HYDRA_CORE_TIMEOUT_SECONDS` | Native handshake/command timeout (default `2`) |
| `HYDRA_TUI_BIN` | Explicit optional `hydra-tui` executable for qualification |
| `HYDRA_TUI_KEYS` | Keys file for the native TUI instead of `$HYDRA_HOME/tui/keys.conf` (see [TUI keys](#tui-keys)) |

Per-head profile, task, identity, worktree path, instance, and lifecycle event are
stored in project-scoped state v2. The durable state, security, out-of-band
execution, provenance, and parallel coordination rules are in
[Contracts](CONTRACTS.md) and [workflows](workflows.md). Native dispatch, terminal safety,
keymaps, accessibility, and recovery are documented in
the native helpers.

Supported systems and upgrade policy are in the root README and [Contracts](CONTRACTS.md).
Existing 1.9 installations should review the state backup and migration guidance
in [Contracts](CONTRACTS.md) before removing their backup.

### TUI keys

While you type to an attached agent every key goes to the agent, except Hydra's
own chords. A chord is one to four keys pressed in order. `Ctrl-]` alone leaves
agent input; the other commands follow `Ctrl-\`:

| Action | Default chords | Effect |
|--------|----------------|--------|
| `terminal.leave` | `ctrl+]`, `ctrl+\ tab` | Leave input: keys go to Hydra, the view stays open |
| `terminal.close` | `ctrl+\ x` | Close the view; the agent keeps running (`a` reopens it) |
| `terminal.next` | `ctrl+\ n` | Next open agent view |
| `terminal.reconnect` | `ctrl+\ r` | Reattach a disconnected client |
| `terminal.history` | `ctrl+\ [` | The agent's tmux history (PgUp/PgDn; `q` returns) |
| `terminal.live` | `ctrl+\ ]` | Back to live output |
| `terminal.literal` | `ctrl+\ ctrl+\`, `ctrl+\ ctrl+]` | Send the chord's last key to the agent |
| `app.quit` | `ctrl+\ q` | Quit Hydra; agents keep running |
| `screen.work` ... `screen.stats` | `ctrl+\ 1` ... `ctrl+\ 4` | Work, Inbox, Hosts, Stats |
| `view.statistics` | `ctrl+\ D` | Stats |
| `workspace.layout.conversation` | `ctrl+\ A` | Work's Details page |
| `workspace.layout.plan` | `ctrl+\ B` | Work's Plan page |
| `workspace.layout.monitor` | `ctrl+\ C` | Work's Run page |
| `workspace.zoom` | `ctrl+\ z` | Zoom the focused pane |
| `workspace.split` | `ctrl+\ S` | Two agents side by side |

After the first key of a chord the status line shows the chord so far and what
may follow (`Ctrl-\ ...  Tab leave input / x close view / ...`). A key that completes
nothing is named (`Ctrl-\ k is not bound`) and the whole chord is dropped: no part
of it reaches the agent. Letters match by case, so `ctrl+\ D` and `ctrl+\ d` differ.
Chords that start with a Ctrl key also work when no agent pane holds input, from
every view; other chords work only while you type to the agent. Hints, notices and
`?` help always name the chords in effect.

`Ctrl-B` is not Hydra's: it reaches the agent, so the tmux the agent runs in
answers its usual prefix. `Ctrl-\` and `Ctrl-]` are single control bytes that
terminals send without special settings; a shell normally turns `Ctrl-\` into a quit
signal for the running program, and Hydra takes the key before that happens. The
two `terminal.literal` chords type either key to the agent. On a keyboard layout
where `\` or `]` needs AltGr, choose other keys.

To change the keys, write a keys file at `$HYDRA_HOME/tui/keys.conf` (default
`~/.hydra/tui/keys.conf`), or name one in `HYDRA_TUI_KEYS`. `hydra tui --print-keys`
prints the keys in effect in the file's own syntax, so this is a valid start:

```sh
mkdir -p ~/.hydra/tui && hydra tui --print-keys > ~/.hydra/tui/keys.conf
```

- One `action = chord[, chord]` per line, at most two chords per action; `none`
  unbinds the action; `#` starts a comment; actions a file does not name keep
  their defaults.
- A chord's keys are separated by spaces. A key is a single character, written as
  typed (`D` is Shift-d), or a name: `tab`, `enter`, `esc`, `space`, `backspace`,
  `delete`, `insert`, `up`, `down`, `left`, `right`, `home`, `end`, `pageup`,
  `pagedown`, `f1` to `f12`, and `comma`, `hash`, `plus`, `minus` for those characters.
- Modifiers are `ctrl`, `alt` and `shift`, joined with `+` (or `-`): `ctrl+]`,
  `alt+enter`, `shift+tab`, `ctrl+alt+x`. A terminal sends some combinations as
  another key, and they are read that way: `ctrl+i` is `tab`, `ctrl+m` is `enter`,
  `ctrl+[` is `esc`; `ctrl+B` is `ctrl+b`.
- Two actions cannot share a chord, and a chord cannot be the beginning of another.
- `terminal.leave` cannot be unbound: without it input could never return to Hydra.

A keys file never stops Hydra from starting. A line it cannot read keeps that
action's default while the other lines apply; conflicting chords put every key back
to its default. Either way a notice (and the `?` help) names the file, the line and
the reason. `hydra tui --print-keys` reports the same on stderr and exits 1, so it
also checks a file. This file brings back the `Ctrl-B` prefix of earlier releases:

```
terminal.leave = ctrl+b tab
terminal.close = ctrl+b x
terminal.next = ctrl+b n
terminal.reconnect = ctrl+b r
terminal.history = ctrl+b [
terminal.live = ctrl+b ]
terminal.literal = ctrl+b ctrl+b
app.quit = ctrl+b q
view.statistics = ctrl+b D
workspace.layout.conversation = ctrl+b A
workspace.layout.plan = ctrl+b B
workspace.layout.monitor = ctrl+b C
workspace.zoom = ctrl+b z
workspace.split = ctrl+b S
screen.work = ctrl+b 1
screen.inbox = ctrl+b 2
screen.hosts = ctrl+b 3
screen.stats = ctrl+b 4
```

With it `Ctrl-B` is Hydra's again: the agent's tmux receives its prefix only through
`ctrl+b ctrl+b`.

## Project registration and shared config

`hydra init` registers the repository on the current host only. Its identity,
default agent profile, worktree root, and trust decision live under the
repository's Git common directory (`.git/hydra/`), so the source tree and
`git status` stay unchanged. Rerunning `hydra init` keeps the stored profile and
worktree root unless you pass `--profile`, `--no-agent`, or `--worktree-root`.

Shared repository configuration is an explicit opt-in:
`hydra init --write-shared-config` previews and writes a commented
`.hydra/config.yml` template meant to be committed; `--force` replaces an
existing file. The template runs nothing by itself, and every host still has to
approve the exact content with `hydra init --trust` before setup commands,
startup commands, or repository workflows execute.

Earlier releases left a generated `.hydra/config.yml` stub and `.hydra/local.yml`
in the tree and added an `.git/info/exclude` rule for the latter. The next
`hydra init` removes exactly those generated files and that rule, imports a
differing `worktree_root` from `local.yml` into the host record, and reports the
migration once. Tracked files and any `.hydra/config.yml` with real content are
never removed.

## YAML Config (optional)

Place `.hydra/config.yml` in the worktree or repo root to declare windows/panes and optional startup commands:

```yaml
windows:
  - name: editor
    panes:
      - cmd: nvim
      - cmd: bash
        split: v
  - name: server
    panes:
      - cmd: npm run dev
startup:
  - echo "Project ready"
```

- Repository-controlled setup is inert until its exact config hash is accepted with
  `hydra init --trust`; changing the file requires renewed trust.
- On spawn/regenerate: windows and panes are applied. `startup` runs on spawn, and on regenerate only if `HYDRA_REGENERATE_RUN_STARTUP=1`.
- Minimal parser supports the fields above; values are plain strings.

## Hooks (optional)

Add `.hydra/` scripts to customize lifecycle:

- `hooks/pre-spawn`: runs before tmux session; env: `HYDRA_WORKTREE`, `HYDRA_BRANCH`.
- `hooks/layout`: override built‑in layouts; env: `HYDRA_SESSION`, `HYDRA_WORKTREE`.
- `startup`: one command per line; sent to the main pane after spawn.
- `hooks/post-spawn`: after layout/startup; env: `HYDRA_SESSION`, `HYDRA_WORKTREE`, `HYDRA_BRANCH`.

## Head session bootstrap

An interactive head receives `HYDRA_PROJECT_ID`, `HYDRA_HEAD_ID`,
`HYDRA_INSTANCE_ID`, `HYDRA_BRANCH`, `HYDRA_WORKTREE`, `HYDRA_STATE_DIR`, and
`HYDRA_TASK_FILE` without anything being typed into its shell or shell history:

- The values are set in the tmux session environment before the first pane
  starts (`new-session -e` on tmux 3.2 or newer; `set-environment` on 3.0/3.1),
  so every window and pane created later inherits them.
- The first pane runs a generated launcher, recorded at
  `<state-dir>/instances/<instance>/launcher`, which exports the same values,
  prints a short banner (`Hydra head <branch> · agent <profile> · repo <name>`
  plus a `hydra provenance` pointer), starts the agent by the absolute path that
  `hydra agent list` and `hydra provenance` report, and then execs your login
  shell so the pane stays usable after the agent exits. While the agent runs it
  owns the terminal: Ctrl-C reaches only the agent, and Ctrl-Z is ignored.
- Exact identities and paths stay out of the transcript; `hydra provenance
  <branch>` prints them, together with the launcher path. `hydra resume` uses
  the same mechanism with its resume recipe.
- `startup` lines and YAML `startup`, window, or pane entries are still typed
  keys. When such entries apply, the agent command is typed after them, exactly
  as before, so it keeps following those commands.

## Dashboard

- Shows panes from all heads in one tmux window; exits with `q` and restores everything.
- Collect more than one pane per head with `--panes-per-session <N|all>` or `HYDRA_DASHBOARD_PANES_PER_SESSION`.

## TUI

`hydra tui` is native mission control: the `hydra-tui` helper renders shell-owned
state and runs every action through the shell CLI. There is no other interface
behind it. Without a qualified helper (missing, or built for another Hydra
version) `hydra tui` prints how to get one and exits with status 1; `hydra list`
and `hydra switch` work without it. If the helper exits with an unexpected status,
Hydra restores the terminal, reports that status on one line and returns it.

The source installer builds the native TUI automatically when a C99 toolchain is
available; `make build-tui` builds it in a checkout. Set `HYDRA_INSTALL_TUI=never`
for a compiler-free install without `hydra tui`.

```sh
hydra tui                  # native mission control
hydra tui --capabilities   # availability and observation diagnostics
```

Every native screen shares one frame: a header with the four screens (`1 Work`,
`2 Inbox` with how many items need you, `3 Hosts`, `4 Stats`) and a summary of the
project at the right, the screen itself, and a two-line footer with the current
status and the keys that apply. `--view` names a screen (`work`, `inbox`, `hosts`,
`stats`); the names of earlier releases still work and open the page that replaced
them (`heads` and `workspace` Work, `detail` and `coordination` Work's Details page,
`overview` and `workflows` its Run page, `attention` and `recovery` Inbox,
`statistics` Stats). Only changed cells are repainted, so idle refreshes
do not flicker. Session states use plain words: `running`, `terminal gone`,
`stopped` and `unknown`; internal tokens stay behind `d` (technical details).
Statistics (`D`) reads recorded workflow runs: `M` cycles the metric pages, `C`
opens the Cohorts page (steps that ran as fleet tasks grouped by host,
placement, generation and provider, with the receipts' wall, CPU, peak RSS and
transport bytes), and `L` cycles the location filter between all, local, remote
and each recorded host. Unmeasured values read `unknown` with their reason, never
zero; see [workflows](workflows.md#reviewing-a-finished-plan).

One interaction model applies everywhere:

| Key | Action |
|-----|--------|
| `1`-`4` | Work, Inbox, Hosts, Stats (`I`, `H`, `W`, `D` jump there too, and Esc comes back) |
| `Tab` / `Shift-Tab` | Next / previous pane in Work (list, conversation, side pane); next / previous tab elsewhere |
| `[` / `]` | Previous / next page of Work's side pane: Details, Plan, Run (and Conversation below 120 columns) |
| `Up` / `Down`, `j` / `k` | Select |
| `Enter`, `a` | Talk to the selected agent in the conversation (a remote head attaches through `hydra fleet attach`); on a run, expand or collapse it |
| `l` / `h` | Expand / collapse the selected run in Work |
| `Esc` | One step back: the filter, the location, the zoom, then focus back to the list |
| `n` | Start a new task (branch, worktree, terminal and agent); choose "Plan first" or "Just do it" |
| `x` | Remove the selected or marked heads after an in-app confirmation; on a remote head, interrupt its agent |
| `L`, `r` | Location filter (all, local, remote, each host); observe a remote host again |
| `Y` / `N` / `R` / `X` | On a remote task row: approve, reject, resume, cancel, each confirmed |
| `Space` / `A`, `G` | Mark one / all heads, group the marked heads |
| `/`, `:` | Filter heads, search explicit actions |
| `d` | Technical details on the Details page |
| `3` | Hosts: `A` adds a remote machine over SSH, Enter opens a host's work or continues its setup, `[`/`]` or `O` the Onboarding page |
| `?`, `t`, `q` | Keyboard help, theme, quit |

Work lists the heads you started. Heads a workflow run created sit inside their
run, and a run launched from a planning conversation sits inside that planning
head; runs start collapsed and the header counts their heads separately. Beside
the list the conversation shows the selected head's latest output, read-only,
until you talk to it; the side pane shows the selection's Details, its Plan (a
head whose agent published a plan, or a plan imported with `:`) with `E` approve,
`F` request changes, `L` land the finished worker, `r` result review and `s` run
status, and its Run (the run's graph, its steps and the selected step's
evidence; `Y`, `N`, `R`, `X` act on it). A headless head is described by the step
that runs on it (agent, model and effort, tokens, duration) rather than by a
terminal, and the conversation shows that step's live output. See
[workflows](workflows.md#heads-a-plan-run-creates).

Statistics (`D`) has up to three pages, Sessions, Runs and Cohorts; `[` and `]`
go between them. Head sessions lists
every start or resume of a head in this project, newest first, with its agent,
state (`open`, `ended`, or `replaced` when a resume took over), start, length
and how long its terminal was active or quiet; a project without workflow runs
opens on this page. Active and quiet are inferred from
terminal output: while a control centre is open it notes when each interactive
head's tmux window stops or resumes producing output (quiet after 10 seconds of
silence, active once output has continued for 4 seconds). They say what the
terminal did, not what the agent was waiting for, and the page labels them as
inferred. Time nobody watched (no control centre open) is shown as unobserved,
never as quiet, together with the share of session time that was observed; a
value that is not known is `--`. Headless and remote heads are not observed:
their owners report exact status, which counts where it exists. A finished
session is kept as a small summary with the project, so it stays listed after
`hydra kill` and its terminal activity does not accumulate in the head's
records. Tokens and cost
are not measured for sessions. `T` changes the range, `/` filters by branch,
agent or head as you type (Enter keeps the filter, Esc clears it), `!` keeps
sessions declared failed or blocked, `0` resets every filter, and Enter shows one
session's evidence: beside the list when the terminal is wide enough (Tab moves
between the two), in its place otherwise. `g` shows the session's head in Details,
and Esc comes back. A filter that hides everything says so ("Nothing matches the
filters"), which is not the same as nothing recorded. The workflow runs page shows
recorded runs with their steps, timing, retries and tokens (`M` for metrics, `w`
for one workflow at a time, `g` for the run's graph). When steps ran as remote
tasks a third page, Cohorts, groups them (`C` also opens it), and `L` cycles the
location filter, which the page strip shows as a chip. `ctrl+r` reads the sample
again. `hydra workflow statistics-json` exports both.

Hosts (`3`) lists the machines Hydra can run agents on beside a detail pane: This
machine (its heads, tmux and Hydra versions and the agents `hydra agent list`
finds, read when Hosts opens), the observed remote hosts with their connection,
head count and observation age, the setups not finished yet, and "+ Add a host".
Enter on a host (or on This machine) opens its work in Work, filtered to it; Esc
comes back to Hosts. `r` observes the hosts again and `/` filters the list. `A`
opens a form for the SSH destination and a name, then walks through the guided
`hydra remote setup` steps (host key, requirements, installing Hydra, agents and
sign-in) in the detail pane, showing each plan before anything changes: the host
key is trusted only by typing `yes`, and a plan is approved with `y` (Enter never
approves; `n` or Esc declines). A running step shows its elapsed time; `c`
cancels it and `b` or Esc leaves it running. Enter continues an unfinished setup,
`e` edits one that has not changed the remote yet and `x` removes its record
after showing what stays. See [guided remote setup](FLEET.md#guided-remote-setup).

The Onboarding page of Hosts (`]`, or `O`) opens onboarding for hosts found through `hydra fleet discover`:
candidates with filters for state, source, platform and label (`s`, `S`, `P`,
`l`, `/`), a detail with provenance, observation age, qualification result,
fingerprints and revocation, the source changes (`c`) and the reviewed intent
(`i`) with per-host apply progress. `Q` qualifies, `r` reviews a compatible
candidate into an intent, and `a` shows the exact `hydra fleet enroll apply
--confirm DIGEST` command and runs it once you confirm the dialog that names
that digest (it defaults to Cancel).
Revoked identities can be inspected, not reviewed. See
[onboarding in the control centre](FLEET.md#onboarding-in-the-control-centre).

Removal confirms the exact targets in the UI, runs `hydra kill` per head with output
captured, reports a concise result in the status line and opens the full output only
when something was kept (for example uncommitted changes). The current tmux session
is always skipped. Recovery findings are explained in plain language; `Enter` runs
the recorded check and shows its output in place, and `d` keeps the raw kind,
source and confidence available.

In Work, `z` zooms the focused pane and `S` shows two agents side by side.
While typing to an attached agent, Esc, Tab and `Ctrl-B` go to the agent; `Ctrl-]` leaves
input (keys go to Hydra, the view stays open), `Ctrl-\ x` closes the view while the
agent keeps running in its tmux session (`a` reopens it), `Ctrl-\ n` switches agent
and `Ctrl-\ [` opens the agent's tmux history (PgUp/PgDn, `q` returns). A client
that disconnects never holds input; `a` or `Ctrl-\ r` reattaches. After `Ctrl-\` the
status line lists what may follow. These chords are yours to change: see
[TUI keys](#tui-keys). The conversation's read-only output keeps the agent's colors
and wraps long lines (PgUp/PgDn scroll it once the conversation has focus); key
hints printed by the agent there only work in live input. The action
palette (`:`) still delegates interactive commands such as `switch` and `dashboard`
to the shell CLI with argument-vector execution. Press `2` (or `I`) for Inbox;
`j`/`k` or arrows select an item, `Enter` opens its detail, `s` marks that revision
seen for this client, and `r` opens the exact read-only review. In review, `j`/`k`
scroll, `i` shows the full identity, `f` shows supplied references, `o` follows a
selected reference, and `Esc` backs out. `l`, `t`, and `p` supply one explicit log,
transcript, or PR reference to the public review producer. A stale attention snapshot
cannot be reviewed until it is refreshed. Seen state is client-local and does not
approve, accept, or mutate a workflow.
