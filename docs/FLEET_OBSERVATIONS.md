# Fleet task observations

`hydra fleet task observe HOST --id TASK_ID` returns the receiver-owned
observation snapshot. V2 adds `--cursor N` and `--event-limit N` (1 through
128). The cursor is the last event sequence the client has confirmed.
`next_byte_offset` is the byte position paired with that cursor for bounded
retained journals; send it back as `--byte-offset` with the same `--stream-id`.
A reconnect sends that cursor and byte position and receives only later events,
in sequence order. Repeating a request can return the same sequence IDs; clients
confirm the cursor after processing and use those IDs to avoid duplicate display.

The `event_observation` object is bounded and versioned. `next_cursor` is the
highest sequence returned; `head_cursor` reports the retained stream head.
`next_byte_offset` resumes after the returned page, even when the stream head
is farther ahead. `oldest_cursor` identifies the beginning of retained
history, and `retention_gap` is true when the requested cursor precedes that
history. `stream_reset` is true when the receiver cannot prove a contiguous
stream, such as malformed or discontinuous retained records. These conditions
are explicit and are never reported as an empty event list. A missing stream
is an empty unavailable observation with the same cursor contract.

The task snapshot keeps process exit, result collection, integrity verification,
approval requests, attempt history, artifact inventory, and provider
observations in separate fields. A process exit of zero does not create a
result collection or verification record, and neither is treated as an
accepted result when the corresponding evidence is unavailable. Verification
with `kind: integrity`, `state: recorded`, and `recheck: not_rechecked` reports
retained result metadata. Observation polling does not reverify the bundle or
claim domain acceptance.

Each retained attempt includes its step and attempt IDs, process exit, completion
time, and failure class. Missing records have `retention: missing`; unavailable
history has `retention: unavailable`, with unknown outcomes left null. The
history contains at most 512 entries and sets `attempt_history_truncated` when
that bound excludes entries. Within a truncated step it retains the latest
attempts. Per-attempt collection and verification remain explicitly unavailable
when only task-level result records exist.

Log retrieval continues to use the existing `--source`, `--stream`,
`--offset`, `--limit`, `--step`, and `--attempt` selectors. Event cursors do
not imply stdin access or terminal attachment; attach still requires an
actual terminal capability.
