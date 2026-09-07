# Agent adapters

Hydra keeps provider state non-authoritative. Headless execution supports
Antigravity (`agy`), Cursor Agent, OpenCode, Claude Code, Codex, and Pi through
bounded JSONL translations. See the [supported-agent matrix](PROFILES.md),
[versioned headless contract](AGENT_CONTRACT.md), and
[dated qualification evidence](WORKFLOW_AGENT_ACCEPTANCE.md).

`hydra agent probe NAME` inspects executable versions and required flags.
`hydra exec --profile NAME` supervises the provider and translates observed session,
result, usage, and status fields. Provider completion does not set a declared
outcome or pass a verification gate. Cursor usage is unknown; Antigravity uses
its terminal aggregate and does not concatenate intermediate text deltas.

Interactive launch profiles do not install automatic provider hooks. Their
`adapter: none` remains accurate even when the same agent has a separate headless
JSONL adapter. `hydra agent doctor` describes the interactive launch surface;
`hydra agent contract NAME` describes headless capabilities.

The universal integration point is canonical adapter JSON v1 on standard input:

```sh
printf '%s\n' '{"schema_version":1,"instance_id":"instance_...","kind":"observed","status":"idle"}' |
  hydra adapter ingest feature
```

The object must use exactly that field order and contain only restricted identifiers.
`kind` is `observed` (`starting`, `running`, `idle`, `exited`, `failed`, or
`unavailable`) or `outcome` (`done`, `failed`, `blocked`, `abandoned`, or
`canceled`). Input is bounded to 8 KiB. Hydra rejects malformed input, unknown
values, and any event whose instance is not currently active. Accepted input is
translated into the provider-neutral lifecycle state and Events v1 stream.
Rejection is side-effect free: version skew, malformed input, missed delivery, or a
stale instance cannot change declared outcome, observed status, or completion.

Provider adapters may be added only with a dated version probe, fixtures from the
documented provider surface, deterministic absence/version-skew behavior, and the
same generic ingest boundary.
