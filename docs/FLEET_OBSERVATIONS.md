# Fleet task observations

`hydra fleet task observe HOST --id TASK_ID` returns the receiver-owned
observation snapshot. V2 adds `--cursor N` and `--event-limit N` (1 through
128). The cursor is the last event sequence the client has confirmed. A
reconnect sends that cursor and receives only later events, in sequence order;
repeating a cursor is therefore safe and does not duplicate transitions.

The `event_observation` object is bounded and versioned. `next_cursor` is the
highest sequence read, `oldest_cursor` identifies the beginning of retained
history, and `retention_gap` is true when the requested cursor precedes that
history. `stream_reset` is true when the receiver cannot prove a contiguous
stream, such as malformed or discontinuous retained records. These conditions
are explicit and are never reported as an empty event list. A missing stream
is an empty unavailable observation with the same cursor contract.

The task snapshot keeps process exit, result collection, verification,
approval requests, attempt history, artifact inventory, and provider
observations in separate fields. A process exit of zero does not create a
result collection or verification record, and neither is treated as an
accepted result when the corresponding evidence is unavailable.

Log retrieval continues to use the existing `--source`, `--stream`,
`--offset`, `--limit`, `--step`, and `--attempt` selectors. Event cursors do
not imply stdin access or terminal attachment; attach still requires an
actual terminal capability.
