# Events v1

Each head owns `events/events.jsonl`. Records are UTF-8 JSON objects with schema
version, opaque event ID, monotonic per-head sequence, UTC occurrence time, project,
head, current instance, optional run, type, actor, and payload. Unknown fields are
ignored by readers. Writers serialize through the head event lock and reject records
over 32 KiB.

Sequence is authoritative for local ordering; timestamps are descriptive and may be
skewed. Event IDs are immutable. Retention archives the removed prefix before
keeping a bounded tail. Repair preserves the original corrupt stream and retains only
the longest valid prefix. Prompts, environment values, terminal output, and paths are
not event payloads unless a command's documented redaction policy explicitly allows
them.

```sh
hydra events verify
hydra events tail --max-events 50
hydra events filter --type lifecycle.declared
hydra events repair
hydra events retain --max-events 1000 --archive-max-count 32 \
  --archive-max-bytes 67108864 --archive-keep-seconds 2592000 --apply
```

Workflow runs have a separate run-scoped `events.jsonl` because they may span several
heads. Its schema version 1 records a unique monotonic sequence, UTC time, run ID,
nullable step ID, transition type, and bounded detail. Step and run scalar state is
authoritative; the stream is the ordered audit trail. Event writers use a run-local
directory lock so parallel steps cannot reuse a sequence number.

## Archive retention

`events retain` keeps the current stream in place and archives only the prefix removed
by `--max-events`. The mutation uses the existing per-stream event lock. `--dry-run`
reports the archive that would be created; `--apply` performs the archive (the legacy
default remains apply when neither flag is supplied). Archive policy options are:

- `--archive-max-count`: 1 through 256 protected archives;
- `--archive-max-bytes`: 1024 through 67108864 bytes across protected archives;
- `--archive-keep-seconds`: 1 through 31536000 seconds from archive creation.

Each archive has a `.meta` record with its stream ID, first and last sequence,
creation and expiry times, byte count, event count, and SHA-256. An archive whose
expiry has passed is removed only during a later retention operation, and its bounded
record is appended to `archive/expiry-summary.jsonl` with `status: "expired"` and the
same sequence range and hash. Thus an expired range is distinguishable from an empty
stream or a stream that was never created. The summary retains the most recent 64
expiry records.

Retention refuses to create a new archive when the requested archive would exceed a
count or byte limit occupied by unexpired archives. The error names the protected
limits and directs the operator to increase them or wait for the declared audit
windows to expire; archives are never silently deleted early. A current stream may
be reduced to zero events (`--max-events 0`); a sidecar next-sequence cursor preserves
monotonic sequence allocation for subsequent appends. Missing or corrupt cursors
on an empty stream with archive history require explicit reconciliation.
