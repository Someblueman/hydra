# Typed messages and steering

Messages remain filesystem inboxes. Hydra never types a steering message into an
agent pane. The sender chooses a type (`note`, `request`, `steer`, `handoff`, or
`cancel`) and delivery (`inbox` or `safe-point`):

```sh
hydra send --type steer --delivery safe-point feature "Pause after the current edit"
hydra recv
hydra recv --receipts feature --json
```

`safe-point` means the target retrieves the message by invoking `hydra recv` at a
safe boundary; it is still inbox delivery and works for every profile. Each queued
message has mode-0600 metadata and a durable `queued`, `delivered`, or `stale`
receipt. Messages target the current instance. After resume, unread messages for the
prior instance are archived as stale and are not delivered to the new instance.

Message bodies are not copied into lifecycle events or notifications. Teardown
archives unread state-v2 messages and retains their receipts. Hydra 2.0 has no
global message-queue fallback.
