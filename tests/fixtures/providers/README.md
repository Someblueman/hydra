# Real provider output fixtures

Recorded on 27 September 2026 (macOS, tmux 3.5a, `LANG=en_GB.UTF-8`) from
authenticated local CLIs, each answering one short prompt that asked for exactly
the same reply: a Unicode line (accents, CJK, emoji, arrows, box drawing, a
combining accent), a fenced unified diff and a 250-column line.

| File | Source |
| --- | --- |
| `claude-interactive.raw` | Raw terminal bytes of interactive Claude Code 2.1.283 (`claude --model haiku PROMPT`) in a private 100x40 tmux pane, recorded with `script -q`, including the folder-trust dialog, the answer and the footer. The recording stops when its tmux server was closed. |
| `claude-interactive.screen` | tmux 3.5a's `capture-pane -p` of a fresh 100x40 pane that replayed `claude-interactive.raw` with echo disabled: the reference screen an independent terminal emulator produces for those bytes. |
| `claude-interactive.screen.ansi` | The same replay captured with `capture-pane -p -e` (colors). |
| `claude-capture.ansi` | `capture-pane -p -e -J -S -` of the live Claude Code pane after the answer: what Hydra's Details view reads. Claude colors its own diff. |
| `codex-exec.raw` | Raw bytes of `codex exec --skip-git-repo-check PROMPT` (Codex CLI 0.157.1) in a private 100x40 tmux pane, recorded with `script -q`. |
| `codex-capture.ansi` | `capture-pane -p -e -J -S -` of that pane. Codex prints its diff uncolored. |

The bytes are unmodified except for same-length scrubbing, so cursor addressing
in the raw streams still lines up: the recording directory became
`/tmp/hydra-provider-fixture/-x-x...`, the user name `usr`, session and
conversation identifiers zeros, the plan name `Pro` and a credit amount `$000`.
No e-mail addresses, tokens or other account identifiers remain.

`tests/c/test_tui_transcript.c` renders these through Hydra's read-only
transcript at 80x24 and 140x40 and through the attached-pane terminal model,
comparing with an independent escape stripper and with the tmux reference.
