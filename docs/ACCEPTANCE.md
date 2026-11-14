# Installed-build acceptance

`make test-acceptance` replaces "build and manually test" for the roadmap items
whose source has shipped but which stay open until their installed-build
acceptance has been observed: U1, U2, U3, U6, U7, U8, U9, U12, U13, U15 and
U17 (the "Immediate product priority" list in [ROADMAP.md](ROADMAP.md)). Each
item's Acceptance sentences are the specification; the matrix maps every
sentence (an *acceptance bullet*) to the checks that establish it and to the
retained evidence. What an automated run cannot establish stays listed as a
human gate on the report and in the roadmap's delivery boundary table.

Run `LANG=C.UTF-8 LC_ALL=C.UTF-8 make test-acceptance`. It builds the native
helpers and the PTY observer, installs a fresh prefix (`install.sh` with all
helpers required), runs every case on real Git, tmux and pseudo-terminals, and
writes `build/acceptance-<timestamp>/index.html` and `report.json`. Set
`ACCEPTANCE_OUTPUT=/absolute/new/directory` to name the run, and
`ACCEPTANCE_ARGS` to select: `--item U6`, `--case u12`, `--columns 80`,
`--list`. The runner is the stdlib package `scripts/acceptance/`
(`PYTHONPATH=scripts python3 -m acceptance`); it reuses the usability
runner's journey, installation and PTY observer helpers (`scripts/usability/`,
`scripts/bench_i1_pty.py`) and adds a bounded VT emulator (`vt.py`) that
re-derives styled screens and counts repaint behaviour from the raw bytes.
A full run takes about twelve minutes on a shared 4-core machine. `--prefix
DIR` reuses an installed prefix while iterating on a case; a report made that
way says so and is never a release report.

The cases drive the four screens of the control centre (`1` Work, `2` Inbox,
`3` Hosts, `4` Stats) and its dialogs. They read a screen the way a user does:
the numbered top bar, the generated footer (which follows the focused pane),
a page's rows from its label column (Details) and dialogs over any screen.

The exit status is 0 when every check passed, 1 when any check failed and 2 on
an infrastructure error (install or observer failure). A failed check is a
finding against the build: the assertion states the desired behaviour and is
not relaxed to the measurement. The report separates *failed* (the product did
not do what the bullet requires) from *blocked* (the case could not reach the
check, for example a fixture or observer problem).

## Cases and bullets

Bullet ids (`U6.a`, …) are stable keys defined in `scripts/acceptance/matrix.py`;
the roadmap text remains the specification. Checks run against the installed
`hydra` (public entry `hydra` in a repository, `hydra spawn`, `tmux attach`)
with the private HOME, `HYDRA_HOME`, tmux socket namespace and disposable
repository of the usability journeys. No live provider is used: the planning
and conversation cases install the labelled fixture
(`tests/fixtures/usability/provider.py`) as `codex` on a private PATH, and
`tests/fixtures/acceptance/noauth-provider.sh` is a provider that only reports
a missing login.

| Case | Width | Bullets | What it drives and what it inspects |
|---|---|---|---|
| `u1-clean-repo` | 80 | U1.a U1.c U1.d U3.d | Opens, configures (`init --no-agent`, an agent profile) and reopens Hydra in a clean repository; compares a SHA-256 tree, `git status --porcelain` and `.git/info/exclude` byte for byte before and after. Then the shared-config opt-in (`--write-shared-config`) must preview what it writes and leave only `.hydra/config.yml` in `git status`; a committed repository workflow is refused by `workflow validate` before `init --trust` and after entering the UI. |
| `u1-dirty-repo` | 80 | U1.b | Same open/configure/reopen on a repository with a tracked edit, an untracked file, `.gitignore` and `.git/info/exclude` rules and ignored files; everything stays byte-identical and ignored files stay ignored. |
| `u3-ui-conversation` | 80 | U3.a U3.d | From an uninitialized repository, `n` opens the new-task form (task name, agent chosen along its choice row, objective) and reaches the agent conversation inside the control centre (top bar and agent pane together); typed input is answered. The case logs every `hydra` command it ran itself: none was `init` or `spawn`, and the repository is unchanged. Entering the UI registered nothing, started no terminal and trusted nothing. |
| `u3-cli-launch` | 80 | U3.b | An interactive `hydra spawn cli-task --profile codex --prompt …` on a PTY opens the control centre with the agent visible inside it ("typing to …"); Ctrl-] leaves input inside Hydra ("Keys go to Hydra"); `q` returns to the shell with exit 0 and the task kept. |
| `u3-missing-provider` | 80 | U3.c | With `codex` absent from PATH, the new-task form's agent row offers only the available agents; Hosts (`3`) says on This machine that codex is "not found on PATH"; nothing is launched and no head is created. |
| `u3-missing-auth` | 80 | U3.c | The no-login provider's own instruction (`Not logged in. Run: codex login`) is visible in the control centre; Hydra's line in the pane says the agent exited and the pane is a shell; a sign-in command typed there runs in the head's worktree. Details must then say the agent exited rather than present a running agent. |
| `u2-spawn-reconnect` | 80, 140 | U2.a U2.b U2.c | `hydra spawn --profile codex`, then a real `tmux attach` client on a PTY of that width: the visible transcript, the pane scrollback (`capture-pane -S -`) and the shell's history file must contain no `export HYDRA_…`, `set-environment` or identity assignment; Hydra's banner is one line within the width with no raw ids. `/proc/<agent pid>/environ` must carry `HYDRA_PROJECT_ID`, `HYDRA_HEAD_ID`, `HYDRA_BRANCH` and `HYDRA_WORKTREE` matching the durable state. After the agent exits, the shell prompt is usable (a typed command lands in the worktree); detach and reconnect at the other width keeps the prompt usable and the transcript clean. |
| `u6-flicker` | 80, 140 | U6.a–U6.f | Records the raw PTY bytes of the installed TUI through the native observer's timestamped read index while idle for 30 s on Work, while an attached head streams 300 lines, while the CLI changes local state (spawn, outcome), while Tab cycles Work's panes and `3`, `4`, `2`, `1` switch screens, and across two resizes (80x24 ↔ 140x40). Each phase is replayed through `vt.py`, split into refreshes (reads less than 5 ms apart) and counted: full-screen clears (`ED 2`/`ED 3`, or every row rewritten), identical-cell rewrites, blinks (a cell erased and painted back with the same content within a refresh), cursor moves that paint nothing, scrolls, cursor positions and visibility. Thresholds: zero full clears while idle, streaming, polling and switching; unchanged cells rewritten by any one refresh ≤ 25 % of the screen and ≤ 5 % on average (idle, streaming, polling); zero blinks and idle cursor moves; selection unchanged after idling with the cursor hidden; at most one full repaint per resize. The raw recording of every phase is retained (≤ 2 MiB each) with per-phase metrics and the styled end screen. |
| `u7-details-recovery` | 80 | U7.a–U7.d | Three heads: running (fixture agent started with an objective), stopped (the agent reported `exited` through `hydra adapter ingest`, terminal killed) and terminal-gone (`--no-agent`, terminal killed). Each head's Details page (`]` from the list), read from its label column, must group identity (Agent, Session, Reported), CHANGES and CHECKS with an Approvals row, wrap within the width and keep raw ids, state paths and confidence words to the technical page (`d`, IDENTITY and LIFECYCLE). The running head's Details show the objective it was started with. Stopped and terminal-gone heads are explained in plain words (files kept, no `dead-session`/`try-hydra`/`hydra doctor`, no inferred loss); restart (`a`) and remove (`x`) are separate actions. The Inbox lists the terminal-gone head as "Terminal stopped: gone" without its raw kind; its account says what stopped, that files are kept and that `o` opens it in Work; its actions restore access (Restart terminal, its check, Open in Work) and never remove; kind, source and confidence appear only on its Technical page, under DIAGNOSTICS. |
| `u8-hints` | 80, 140 | U8.a U8.b U8.c | Real key events (`\x1b[B`, `\x1b[A`, `\x1b[Z`, `\r`, `\x1b`, `\t`) on Work, help, the Inbox and the removal dialog, each compared with the generated footer shown on that screen: Up/Down move the selection, `enter/a talk` attaches, Esc after Ctrl-] returns the list its focus and selection, the dialog's `y remove` / `esc cancel` do what they say. Pane focus is judged by the footer, which follows the focused pane, across a full Tab cycle and a Shift-Tab reversal. Attached: Tab and Esc typed to the agent must arrive in its input (verified in a file it writes), Ctrl-] leaves, the screen says how to return, and Tab cycles back to the agent. |
| `u9-empty` | 80 | U9.a U9.d U13.a | No work: Work's empty state explains a head and offers `n`, `3` and `:`; the Inbox says what would appear there and, muted, that no workflow runs are recorded; Stats says no sessions are recorded yet and what it covers, reports an unregistered repository's scope as unavailable rather than zero, and keeps the top bar. |
| `u9-single-agent` | 80, 140 | U9.b U9.d U15.b U13.a | One head: the top bar counts it running, Work lists it with its state and `enter/a talk`, Details give session, changes, reported outcome and approvals and explain coordination in the Coordinate row; with no run there is no Run page. Stats explains that a run is recorded when a plan is executed and says no runs are recorded (not that filters exclude them); round trips to Stats keep the top bar and the Work selection; the Inbox says nothing needs the user in words. |
| `u9-multi-agent-blocked` | 80 | U9.c U13.b U15.b | Two heads and a trusted workflow (`tests/fixtures/acceptance/handoff-flow.yml`) whose handoff to `worker-b` waits on an approval on `worker-a` (exit 3, `handoff [exec]: queued`). The run's Run page lists each step with its state and draws the chain, the footer offers `Y approve` / `N reject`, the Work list counts what needs the user, and the Inbox lists the approval first under Decisions with an explanation and `r` review. `worker-b`'s Details must name the run and step that target it and the approval on `worker-a` that blocks it. Stats lists and counts the run. Approving from the Inbox (`Y`, one dialog, nothing typed) and resuming the run (`R` on its row; a decision never resumes work by itself) must run the dependent step, and the Run page then reports the run succeeded. |
| `u12-removal` | 80 | U12.a–U12.e | Removes a stopped head and an active head (a process running in its terminal) through the confirm dialog (`y`), cancels a confirmation, is refused on dirty work (the REMOVAL OUTPUT viewer explains the protection) and runs a two-head bulk removal (`space` marks, `x`, "Remove 2 heads") with one protected head. Continuous snapshots during each action must keep the top bar and never show "Press Enter to return"; outcomes are verified against `hydra list`, the worktrees, `git rev-parse` of the kept branches and `/proc` of the stopped process; focus must move to surviving work. |
| `u17-plan-from-conversation` | 80 | U17.a U17.b U17.c U15.a | The fixture publishes a proposal from the conversation (`draft`); the Plan page shows it as "Plan ready" with no key pressed, says nothing runs until approval, and no run is recorded. `E` opens one dialog naming the revision ("Approve and run greeting revision 1?") with no file or JSON to find; behind it the revision is labelled DRAFT / not validated yet; Esc runs nothing. `revise` yields a new DRAFT revision whose dialog names it; `y` approves and runs exactly that revision, whose launched digest must equal an independent `workflow plan compile` of it and differ from the first; `workflow plan result` verifies the delivery. The only `hydra` commands the case runs after the conversation starts are its own inspections. Then (U15.a) the worker's committed two-file change with a clean worktree is not shown as "0 files" in Work, Details label uncommitted changes separately, and `:diff` lists both files in the output viewer. |

The cases reuse the usability suite by reference rather than duplicating it:
`u17-plan-from-conversation` (with its U15.a checks) uses `scripts/usability/journeys_plan.py`
(`provider_fixture`, `independent_digest`, `finished_run`, `worker_diff`), and the
`planning`, `result-review`, `recovery-terminal` and `removal` journeys and the
`why-stopped`, `remove-stopped` and `leave-return` trials of `make test-usability`
remain the broader scripted coverage of the same areas.

## Report and evidence

`index.html` holds the matrix (item, bullet, check, result, case, evidence), the
per-item summary (bullets and checks passed/failed/blocked, uncovered bullets),
the human gates and a per-case section linking the action log (`proof.json`)
and every screen snapshot. `report.json` carries the same rows plus the source
commit, installed file hashes and the SHA-256 of every source file under `src`,
`lib`, `scripts/acceptance` and the fixtures, so a report is tied to the build
it measured. Per case the run retains: escaped ASCII snapshots from the native
observer, styled snapshots re-derived from the raw bytes (`styled-*.html`),
`terminal.raw` with its timestamped read index, the PTY recordings of
`tmux attach` and `hydra spawn` clients, kept command outputs, `/proc` reads
and Git/tmux/state inspection results (`evidence-NN-*`), the flicker
recordings and metrics, and a traceback for a case that stopped early. A check
that a case did not reach is reported as failed (or blocked) with the reason,
never silently omitted.

## Findings

A red row is a finding against the product, kept red until the product
changes. On the four-screen build every check passes; the last findings, fixed
in the product with tests of their own, were:

- U3.c (`u3-missing-auth`): after the provider exited for lack of a login,
  Details said `Session running (the live terminal)`. The Session row now says
  "agent exited (status N); the terminal is a shell" once Hydra's launcher line
  says so in the head's terminal.
- U7.a (`u7-details-recovery`): Details did not show the objective given at
  launch. The Objective row is read from the task the launch wrote beside the
  head's record.
- U9.c and U15.b (`u9-multi-agent-blocked`): `worker-b`'s Details named
  neither the run whose `handoff` step targets it nor the approval on
  `worker-a` that blocks it. The Workflow row names the step, its run and the
  unfinished steps it waits for, with the head each runs on.

## What still needs a human

- U6: repaint counts and clears bound flicker but cannot establish perceptual
  flicker in a real terminal application; review the retained recordings
  (`recording-<phase>.raw`, replayable with the emulator) in the supported
  terminal.
- U7, U9, U15: the checks assert the required words, groups and routes; whether
  the account reads as plain language to a user is a judgement.
- U8: footers match behaviour under scripted keys; discoverability by a
  first-time user is the goal-driven trial of `make test-usability` and human
  first use.
- U3, U17: the provider is a deterministic fixture reached through the public
  profile launch; live provider sign-in, missing-auth states of real CLIs and a
  real agent authoring a plan stay unqualified.
- U2: real terminal applications (macOS Terminal, iTerm2) are not exercised;
  the run uses PTYs with `TERM=xterm-256color`.

Keep failures red. A finding is fixed in the product and the case rerun; the
assertion is not weakened and the runner is not taught a workaround.
