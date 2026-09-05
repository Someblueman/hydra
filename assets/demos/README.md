# Native mission control and fleet tour

This recording uses the current checkout's native C TUI and fleet coordinator,
real local Git worktrees, and a real SSH host. It shows a Codex task in local
mission control, then creates a remote head and attaches through the fleet view.

[Watch the MP4](quick-tour.mp4) or view the [GIF](quick-tour.gif).

## What the recording shows

1. Open `hydra tui`, search for the `search` head, enable pane preview, and open
   its details. The custom `codex-plan` profile runs real `codex exec` with a
   read-only sandbox to produce a short implementation plan from the fixture's
   README. The response is generated before recording. The launcher clears its diagnostic
   output and displays that actual response for a readable preview.
2. Run `hydra fleet spawn build --project "$REMOTE_PROJECT" -- "$REMOTE_HEAD"
   --no-agent` against the configured SSH host.
3. Open `hydra fleet tui`, press `a` to attach to that remote head, and run
   `uname -s` in its terminal. Detach with `Ctrl-b`, then `d`, and return to the
   fleet view with Enter.

The remote prompt and tmux status use neutral demo labels. A private, temporary
SSH alias retains the supplied host settings and host-key checks. Demo-only
`LogLevel=ERROR` suppresses the informational disconnect message; connection
errors are not suppressed. Connection setup is off camera.

The local project, agent profile, second shell head, host alias, and remote project
are prepared off camera. `HYDRA_NO_SWITCH=1` keeps spawn in the current terminal.
The remote head is a regular shell; this recording does not claim that an AI agent
runs on the remote host or that the local planning task implemented working code.
Fleet displays recorded desired state, not inferred agent completion.

## Record it again

Install VHS, ffmpeg, ttyd, Git, tmux, Bash, and an authenticated Codex CLI. Building
the current checkout also needs a C99 compiler, Make, pkg-config, and JSON-C
development files. Supply a trusted SSH target with a qualified fleet-enabled
Hydra installation:

```sh
export HYDRA_DEMO_HOST=ubuntu@your-host
export HYDRA_DEMO_REMOTE_HYDRA=/absolute/path/to/bin/hydra
assets/demos/record.sh
```

Follow the [fleet bootstrap guide](../../docs/FLEET.md) to prepare the remote
installation. SSH uses batch authentication and strict host-key verification.
The recording does not install packages or change the host's default Hydra.

The script builds the local helpers from the checkout, creates temporary local
and remote projects and private Hydra state, and uses a dedicated local tmux
socket. The remote head has a unique generated name. Cleanup removes only the
created heads and temporary directories; a failed remote cleanup prints the path
that needs inspection. The bounded agent task can consume normal Codex usage.

The script overwrites the GIF and MP4 beside this document without publishing
them. It requires successful agent output and records native capabilities and
real remote fleet data under ignored `build/demo/`. The script checks worktree
cleanup; inspect the rendered recording before accepting regenerated assets.

[quick-tour.tape](quick-tour.tape) controls commands, timing, and appearance.
Keep this transcript synchronized with the tape when changing the tour.
