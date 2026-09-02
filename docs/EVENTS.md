# Events v1

Each head owns `events/events.jsonl`. Records are UTF-8 JSON objects with schema
version, opaque event ID, monotonic per-head sequence, UTC occurrence time, project,
head, current instance, optional run, type, actor, and payload. Unknown fields are
ignored by readers. Writers serialize through the head event lock and reject records
over 32 KiB.

Sequence is authoritative for local ordering; timestamps are descriptive and may be
skewed. Event IDs are immutable. Retention archives the complete prior stream before
keeping a bounded tail. Repair preserves the original corrupt stream and retains only
the longest valid prefix. Prompts, environment values, terminal output, and paths are
not event payloads unless a command's documented redaction policy explicitly allows
them.

```sh
hydra events verify
hydra events tail --max-events 50
hydra events filter --type lifecycle.declared
hydra events retain --max-events 1000
hydra events repair
```

Workflow runs have a separate run-scoped `events.jsonl` because they may span several
heads. Its schema version 1 records a unique monotonic sequence, UTC time, run ID,
nullable step ID, transition type, and bounded detail. Step and run scalar state is
authoritative; the stream is the ordered audit trail. Event writers use a run-local
directory lock so parallel steps cannot reuse a sequence number.
