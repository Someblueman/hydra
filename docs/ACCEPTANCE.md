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
helpers required), runs the flicker case on real Git, tmux and pseudo-terminals,
and writes `build/acceptance-<timestamp>/index.html` and `report.json`. Set
`ACCEPTANCE_OUTPUT=/absolute/new/directory` to name the run, and
`ACCEPTANCE_ARGS` to select: `--columns 80`, `--list`. The runner is the stdlib
package `scripts/acceptance/` (`PYTHONPATH=scripts python3 -m acceptance`); it
reuses the usability runner's journey and installation helpers
(`scripts/usability/`, `scripts/bench_i1_pty.py`) and adds a bounded VT emulator
(`vt.py`) that re-derives styled screens and counts repaint behaviour from the
raw bytes. A run takes about two minutes. `--prefix DIR` reuses an installed
prefix; a report made that way says so and is never a release report.

Only the `u6-flicker` case remains. The journeys for U1, U2, U3, U7, U8, U9,
U12, U13, U15 and U17 were removed from the runner; the changelog lists the
focused native and shell tests that cover their assertions.

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
the roadmap text remains the specification. The case runs against the installed
`hydra` with the private HOME, `HYDRA_HOME`, tmux socket namespace and
disposable repository of the usability journeys. No live provider is used.

| Case | Width | Bullets | What it drives and what it inspects |
|---|---|---|---|
| `u6-flicker` | 80, 140 | U6.a–U6.f | Records the raw PTY bytes of the installed TUI through the native observer's timestamped read index while idle for 30 s on Work, while an attached head streams 300 lines, while the CLI changes local state (spawn, outcome), while Tab cycles Work's panes and `3`, `4`, `2`, `1` switch screens, and across two resizes (80x24 ↔ 140x40). Each phase is replayed through `vt.py`, split into refreshes (reads less than 5 ms apart) and counted: full-screen clears (`ED 2`/`ED 3`, or every row rewritten), identical-cell rewrites, blinks (a cell erased and painted back with the same content within a refresh), cursor moves that paint nothing, scrolls, cursor positions and visibility. Thresholds: zero full clears while idle, streaming, polling and switching; unchanged cells rewritten by any one refresh ≤ 25 % of the screen and ≤ 5 % on average (idle, streaming, polling); zero blinks and idle cursor moves; selection unchanged after idling with the cursor hidden; at most one full repaint per resize. The raw recording of every phase is retained (≤ 2 MiB each) with per-phase metrics and the styled end screen. |

## Report and evidence

`index.html` holds the matrix (item, bullet, check, result, case, evidence), the
per-item summary (bullets and checks passed/failed/blocked, uncovered bullets),
the human gates and a per-case section linking the action log (`proof.json`)
and every screen snapshot. `report.json` carries the same rows plus the source
commit, installed file hashes and the SHA-256 of every source file under `src`,
`lib` and `scripts/acceptance`, so a report is tied to the build
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
  "agent exited (status N); the terminal is a shell" once Hydra's launcher has
  recorded the exit beside the instance (`agent-exit`), even after later output
  scrolled its line out of the terminal.
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
