# Automation contract v1

Automation-relevant commands opt into a versioned envelope with `--json`. Success:

```json
{"schema_version":1,"ok":true,"command":"init","data":{}}
```

Failure:

```json
{"schema_version":1,"ok":false,"command":"init","error":{"code":"invalid_input","message":"...","recovery":"..."}}
```

`schema_version`, `ok`, and `command` are required. Success has `data`; failure has
an error object with stable `code`, human-readable `message`, and an actionable
`recovery`. Unknown fields must be ignored. A JSON error still exits nonzero. Human
diagnostics go to stderr; JSON mode emits one envelope to stdout and never mixes it
with progress text.

The versioned surfaces are `init`, `capabilities`, `lifecycle`, `wait`, `list`,
`status`, `group status`, `recv`/`receipts`, `queue`, `exec`, `diff`, `review`, and
`provenance`. Existing human output remains the default. `list --deps --json` is
rejected because the dependency tree does not yet have a versioned data shape.
Command validation and expected recoverable failures emit the error envelope on
stdout and return nonzero; additional low-level diagnostics may appear on stderr.

`hydra capabilities --json` is the negotiation entry point. It reports the state,
event, and JSON schema versions plus profile availability, integration tier, prompt
and resume modes, adapter, and confidence. Capability absence is data, not success
inference. Commands must not label observed quiet, process exit, or an agent report as
a passed gate or human approval.

Lifecycle automation uses `hydra lifecycle --json`, `hydra wait --json`, and the
canonical adapter-ingest format documented in [AGENT_ADAPTERS.md](AGENT_ADAPTERS.md).
`wait` returns 0 when satisfied, 2 on timeout, and 3 if the current instance changes.
Typed message receipts are available through `hydra recv --receipts <branch> --json`.

Out-of-band automation uses `hydra exec ... --json -- command`. Hydra completing the
batch is distinct from each workload exit status: the envelope contains every
captured result, while any nonzero or timed-out workload makes the process exit
nonzero. Git inspection and provenance use `diff`, `review`, `list --git`, and
`provenance`; see [OPERATIONS.md](OPERATIONS.md) and
[PROVENANCE.md](PROVENANCE.md).

Rate-limited notification sinks are host-local and contain only branch plus event
name:

```sh
hydra notify enable lifecycle.declared --sink desktop --interval 60
hydra notify list
```

Supported sinks are `desktop` (macOS Notification Center or `notify-send`, with a
terminal fallback) and `terminal`. Notification state is protected by the shared
lock protocol and does not require a TUI or daemon.
