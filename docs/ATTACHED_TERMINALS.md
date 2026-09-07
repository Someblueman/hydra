# Attached agent terminals

In the native workspace, select a recorded local head and press `a`. Its existing
tmux session appears in the conversation pane. The pane owns a tmux attachment
client and terminal screen storage; tmux continues owning the agent and its shell.
Opening, switching or closing clients does not create another agent execution.
Up to four clients remain attached while the UI is open.
After attaching two heads, `S` in Hydra or `Ctrl-B S` in an agent toggles two
visible agent panes in the current layout. A/B/C retain their own split and focus.
Each visible pane has a different client and independent scrollback; changing
focus also selects that agent's head. Narrow layouts expose the focused pane,
and focus cycling reaches the other one. Closing a split does not close a client.

## Input and navigation

The focused conversation pane receives ordinary keys, paste and Ctrl-C. Its header
identifies the head and input destination. Hydra commands use a Ctrl-B prefix:

| Keys | Action |
| --- | --- |
| `Ctrl-B`, `Tab` | Move focus back to Hydra's other panes |
| `Ctrl-B`, `z` | Expand the focused pane or restore its saved splits |
| `Ctrl-B`, `S` | Toggle one/two visible agent panes after attaching two heads |
| `Ctrl-B`, `D` | Open statistics; `D` there returns to the same conversation |
| `Ctrl-B`, `n` | Switch to the next attached client and select its head |
| `Ctrl-B`, `[` / `]` | Enter scrollback / return to live terminal output |
| `j` / `k`, arrows, Page Up/Down in scrollback | Scroll terminal history |
| `Ctrl-B`, `x` | Close this client; leave its tmux session running |
| `Ctrl-B`, `r` | Reconnect the same recorded instance after client disconnection |
| `Ctrl-B`, `q` | Exit Hydra and close its attachment clients |
| `Ctrl-B`, `Ctrl-B` | Send a literal Ctrl-B to tmux |

At narrow sizes (below 65 content columns or 16 rows), compact chrome shows only
the focused pane. Tab still reaches hidden panes. At larger sizes, `z` while
Hydra has focus, or `Ctrl-B z` from an agent, toggles focus zoom. The saved pane
splits and scroll remain intact, including across a statistics round trip.

Clicking another workspace pane transfers focus. When the child requests mouse
reports, content-area reports are translated into its own coordinates. Pane borders
and dividers remain Hydra controls. Hidden clients retain their last terminal size;
showing a pane resizes its client. An unsent command remains in the existing agent
or shell process across view changes, client detach and reconnect.

The selected client reports attachment/disconnection separately from recorded agent
outcomes. A detached client is not evidence that an agent completed or failed.
Disconnected clients refuse input and are never restarted automatically.

## Identity and lifecycle boundary

`hydra tui --attach <head-id> <instance-id>` is the internal native attachment
entrypoint. It resolves the current project's recorded head and current instance,
checks tmux's project/head/instance environment, and attaches by tmux session ID.
Invalid identifiers, stale instances and mismatched ownership are refused before
interactive input reaches a session. It does not select by a partial branch name.

Exiting the UI, closing a client, or receiving a handled termination signal closes
only the attachment PTY and its client process group. The existing tmux server,
shell and agent remain alive. Reconnection rechecks identity rather than attaching
to a replacement instance silently. Existing palette actions still invoke the
public CLI; the shell-only TUI and full-terminal switch remain available.

Periodic head, workflow and statistics observations run in bounded subprocesses
without blocking the terminal event loop. At most one periodic read per source is
active. Explicit refresh supersedes an in-flight periodic read. Completed valid
samples replace their model as a whole; failures preserve the last good model and
mark it stale. Observation limits and timeouts remain enforced. Initial loading
and explicit legacy detail/palette operations retain their bounded synchronous
behavior.

## Qualification and current limits

`make test-attached-pty` creates two real Hydra heads in a disposable repository
on a tmux socket with a clean configuration. It verifies actual file effects from
terminal input, Ctrl-C, bracketed paste, an unsent draft across statistics, switching
clients, client-only close, reconnect and process identity preservation, and
140x40/80x24/40x10 rendering. A deliberately slow real data adapter proves terminal
input still works during collection, timeout and recovery. Exact outer terminal
settings are checked on exit. `make sanitize-attached` runs the same path under
the platform's sanitizer setup.

These shell/client checks do not qualify every agent CLI. Real Codex planning,
permission prompts and the complete A/B/C/D workflow remain separate roadmap
acceptance requirements. Remote attachments currently retain the existing fleet
handoff path. The supported terminal interpretation subset is documented in
[termviz's terminal contract](../src/termviz/TERMINAL.md).

### Real Codex observation, 7 September 2026

Codex CLI 0.153.3 was launched inside a real Hydra attachment with
`--sandbox workspace-write --ask-for-approval on-request --no-alt-screen` in a
throwaway repository. Its native trust prompt and composer rendered. An unsent
composer draft survived a statistics round trip, three terminal sizes, and
closing/reopening only the attachment client. The first model request failed
because the saved login could not refresh. No model response, tool permission
interaction or agent-authored plan is qualified by this observation. Local
captures are in `build/codex-terminal-evidence/`; credentials must be refreshed
before the remaining live acceptance can run. The initial 40x10 split left the
composer too little room. Compact focused-pane rendering now exposes the real
Codex composer and its unsent draft at that size; the follow-up capture is
`build/codex-terminal-evidence/compact-draft-40x10.txt`. This corrects the observed
layout problem without claiming model or permission-request qualification.
