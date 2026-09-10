# Retained evidence and storage boundaries

Hydra currently bounds individual protocol payloads, observation pages and selected
raw-history stores. It does not yet impose a global quota or automatic expiry on
all workflow runs and receiver task directories. A bounded observation page is not
a bound on the underlying history or worktree disk usage.

| Record | Current retention rule |
| --- | --- |
| Head event stream | Explicit `events retain` archives the removed prefix and keeps the requested tail. Default archive limits are 32 protected archives and 64 MiB, with a 30-day audit window. Limits and expiry are described in [Events](EVENTS.md). |
| Expired head-event ranges | The most recent 64 expiry summaries retain stream ranges and hashes. The next-sequence sidecar prevents a zero-length tail from resetting sequence allocation. |
| Receiver acceptance and submission key | Preserved in the receiver task directory. Repeated submission reconciles the original key/specification binding. There is no automatic key expiry or replacement-task permission. |
| Receiver result, source package and worktree | Preserved with the accepted task. Protocol files and result payloads have declared size limits; trusted commands can still create other workspace files. There is no automatic terminal-task archive policy. |
| Workflow run, contracts, repair journal and sealed artifacts | Preserved together. Selective repair revalidates the original receipts and artifacts, so deleting them invalidates reuse or result retrieval. There is no automatic run archive/expiry policy. |
| Optional provider raw stdout/stderr | `--retain-raw` retains ten completed runs, at most 1 MiB per stream. This rolling diagnostic history is not a declared acceptance-audit archive. See [Agent contract](AGENT_CONTRACT.md). |
| Observation cache and event/log page | A bounded read projection. Missing history, retention gaps, stream resets and truncated scans remain distinct from an empty successful result. |

Active, waiting and unresolved work must keep its recovery records, admission
claims and original execution identity. An unavailable owner or host does not
make those records disposable. Accepted outcome evidence includes its raw
measurements, recipe/source/input bindings, contract version and independent
report; a summary alone does not preserve that evidence.

A future receiver/run archive operation must therefore establish and retain the
reference set before removing bytes, preserve the original deduplication binding,
record an explicit audit expiry, and leave inspectable expiry metadata. It also
needs a capacity rule for protected records and tests covering interrupted archive,
unknown owners, referenced accepted results and repeated submission. Those global
archive and quota mechanisms remain open; the head-event command does not perform
them. No operational records were pruned during the workstream qualification.
