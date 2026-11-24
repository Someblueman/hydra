# Reviewing the control centre

Three commands exercise the installed native TUI (`hydra tui`) over a
pseudo-terminal and keep what they saw, so a person can review evidence instead of
rebuilding and clicking through every screen. Each installs this checkout into a
private prefix (`install.sh` with the core, TUI and fleet helpers required) and
uses deterministic fixtures. None uses a live provider or a remote host.

| Command | Question it answers | Runner |
| --- | --- | --- |
| `make test-acceptance` | Does the installed build repaint without flicker? | `scripts/acceptance/` |
| `make test-usability` | Can a first-time user get from a new repository to an approved, delivered plan? | `scripts/usability/` |
| `make visual-review` | What does every screen look like, and does the layout lint pass? | `scripts/visual_review/` |

The acceptance and usability runners drive the installed `hydra` on real Git, tmux
and pseudo-terminals in a disposable repository with private `HOME`, `HYDRA_HOME`
and tmux sockets. They exit 0 when every case passed and 1 when a case failed or
was blocked; an install or prefix error is written to the report as an
infrastructure error and exits 2 (`make` reports its own status for a failing
recipe). A failed check is a
finding against the build: the assertion states the desired behaviour and is not
relaxed to match the measurement. Both take `--prefix DIR` to reuse an installed
prefix while iterating; a report made that way says so and is not a release report.
Run them without Python optimization (`-O`), which would disable the assertions.
The visual review is different: it drives the installed `hydra-tui` against a fake
`hydra` and the fixtures under `tests/fixtures/tui`, so what it shows is the
rendering of fixture states, not of a live repository.

## Flicker (`make test-acceptance`)

One case, `u6-flicker`, runs at 80x24 and 140x40. It records the raw bytes the TUI
writes (through the observer's timestamped read index) in five phases: 30 seconds
idle on Work, an attached head streaming 300 lines, local state changed from the
CLI (spawn, outcome), Tab cycling Work's panes and `3`, `4`, `2`, `1` switching
screens, and two resizes (80x24 and 140x40, back and forth). `scripts/acceptance/vt.py`
replays each phase and groups reads at most 5 ms apart into one refresh. The
checks (`scripts/acceptance/flicker.py`):

- no full-screen clears while idle, streaming, polling and switching, and at most
  two across the combined resize phase (one per resize);
- a refresh rewrites at most 25% of the screen's cells unchanged, and at most 5% on
  average (idle, streaming and polling; a screen switch legitimately repaints
  whole panels, so only clears and blinks are asserted there);
- no blinks (a cell erased and painted back with the same content in one refresh)
  and no cursor moves that paint nothing, including while switching;
- after idling the selection is unchanged, and the cursor is hidden at the end of
  every sampled idle refresh.

`ACCEPTANCE_OUTPUT=/absolute/new/directory` names the run and
`ACCEPTANCE_ARGS="--columns 80"` narrows it (`--list` lists the case). The output
directory holds `index.html` and `report.json` (the check rows, the source commit,
installed file hashes and a digest of every file under `src`, `lib` and
`scripts/acceptance`), and per case the `proof.json` action log, escaped screen
snapshots, per-phase metrics and `recording-<phase>.raw` (at most 2 MiB each) with
the phase's end screen.

## First-use walkthrough (`make test-usability`)

One journey, `planning`, runs at 80x24 and 140x40
(`scripts/usability/journeys_plan.py`). In a Git repository nobody has initialized
for Hydra it checks that entering the control centre leaves the files and
`git status` unchanged and creates no head, starts a planning conversation from the
new-task form, converses with the provider fixture to a published proposal and
checks that nothing executed. It cancels the approval dialog, asks for a revision,
approves the exact revision once with `y`, and checks that the run launched only
that revision's digest, that the run reached `succeeded` and that
`hydra workflow plan result` verifies the delivery.

The provider is a labelled fixture (`tests/fixtures/usability/provider.py`) reached
through a custom interactive profile; it publishes proposals with the public
`hydra workflow plan propose` command and never writes Hydra state itself.

`USABILITY_OUTPUT=/absolute/new/directory` names the run;
`USABILITY_ARGS="--journey planning --columns 80"` narrows it. The output holds
`report.json` (per-case status and reason, the commit, installed hashes and the
gates the run does not qualify) and, per case, `proof.json`, `cleanup.json` and the
terminal captures. A case is `passed`, `failed` or, when its setup could not start,
`blocked`.

## Screen captures and layout lint (`make visual-review`)

```sh
make visual-review                                  # focused: build/visual-review/index.html
make visual-review VISUAL_REVIEW_FULL=1             # every flow, every theme
make visual-review VISUAL_BASELINE=/path/to/earlier # before/after columns
make test-visual-lint                               # the layout lint only
```

The runner drives the installed `hydra-tui` through fixed states with real key
presses at 80x24 and 140x40, fed only by the shell fixtures under
`tests/fixtures/tui`, the recorded remote-setup envelopes and four synthetic
fixtures in `scripts/visual_review/fixtures`. It keeps the exact byte stream of
each state, replays it through the product's own terminal model (`src/kettle`,
built as `build/kettle-cells` from `src/kettle/tools/cells.c`), renders HTML and
SVG with the real cell colours, lints the layouts and writes one contact sheet.

The default is focused: the `work` flow (Work, its Run and Details pages, the
Inbox and its reviews, Stats, dialogs, help) and the `hosts` flow (the guided
remote setup) in the dark and light themes. `VISUAL_REVIEW_FULL=1` (`--full`) adds
the `fleet` flow, the `hosts-changed-key`, `hosts-unfinished` and `hosts-running`
flows and the no-colour theme (`NO_COLOR=1`). The user's own `terminal` theme is
not captured. A subset:

```sh
python3 scripts/visual_review/run.py --output build/visual-review \
  --cells build/kettle-cells --keep-prefix --flow work --theme dark --size 80x24
```

`--keep-prefix` reuses the installed prefix and keeps the captures of every
combination the run does not repeat. The state list is `scripts/visual_review/flows.py`.

The output directory holds `captures/<id>.raw`, `.json` (the cell grid) and `.txt`,
`renders/<id>.html` and `.svg`, `manifest.json` (commit, installed hashes, each
capture's markers and digest), `lint.json` and `index.html`. The contact sheet lists
the lint findings first, then every state in every variant. With `VISUAL_BASELINE`
set to an earlier output directory each variant shows the baseline beside the new
render, labelled identical or differing by cell comparison; copy
`build/visual-review` somewhere before changing `src/tui` to have a baseline.

### The layout lint

`make test-visual-lint` (`scripts/visual_review/lint.py DIR`) captures first when
`DIR` has no `manifest.json`, then checks every capture:

- `clip-edge`: a glyph in the terminal's last column.
- `clip-area`: a word cut at the end of its area, an unbalanced `(`, or footer
  hints cut to a lone letter.
- `overlap`: panels whose rectangles intersect without one containing the other, or
  text drawn over a panel's side border.
- `focus`: no selected tone, `▌` or `>` marker, visible cursor or action prompt.
- `palette` and `status-tone`: a colour outside the theme's declared palette (the
  tones in `src/tui/render.c`, mirrored in `themes.py` and checked against the C
  source on every run, reported as `palette-mirror`), any colour escape in
  no-colour mode, a negative status word (`failed`, `blocked`, `stale`...) in the
  success tone or a positive one (`succeeded`, `running`...) in the warning tone.
- `identifier`: a raw `run_`, `head_`, `task_`, `instance_` or `project_` id or a
  32 to 64 digit hex digest in a heading, or more than three outside screens in
  the `detail` and `diagnostic` groups (set per state in `flows.py`).
- `hint-repeat`: the same keyboard hint on two rows of one screen.
- `contrast`: WCAG contrast below 4.5 (3.0 for the muted tone and box-drawing
  glyphs) in the dark and light themes; no-colour mode is not judged.

`scripts/visual_review/lint-known.tsv` lists the findings that need a design
decision by the owners of `src/tui`. Its columns are tab-separated: rule, capture
globs, a substring of the finding's message and the reason. The capture column
takes several `fnmatch` globs separated by commas (for example
`work--*--dark-*,fleet--*--dark-*`). A matching finding is reported as known and
does not fail the lint; everything else does. A known row that no finding matches
any more is reported as resolved: remove it. Add a row only for a finding that needs
a design decision, never to hide a regression.

## What still needs a person

The automated runs establish layout, clipping, focus, palette and contrast on
fixtures, repaint counts, and one scripted first-use path. They do not establish:

- **Perceptual flicker.** Repaint counts bound it but cannot show it. Replay the
  retained `recording-<phase>.raw` files, or watch the build, in a supported
  terminal.
- **Aesthetics and plain language.** Hierarchy, spacing, restraint and whether the
  copy reads as plain language are judged on the contact sheet. The scripted
  walkthrough is not a novice, so discoverability needs a first-time user.
- **Real terminals.** macOS Terminal and iTerm2 are not exercised; the runs use PTYs
  with `TERM=xterm-256color`.
- **A live provider and a remote host.** Real provider sign-in, missing-auth states
  of real CLIs, an agent authoring a plan, and onboarding a real machine are
  separate qualification steps (see the U10 item in the
  [roadmap](ROADMAP.md) and [FLEET.md](FLEET.md#guided-remote-setup)).

Keep failure artifacts outside the tested repository. The fixture runs hold no
credentials, but a capture or recording made with a live provider or a real host can:
review it for credentials before publishing it.
