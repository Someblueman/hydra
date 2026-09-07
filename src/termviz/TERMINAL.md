# Embedded terminal contract

This is a bounded terminal subset for the first real-shell milestone. It is not a
claim of full xterm, VT, tmux, Vim, or agent-CLI compatibility. Behavior follows the
relevant portions of [XTerm Control Sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html).
The standalone demo sets `TERM=xterm-256color` to let the qualified shell and test
client use ordinary styling/input conventions; applications requiring other xterm
features are outside the qualification matrix until exercised explicitly.

## Supported output

| Input | Behavior |
| --- | --- |
| UTF-8 text | Incremental decoding; invalid scalars become `?`; shared wide/combining cell rules |
| CR, LF, VT, FF, BS, HT | Cursor movement; HT uses eight-column tab stops |
| ESC D/E/M | Index, next line, reverse index |
| ESC 7/8, CSI s/u | Save/restore cursor and rendition |
| CSI A/B/C/D/E/F/G/H/f/d/a/e | Bounded relative/absolute cursor movement |
| CSI J/K/X | Erase display/line/characters; J=3 clears retained history |
| CSI @/P/L/M/S/T | Insert/delete characters or lines; scroll within margins |
| CSI r, private mode 6 | Vertical scroll margins and origin-relative positioning |
| Private mode 7; normal mode 4 | Autowrap and insert mode |
| CSI m | Reset, bold, dim, italic, underline, reverse, 16/256 colors and RGB; selective attribute/color resets |
| Private modes 47/1047/1049/1048 | Primary/alternate screen and cursor save/restore |
| Private mode 25 | Cursor visibility, rendered only in the focused live pane |
| Private modes 1, 2004 | Application cursor keys and bracketed-paste negotiation |
| Private modes 1000/1002/1003/1006 | Mouse-request state; demo routes SGR reports within the child content rectangle |
| CSI 5n/6n, c, 18t | Bounded status, cursor, basic attributes and size replies queued only for the child |
| ESC c | Reset screen and terminal modes |

The parser accepts sequences split at any byte boundary. Parameters are bounded to
16 entries and 65535 each; excessive or malformed sequences are ignored through
their final byte. OSC, DCS, APC, PM and SOS strings are discarded through their
terminator without storing their payload. No child clipboard, title, hyperlink,
image, window-control, or arbitrary escape payload is forwarded to the outer
terminal. Only rendered cells and presenter-generated cursor/style sequences reach
it. Incomplete UTF-8 is resolved at EOF; incomplete escape strings are discarded.

Unsupported operations increment an observation counter where recognized. That
counter is diagnostic, not a conformance score. Character-set designation is
consumed but not implemented; DEC graphics, custom tab stops, reflow, emoji joining,
full keyboard protocols, palette mutation and terminal graphics remain outside
this milestone. Unsupported SGR attributes are not silently treated as supported.

## Storage, resize and transport

The caller provides distinct primary and alternate cell arrays plus a history
ring. Maximum screen capacity is 512 columns by 256 rows; history capacity is
explicit and at most 10000 physical rows. The demo uses 512 history rows. Full
primary-screen upward scrolling appends history; alternate screens and partial
scroll margins do not. Resize preserves the top-left intersection, clears cropped
cells, clamps cursors and resets scroll margins. It does not reflow logical lines.

The POSIX adapter creates one session with a controlling PTY, configures its size,
executes an explicit argv, and uses a 64 KiB nonblocking input queue. Overflow is
reported as undelivered input. The demo drains at most eight 4 KiB output chunks
per loop so input/focus cannot be starved by continuous child output. Resizing a
visible child pane changes both the model dimensions and PTY window size. Hidden
panes retain their last size until shown again.

The app closes the PTY and hangs up/reaps its child on exit or handled interruption,
with bounded escalation for a child that does not exit. Foreground job groups are
also signaled when they belong to that child session. Deliberately detached jobs
are not an isolation/supervision guarantee. Failures and child exit codes remain
visible in the inspector; no child is automatically restarted. Hydra head lifecycle
continues to belong to its shell CLI and tmux, not this demo's process adapter.

## Local acceptance

Component tests cover screen transitions, colors, wrap/erase/scroll operations,
history bounds, every split point of a representative mixed byte stream, malformed
sequences, and deterministic hostile bytes. The real PTY suite executes `/bin/sh`,
observes command status, streams more than 512 output rows, switches focus during
output, invokes a separate full-screen C client, propagates resize at 140x40,
80x24 and 40x10, checks return from alternate screen, detects blocked escape
payloads, and verifies normal/interrupted/failed-exec cleanup and exact outer
terminal settings. The PTY observer is independent of this parser.
