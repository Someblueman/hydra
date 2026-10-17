# Visual review without manual runs

`make visual-review` replaces "build it and look" for the control centre's
screens (roadmap U4, I4 and the 2.9.0 Hosts screens). It installs this checkout
into a private prefix, drives the installed native TUI over a pseudo-terminal
through a fixed set of states with real key presses, keeps the exact byte
stream of every state, renders each stream through the product's own terminal
interpreter with the theme's exact colours, lints the layouts and writes one
page, `build/visual-review/index.html`, that the reviewer reads instead of
running the application. The captures and the lint are automated; the aesthetic
judgement (hierarchy, spacing, restraint, whether a screen "feels like part of
the same application") remains the reviewer's, and the contact sheet is its
input.

```sh
make visual-review                                  # build/visual-review/index.html
make visual-review VISUAL_BASELINE=/path/to/earlier # before/after columns
make test-visual-lint                               # the bounded layout lint only
```

A full run takes about seven minutes on a shared 4-core machine; a subset is
`python3 scripts/visual_review/run.py --output build/visual-review --cells
build/termviz-cells --keep-prefix --flow work --theme dark --size 80x24`
(`--keep-prefix` reuses the installed prefix and keeps the captures of every
combination the run does not repeat).

## What is captured

Every state is reached by keystrokes through `hydra-tui` from the installed
prefix, fed only by deterministic fixtures: the shell fixtures under
`tests/fixtures/tui` (heads, runs, attention, review, statistics, fleet and the
recorded remote-setup envelopes) plus four synthetic fixtures beside the script
(`scripts/visual_review/fixtures`: a review-ready attention queue, a failed
check, a result review with realistic names, and the same run failed). The
flows in `scripts/visual_review/flows.py` cover:

| Flow | States |
|---|---|
| `work` | the Workspace planning view, the Work list with a run collapsed and expanded, Overview with the run running, finished and failed, a headless worker's details, technical details, a head whose terminal is gone, Coordination, Recovery and a finding's diagnostics, Workflows, Statistics, the Attention queue (an approval waiting for the user), its detail, a failed check, the result review (verdict, verification, changes), the selected identity, the evidence review, keyboard help, the Hosts hub |
| `fleet` | the Fleet workspace, remote heads, the Fleet overview, Hosts with observations, Recovery with an offline host, Fleet attention |
| `hosts` | the guided remote setup: entry, Hosts, the form, host-key trust (a wrong word, typed `yes`), blocking requirements, the missing-tmux pause, the provisioning plan (declined, shown again), the agent inventory, an installer plan, the sign-in hand-off and its terminal, a failed sign-in, done, Hosts afterwards |
| `hosts-changed-key`, `hosts-unfinished`, `hosts-running` | a changed host key (manual recovery only), an unfinished setup with its removal plan, a kept record, a duplicate name and the edit form, a running step with its elapsed time and the cancelled result |

Each flow runs at 80x24 and 140x40 in the dark and light themes and in
no-colour mode (`NO_COLOR=1`): six variants per state. The `terminal` theme
(the user's own colours) is not captured because its colours are not the
product's.

For every state the output directory holds `captures/<id>.raw` (the bytes the
TUI wrote from launch to that settled frame), `captures/<id>.json` (the cell
grid: glyph, foreground, background, attributes, width; produced by
`build/termviz-cells` from `src/termviz/tools/cells.c`, which replays the
bytes through the same `src/termviz` terminal model the native PTY tests
observe), `captures/<id>.txt`, `renders/<id>.html` and `renders/<id>.svg`.
`manifest.json` records the commit, the installed file hashes and every
capture's markers and digest; `lint.json` holds the findings.

## The contact sheet

`index.html` lists the lint findings first, then every state with its six
variants rendered as HTML with the exact cell colours (a `<pre>` per screen,
one `<span>` per style run; SVG files beside them for pasting into other
documents). With `VISUAL_BASELINE=dir`, each variant shows the baseline's
render on the left and the new one on the right, labelled identical or
differing by cell comparison, so a redesign is reviewed as before/after on one
page. A baseline is simply an earlier output directory: copy
`build/visual-review` somewhere (for example `docs/evidence/visual-baseline`,
which is git-ignored) before changing `src/tui`.

## The layout lint

`scripts/visual_review/lint.py` runs over every capture, deterministically and
within the matrix above. It fails on:

- **clip-edge**: a glyph in the terminal's last column (the frame never uses it).
- **clip-area**: a word cut at the end of its area: text that ends exactly one
  column before a panel border or at the content edge and is neither a word
  wrapped onto the next row, a long identifier continued on the next row, nor
  a complete word; an unbalanced `(`; and footer hints cut to a lone letter.
- **overlap**: two panels whose rectangles intersect without one containing
  the other, or text drawn over a panel's side border.
- **focus**: no cell in the theme's selected tone, no `>` marker, no visible
  cursor and no action prompt (`y approve`, `Type yes`, `Enter continues`...).
- **palette** and **status-tone**: a colour outside the theme's declared
  palette (the nine tones in `src/tui/render.c`, mirrored in `themes.py` and
  checked against the C source on every run), any colour escape in no-colour
  mode, a negative status word (`failed`, `blocked`, `terminal gone`, `stale`...)
  painted in the success tone or a positive one (`succeeded`, `running`,
  `done`, `PASS`...) in the warning tone.
- **identifier**: a raw identifier (`run_`, `head_`, `task_`, `instance_`,
  `project_` ids, 32-64 hex digests) in a heading, or more than three outside
  a `detail` or `diagnostic` screen (groups set per state in `flows.py`).
- **hint-repeat**: the same keyboard hint (`key  action`) on two rows of one
  screen.
- **contrast**: text whose foreground/background WCAG contrast is below 4.5
  (3.0 for the muted tone and for box-drawing glyphs) in the dark and light
  themes; no-colour mode uses the terminal's own colours and is not judged.

Findings listed in `scripts/visual_review/lint-known.tsv` (rule, capture glob,
message substring, reason) are reported as known on the contact sheet and do
not fail `make test-visual-lint`; everything else does. That file is the open
list of layout defects for the people who own `src/tui`: a fix removes its
row, a new row needs a reason, and a known finding that disappears is reported
as such. Add a row only for a finding that needs a design decision, never to
hide a regression.

## Boundaries

The captures establish layout, wrapping, clipping, focus, palette and contrast
on deterministic fixtures. They do not establish flicker (U6 needs a recording
of the live refresh path), real-provider output, a real remote host, or the
aesthetic result; the reviewer still looks at the page. States that need a
real agent conversation (the planning journey with a live provider) are
exercised by `make test-usability` with text snapshots and are not on this
sheet; the Workspace planning layout is captured with the fixture heads.
