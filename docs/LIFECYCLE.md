# Lifecycle v1

Hydra records three independent channels:

- declared outcome: an attributed request such as `done`, `failed`, or `blocked`;
- observed status: a timestamped observation such as output activity or process exit;
- liveness: current tmux session and process presence.

None implies another. Quiet output is not completion, a missing session is not
success, and an agent declaration is not a passed gate or human approval. Every
declaration names its actor and instance. Only evidence from the current instance can
satisfy its completion policy; earlier instance events remain history.

Lifecycle events use Events v1. State changes use the shared lock protocol. Resume
and regenerate create a new instance and invalidate transient observations while
retaining head history. Dependencies name the required evidence type and source.

The CLI exposes these channels without collapsing them:

```sh
hydra lifecycle feature --json
hydra outcome feature done --actor agent
hydra wait feature --for outcome=done --timeout 300
hydra resume feature
```

A typed handoff remains an inbox action, never pane injection:

```sh
hydra send --type handoff --delivery safe-point feature "Ready for verification"
hydra recv --receipts feature --json
```

The queued/delivered/stale receipt targets one instance and is inspectable alongside
the lifecycle event and provenance history.

Completion policies are `declared-done`, `observed-exit-zero`, and `either`. They
can be selected at spawn with `--completion-policy`. Dependencies are
comma-separated `branch[:condition]` entries; a bare branch means its completion
policy, while explicit examples include `api:outcome=done` and
`lint:observed=exited`. Missing tmux sessions never satisfy a dependency.

`resume` and `regenerate` preserve the head and prior instance directories, create a
new current instance, record the prior instance as superseded, and use the resolved
profile resume recipe. Old outcomes, observations, adapter input, messages, and
receipts cannot satisfy the new instance.

Teardown defaults to no transcript. `hydra kill <head> --transcript redacted` stores
at most 1 MiB and 2,000 pane-history lines with common secret assignments and API
tokens redacted; `full` is an explicit raw-content opt-in. Files are mode 0600 and
retention defaults to ten instance transcripts. `HYDRA_TRANSCRIPT_MAX_BYTES` and
`HYDRA_TRANSCRIPT_KEEP` set tighter bounds. Trusted `pre-teardown` and
`post-teardown` hooks receive non-secret identity variables.
