# Quick tour of the control centre

[Watch the MP4](quick-tour.mp4) or view the [GIF](quick-tour.gif).

The recording is this checkout's native control centre (`hydra tui`) at 120x32
in the default dark theme, about 70 seconds long. A caption bar under the
terminal names each part of the tour and the key just pressed; the recorder draws
that bar, Hydra does not.

## What the recording shows

1. **1 Work**: the list of heads (three local heads, a head on the remote host
   `build-box`, and three workflow runs, collapsed, with their finished steps)
   beside the selected head's live conversation and its Details page.
2. Enter on `fix-tests` hands the keys to its agent. The question "why was it
   flaky?" is typed into the conversation and the agent answers; `ctrl+]` gives
   the keys back to Hydra while the agent keeps running.
3. `search-index` is a planning head: the plan it published waits on the Plan
   page, `]` pages the side pane to Details and back to Plan. The `ci-check` run
   shows its Run page: the step graph, the steps and the selected step's evidence.
4. **2 Inbox**: an approval request, a published plan, a failed step and a
   stopped terminal. The failed step's detail, then the approval's detail and its
   confirmation dialog, dismissed with Esc: nothing is approved.
5. **3 Hosts**: this machine (its heads, tmux and Hydra versions and the agent
   commands on its PATH) and the observed host `build-box`.
6. **4 Stats**: the Runs page (tiles, timing and the runs table), then the
   Sessions page with the head sessions.
7. `?` lists the keys of Stats, then `q` quits.

## What is real and what is not

`record.sh` builds `hydra-tui` from this checkout and runs it against a throwaway
Git repository in a temporary directory, with a private `HOME`, `HYDRA_HOME`,
tmux server (`TMUX_TMPDIR`) and Git identity. Everything is removed afterwards.

Real, made with this checkout's `bin/hydra`:

- The heads `search-index`, `fix-tests` and `docs-refresh`: `hydra spawn`
  worktrees and tmux sessions. The tmux session of `docs-refresh` is killed
  before the tour, which is why Inbox lists it as a stopped terminal.
- The plan on `search-index`: [plan.json](plan.json), published with
  `hydra plan propose` by the recorder.
- The workflow runs, with shell commands as their steps
  ([workflows/](workflows)): `ci-check` succeeded after one retried step,
  `docs-check` failed and `release-notes` waits at an approval.
- What Work, Inbox, Stats and "This machine" show about them: Hydra's own
  records and observations, read by the control centre as usual. Typing to the
  agent goes through the control centre's real attach to its tmux session.

Not real:

- **The agent is not an AI.** Both conversations come from
  [demo-agent.sh](demo-agent.sh), a script registered as an ordinary agent
  profile named `scripted`. It prints a canned transcript for its branch and
  answers each typed line from a fixed list. It runs no model and changes no
  files; its own first line says so.
- **No remote host is contacted.** `build-box` and its head `bench-tuning` are
  a recorded observation, [fixtures/remote.tsv](fixtures/remote.tsv), which
  [hydra-shim.sh](hydra-shim.sh) returns for the control centre's
  remote-observation call; every other call goes to the real CLI. No SSH
  connection is made.
- Run IDs, start times, durations and session lengths come from the clock of
  the recording, so they differ between recordings.

## Re-record it

```sh
sh assets/demos/record.sh
```

It overwrites `quick-tour.gif` and `quick-tour.mp4` beside this file and takes
about two minutes: setup, a tour of about 70 seconds in real time, then
rendering and encoding. It needs a C99 compiler, Make, pkg-config and JSON-C
development files (for `hydra-fleet`, which plans need), Git, tmux 3.0 or
later, Python 3 with Pillow, ffmpeg with libx264, and fontconfig with DejaVu
Sans Mono (`HYDRA_DEMO_FONT` names another monospace family). It needs no network, AI
CLI, SSH host or terminal recorder. `HYDRA_DEMO_OUTPUT=dir` writes the two files
elsewhere, `HYDRA_DEMO_FRAMES=dir` also keeps every distinct frame as PNG, and
`BUILD_DIR` is honoured as by `make`.

How it is made:

1. `record.sh` builds `hydra-tui`, `hydra-core` and `kettle-cells`, sets up the
   repository, heads, plan and runs, and waits for both agents' prompts.
2. `record.py capture` starts `hydra-tui` on a pseudo-terminal through the
   visual review's driver ([scripts/visual_review](../../scripts/visual_review/driver.py)),
   walks the tour in `tour()` with real key presses (each step waits for its
   screen before pausing on it) and keeps every byte the program wrote with the
   time it arrived.
3. `record.py render` replays those bytes through kettle's own terminal model
   (`kettle-cells`) at every tenth of a second in which output arrived, draws
   each distinct screen with the exact cell colours (box drawing and block
   elements as geometry, other glyphs in DejaVu Sans Mono; cells left on the
   terminal's default colours take the dark theme's text and surface colours),
   and encodes the frames with their real timing: the GIF with ffmpeg's
   palettegen and paletteuse, the MP4 as H.264 in yuv420p.

Inspect the frames before accepting a new recording. When the tour changes,
change `tour()` in [record.py](record.py) and the list above together.
